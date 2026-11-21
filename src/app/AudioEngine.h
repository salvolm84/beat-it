#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

/** Plays the loaded song from memory (with variable speed) and mixes in a
    sample-accurate synthesized metronome that follows the beat grid. */
class AudioEngine : public juce::AudioSource
{
public:
    AudioEngine();
    ~AudioEngine() override;

    //==============================================================================
    void setAudio (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double fileSampleRate);
    void setBeats (std::vector<double> beatTimes, std::vector<bool> downbeatFlags);

    /** Silence (in samples at the file's rate) played before the audio, e.g. a pre-count.
        Positions, beats and the loop are all on this padded timeline. The playhead keeps
        its place in the music when this changes. */
    void setLeadIn (long long samples);
    long long getLeadIn() const noexcept   { return leadIn.load(); }

    void play();
    void pause();
    void stop();            ///< pause + return to start
    bool isPlaying() const noexcept       { return playing.load(); }

    void   setPosition (double seconds);
    double getPosition() const noexcept   { return positionSeconds.load(); }
    double getDuration() const noexcept   { return durationSeconds.load(); }

    void setMusicGain (float g)            { musicGain.store (g); }
    void setMusicMuted (bool m)            { musicMuted.store (m); }
    void setClickGain (float g)            { clickGain.store (g); }
    void setMetronomeEnabled (bool e)      { metronomeOn.store (e); }
    bool isMetronomeEnabled() const        { return metronomeOn.load(); }
    void setAccentDownbeats (bool a)       { accent.store (a); }
    void setSpeed (double s)               { speed.store (juce::jlimit (0.25, 2.0, s)); }

    /** Loop region in seconds; disabled when end <= start. */
    void setLoop (double start, double end, bool enabled);

    /** Peak level of the last block (for a meter), 0..1+ */
    float getOutputLevel() const noexcept  { return outputLevel.load(); }

    //==============================================================================
    /** Renders the metronome alone, offline: the same click sound as playback, but
        sub-sample accurate. dest[n] is the signal at time (startSample + n) / sampleRate.
        Can be called block by block. */
    static void renderMetronome (const std::vector<double>& beatTimes, const std::vector<bool>& downbeatFlags,
                                 bool accentDownbeats, double sampleRate, juce::int64 startSample,
                                 float* dest, int numSamples, float gain = 1.0f);

    /** Writes the metronome as a mono 24-bit WAV, normalised to a 0 dBFS peak, of exactly numSamples at sampleRate
        (pass the song's length and rate so the file lines up with it at 0:00). */
    static juce::Result exportMetronome (const std::vector<double>& beatTimes, const std::vector<bool>& downbeatFlags,
                                         bool accentDownbeats, double sampleRate, juce::int64 numSamples,
                                         const juce::File& file);

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

private:
    struct BeatData
    {
        std::vector<double> times;
        std::vector<bool> downbeats;
    };

    struct ClickVoice
    {
        double phase = 0.0, freq = 1000.0, env = 0.0, gain = 0.0;
        int samplesLeft = 0;
    };

    void triggerClick (bool isDownbeat);
    float renderClickSample();

    juce::SpinLock dataLock;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double fileRate = 44100.0;
    std::shared_ptr<const BeatData> beatData;

    double outputRate = 44100.0;
    std::atomic<bool> playing { false };
    std::atomic<double> positionSeconds { 0.0 }, durationSeconds { 0.0 };
    std::atomic<double> pendingSeek { -1.0 };
    std::atomic<float> musicGain { 0.8f }, clickGain { 0.7f }, outputLevel { 0.0f };
    std::atomic<bool> musicMuted { false }, metronomeOn { true }, accent { true };
    std::atomic<double> speed { 1.0 };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<bool> loopOn { false };
    std::atomic<bool> resyncBeats { false };
    std::atomic<long long> leadIn { 0 };
    int audioSamples = 0;   // guarded by dataLock

    // audio-thread state
    double readPos = 0.0;       // in file samples
    int nextBeat = 0;
    ClickVoice voice;
    float smoothedMusicGain = 0.8f, smoothedClickGain = 0.7f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
