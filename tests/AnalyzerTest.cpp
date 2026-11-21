// Headless end-to-end test: synthesize a song with a known variable tempo,
// run the analyzer, score the detected beats against ground truth, export the
// MIDI tempo map and verify (with JUCE's own MIDI timing code) that each beat
// in the MIDI file lands on the detected audio beat.
//
// Usage: BeatItTest [output-dir]   (writes test WAVs + MIDI there if given)

#include "BeatAnalyzer.h"
#include "MidiExporter.h"
#include "TempoMap.h"
#include "AudioEngine.h"
#include "AudioPadder.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>

using namespace beatit;

namespace
{
struct Song
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    std::vector<double> beats;      // ground truth
    int firstDownbeat = 0;          // index of a downbeat in `beats`
};

Song synthesize (const std::function<double (double)>& bpmAt, double duration, double leadIn,
                 bool hard, unsigned seed)
{
    Song s;
    const double sr = s.sampleRate;
    const int n = (int) (duration * sr);
    s.audio.setSize (2, n);
    s.audio.clear();

    for (double t = leadIn; t < duration - 0.5; t += 60.0 / bpmAt (t))
        s.beats.push_back (t);

    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    auto* L = s.audio.getWritePointer (0);
    auto* R = s.audio.getWritePointer (1);

    auto addKick = [&] (double t, float gain)
    {
        const int start = (int) (t * sr);
        double phase = 0.0;
        for (int i = 0; i < (int) (0.35 * sr) && start + i < n; ++i)
        {
            const double tt = i / sr;
            const double f = 50.0 + 110.0 * std::exp (-tt * 30.0);
            phase += juce::MathConstants<double>::twoPi * f / sr;
            const auto v = (float) (std::sin (phase) * std::exp (-tt * 9.0)) * gain;
            L[start + i] += v; R[start + i] += v;
        }
    };

    auto addSnare = [&] (double t, float gain)
    {
        const int start = (int) (t * sr);
        for (int i = 0; i < (int) (0.2 * sr) && start + i < n; ++i)
        {
            const double tt = i / sr;
            const auto v = (float) ((uni (rng) * 0.7 + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 190.0 * tt))
                                     * std::exp (-tt * 22.0)) * gain;
            L[start + i] += v * 0.9f; R[start + i] += v;
        }
    };

    auto addHat = [&] (double t, float gain)
    {
        const int start = (int) (t * sr);
        float prev = 0.0f;
        for (int i = 0; i < (int) (0.05 * sr) && start + i < n; ++i)
        {
            const float w = uni (rng);
            const float hp = w - prev; prev = w; // crude high-pass
            const auto v = hp * (float) std::exp (-(i / sr) * 90.0) * gain;
            L[start + i] += v; R[start + i] += v * 0.8f;
        }
    };

    auto addTone = [&] (double t0, double t1, double freq, float gain)
    {
        const int a = (int) (t0 * sr), b = std::min (n, (int) (t1 * sr));
        for (int i = a; i < b; ++i)
        {
            const double tt = (i - a) / sr;
            const double env = std::min (1.0, tt / 0.01) * std::exp (-tt * 1.5);
            const auto v = (float) (std::sin (juce::MathConstants<double>::twoPi * freq * tt) * env) * gain;
            L[i] += v; R[i] += v;
        }
    };

    const double bassNotes[] = { 55.0, 65.41, 49.0, 58.27 };
    const double padNotes[][3] = { { 220.0, 261.6, 329.6 }, { 261.6, 329.6, 392.0 },
                                   { 196.0, 246.9, 293.7 }, { 233.1, 293.7, 349.2 } };

    for (size_t i = 0; i < s.beats.size(); ++i)
    {
        const double t = s.beats[i];
        const double next = i + 1 < s.beats.size() ? s.beats[i + 1] : t + 0.5;
        const int beatInBar = (int) (i % 4);
        const int bar = (int) (i / 4);

        if (beatInBar == 0 || (! hard && beatInBar == 2)) addKick (t, 0.8f);
        if (hard && beatInBar == 2 && bar % 2 == 1)        addKick (t + 0.5 * (next - t), 0.6f); // syncopated
        if (beatInBar == 1 || beatInBar == 3)              addSnare (t, hard ? 0.35f : 0.45f);

        addHat (t, 0.12f);
        addHat (0.5 * (t + next), hard ? 0.10f : 0.07f);

        addTone (t, next, bassNotes[bar % 4], 0.18f);

        if (beatInBar == 0)
        {
            const double barEnd = i + 4 < s.beats.size() ? s.beats[i + 4] : t + 2.0;
            for (double f : padNotes[bar % 4])
                addTone (t + (hard ? 0.13 : 0.0), barEnd, f, hard ? 0.06f : 0.04f);
        }
    }

    for (int i = 0; i < n; ++i) { L[i] += uni (rng) * 0.002f; R[i] += uni (rng) * 0.002f; }

    s.audio.applyGain (0.7f);
    return s;
}

struct Score { double fMeasure, meanAbsErrMs, medianErrMs, p95ErrMs; int matched, truth, detected; };

Score score (const std::vector<double>& truth, const std::vector<double>& detected, double tol = 0.07)
{
    std::vector<double> errs;
    std::vector<bool> used (detected.size(), false);

    for (double t : truth)
    {
        int best = -1; double bestD = tol;
        for (size_t j = 0; j < detected.size(); ++j)
            if (! used[j] && std::abs (detected[j] - t) <= bestD) { bestD = std::abs (detected[j] - t); best = (int) j; }

        if (best >= 0) { used[(size_t) best] = true; errs.push_back (detected[(size_t) best] - t); }
    }

    Score s {};
    s.matched = (int) errs.size(); s.truth = (int) truth.size(); s.detected = (int) detected.size();
    const double p = detected.empty() ? 0 : (double) s.matched / detected.size();
    const double r = truth.empty() ? 0 : (double) s.matched / truth.size();
    s.fMeasure = p + r > 0 ? 2 * p * r / (p + r) : 0;

    if (! errs.empty())
    {
        double sum = 0; for (double e : errs) sum += std::abs (e);
        s.meanAbsErrMs = 1000.0 * sum / errs.size();
        auto sorted = errs; std::sort (sorted.begin(), sorted.end());
        s.medianErrMs = 1000.0 * sorted[sorted.size() / 2];
        std::vector<double> absErr; for (double e : errs) absErr.push_back (std::abs (e));
        std::sort (absErr.begin(), absErr.end());
        s.p95ErrMs = 1000.0 * absErr[(size_t) (0.95 * (absErr.size() - 1))];
    }
    return s;
}

/** Converts the MIDI file to seconds with JUCE and returns the click-note times. */
std::vector<double> midiClickTimes (juce::MidiFile midi, int& timeSigCount, int& tempoCount)
{
    // round-trip through bytes, exactly like a DAW would read the file
    juce::MemoryOutputStream mo;
    midi.writeTo (mo, 1);
    juce::MemoryInputStream mi (mo.getData(), mo.getDataSize(), false);
    juce::MidiFile loaded;
    loaded.readFrom (mi);
    loaded.convertTimestampTicksToSeconds();

    timeSigCount = 0; tempoCount = 0;
    std::vector<double> times;

    for (int tr = 0; tr < loaded.getNumTracks(); ++tr)
        for (auto* e : *loaded.getTrack (tr))
        {
            if (e->message.isTimeSignatureMetaEvent()) ++timeSigCount;
            if (e->message.isTempoMetaEvent()) ++tempoCount;
            if (e->message.isNoteOn()) times.push_back (e->message.getTimeStamp());
        }

    std::sort (times.begin(), times.end());
    return times;
}

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void runCase (const char* name, const Song& song, const juce::File& outDir, double minF)
{
    std::printf ("\n=== %s: %.1fs, %d true beats ===\n", name, song.audio.getNumSamples() / song.sampleRate, (int) song.beats.size());

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto result = BeatAnalyzer::analyze (song.audio, song.sampleRate);
    const auto t1 = juce::Time::getMillisecondCounterHiRes();

    check (result.has_value(), "analysis ran");
    if (! result) return;
    std::printf ("  analysis took %.0f ms\n", t1 - t0);

    auto raw = score (song.beats, result->rawBeats);
    std::printf ("  raw BTrack     : F=%.3f  matched %d/%d (detected %d)  mean|err| %.1f ms  median %.1f ms  p95 %.1f ms\n",
                 raw.fMeasure, raw.matched, raw.truth, raw.detected, raw.meanAbsErrMs, raw.medianErrMs, raw.p95ErrMs);

    if (juce::SystemStats::getEnvironmentVariable ("BEATIT_DUMP", "") == name)
    {
        for (double t : song.beats)
        {
            double nr = 1e9, nf = 1e9;
            for (double d : result->rawBeats) if (std::abs (d - t) < std::abs (nr)) nr = d - t;
            std::printf ("    true %7.3f  rawErr %+7.1f ms\n", t, nr * 1000);
        }
        for (double d : result->rawBeats) std::printf ("    raw %7.3f\n", d);
    }

    std::printf ("  alternate-beat strength ratio: %.3f\n", ops::alternateBeatStrengthRatio (result->rawBeats, *result));

    ProcessingOptions opts; // defaults, as the app uses them
    auto beats = ops::process (*result, opts);
    auto fin = score (song.beats, beats);
    std::printf ("  post-processed : F=%.3f  matched %d/%d (detected %d)  mean|err| %.1f ms  median %.1f ms  p95 %.1f ms\n",
                 fin.fMeasure, fin.matched, fin.truth, fin.detected, fin.meanAbsErrMs, fin.medianErrMs, fin.p95ErrMs);

    check (fin.fMeasure >= minF, "F-measure >= " + juce::String (minF, 2));
    check (std::abs (fin.medianErrMs) < 10.0, "median timing bias < 10 ms");

    // downbeat guess
    BeatGrid grid;
    grid.beats = beats;
    grid.downbeatOffset = ops::guessDownbeatOffset (beats, 4, *result);
    {
        // which detected beat is the true first downbeat (truth index 0)?
        int detIdx = grid.nearestBeat (song.beats[(size_t) song.firstDownbeat], 0.07);
        const bool ok = detIdx >= 0 && grid.isDownbeat (detIdx);
        check (ok, "downbeat guess lands on the bar start");
        if (! ok && detIdx >= 0)
            grid.downbeatOffset = detIdx % 4; // continue the MIDI test with the right bars
    }

    // MIDI: every exported beat must land on the detected audio beat
    MidiExportOptions mo;
    auto midi = MidiExporter::build (grid, mo);
    int timeSigs = 0, tempos = 0;
    auto midiTimes = midiClickTimes (midi, timeSigs, tempos);

    double maxErr = 0.0;
    const bool sameCount = midiTimes.size() == grid.beats.size();
    if (sameCount)
        for (size_t i = 0; i < midiTimes.size(); ++i)
            maxErr = std::max (maxErr, std::abs (midiTimes[i] - grid.beats[i]));

    const auto layout = grid.computeLayout();
    std::printf ("  MIDI: %d tempo events, %d time signatures, pre-roll %d beats, pickup %d beats, max beat error %.4f ms\n",
                 tempos, timeSigs, layout.preRollBeats, layout.pickupBeats, maxErr * 1000.0);
    check (sameCount, "MIDI contains one click per beat");
    check (maxErr < 0.0005, "MIDI beats match audio beats within 0.5 ms");

    // the downbeat must be on beat 1 of a bar in the export
    {
        const int d = grid.firstDownbeatIndex();
        auto [bar, beat] = grid.barAndBeatOfDetected (d, layout);
        check (beat == 1, "first downbeat is beat 1 of bar " + juce::String (bar));
    }

    if (outDir != juce::File())
    {
        juce::WavAudioFormat wav;
        auto f = outDir.getChildFile (juce::String (name) + ".wav");
        f.deleteFile();
        if (auto os = f.createOutputStream())
        {
            std::unique_ptr<juce::OutputStream> stream (os.release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                           .withSampleRate (song.sampleRate)
                                                           .withNumChannels (2)
                                                           .withBitsPerSample (24));
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (song.audio, 0, song.audio.getNumSamples());
        }
        MidiExporter::exportToFile (grid, mo, outDir.getChildFile (juce::String (name) + ".mid"));
        std::printf ("  wrote %s(.wav/.mid)\n", f.getFileNameWithoutExtension().toRawUTF8());
    }
}

void testLayoutEdgeCases()
{
    std::printf ("\n=== layout / MIDI edge cases ===\n");

    auto roundTrip = [] (const BeatGrid& g)
    {
        int ts = 0, te = 0;
        auto times = midiClickTimes (MidiExporter::build (g, {}), ts, te);
        double maxErr = 0.0;
        if (times.size() != g.beats.size()) return 1.0;
        for (size_t i = 0; i < times.size(); ++i) maxErr = std::max (maxErr, std::abs (times[i] - g.beats[i]));
        return maxErr;
    };

    // pickup: first detected beat is beat 4 of a bar, 3/4 time, 6/8 time
    for (int bpb : { 4, 3, 6 })
        for (int den : { 4, 8 })
            for (int off : { 0, 1, 2 })
            {
                BeatGrid g;
                g.beatsPerBar = bpb; g.denominator = den; g.downbeatOffset = off;
                double t = 0.737;
                for (int i = 0; i < 64; ++i) { g.beats.push_back (t); t += 0.45 + 0.05 * std::sin (i * 0.3); }

                const auto layout = g.computeLayout();
                auto [bar, beat] = g.barAndBeatOfDetected (g.firstDownbeatIndex(), layout);
                const double err = roundTrip (g);
                check (beat == 1 && err < 0.0005,
                       juce::String (bpb) + "/" + juce::String (den) + " offset " + juce::String (off)
                       + ": downbeat on bar " + juce::String (bar) + " beat " + juce::String (beat)
                       + ", max err " + juce::String (err * 1000.0, 4) + " ms");
            }

    // first beat very close to zero (absorbed) and exactly at zero
    for (double first : { 0.0, 0.03 })
    {
        BeatGrid g;
        for (int i = 0; i < 32; ++i) g.beats.push_back (first + i * 0.5);
        const double err = roundTrip (g);
        // the first beat may be moved to 0 when it is < 1/16 s; all later beats exact
        int ts = 0, te = 0;
        auto times = midiClickTimes (MidiExporter::build (g, {}), ts, te);
        double laterErr = 0.0;
        for (size_t i = 1; i < times.size(); ++i) laterErr = std::max (laterErr, std::abs (times[i] - g.beats[i]));
        check (laterErr < 0.0005 && err <= first + 0.0005,
               "first beat at " + juce::String (first, 2) + "s: later beats exact (" + juce::String (laterErr * 1000.0, 4) + " ms)");
    }

    // constant tempo export collapses to a single tempo event
    {
        BeatGrid g;
        for (int i = 0; i < 100; ++i) g.beats.push_back (2.0 + i * 0.5);
        int ts = 0, te = 0;
        midiClickTimes (MidiExporter::build (g, {}), ts, te);
        check (te <= 3, "constant 120 BPM exports few tempo events (" + juce::String (te) + ")");
    }
}
/** Regression: the backward pass locks onto the offbeats for a while (seen on a real
    song around a breakdown). The merge must not interleave both passes into a
    double-tempo sequence. */
/** Exports the metronome as a WAV and checks length, sample rate, click timing and accents. */
void testMetronomeExport()
{
    std::printf ("\n=== metronome audio export ===\n");

    const double rate = 48000.0;
    const juce::int64 numSamples = (juce::int64) (rate * 6.0) + 123;

    std::vector<double> beats;
    for (double t = 0.2371; t < 5.8; t += 0.45 + 0.0137 * (double) beats.size()) beats.push_back (t);
    std::vector<bool> downs (beats.size());
    for (size_t i = 0; i < beats.size(); ++i) downs[i] = i % 3 == 0;

    juce::TemporaryFile temp (".wav");
    const auto r = AudioEngine::exportMetronome (beats, downs, true, rate, numSamples, temp.getFile());
    check (r.wasOk(), "metronome WAV written " + r.getErrorMessage());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (temp.getFile().createInputStream().release(), true));
    check (reader != nullptr, "metronome WAV readable");
    if (reader == nullptr)
        return;

    check (reader->lengthInSamples == numSamples && reader->sampleRate == rate && reader->numChannels == 1,
           "WAV is mono, " + juce::String (reader->lengthInSamples) + " samples at " + juce::String (reader->sampleRate) + " Hz");

    juce::AudioBuffer<float> buf (1, (int) reader->lengthInSamples);
    reader->read (&buf, 0, buf.getNumSamples(), 0, true, false);
    const float* d = buf.getReadPointer (0);

    // onset = first sample above threshold after silence, extrapolated back to the zero crossing
    std::vector<double> onsets;
    std::vector<float> peaks;
    int silent = 100000;
    for (int i = 0; i + 1 < buf.getNumSamples(); ++i)
    {
        if (std::abs (d[i]) > 1.0e-4f)
        {
            if (silent > (int) (0.02 * rate))
            {
                const double slope = d[i + 1] - d[i];
                onsets.push_back ((i - (slope != 0.0 ? d[i] / slope : 0.0)) / rate);
                peaks.push_back (0.0f);
            }
            peaks.back() = std::max (peaks.back(), std::abs (d[i]));
            silent = 0;
        }
        else ++silent;
    }

    double maxErr = 0.0;
    bool accentsOk = true, levelOk = true;
    const bool countOk = onsets.size() == beats.size();
    if (countOk)
        for (size_t i = 0; i < beats.size(); ++i)
        {
            maxErr = std::max (maxErr, std::abs (onsets[i] - beats[i]));
            accentsOk = accentsOk && (downs[i] ? peaks[i] > 0.9f : peaks[i] < 0.7f);
            levelOk = levelOk && peaks[i] <= 1.0f;
        }

    check (countOk && maxErr < 0.25 / rate,
           juce::String ((int) onsets.size()) + "/" + juce::String ((int) beats.size())
           + " clicks, max error " + juce::String (maxErr * rate, 3) + " samples");
    const float filePeak = buf.getMagnitude (0, 0, buf.getNumSamples());
    check (accentsOk && levelOk && filePeak > 0.9999f,
           "bar starts accented, normalised to " + juce::String (juce::Decibels::gainToDecibels (filePeak), 4) + " dBFS peak");
}

/** Pre-count: shifted grid, MIDI alignment, metronome length and padded stems all agree. */
void testPreCount()
{
    std::printf ("\n=== pre-count ===\n");

    const double songRate = 48000.0;
    BeatGrid grid;
    for (double t = 0.731; t < 20.0; t += 0.5 + 0.002 * (double) grid.beats.size()) grid.beats.push_back (t);
    grid.beatsPerBar = 4;
    grid.downbeatOffset = 2;   // pickup of 2 beats

    for (int n : { 1, 3, 4, 7 })
    {
        const auto pc = ops::computePreCount (grid, n, songRate);
        const auto eg = ops::applyPreCount (grid, pc);
        const double s = pc.silenceSeconds();
        const double expected = n * (grid.beats[4] - grid.beats[0]) / 4.0;

        bool shiftOk = eg.beats.size() == grid.beats.size() + (size_t) n && std::abs (s - expected) <= 0.5 / songRate
                    && std::abs (s * songRate - (double) pc.silenceSamples) < 1.0e-6;
        for (size_t i = 0; shiftOk && i < grid.beats.size(); ++i)
            shiftOk = std::abs (eg.beats[i + (size_t) n] - (grid.beats[i] + s)) < 1.0e-9
                   && eg.isDownbeat ((int) i + n) == grid.isDownbeat ((int) i);
        for (int k = 0; shiftOk && k < n; ++k)
            shiftOk = std::abs ((eg.beats[(size_t) k + 1] - eg.beats[(size_t) k]) - s / n) < 1.0e-9 && eg.beats[0] == grid.beats[0];

        int timeSigs = 0, tempos = 0;
        auto midiTimes = midiClickTimes (MidiExporter::build (eg, MidiExportOptions{}), timeSigs, tempos);
        double maxErr = 0.0;
        const bool countOk = midiTimes.size() == eg.beats.size();
        for (size_t i = 0; countOk && i < midiTimes.size(); ++i)
            maxErr = std::max (maxErr, std::abs (midiTimes[i] - eg.beats[i]));

        const auto el = eg.computeLayout();
        const bool barOk = grid.barAndBeatOfDetected (grid.firstDownbeatIndex(), grid.computeLayout()).second == 1
                        && eg.barAndBeatOfDetected (grid.firstDownbeatIndex() + n, el).second == 1;

        check (shiftOk && countOk && maxErr < 0.0005 && barOk,
               juce::String (n) + " beats: silence " + juce::String (s, 4) + " s (" + juce::String ((juce::int64) pc.silenceSamples)
               + " samples), MIDI max error " + juce::String (maxErr * 1000.0, 4) + " ms, bar start kept");
    }

    // padding: same-rate files get the exact sample count, others the nearest sample
    const auto pc = ops::computePreCount (grid, 4, songRate);
    juce::TemporaryFile dir;
    dir.getFile().createDirectory();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    struct Src { const char* name; double rate; int channels, bits; };
    for (const auto& src : { Src { "song", 48000.0, 2, 24 }, Src { "stem", 44100.0, 1, 16 } })
    {
        const int len = (int) (src.rate * 1.5) + 7;
        juce::AudioBuffer<float> in (src.channels, len);
        for (int ch = 0; ch < src.channels; ++ch)
            for (int i = 0; i < len; ++i)
                in.setSample (ch, i, 0.5f * (float) std::sin (0.01 * i + ch));

        const auto srcFile = dir.getFile().getChildFile (juce::String (src.name) + ".wav");
        {
            std::unique_ptr<juce::OutputStream> os (srcFile.createOutputStream().release());
            juce::WavAudioFormat wav;
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (src.rate)
                                                  .withNumChannels (src.channels).withBitsPerSample (src.bits));
            w->writeFromAudioSampleBuffer (in, 0, len);
        }

        const auto outFile = dir.getFile().getChildFile ("out").getChildFile (juce::String (src.name) + ".wav");
        outFile.getParentDirectory().createDirectory();
        const auto r = AudioPadder::padFile (formats, srcFile, outFile, pc);

        std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (outFile));
        const auto silence = pc.silenceSamplesAt (src.rate);
        bool ok = r.wasOk() && rd != nullptr && rd->sampleRate == src.rate && (int) rd->numChannels == src.channels
               && (int) rd->bitsPerSample == src.bits && rd->lengthInSamples == len + silence
               && silence == std::llround (pc.silenceSeconds() * src.rate);
        if (ok)
        {
            juce::AudioBuffer<float> out (src.channels, (int) rd->lengthInSamples);
            rd->read (&out, 0, out.getNumSamples(), 0, true, true);
            const float tol = src.bits == 16 ? 1.0e-4f : 1.0e-6f;
            for (int ch = 0; ok && ch < src.channels; ++ch)
            {
                ok = out.getMagnitude (ch, 0, (int) silence) == 0.0f;
                for (int i = 0; ok && i < len; ++i)
                    ok = std::abs (out.getSample (ch, (int) silence + i) - in.getSample (ch, i)) < tol;
            }
        }
        check (ok, juce::String (src.name) + " @ " + juce::String (src.rate, 0) + " Hz: +" + juce::String ((juce::int64) silence)
                   + " samples of silence, audio intact " + r.getErrorMessage());
    }

    // file discovery: top-level audio files only, name collisions get distinct outputs
    dir.getFile().getChildFile ("notes.txt").replaceWithText ("x");
    dir.getFile().getChildFile ("song.aiff").replaceWithText ("x");
    const auto found = AudioPadder::findAudioFiles (formats, dir.getFile());
    const auto dests = AudioPadder::destinationsFor (found, dir.getFile().getChildFile ("out"));
    juce::StringArray names;
    for (const auto& d : dests) names.add (d.getFileName());
    check (found.size() == 3 && names.joinIntoString (",") == "song (aiff).wav,song (wav).wav,stem.wav",
           "found " + juce::String (found.size()) + " top-level audio files -> " + names.joinIntoString (", "));

    dir.getFile().deleteRecursively();
}

/** Playback with a pre-count lead-in: audio is delayed by exactly the silence, the count-in clicks. */
void testEngineLeadIn()
{
    std::printf ("\n=== playback with pre-count lead-in ===\n");

    const double rate = 48000.0;
    auto audio = std::make_shared<juce::AudioBuffer<float>> (1, (int) (rate * 3.0));
    audio->clear();
    const int impulseAt = 12345;
    audio->setSample (0, impulseAt, 1.0f);

    BeatGrid grid;
    for (double t = 0.5; t < 2.9; t += 0.5) grid.beats.push_back (t);
    const auto pc = ops::computePreCount (grid, 2, rate);   // 1.0 s
    const auto shown = ops::applyPreCount (grid, pc);

    AudioEngine engine;
    engine.prepareToPlay (512, rate);
    engine.setAudio (audio, rate);
    engine.setPosition (1.0);
    engine.setLeadIn (pc.silenceSamples);
    const bool posKept = std::abs (engine.getPosition() - (1.0 + pc.silenceSeconds())) < 1.0e-9
                      && std::abs (engine.getDuration() - (3.0 + pc.silenceSeconds())) < 1.0e-9;
    engine.setPosition (0.0);

    std::vector<bool> downs (shown.beats.size(), false);
    engine.setBeats (shown.beats, downs);

    // music only: the impulse must come out exactly silenceSamples later
    engine.setMusicGain (1.0f);
    engine.setMetronomeEnabled (false);
    engine.play();
    const int total = (int) (rate * 4.5);
    juce::AudioBuffer<float> out (1, total), block (1, 512);
    out.clear();
    for (int pos = 0; pos < total; pos += 512)
    {
        block.clear();
        juce::AudioSourceChannelInfo info (&block, 0, 512);
        engine.getNextAudioBlock (info);
        out.copyFrom (0, pos, block, 0, 0, std::min (512, total - pos));
    }

    int peakAt = -1;
    float peak = 0.0f;
    for (int i = 0; i < total; ++i)
        if (std::abs (out.getSample (0, i)) > peak) { peak = std::abs (out.getSample (0, i)); peakAt = i; }

    // music smoothing ramps the gain from its default, so only the position is checked
    check (posKept && peakAt == impulseAt + (int) pc.silenceSamples,
           "audio delayed by " + juce::String ((juce::int64) pc.silenceSamples) + " samples (impulse at "
           + juce::String (peakAt) + "), playhead kept its place in the music");
}

/** Half-beat shift moves the grid onto the off-beats and keeps the tempo. */
void testHalfBeatShift()
{
    std::printf ("\n=== half-beat shift ===\n");

    std::vector<double> beats;
    for (double t = 0.4; t < 10.0; t += 0.6 + 0.001 * (double) beats.size()) beats.push_back (t);
    const auto shifted = ops::shiftHalfBeat (beats);

    bool ok = shifted.size() == beats.size() + 1 && std::abs (shifted[0] - (beats[0] - 0.5 * (beats[1] - beats[0]))) < 1.0e-12;
    for (size_t i = 0; ok && i + 1 < beats.size(); ++i)
        ok = std::abs (shifted[i + 1] - 0.5 * (beats[i] + beats[i + 1])) < 1.0e-12;
    ok = ok && shifted.back() > beats.back();

    const auto twice = ops::shiftHalfBeat (ops::shiftHalfBeat (beats));
    double maxErr = 0.0;   // two half shifts = one whole beat, so the interior must line up again
    for (size_t i = 2; i + 2 < beats.size(); ++i)
        maxErr = std::max (maxErr, std::abs (twice[i] - beats[i]));   // 2nd shift adds no beat in front (< 0)

    check (ok && maxErr < 0.002, "midpoints + one beat in front; two shifts land back on the grid (max "
                                 + juce::String (maxErr * 1000.0, 3) + " ms)");
}

void testMergeOffbeatPasses()
{
    std::printf ("\n=== merge: passes half a beat apart ===\n");

    for (double offbeatStrength : { 0.6, 1.3 })
    {
        AnalysisResult a;
        a.durationSeconds = 60.0;
        a.odfHop = 128.0 / 44100.0;
        a.odfTimeOffset = 0.0;
        a.odf.assign ((size_t) (60.0 / a.odfHop), 0.05f);

        std::vector<double> truth, fwd, bwd;
        for (double t = 0.5; t < 59.0; t += 0.5)
        {
            truth.push_back (t);
            a.odf[(size_t) std::lround (t / a.odfHop)] = 1.0f;
            a.odf[(size_t) std::lround ((t + 0.25) / a.odfHop)] = (float) offbeatStrength;
        }

        fwd = truth;
        for (double t : truth)
            bwd.push_back (t >= 20.0 && t < 35.0 ? t + 0.25 : t);  // offbeat-locked section

        auto merged = BeatAnalyzer::mergePasses (fwd, bwd, a);

        int doubled = 0;
        for (size_t i = 1; i < merged.size(); ++i)
            if (merged[i] - merged[i - 1] < 0.4) ++doubled;

        const auto sc = score (truth, merged, 0.07);
        const bool phaseOk = offbeatStrength > 1.0 || sc.fMeasure > 0.99; // with stronger offbeats either phase is defensible
        check (doubled == 0 && phaseOk,
               "offbeat strength " + juce::String (offbeatStrength, 1) + ": " + juce::String ((int) merged.size())
               + " beats, " + juce::String (doubled) + " doubled intervals, F=" + juce::String (sc.fMeasure, 3));
    }
}

/** Regression: one pass counts eighth notes (double tempo) during the intro while
    the other has the real tempo. The merge must follow the song's overall tempo. */
void testMergeDoubleTempoIntro()
{
    std::printf ("\n=== merge: one pass at double tempo in the intro ===\n");

    AnalysisResult a;
    a.durationSeconds = 80.0;
    a.odfHop = 128.0 / 44100.0;
    a.odf.assign ((size_t) (80.0 / a.odfHop), 0.05f);

    std::vector<double> truth, fwd, bwd;
    const double period = 60.0 / 82.0;
    for (double t = 0.5; t < 79.0; t += period)
    {
        truth.push_back (t);
        a.odf[(size_t) std::lround (t / a.odfHop)] = 1.0f;
        a.odf[(size_t) std::lround ((t + period / 2) / a.odfHop)] = 0.8f; // strong eighth notes
    }

    for (double t : truth)
    {
        fwd.push_back (t);
        if (t < 15.0) fwd.push_back (t + period / 2);   // forward pass at double tempo in the intro
    }
    std::sort (fwd.begin(), fwd.end());
    bwd = truth;

    auto merged = BeatAnalyzer::mergePasses (fwd, bwd, a);
    const auto sc = score (truth, merged, 0.07);
    check (sc.fMeasure > 0.99, "intro follows the real tempo: " + juce::String ((int) merged.size()) + " beats (truth "
                               + juce::String ((int) truth.size()) + "), F=" + juce::String (sc.fMeasure, 3));
}

/** Regression: the song opens with loud pickup notes before the first beat. The
    forward pass, still settling from its cold start, puts beats on them at a faster,
    slowing-down tempo; the backward pass (fully locked by then) has the real grid. */
void testMergeColdStart()
{
    std::printf ("\n=== merge: forward pass still settling at the song start ===\n");

    // Onset strengths relative to a typical beat, as measured on the real song that
    // showed the problem: quiet first beat, then loud pickup notes off the grid.
    // `harsh` also makes the second true beat quiet: that case is genuinely
    // ambiguous from the audio alone, so it is reported but not required to pass.
    for (bool harsh : { false, true })
    {
        AnalysisResult a;
        a.durationSeconds = 60.0;
        a.odfHop = 128.0 / 44100.0;
        a.odf.assign ((size_t) (60.0 / a.odfHop), 0.05f);

        const double period = 60.0 / 98.0;
        std::vector<double> truth, fwd, bwd;
        for (double t = 1.32; t < 59.0; t += period)
        {
            truth.push_back (t);
            float strength = 1.0f;
            if (truth.size() == 1) strength = 0.13f;
            if (truth.size() == 2) strength = harsh ? 0.13f : 1.4f;
            a.odf[(size_t) std::lround (t / a.odfHop)] = strength;
        }

        const std::pair<double, float> pickups[] = { { 1.00, 1.7f }, { 1.52, 1.15f }, { 2.03, 0.45f } };
        for (auto [t, v] : pickups)
            a.odf[(size_t) std::lround (t / a.odfHop)] = v;

        fwd = { 0.46, 1.00, 1.52, 2.03, 2.59 };   // 115 BPM slowing down towards 98
        for (double t : truth) if (t > 3.0) fwd.push_back (t);
        bwd = { 0.10, 0.71 };                     // backward pass extrapolates the grid into the silence
        for (double t : truth) bwd.push_back (t);

        auto merged = BeatAnalyzer::mergePasses (fwd, bwd, a);
        std::vector<double> firstBeats;
        for (double t : merged) if (t < 3.5) firstBeats.push_back (t);

        bool onGrid = true;
        for (double t : firstBeats)
            if (t > 0.9 && std::abs (std::remainder (t - truth[0], period)) > 0.03)
                onGrid = false;

        juce::String list;
        for (double t : firstBeats) list << juce::String (t, 2) << " ";

        if (harsh)
            std::printf ("  [INFO] ambiguous variant (two quiet true beats): first beats %s -> %s\n",
                         list.trim().toRawUTF8(), onGrid ? "on grid" : "follows the pickup notes");
        else
            check (onGrid, "first beats follow the steady grid: " + list.trim());
    }
}

/** Renders the playback engine offline and checks the metronome clicks land on the beats. */
void testMetronome()
{
    std::printf ("\n=== playback engine / metronome ===\n");

    for (double speed : { 1.0, 0.5 })
    {
        const double fileRate = 48000.0, outRate = 44100.0;   // force sample-rate conversion
        auto audio = std::make_shared<juce::AudioBuffer<float>> (2, (int) (fileRate * 12.0));
        audio->clear();

        std::vector<double> beats;
        for (double t = 0.25; t < 5.5; t += 0.5 + 0.01 * (double) beats.size()) beats.push_back (t);
        std::vector<bool> downs (beats.size());
        for (size_t i = 0; i < beats.size(); ++i) downs[i] = i % 4 == 0;

        AudioEngine engine;
        engine.prepareToPlay (512, outRate);
        engine.setAudio (audio, fileRate);
        engine.setBeats (beats, downs);
        engine.setMusicGain (0.0f);
        engine.setClickGain (1.0f);
        engine.setMetronomeEnabled (true);
        engine.setSpeed (speed);
        engine.play();

        const int total = (int) (outRate * 6.0 / speed);
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        juce::AudioBuffer<float> block (2, 512);
        for (int pos = 0; pos < total; pos += 512)
        {
            block.clear();
            juce::AudioSourceChannelInfo info (&block, 0, 512);
            engine.getNextAudioBlock (info);
            out.copyFrom (0, pos, block, 0, 0, std::min (512, total - pos));
        }

        // click onsets = first non-silent sample after >= 20 ms of silence
        std::vector<double> onsets;
        const float* d = out.getReadPointer (0);
        int silent = 100000;
        for (int i = 0; i < total; ++i)
        {
            if (std::abs (d[i]) > 1.0e-4f) { if (silent > (int) (0.02 * outRate)) onsets.push_back (i / outRate); silent = 0; }
            else ++silent;
        }

        double maxErr = 0.0;
        const bool countOk = onsets.size() == beats.size();
        if (countOk)
            for (size_t i = 0; i < beats.size(); ++i)
                maxErr = std::max (maxErr, std::abs (onsets[i] - beats[i] / speed));

        // <= 1 sample of scheduling quantisation + 1 sample because the click's
        // sine starts at phase 0 (first sample is silent to the onset detector)
        check (countOk && maxErr < 2.5 / outRate,
               "speed " + juce::String (speed, 1) + ": " + juce::String ((int) onsets.size()) + "/" + juce::String ((int) beats.size())
               + " clicks, max error " + juce::String (maxErr * outRate, 2) + " samples");
    }
}
} // namespace

/** --compare ref.wav other.mp3 : decode both with the app's decoders (CoreAudio for MP3),
    report their alignment and compare the analyzed beats. */
int compareFiles (const juce::File& refFile, const juce::File& otherFile)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    auto load = [&] (const juce::File& f, double& rate) -> juce::AudioBuffer<float>
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) { std::printf ("cannot read %s\n", f.getFullPathName().toRawUTF8()); return {}; }
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        rate = r->sampleRate;
        std::printf ("%s: %s, %.0f Hz, %d ch, %.4f s\n", f.getFileName().toRawUTF8(), r->getFormatName().toRawUTF8(),
                     rate, (int) r->numChannels, r->lengthInSamples / rate);
        return b;
    };

    double ra = 0, rb = 0;
    auto a = load (refFile, ra), b = load (otherFile, rb);
    if (a.getNumSamples() == 0 || b.getNumSamples() == 0 || std::abs (ra - rb) > 0.5) return 1;

    // sample offset via brute-force cross-correlation over +-3000 samples on 5 s of audio
    const float* x = a.getReadPointer (0); const float* y = b.getReadPointer (0);
    const int n = std::min ({ a.getNumSamples(), b.getNumSamples() - 3000, (int) (ra * 5) }) ;
    int bestLag = 0; double best = -1e300;
    for (int lag = -3000; lag <= 3000; ++lag)
    {
        double acc = 0;
        for (int i = 3000; i < n; i += 2) acc += (double) x[i] * y[i + lag];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    std::printf ("alignment: other file is offset by %d samples (%.2f ms)\n", bestLag, bestLag * 1000.0 / ra);

    auto ba = ops::process (*BeatAnalyzer::analyze (a, ra), {});
    auto bb = ops::process (*BeatAnalyzer::analyze (b, rb), {});
    auto sc = score (ba, bb, 0.05);
    std::printf ("beats: %d vs %d, F=%.3f, median diff %.2f ms, mean|diff| %.2f ms\n",
                 (int) ba.size(), (int) bb.size(), sc.fMeasure, sc.medianErrMs, sc.meanAbsErrMs);
    return std::abs (bestLag) <= 2 && sc.fMeasure > 0.98 ? 0 : 1;
}

/** --dump file : print the analysis of a real file beat by beat. */
int dumpFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) { std::printf ("cannot read file\n"); return 1; }
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);

    auto res = BeatAnalyzer::analyze (b, r->sampleRate);
    if (! res) return 1;

    BeatGrid g;
    ProcessingOptions po;
    po.halfBeatShift = juce::SystemStats::getEnvironmentVariable ("BEATIT_HALF", "").isNotEmpty();
    g.beats = ops::process (*res, po);
    g.downbeatOffset = ops::guessDownbeatOffset (g.beats, 4, *res);
    const auto layout = g.computeLayout();

    auto nearest = [] (const std::vector<double>& v, double t)
    {
        double best = 1e9; for (double x : v) if (std::abs (x - t) < std::abs (best)) best = x - t; return best * 1000.0;
    };

    std::printf ("duration %.2f s, raw %d, fwd %d, bwd %d, final %d beats, avg %.2f BPM, ratio %.3f\n",
                 res->durationSeconds, (int) res->rawBeats.size(), (int) res->forwardBeats.size(),
                 (int) res->backwardBeats.size(), (int) g.beats.size(), g.averageBpm(),
                 ops::alternateBeatStrengthRatio (res->rawBeats, *res));
    if (juce::SystemStats::getEnvironmentVariable ("BEATIT_PASSES", "").isNotEmpty())
    {
        // per-pass local tempo every 5 s
        auto localBpm = [] (const std::vector<double>& v, double t0, double t1)
        {
            int n = 0; double first = 0, last = 0;
            for (double x : v) if (x >= t0 && x < t1) { if (n == 0) first = x; last = x; ++n; }
            return n > 1 ? 60.0 * (n - 1) / (last - first) : 0.0;
        };
        for (double t = 0; t < res->durationSeconds; t += 5.0)
            std::printf ("  %6.1f s  fwd %6.1f  bwd %6.1f  merged %6.1f  final %6.1f  rms %.3f\n", t,
                         localBpm (res->forwardBeats, t, t + 5), localBpm (res->backwardBeats, t, t + 5),
                         localBpm (res->rawBeats, t, t + 5), localBpm (g.beats, t, t + 5), res->rmsAt (t + 2.5));
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("BEATIT_START", "").isNotEmpty())
    {
        auto show = [&] (const char* label, const std::vector<double>& v)
        {
            std::printf ("  %-6s", label);
            for (double x : v) if (x < 4.5) std::printf (" %.3f(%.0f)", x, res->peakOdfNear (x, 0.03));
            std::printf ("\n");
        };
        show ("fwd", res->forwardBeats); show ("bwd", res->backwardBeats); show ("merged", res->rawBeats);
        std::printf ("  rms per 0.25 s:");
        for (double t = 0; t < 4.5; t += 0.25) std::printf (" %.3f", res->rmsAt (t));
        std::printf ("\n");
        return 0;
    }
    std::printf ("  idx  time      bar.beat  bpm      dFwd(ms) dBwd(ms) odf\n");
    for (int i = 0; i < (int) g.beats.size(); ++i)
    {
        auto [bar, beat] = g.barAndBeatOfDetected (i, layout);
        std::printf ("  %4d %8.3f  %4d.%d  %7.2f  %+8.1f %+8.1f %.3f\n", i, g.beats[(size_t) i], bar, beat,
                     g.bpmOfInterval (i), nearest (res->forwardBeats, g.beats[(size_t) i]),
                     nearest (res->backwardBeats, g.beats[(size_t) i]), res->peakOdfNear (g.beats[(size_t) i], 0.03));
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc == 3 && juce::String (argv[1]) == "--dump")
        return dumpFile (juce::File (argv[2]));

    if (argc == 4 && juce::String (argv[1]) == "--compare")
        return compareFiles (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                             juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]));

    juce::File outDir;
    if (argc > 1)
    {
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
        outDir.createDirectory();
    }

    // 1) steady -> ramp up -> steady -> sudden drop -> ramp up
    auto tempoA = [] (double t)
    {
        if (t < 20) return 100.0;
        if (t < 40) return 100.0 + (t - 20) / 20.0 * 32.0;
        if (t < 60) return 132.0;
        if (t < 80) return 92.0;
        return 92.0 + (t - 80) / 15.0 * 18.0;
    };
    runCase ("variable_tempo", synthesize (tempoA, 96.0, 1.25, false, 1), outDir, 0.95);

    // 2) harder groove: syncopated kick, louder pad, continuous drift (rubato-like)
    auto tempoB = [] (double t) { return 118.0 + 10.0 * std::sin (t / 12.0) + 0.15 * t; };
    runCase ("drifting_groove", synthesize (tempoB, 80.0, 0.4, true, 7), outDir, 0.90);

    // 3) slow ballad tempo (BTrack's native range is 80-160)
    auto tempoC = [] (double t) { return 72.0 + t * 0.1; };
    runCase ("slow_ballad", synthesize (tempoC, 70.0, 2.0, false, 3), outDir, 0.90);

    testLayoutEdgeCases();
    testMetronome();
    testMetronomeExport();
    testPreCount();
    testEngineLeadIn();
    testHalfBeatShift();
    testMergeOffbeatPasses();
    testMergeDoubleTempoIntro();
    testMergeColdStart();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
