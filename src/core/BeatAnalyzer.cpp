#include "BeatAnalyzer.h"

#include "BTrack.h"
#include "OnsetDetectionFunction.h"
#include "samplerate.h"
#include "kiss_fft.h"

#include <algorithm>
#include <cmath>

namespace beatit
{

namespace
{
    /** Chromagram with a 4096-point FFT (93 ms) every 2048 samples. */
    void computeChroma (const std::vector<double>& mono, AnalysisResult& r)
    {
        constexpr int fftSize = 4096, hop = 2048;
        const double fs = BeatAnalyzer::analysisRate;
        r.chromaHop = hop / fs;

        kiss_fft_cfg cfg = kiss_fft_alloc (fftSize, 0, nullptr, nullptr);
        if (cfg == nullptr)
            return;

        std::vector<kiss_fft_cpx> in ((size_t) fftSize), out ((size_t) fftSize);
        std::vector<double> window ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (fftSize - 1));

        // map FFT bins (60 Hz .. 2.1 kHz) to pitch classes
        std::vector<int> pitchClass ((size_t) fftSize / 2, -1);
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double f = k * fs / fftSize;
            if (f >= 60.0 && f <= 2100.0)
            {
                const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
                pitchClass[(size_t) k] = ((midi % 12) + 12) % 12;
            }
        }

        const int numFrames = (int) (mono.size() / hop);
        r.chroma.reserve ((size_t) numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const long centre = (long) f * hop;
            for (int i = 0; i < fftSize; ++i)
            {
                const long idx = centre - fftSize / 2 + i;
                const double v = (idx >= 0 && idx < (long) mono.size()) ? mono[(size_t) idx] : 0.0;
                in[(size_t) i].r = (kiss_fft_scalar) (v * window[(size_t) i]);
                in[(size_t) i].i = 0;
            }

            kiss_fft (cfg, in.data(), out.data());

            std::array<float, 12> c {};
            for (int k = 1; k < fftSize / 2; ++k)
                if (const int pc = pitchClass[(size_t) k]; pc >= 0)
                {
                    const double mag = std::sqrt ((double) out[(size_t) k].r * out[(size_t) k].r
                                                + (double) out[(size_t) k].i * out[(size_t) k].i);
                    c[(size_t) pc] += (float) std::log1p (100.0 * mag);
                }

            float norm = 0.0f;
            for (float v : c) norm += v * v;
            norm = std::sqrt (norm);
            if (norm > 0.0f)
                for (float& v : c) v /= norm;

            r.chroma.push_back (c);
        }

        kiss_fft_free (cfg);
    }

    struct Canceller
    {
        BeatAnalyzer::ProgressFn fn;
        bool cancelled = false;

        bool report (float p)
        {
            if (fn && ! fn (juce::jlimit (0.0f, 1.0f, p)))
                cancelled = true;
            return ! cancelled;
        }
    };

    /** Runs an onset detection function over the whole signal. */
    std::vector<double> computeOdf (const std::vector<double>& mono, int hop, int frame, int type,
                                    Canceller& c, float p0, float p1)
    {
        OnsetDetectionFunction odf (hop, frame, type, HanningWindow);

        const size_t numFrames = mono.size() / (size_t) hop;
        std::vector<double> out;
        out.reserve (numFrames);

        std::vector<double> buf ((size_t) hop);
        for (size_t k = 0; k < numFrames; ++k)
        {
            std::copy_n (mono.begin() + (long) (k * (size_t) hop), hop, buf.begin());
            out.push_back (odf.calculateOnsetDetectionFunctionSample (buf.data()));

            if ((k & 1023) == 0 && ! c.report (p0 + (p1 - p0) * (float) k / (float) numFrames))
                return {};
        }

        return out;
    }

    std::vector<double> runTracker (const std::vector<double>& odf, bool reverse,
                                    Canceller& c, float p0, float p1)
    {
        BTrack tracker (BeatAnalyzer::trackerHop, BeatAnalyzer::trackerFrame);

        std::vector<double> beats;
        const int n = (int) odf.size();
        const double hopSeconds = BeatAnalyzer::trackerHop / BeatAnalyzer::analysisRate;

        for (int k = 0; k < n; ++k)
        {
            const int idx = reverse ? n - 1 - k : k;
            tracker.processOnsetDetectionFunctionSample (odf[(size_t) idx]);

            if (tracker.beatDueInCurrentFrame())
                beats.push_back (idx * hopSeconds);

            if ((k & 2047) == 0 && ! c.report (p0 + (p1 - p0) * (float) k / (float) n))
                return {};
        }

        if (reverse)
            std::reverse (beats.begin(), beats.end());

        return beats;
    }


}

//==============================================================================
/** Merges the forward and backward BTrack passes.

    A causal tracker lags behind tempo ramps and needs a few seconds to
    re-lock after an abrupt tempo change or at the song start; the
    backward pass has the same weaknesses mirrored in time. So we pool the
    beats of both passes as candidates and pick, with dynamic programming,
    the sequence that maximises onset strength while penalising irregular
    consecutive intervals. The path can switch passes anywhere, taking
    whichever one is locked in each section.

    Intervals are also compared to the local beat period that each pass
    reports on its own. Without that term, two passes locked half a beat
    apart (one on the offbeats) interleave into a perfectly regular
    sequence at double tempo. */
std::vector<double> BeatAnalyzer::mergePasses (const std::vector<double>& fwd, const std::vector<double>& bwd,
                                           const AnalysisResult& a)
{
    if (bwd.size() < 8) return fwd;
    if (fwd.size() < 8) return bwd;

    struct Cand { double t; int mask; double s; };
    std::vector<Cand> cands;
    {
        std::vector<std::pair<double, int>> all;
        for (double t : fwd) all.push_back ({ t, 1 });
        for (double t : bwd) all.push_back ({ t, 2 });
        std::sort (all.begin(), all.end());

        for (auto [t, m] : all)
        {
            if (! cands.empty() && t - cands.back().t < 0.03 && (cands.back().mask & m) == 0)
            {
                cands.back().t = 0.5 * (cands.back().t + t);
                cands.back().mask |= m;
            }
            else
            {
                cands.push_back ({ t, m, 0.0 });
            }
        }
    }

    // onset strength of each candidate, normalised by the median
    std::vector<double> peaks;
    for (auto& c : cands)
    {
        c.s = a.peakOdfNear (c.t, 0.035);
        peaks.push_back (c.s);
    }

    std::nth_element (peaks.begin(), peaks.begin() + (long) peaks.size() / 2, peaks.end());
    const double median = std::max (1.0e-9, peaks[peaks.size() / 2]);

    for (auto& c : cands)
        c.s = std::min (3.0, c.s / median) + (c.mask == 3 ? 0.5 : 0.0); // bonus when both passes agree

    // DP over pairs (previous beat, current beat) so that the transition
    // cost can compare consecutive intervals (second-order smoothness).
    constexpr double minInterval = 0.2, maxInterval = 1.6; // 300 .. 37.5 BPM
    constexpr double lambda = 4.0;      // weight of interval-to-interval smoothness
    constexpr double lambdaTempo = 4.0; // weight of agreement with the passes' own tempo
    constexpr double lambdaGlobal = 2.0; // weight of agreement with the song's overall tempo

    // local beat period of a pass around time t: median interval within +-3 s
    auto localPeriod = [] (const std::vector<double>& beats, double t)
    {
        auto lo = std::lower_bound (beats.begin(), beats.end(), t - 3.0);
        auto hi = std::upper_bound (beats.begin(), beats.end(), t + 3.0);
        std::vector<double> d;
        for (auto it = lo; it != hi && it + 1 != beats.end(); ++it)
            if (*(it + 1) <= t + 3.0)
                d.push_back (*(it + 1) - *it);
        if (d.size() < 2)
            return 0.0;
        std::nth_element (d.begin(), d.begin() + (long) d.size() / 2, d.end());
        return d[d.size() / 2];
    };

    const int numCands = (int) cands.size();
    std::vector<double> refFwd ((size_t) numCands), refBwd ((size_t) numCands);
    for (int j = 0; j < numCands; ++j)
    {
        refFwd[(size_t) j] = localPeriod (fwd, cands[(size_t) j].t);
        refBwd[(size_t) j] = localPeriod (bwd, cands[(size_t) j].t);
    }

    // Overall beat period, from both passes. When the passes disagree by an
    // octave somewhere (typically one pass counting eighth notes during an
    // intro), this decides which one to follow; real tempo changes are
    // unaffected because there both passes agree and no alternative exists.
    double globalPeriod = 0.0;
    {
        std::vector<double> d;
        for (const auto* pass : { &fwd, &bwd })
            for (size_t i = 1; i < pass->size(); ++i)
                d.push_back ((*pass)[i] - (*pass)[i - 1]);
        std::nth_element (d.begin(), d.begin() + (long) d.size() / 2, d.end());
        globalPeriod = d[d.size() / 2];
    }

    // cost of an interval ending at candidate j: distance (in log tempo)
    // to the closer of the two passes' local periods, plus to the global period
    auto tempoCost = [&] (int j, double interval)
    {
        const double g = std::log (interval / globalPeriod);
        const double globalCost = lambdaGlobal * g * g;

        double best = 1.0e9;
        for (double ref : { refFwd[(size_t) j], refBwd[(size_t) j] })
            if (ref > 0.0)
            {
                const double r = std::log (interval / ref);
                best = std::min (best, r * r);
            }
        return (best < 1.0e8 ? lambdaTempo * best : 0.0) + globalCost;
    };

    const int m = (int) cands.size();
    std::vector<std::vector<int>> preds ((size_t) m);
    for (int j = 0; j < m; ++j)
        for (int i = j - 1; i >= 0; --i)
        {
            const double d = cands[(size_t) j].t - cands[(size_t) i].t;
            if (d > maxInterval) break;
            if (d >= minInterval) preds[(size_t) j].push_back (i);
        }

    // score[j][k] = best path ending with beats (preds[j][k], j)
    std::vector<std::vector<double>> score ((size_t) m);
    std::vector<std::vector<int>> back ((size_t) m);   // index into preds[i] of the beat before i, or -1

    for (int j = 0; j < m; ++j)
    {
        const auto& pj = preds[(size_t) j];
        score[(size_t) j].assign (pj.size(), 0.0);
        back[(size_t) j].assign (pj.size(), -1);

        for (size_t k = 0; k < pj.size(); ++k)
        {
            const int i = pj[k];
            const double dij = cands[(size_t) j].t - cands[(size_t) i].t;

            double best = cands[(size_t) i].s;  // path starting at i
            int bestH = -1;

            const auto& pi = preds[(size_t) i];
            for (size_t q = 0; q < pi.size(); ++q)
            {
                const double dhi = cands[(size_t) i].t - cands[(size_t) pi[q]].t;
                const double r = std::log (dij / dhi);
                const double v = score[(size_t) i][q] - lambda * r * r;
                if (v > best) { best = v; bestH = (int) q; }
            }

            score[(size_t) j][k] = best + cands[(size_t) j].s - tempoCost (j, dij);
            back[(size_t) j][k] = bestH;
        }
    }

    // best end state
    int endJ = -1, endK = -1;
    double bestScore = -1.0e300;
    for (int j = 0; j < m; ++j)
        for (size_t k = 0; k < score[(size_t) j].size(); ++k)
            if (score[(size_t) j][k] > bestScore) { bestScore = score[(size_t) j][k]; endJ = j; endK = (int) k; }

    if (endJ < 0)
        return fwd;

    std::vector<double> out;
    int j = endJ, k = endK;
    out.push_back (cands[(size_t) j].t);
    while (k >= 0)
    {
        const int i = preds[(size_t) j][(size_t) k];
        out.push_back (cands[(size_t) i].t);
        k = back[(size_t) j][(size_t) k];
        j = i;
    }

    std::reverse (out.begin(), out.end());
    return out;
}

//==============================================================================
std::vector<double> BeatAnalyzer::prepareMono (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    const int numSamples = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();
    if (numSamples == 0 || numChannels == 0)
        return {};

    std::vector<float> mono ((size_t) numSamples, 0.0f);
    const float gain = 1.0f / (float) numChannels;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = audio.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += src[i] * gain;
    }

    if (std::abs (sampleRate - analysisRate) < 0.5)
        return { mono.begin(), mono.end() };

    const double ratio = analysisRate / sampleRate;
    std::vector<float> resampled ((size_t) std::ceil (numSamples * ratio) + 16);

    SRC_DATA data {};
    data.data_in = mono.data();
    data.input_frames = numSamples;
    data.data_out = resampled.data();
    data.output_frames = (long) resampled.size();
    data.src_ratio = ratio;

    if (src_simple (&data, SRC_SINC_MEDIUM_QUALITY, 1) != 0)
    {
        // fallback: linear interpolation
        juce::LinearInterpolator interp;
        const int outCount = (int) (numSamples * ratio);
        resampled.assign ((size_t) outCount, 0.0f);
        interp.process (1.0 / ratio, mono.data(), resampled.data(), outCount);
        return { resampled.begin(), resampled.end() };
    }

    resampled.resize ((size_t) data.output_frames_gen);
    return { resampled.begin(), resampled.end() };
}

//==============================================================================
std::optional<AnalysisResult> BeatAnalyzer::analyze (const juce::AudioBuffer<float>& audio,
                                                     double sampleRate, ProgressFn progress)
{
    Canceller c { std::move (progress) };

    if (audio.getNumSamples() < (int) (sampleRate * 2.0))
        return std::nullopt; // need at least a couple of seconds

    if (! c.report (0.0f))
        return std::nullopt;

    auto mono = prepareMono (audio, sampleRate);
    if (mono.empty() || ! c.report (0.08f))
        return std::nullopt;

    AnalysisResult result;
    result.durationSeconds = audio.getNumSamples() / sampleRate;

    // ---- envelopes (RMS + low band for downbeat estimation)
    {
        const int hop = trackerHop;
        result.envHop = hop / analysisRate;

        juce::IIRFilter lp1, lp2;
        lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (analysisRate, 150.0));
        lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (analysisRate, 150.0));

        const size_t numHops = mono.size() / (size_t) hop;
        result.rmsEnvelope.reserve (numHops);
        result.lowBandEnvelope.reserve (numHops);

        for (size_t k = 0; k < numHops; ++k)
        {
            double sum = 0.0, low = 0.0;
            for (int i = 0; i < hop; ++i)
            {
                const auto s = (float) mono[k * (size_t) hop + (size_t) i];
                const float l = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (s));
                sum += (double) s * s;
                low += (double) l * l;
            }

            result.rmsEnvelope.push_back ((float) std::sqrt (sum / hop));
            result.lowBandEnvelope.push_back ((float) std::sqrt (low / hop));
        }
    }

    computeChroma (mono, result);

    if (! c.report (0.12f))
        return std::nullopt;

    // ---- onset detection functions
    // Tracker ODF: identical to BTrack's internal one (complex spectral difference, HWR).
    auto trackerOdf = computeOdf (mono, trackerHop, trackerFrame, ComplexSpectralDifferenceHWR, c, 0.12f, 0.35f);
    if (c.cancelled || trackerOdf.empty())
        return std::nullopt;

    // High-resolution spectral flux for snapping beats onto transients.
    auto snapOdf = computeOdf (mono, snapHop, snapFrame, SpectralDifferenceHWR, c, 0.35f, 0.6f);
    if (c.cancelled)
        return std::nullopt;

    result.odfHop = snapHop / analysisRate;
    // frame k holds samples [(k+1)*hop - frame, (k+1)*hop): centre at (k+1)*hop - frame/2
    result.odfTimeOffset = (snapHop - snapFrame / 2) / analysisRate;
    result.odf.assign (snapOdf.begin(), snapOdf.end());

    // ---- beat tracking: forward + backward, then stitch
    auto forward = runTracker (trackerOdf, false, c, 0.6f, 0.8f);
    if (c.cancelled)
        return std::nullopt;

    auto backward = runTracker (trackerOdf, true, c, 0.8f, 0.98f);
    if (c.cancelled)
        return std::nullopt;

    result.rawBeats = mergePasses (forward, backward, result);
    result.forwardBeats = std::move (forward);
    result.backwardBeats = std::move (backward);

    c.report (1.0f);
    return result;
}

} // namespace beatit
