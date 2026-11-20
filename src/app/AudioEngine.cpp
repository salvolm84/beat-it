#include "AudioEngine.h"

#include <algorithm>
#include <cmath>

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() = default;

void AudioEngine::setAudio (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double rate)
{
    pause();

    const double dur = (buffer != nullptr && rate > 0.0) ? buffer->getNumSamples() / rate : 0.0;
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        audio = std::move (buffer);
        fileRate = rate > 0.0 ? rate : 44100.0;
    }

    durationSeconds.store (dur);
    setPosition (0.0);
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
    voice.freq = acc ? 1760.0 : 1100.0;
    voice.gain = acc ? 1.0 : 0.65;
    voice.env = 1.0;
    voice.samplesLeft = (int) (0.06 * outputRate);
}

float AudioEngine::renderClickSample()
{
    if (voice.samplesLeft <= 0)
        return 0.0f;

    const double decay = std::exp (-1.0 / (0.012 * outputRate));
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
    {
        const juce::SpinLock::ScopedTryLockType sl (dataLock);
        if (! sl.isLocked())
            return;
        buf = audio;
        beats = beatData;
        rate = fileRate;
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

        if (readPos >= numFileSamples - 2)
        {
            playing.store (false);
            readPos = numFileSamples;
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

        // cubic Hermite interpolation
        const int i1 = (int) readPos;
        const auto frac = (float) (readPos - i1);
        const int i0 = std::max (0, i1 - 1), i2 = std::min (numFileSamples - 1, i1 + 1), i3 = std::min (numFileSamples - 1, i1 + 2);

        for (int ch = 0; ch < numOut; ++ch)
        {
            const float* src = buf->getReadPointer (std::min (ch, numFileChannels - 1));
            const float y0 = src[i0], y1 = src[i1], y2 = src[i2], y3 = src[i3];
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
