#include "TempoMap.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace beatit
{

//==============================================================================
static float sampleEnvelope (const std::vector<float>& env, double hop, double offset, double t)
{
    if (env.empty() || hop <= 0.0)
        return 0.0f;

    const double pos = (t - offset) / hop;
    if (pos <= 0.0)               return env.front();
    if (pos >= (double) (env.size() - 1)) return env.back();

    const auto i = (size_t) pos;
    const auto frac = (float) (pos - (double) i);
    return env[i] + frac * (env[i + 1] - env[i]);
}

float AnalysisResult::odfAt (double t) const      { return sampleEnvelope (odf, odfHop, odfTimeOffset, t); }
float AnalysisResult::rmsAt (double t) const      { return sampleEnvelope (rmsEnvelope, envHop, 0.0, t); }
float AnalysisResult::lowBandAt (double t) const  { return sampleEnvelope (lowBandEnvelope, envHop, 0.0, t); }

float AnalysisResult::peakOdfNear (double t, double halfWidth) const
{
    if (odf.empty() || odfHop <= 0.0)
        return 0.0f;

    const int n = (int) odf.size();
    const int lo = std::max (0, (int) std::floor ((t - halfWidth - odfTimeOffset) / odfHop));
    const int hi = std::min (n - 1, (int) std::ceil ((t + halfWidth - odfTimeOffset) / odfHop));

    float m = 0.0f;
    for (int i = lo; i <= hi; ++i)
        m = std::max (m, odf[(size_t) i]);
    return m;
}

//==============================================================================
static int positiveMod (int a, int m) noexcept   { return m <= 0 ? 0 : ((a % m) + m) % m; }

std::pair<int, int> GridLayout::barAndBeat (int fullIndex) const
{
    const int bpb = std::max (1, beatsPerBar);

    if (pickupBeats == 0)
        return { fullIndex / bpb + 1, fullIndex % bpb + 1 };

    if (fullIndex < pickupBeats)
        return { 1, fullIndex + 1 };

    const int j = fullIndex - pickupBeats;
    return { j / bpb + 2, j % bpb + 1 };
}

bool BeatGrid::isDownbeat (int i) const noexcept
{
    const int d = firstDownbeatIndex();
    return i >= d && positiveMod (i - d, beatsPerBar) == 0;
}

int BeatGrid::firstDownbeatIndex() const noexcept
{
    return positiveMod (downbeatOffset, std::max (1, beatsPerBar));
}

double BeatGrid::bpmOfInterval (int i) const noexcept
{
    if (beats.size() < 2)
        return 0.0;

    i = std::clamp (i, 0, (int) beats.size() - 2);
    const double dt = beats[(size_t) i + 1] - beats[(size_t) i];
    return dt > 0.0 ? 60.0 / dt : 0.0;
}

double BeatGrid::bpmAtTime (double t) const noexcept
{
    return bpmOfInterval (std::max (0, beatIndexAtOrBefore (t)));
}

double BeatGrid::averageBpm() const noexcept
{
    if (beats.size() < 2)
        return 0.0;

    return 60.0 * (double) (beats.size() - 1) / (beats.back() - beats.front());
}

int BeatGrid::beatIndexAtOrBefore (double t) const noexcept
{
    auto it = std::upper_bound (beats.begin(), beats.end(), t);
    return (int) std::distance (beats.begin(), it) - 1;
}

int BeatGrid::nearestBeat (double t, double tol) const noexcept
{
    if (beats.empty())
        return -1;

    auto it = std::lower_bound (beats.begin(), beats.end(), t);
    int best = -1;
    double bestDist = tol;

    auto consider = [&] (std::vector<double>::const_iterator c)
    {
        if (c < beats.begin() || c >= beats.end())
            return;

        const double d = std::abs (*c - t);
        if (d <= bestDist) { bestDist = d; best = (int) std::distance (beats.cbegin(), c); }
    };

    consider (it);
    if (it != beats.begin())
        consider (it - 1);

    return best;
}

long long PreCount::silenceSamplesAt (double rate) const noexcept
{
    // llround makes this exact when the rates match
    return sampleRate > 0.0 ? std::llround ((double) silenceSamples * rate / sampleRate) : 0;
}

PreCount ops::computePreCount (const BeatGrid& grid, int beats, double sampleRate)
{
    PreCount pc;
    pc.sampleRate = sampleRate;
    if (beats <= 0 || grid.isEmpty() || sampleRate <= 0.0)
        return pc;

    const size_t n = std::min<size_t> (4, grid.beats.size() - 1);
    const double interval = (grid.beats[n] - grid.beats[0]) / (double) n;

    pc.beats = beats;
    pc.silenceSamples = std::max (1LL, (long long) std::llround (beats * interval * sampleRate));
    return pc;
}

BeatGrid ops::applyPreCount (const BeatGrid& grid, const PreCount& pc)
{
    if (! pc.isActive() || grid.isEmpty())
        return grid;

    const double shift = pc.silenceSeconds();
    const double spacing = shift / pc.beats;
    const double b0 = grid.beats[0];

    BeatGrid out = grid;
    out.beats.clear();
    out.beats.reserve (grid.beats.size() + (size_t) pc.beats);
    for (int k = 0; k < pc.beats; ++k)
        out.beats.push_back (b0 + k * spacing);
    for (double t : grid.beats)
        out.beats.push_back (t + shift);

    out.downbeatOffset = positiveMod (grid.firstDownbeatIndex() + pc.beats, std::max (1, grid.beatsPerBar));
    return out;
}

GridLayout BeatGrid::computeLayout() const
{
    GridLayout layout;
    layout.beatsPerBar = std::max (1, beatsPerBar);

    if (beats.size() < 2)
        return layout;

    const double b0 = beats[0];
    const double firstInterval = beats[1] - beats[0];

    // Fill the gap between t=0 and the first detected beat with evenly spaced
    // virtual beats, so the MIDI tempo map starts at tick 0 = audio start.
    // Reaper accepts tempos up to 960 BPM, so a gap shorter than 1/16 s is
    // simply absorbed (first beat lands on t=0, every later beat stays exact).
    constexpr double maxBpm = 960.0;
    if (b0 >= 60.0 / maxBpm)
    {
        layout.preRollBeats = std::max (1, (int) std::lround (b0 / std::max (1.0e-3, firstInterval)));
        while (layout.preRollBeats > 1 && 60.0 * layout.preRollBeats / b0 > maxBpm)
            --layout.preRollBeats;
    }
    else if (b0 > 1.0e-4)
    {
        layout.firstBeatSnappedToZero = true;
    }

    // Beats preceding the first downbeat form a (possibly partial) pickup bar.
    const int beatsBeforeDownbeat = layout.preRollBeats + firstDownbeatIndex();
    layout.pickupBeats = beatsBeforeDownbeat % layout.beatsPerBar;
    return layout;
}

std::pair<int, int> BeatGrid::barAndBeatOfDetected (int i, const GridLayout& layout) const
{
    return layout.barAndBeat (i + layout.preRollBeats);
}

BeatGrid::Position BeatGrid::positionAt (double t, const GridLayout& layout) const
{
    Position p;
    if (beats.size() < 2)
        return p;

    const int i = beatIndexAtOrBefore (t);

    if (i < 0)
    {
        // inside the pre-roll
        if (layout.preRollBeats <= 0 || beats[0] <= 0.0)
            return p;

        const double beatLen = beats[0] / layout.preRollBeats;
        const double fullPos = t / beatLen;
        const int full = std::clamp ((int) fullPos, 0, layout.preRollBeats - 1);
        auto [bar, beat] = layout.barAndBeat (full);
        p = { bar, beat, fullPos - full, true };
        return p;
    }

    const int last = (int) beats.size() - 1;
    const int k = std::min (i, last - 1);
    const double len = beats[(size_t) k + 1] - beats[(size_t) k];
    double frac = len > 0.0 ? (t - beats[(size_t) i]) / len : 0.0;

    auto [bar, beat] = barAndBeatOfDetected (i, layout);
    p = { bar, beat, std::clamp (frac, 0.0, 0.999), true };
    return p;
}

//==============================================================================
namespace ops
{

double medianInterval (const std::vector<double>& beats)
{
    if (beats.size() < 2)
        return 0.5;

    std::vector<double> d;
    d.reserve (beats.size() - 1);
    for (size_t i = 1; i < beats.size(); ++i)
        d.push_back (beats[i] - beats[i - 1]);

    std::nth_element (d.begin(), d.begin() + (long) d.size() / 2, d.end());
    return d[d.size() / 2];
}

static double localMedianInterval (const std::vector<double>& beats, size_t centre, size_t halfWindow)
{
    const size_t lo = centre > halfWindow ? centre - halfWindow : 1;
    const size_t hi = std::min (beats.size() - 1, centre + halfWindow);

    std::vector<double> d;
    for (size_t i = std::max<size_t> (1, lo); i <= hi; ++i)
        d.push_back (beats[i] - beats[i - 1]);

    if (d.empty())
        return medianInterval (beats);

    std::nth_element (d.begin(), d.begin() + (long) d.size() / 2, d.end());
    return d[d.size() / 2];
}

double alternateBeatStrengthRatio (const std::vector<double>& beats, const AnalysisResult& a)
{
    if (beats.size() < 16 || a.odf.empty())
        return 1.0;

    // Compare parities locally (8-beat windows) so that a single missed or
    // extra beat somewhere does not flip the parity for the rest of the song.
    double weak = 0.0, strong = 0.0;
    for (size_t start = 0; start + 8 <= beats.size(); start += 8)
    {
        double p[2] = { 0.0, 0.0 };
        for (size_t i = start; i < start + 8; ++i)
            p[i % 2] += a.peakOdfNear (beats[i], 0.03);

        weak += std::min (p[0], p[1]);
        strong += std::max (p[0], p[1]);
    }

    return strong > 0.0 ? weak / strong : 1.0;
}

std::vector<double> doubleTempo (const std::vector<double>& beats)
{
    if (beats.size() < 2)
        return beats;

    std::vector<double> out;
    out.reserve (beats.size() * 2);

    for (size_t i = 0; i + 1 < beats.size(); ++i)
    {
        out.push_back (beats[i]);
        out.push_back (0.5 * (beats[i] + beats[i + 1]));
    }

    out.push_back (beats.back());
    return out;
}

std::vector<double> halveTempo (const std::vector<double>& beats, const AnalysisResult* analysis)
{
    if (beats.size() < 4)
        return beats;

    // keep the parity that sits on the stronger onsets
    double score[2] = { 0.0, 0.0 };
    if (analysis != nullptr)
        for (size_t i = 0; i < beats.size(); ++i)
            score[i % 2] += analysis->peakOdfNear (beats[i], 0.03);

    const size_t parity = score[1] > score[0] ? 1 : 0;

    std::vector<double> out;
    for (size_t i = parity; i < beats.size(); i += 2)
        out.push_back (beats[i]);

    return out;
}

std::vector<double> fixOutliers (const std::vector<double>& input, const AnalysisResult* analysis)
{
    if (input.size() < 8)
        return input;

    std::vector<double> beats = input;

    // 1) remove spurious extra beats (interval much shorter than local median)
    for (int pass = 0; pass < 3; ++pass)
    {
        bool changed = false;
        std::vector<double> out;
        out.reserve (beats.size());
        out.push_back (beats[0]);

        for (size_t i = 1; i < beats.size(); ++i)
        {
            const double m = localMedianInterval (beats, i, 8);
            const double d = beats[i] - out.back();

            if (d < 0.6 * m)
            {
                // Two beats too close together: drop one of them. Keep the one
                // that leaves intervals closest to the local median, with onset
                // strength as a small tie-breaker.
                const double prevBeat = out.back();
                const double curBeat  = beats[i];
                const double beforePrev = out.size() >= 2 ? out[out.size() - 2] : prevBeat - m;
                const double afterCur   = i + 1 < beats.size() ? beats[i + 1] : curBeat + m;

                double errKeepPrev = std::abs ((afterCur - prevBeat) - m);
                double errKeepCur  = std::abs ((curBeat - beforePrev) - m);

                if (analysis != nullptr)
                {
                    const bool prevStronger = analysis->odfAt (prevBeat) >= analysis->odfAt (curBeat);
                    (prevStronger ? errKeepPrev : errKeepCur) -= 0.01;
                }

                if (errKeepCur < errKeepPrev)
                    out.back() = curBeat;

                changed = true;
                continue;
            }

            out.push_back (beats[i]);
        }

        beats.swap (out);
        if (! changed)
            break;
    }

    // 2) fill gaps (missed beats)
    std::vector<double> out;
    out.reserve (beats.size() + 16);
    out.push_back (beats[0]);

    for (size_t i = 1; i < beats.size(); ++i)
    {
        const double m = localMedianInterval (beats, i, 8);
        const double d = beats[i] - beats[i - 1];

        if (d > 1.5 * m)
        {
            const int n = (int) std::lround (d / m);
            for (int k = 1; k < n; ++k)
                out.push_back (beats[i - 1] + d * k / n);
        }

        out.push_back (beats[i]);
    }

    return out;
}

std::vector<double> snapToTransients (const std::vector<double>& beats, const AnalysisResult& a)
{
    if (a.odf.empty() || a.odfHop <= 0.0 || beats.size() < 2)
        return beats;

    std::vector<double> out = beats;
    const int n = (int) a.odf.size();

    // local mean of the ODF, for an adaptive "is this a real transient" test
    auto localMean = [&] (int centre, int half)
    {
        const int lo = std::max (0, centre - half), hi = std::min (n - 1, centre + half);
        double s = 0.0;
        for (int i = lo; i <= hi; ++i) s += a.odf[(size_t) i];
        return s / std::max (1, hi - lo + 1);
    };

    const int meanHalf = (int) (1.0 / a.odfHop);

    for (size_t b = 0; b < beats.size(); ++b)
    {
        const double interval = (b + 1 < beats.size()) ? beats[b + 1] - beats[b] : beats[b] - beats[b - 1];
        const double window = std::clamp (0.12 * interval, 0.015, 0.05);
        const double sigma = window * 0.6;

        const int centre = (int) std::lround ((beats[b] - a.odfTimeOffset) / a.odfHop);
        const int half = (int) std::ceil (window / a.odfHop);

        int bestIdx = -1;
        double bestScore = 0.0;

        for (int i = std::max (1, centre - half); i <= std::min (n - 2, centre + half); ++i)
        {
            const float v = a.odf[(size_t) i];
            if (v < a.odf[(size_t) i - 1] || v < a.odf[(size_t) i + 1])
                continue; // only local peaks

            const double dt = (i - centre) * a.odfHop;
            const double score = v * std::exp (-0.5 * (dt * dt) / (sigma * sigma));
            if (score > bestScore) { bestScore = score; bestIdx = i; }
        }

        if (bestIdx < 0)
            continue;

        const double mean = localMean (bestIdx, meanHalf);
        if (a.odf[(size_t) bestIdx] < 1.5 * mean)
            continue; // weak peak: keep the tracker's estimate

        // parabolic interpolation for sub-hop precision
        const double y0 = a.odf[(size_t) bestIdx - 1], y1 = a.odf[(size_t) bestIdx], y2 = a.odf[(size_t) bestIdx + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        const double delta = std::abs (denom) > 1.0e-12 ? std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5) : 0.0;

        out[b] = a.odfTimeOffset + (bestIdx + delta) * a.odfHop;
    }

    // keep strictly increasing
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i] <= out[i - 1] + 0.05)
            out[i] = std::max (beats[i], out[i - 1] + 0.05);

    return out;
}

std::vector<double> smooth (const std::vector<double>& beats, int halfWindow)
{
    if (halfWindow <= 0 || beats.size() < 3)
        return beats;

    // Local linear regression of beat time vs. beat index. Keeps beats on the
    // timeline (no drift) while removing per-beat jitter from the tempo curve.
    const int n = (int) beats.size();
    std::vector<double> out ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const int lo = std::max (0, i - halfWindow), hi = std::min (n - 1, i + halfWindow);
        double sx = 0, sy = 0, sxx = 0, sxy = 0, sw = 0;

        for (int k = lo; k <= hi; ++k)
        {
            const double w = 1.0 - std::abs (k - i) / (halfWindow + 1.0); // triangular weights
            const double x = k - i, y = beats[(size_t) k];
            sw += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y;
        }

        const double denom = sw * sxx - sx * sx;
        out[(size_t) i] = std::abs (denom) > 1.0e-12 ? (sy * sxx - sx * sxy) / denom : beats[(size_t) i];
    }

    return out;
}

std::vector<double> fitConstantTempo (const std::vector<double>& beats)
{
    if (beats.size() < 3)
        return beats;

    // robust least squares fit t_i = a + b*i, rejecting outliers iteratively
    const int n = (int) beats.size();
    std::vector<bool> use ((size_t) n, true);
    double a = beats.front(), b = (beats.back() - beats.front()) / (n - 1);

    for (int iter = 0; iter < 4; ++iter)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, cnt = 0;
        for (int i = 0; i < n; ++i)
        {
            if (! use[(size_t) i]) continue;
            sx += i; sy += beats[(size_t) i]; sxx += (double) i * i; sxy += i * beats[(size_t) i]; ++cnt;
        }

        const double denom = cnt * sxx - sx * sx;
        if (cnt < 3 || std::abs (denom) < 1.0e-12)
            break;

        b = (cnt * sxy - sx * sy) / denom;
        a = (sy - b * sx) / cnt;

        // reject points further than 25% of a beat from the line
        for (int i = 0; i < n; ++i)
            use[(size_t) i] = std::abs (beats[(size_t) i] - (a + b * i)) < 0.25 * b;
    }

    std::vector<double> out ((size_t) n);
    for (int i = 0; i < n; ++i)
        out[(size_t) i] = a + b * i;

    return out;
}

static void zNormalise (std::vector<double>& v)
{
    if (v.empty()) return;
    double mean = 0.0, var = 0.0;
    for (double x : v) mean += x;
    mean /= (double) v.size();
    for (double x : v) var += (x - mean) * (x - mean);
    const double sd = std::sqrt (var / (double) v.size());
    for (double& x : v) x = sd > 1.0e-12 ? (x - mean) / sd : 0.0;
}

int guessDownbeatOffset (const std::vector<double>& beats, int bpb, const AnalysisResult& a)
{
    const size_t n = beats.size();
    if (bpb <= 1 || n < (size_t) bpb * 2)
        return 0;

    // --- cue 1: harmonic change. Beat-synchronous chroma, then novelty at each
    // beat = distance between the chroma before and after it.
    std::vector<double> novelty (n, 0.0);
    if (! a.chroma.empty() && a.chromaHop > 0.0)
    {
        std::vector<std::array<double, 12>> beatChroma (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t0 = beats[i];
            const double t1 = i + 1 < n ? beats[i + 1] : beats[i] + (beats[i] - beats[i - 1]);
            const int f0 = std::max (0, (int) std::ceil (t0 / a.chromaHop));
            const int f1 = std::min ((int) a.chroma.size() - 1, (int) std::floor (t1 / a.chromaHop));

            auto& c = beatChroma[i];
            c.fill (0.0);
            for (int f = f0; f <= f1; ++f)
                for (int k = 0; k < 12; ++k)
                    c[(size_t) k] += a.chroma[(size_t) f][(size_t) k];
        }

        const int w = std::max (1, std::min (2, bpb / 2));
        auto windowSum = [&] (int from, int to) // [from, to)
        {
            std::array<double, 12> s {};
            for (int i = std::max (0, from); i < std::min ((int) n, to); ++i)
                for (int k = 0; k < 12; ++k)
                    s[(size_t) k] += beatChroma[(size_t) i][(size_t) k];
            return s;
        };

        for (int i = w; i + w <= (int) n; ++i)
        {
            const auto before = windowSum (i - w, i), after = windowSum (i, i + w);
            double dot = 0.0, nb = 0.0, na = 0.0;
            for (int k = 0; k < 12; ++k)
            {
                dot += before[(size_t) k] * after[(size_t) k];
                nb += before[(size_t) k] * before[(size_t) k];
                na += after[(size_t) k] * after[(size_t) k];
            }
            novelty[(size_t) i] = (nb > 0.0 && na > 0.0) ? 1.0 - dot / std::sqrt (nb * na) : 0.0;
        }
    }

    // --- cue 2: low-frequency energy right after the beat (kick drums)
    // --- cue 3: general onset strength
    std::vector<double> low (n), onset (n);
    for (size_t i = 0; i < n; ++i)
    {
        low[i] = a.lowBandAt (beats[i] + 0.03);
        onset[i] = a.peakOdfNear (beats[i], 0.03);
    }

    zNormalise (novelty);
    zNormalise (low);
    zNormalise (onset);

    std::vector<double> score ((size_t) bpb, 0.0);
    std::vector<int> count ((size_t) bpb, 0);
    for (size_t i = 0; i < n; ++i)
    {
        const auto p = i % (size_t) bpb;
        score[p] += novelty[i] + 0.6 * low[i] + 0.3 * onset[i];
        ++count[p];
    }

    int best = 0;
    for (int p = 1; p < bpb; ++p)
        if (score[(size_t) p] / std::max (1, count[(size_t) p]) > score[(size_t) best] / std::max (1, count[(size_t) best]))
            best = p;

    return best;
}

static std::vector<double> trimSilentRegions (const std::vector<double>& beats, const AnalysisResult& a)
{
    if (a.rmsEnvelope.empty() || beats.size() < 4)
        return beats;

    const float peak = *std::max_element (a.rmsEnvelope.begin(), a.rmsEnvelope.end());
    const float threshold = peak * 0.0056f; // ~ -45 dB below the loudest part

    size_t first = 0, last = a.rmsEnvelope.size() - 1;
    while (first < last && a.rmsEnvelope[first] < threshold) ++first;
    while (last > first && a.rmsEnvelope[last] < threshold) --last;

    const double soundStart = (double) first * a.envHop;
    const double soundEnd = (double) (last + 1) * a.envHop;
    const double m = medianInterval (beats);

    std::vector<double> out;
    for (double t : beats)
        if (t >= soundStart - 0.3 * m && t <= soundEnd + 0.3 * m)
            out.push_back (t);

    return out.size() >= 2 ? out : beats;
}

std::vector<double> shiftHalfBeat (const std::vector<double>& beats)
{
    if (beats.size() < 2)
        return beats;

    std::vector<double> out;
    out.reserve (beats.size() + 1);

    const double first = beats[0] - 0.5 * (beats[1] - beats[0]);
    if (first >= 0.0)
        out.push_back (first);

    for (size_t i = 0; i + 1 < beats.size(); ++i)
        out.push_back (0.5 * (beats[i] + beats[i + 1]));

    const size_t n = beats.size();
    out.push_back (beats[n - 1] + 0.5 * (beats[n - 1] - beats[n - 2]));
    return out;
}

std::vector<double> process (const AnalysisResult& a, const ProcessingOptions& o)
{
    std::vector<double> beats = a.rawBeats;
    if (beats.size() < 2)
        return beats;

    // octave correction into [minBpm, maxBpm]
    const double lo = std::min (o.minBpm, o.maxBpm), hi = std::max (o.minBpm, o.maxBpm);
    for (int guard = 0; guard < 4 && beats.size() >= 4; ++guard)
    {
        const double bpm = 60.0 / medianInterval (beats);
        if (bpm > hi && bpm * 0.5 >= lo * 0.9)        beats = halveTempo (beats, &a);
        else if (bpm < lo && bpm * 2.0 <= hi * 1.1)   beats = doubleTempo (beats);
        else break;
    }

    // BTrack folds tempi into 80..160 BPM, so slow songs come out doubled.
    // If every other beat is much weaker, the real beat is the strong half.
    if (beats.size() >= 16)
    {
        const double bpm = 60.0 / medianInterval (beats);
        if (bpm * 0.5 >= lo && alternateBeatStrengthRatio (beats, a) < 0.45)
            beats = halveTempo (beats, &a);
    }

    if (o.fixOutliers)
        beats = fixOutliers (beats, &a);

    // before snapping, so the moved beats still land on real transients
    if (o.halfBeatShift)
        beats = shiftHalfBeat (beats);

    if (o.trimSilence)
        beats = trimSilentRegions (beats, a);

    if (o.snapToTransients)
        beats = snapToTransients (beats, a);

    if (o.constantTempo)
        beats = fitConstantTempo (beats);
    else
        beats = smooth (beats, o.smoothing);

    const double offset = o.offsetMs * 0.001;
    std::vector<double> out;
    out.reserve (beats.size());
    for (double t : beats)
    {
        const double v = t + offset;
        if (v >= 0.0 && v <= a.durationSeconds + 1.0e-6 && (out.empty() || v > out.back() + 0.02))
            out.push_back (v);
    }

    return out;
}

} // namespace ops
} // namespace beatit
