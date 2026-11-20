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

    // audio-thread state
    double readPos = 0.0;       // in file samples
    int nextBeat = 0;
    ClickVoice voice;
    float smoothedMusicGain = 0.8f, smoothedClickGain = 0.7f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
