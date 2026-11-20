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

int main (int argc, char** argv)
{
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

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
