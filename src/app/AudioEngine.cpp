#include "AudioEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // click sound, shared by playback and offline rendering
    constexpr double clickLength = 0.06;          // seconds
    constexpr double clickDecay  = 0.012;         // envelope time constant, seconds
    constexpr double accentFreq = 1760.0, normalFreq = 1100.0;
    constexpr double accentGain = 1.0,    normalGain = 0.65;
}

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() = default;

void AudioEngine::setAudio (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double rate)
{
    pause();

    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        audioSamples = buffer != nullptr ? buffer->getNumSamples() : 0;
        audio = std::move (buffer);
        fileRate = rate > 0.0 ? rate : 44100.0;
        leadIn.store (0);
        durationSeconds.store (audioSamples / fileRate);
    }

    setPosition (0.0);
}

void AudioEngine::setLeadIn (long long samples)
{
    samples = std::max (0LL, samples);
    long long delta;
    double rate;
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        delta = samples - leadIn.load();
        rate = fileRate;
        leadIn.store (samples);
        durationSeconds.store (audioSamples > 0 ? (double) (audioSamples + samples) / fileRate : 0.0);
    }

    if (delta != 0)
        setPosition (positionSeconds.load() + (double) delta / rate);
}

void AudioEngine::setBeats (std::vector<double> times, std::vector<bool> downbeats)
{
    auto data = std::make_shared<BeatData>();
    data->times = std::move (times);
    data->downbeats = std::move (downbeats);
    data->downbeats.resize (data->times.size(), false);

    std::shared_ptr<const BeatData> old;
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        old = std::move (beatData);
        beatData = std::move (data);
    }
    // `old` is released here, on the message thread
    resyncBeats.store (true);
}

void AudioEngine::play()
{
    if (durationSeconds.load() <= 0.0)
        return;

    if (positionSeconds.load() >= durationSeconds.load() - 0.01)
        setPosition (0.0);

    playing.store (true);
}

void AudioEngine::pause()  { playing.store (false); }
void AudioEngine::stop()   { playing.store (false); setPosition (0.0); }

void AudioEngine::setPosition (double seconds)
{
    seconds = juce::jlimit (0.0, std::max (0.0, durationSeconds.load()), seconds);
    positionSeconds.store (seconds);
    pendingSeek.store (seconds);
}

void AudioEngine::setLoop (double start, double end, bool enabled)
{
    loopStart.store (start);
    loopEnd.store (end);
    loopOn.store (enabled && end > start + 0.05);
}

void AudioEngine::prepareToPlay (int, double sampleRate)
{
    outputRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    pendingSeek.store (positionSeconds.load());
}

void AudioEngine::triggerClick (bool isDownbeat)
{
    const bool acc = accent.load() && isDownbeat;
    voice.phase = 0.0;
    voice.freq = acc ? accentFreq : normalFreq;
    voice.gain = acc ? accentGain : normalGain;
    voice.env = 1.0;
    voice.samplesLeft = (int) (clickLength * outputRate);
}

float AudioEngine::renderClickSample()
{
    if (voice.samplesLeft <= 0)
        return 0.0f;

    const double decay = std::exp (-1.0 / (clickDecay * outputRate));
    const auto s = (float) (std::sin (voice.phase) * voice.env * voice.gain);
    voice.phase += juce::MathConstants<double>::twoPi * voice.freq / outputRate;
    voice.env *= decay;
    --voice.samplesLeft;
    return s;
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    std::shared_ptr<const juce::AudioBuffer<float>> buf;
    std::shared_ptr<const BeatData> beats;
    double rate;
    double lead;   // in file samples
    {
        const juce::SpinLock::ScopedTryLockType sl (dataLock);
        if (! sl.isLocked())
            return;
        buf = audio;
        beats = beatData;
        rate = fileRate;
        lead = (double) leadIn.load();
    }

    auto syncBeatIndex = [&] (double timeSeconds)
    {
        nextBeat = 0;
        if (beats != nullptr)
            nextBeat = (int) std::distance (beats->times.begin(),
                                            std::lower_bound (beats->times.begin(), beats->times.end(), timeSeconds));
    };

    const double seek = pendingSeek.exchange (-1.0);
    if (seek >= 0.0)
    {
        readPos = seek * rate;
        syncBeatIndex (seek);
        voice.samplesLeft = 0;
    }
    else if (resyncBeats.exchange (false))
    {
        syncBeatIndex (readPos / rate);
    }

    if (! playing.load() || buf == nullptr || buf->getNumSamples() < 4)
    {
        outputLevel.store (0.0f);
        return;
    }

    const int numFileSamples = buf->getNumSamples();
    const int numFileChannels = buf->getNumChannels();
    const int numOut = info.buffer->getNumChannels();
    const double step = speed.load() * rate / outputRate;   // file samples per output sample

    const float targetMusic = musicMuted.load() ? 0.0f : musicGain.load();
    const float targetClick = metronomeOn.load() ? clickGain.load() : 0.0f;
    const bool looping = loopOn.load();
    const double lStart = loopStart.load() * rate, lEnd = loopEnd.load() * rate;

    float peak = 0.0f;

    for (int s = 0; s < info.numSamples; ++s)
    {
        if (looping && readPos >= lEnd)
        {
            readPos = lStart;
            syncBeatIndex (lStart / rate);
        }

        if (readPos >= lead + numFileSamples - 2)
        {
            playing.store (false);
            readPos = lead + numFileSamples;
            break;
        }

        // metronome: trigger when the playhead crosses a beat
        const double now = readPos / rate;
        if (beats != nullptr && nextBeat < (int) beats->times.size() && now >= beats->times[(size_t) nextBeat])
        {
            if (now - beats->times[(size_t) nextBeat] < 0.05) // skip stale beats after a jump
                triggerClick (beats->downbeats[(size_t) nextBeat]);

            while (nextBeat < (int) beats->times.size() && beats->times[(size_t) nextBeat] <= now)
                ++nextBeat;
        }

        smoothedMusicGain += 0.002f * (targetMusic - smoothedMusicGain);
        smoothedClickGain += 0.002f * (targetClick - smoothedClickGain);

        const float click = renderClickSample() * smoothedClickGain;

        // cubic Hermite interpolation; the lead-in (and anything outside the file) is silence
        const double audioPos = readPos - lead;
        const int i1 = (int) std::floor (audioPos);
        const auto frac = (float) (audioPos - i1);

        for (int ch = 0; ch < numOut; ++ch)
        {
            const float* src = buf->getReadPointer (std::min (ch, numFileChannels - 1));
            auto at = [&] (int i) { return i >= 0 && i < numFileSamples ? src[i] : 0.0f; };
            const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float v = ((c3 * frac + c2) * frac + c1) * frac + y1;

            const float out = v * smoothedMusicGain + click;
            info.buffer->setSample (ch, info.startSample + s, out);
            peak = std::max (peak, std::abs (out));
        }

        readPos += step;
    }

    outputLevel.store (peak);

    // don't clobber a seek requested by the UI during this block
    if (pendingSeek.load() < 0.0)
        positionSeconds.store (std::min (readPos / rate, durationSeconds.load()));
}

//==============================================================================
void AudioEngine::renderMetronome (const std::vector<double>& times, const std::vector<bool>& downbeats,
                                   bool accentDownbeats, double rate, juce::int64 startSample,
                                   float* dest, int numSamples, float gain)
{
    std::fill (dest, dest + numSamples, 0.0f);
    if (rate <= 0.0 || numSamples <= 0)
        return;

    const juce::int64 endSample = startSample + numSamples;
    const double blockStart = (double) startSample / rate, blockEnd = (double) endSample / rate;
    const double twoPi = juce::MathConstants<double>::twoPi;

    // first click that can still be sounding at the start of the block
    auto i = (size_t) std::distance (times.begin(), std::lower_bound (times.begin(), times.end(), blockStart - clickLength));

    for (; i < times.size() && times[i] < blockEnd; ++i)
    {
        const double t0 = times[i];
        // like playback, one voice: a click is cut off by the next one
        const double tEnd = std::min (t0 + clickLength, i + 1 < times.size() ? times[i + 1] : std::numeric_limits<double>::max());

        const bool acc = accentDownbeats && i < downbeats.size() && downbeats[i];
        const double freq = acc ? accentFreq : normalFreq;
        const double g = (acc ? accentGain : normalGain) * gain;

        const auto first = std::max (startSample, (juce::int64) std::ceil (t0 * rate));
        const auto last  = std::min (endSample,   (juce::int64) std::ceil (tEnd * rate));

        for (auto n = first; n < last; ++n)
        {
            const double dt = std::max (0.0, (double) n / rate - t0);
            dest[n - startSample] += (float) (g * std::sin (twoPi * freq * dt) * std::exp (-dt / clickDecay));
        }
    }
}

juce::Result AudioEngine::exportMetronome (const std::vector<double>& times, const std::vector<bool>& downbeats,
                                           bool accentDownbeats, double rate, juce::int64 numSamples,
                                           const juce::File& file)
{
    if (rate <= 0.0 || numSamples <= 0)
        return juce::Result::fail ("Nothing to render.");

    juce::TemporaryFile temp (file);
    std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();
    if (stream == nullptr)
        return juce::Result::fail ("Couldn't write to " + file.getFullPathName());

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return juce::Result::fail ("Couldn't create a WAV writer at " + juce::String (rate) + " Hz.");

    constexpr int blockSize = 65536;
    juce::AudioBuffer<float> block (1, blockSize);

    // pass 1: find the peak, so the file can be normalised to exactly 0 dBFS
    float peak = 0.0f;
    for (juce::int64 pos = 0; pos < numSamples; pos += blockSize)
    {
        const int n = (int) std::min ((juce::int64) blockSize, numSamples - pos);
        renderMetronome (times, downbeats, accentDownbeats, rate, pos, block.getWritePointer (0), n);
        peak = std::max (peak, block.getMagnitude (0, 0, n));
    }
    const float gain = peak > 0.0f ? 1.0f / peak : 1.0f;

    // pass 2: render normalised and write
    for (juce::int64 pos = 0; pos < numSamples; pos += blockSize)
    {
        const int n = (int) std::min ((juce::int64) blockSize, numSamples - pos);
        renderMetronome (times, downbeats, accentDownbeats, rate, pos, block.getWritePointer (0), n, gain);
        if (! writer->writeFromAudioSampleBuffer (block, 0, n))
            return juce::Result::fail ("Error while writing " + file.getFileName());
    }

    writer.reset();   // flushes and closes the stream

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Couldn't replace " + file.getFullPathName());

    return juce::Result::ok();
}
