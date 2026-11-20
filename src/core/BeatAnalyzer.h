#pragma once

#include "TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <optional>

namespace beatit
{

/** Offline wrapper around BTrack (https://github.com/adamstark/BTrack).

    BTrack is a causal (real-time) tracker, so it needs a few seconds to lock on
    at the start of a song. Because we have the whole file we run it twice:
    forwards over the onset function and backwards over the time-reversed onset
    function, then stitch the backward result (good at the start) onto the
    forward result (good everywhere after warm-up) at a point where both agree.
*/
class BeatAnalyzer
{
public:
    /** Progress callback: receives 0..1, return false to cancel. */
    using ProgressFn = std::function<bool (float)>;

    static constexpr double analysisRate = 44100.0;   // BTrack's internal constants assume 44.1 kHz
    static constexpr int    trackerHop   = 512;
    static constexpr int    trackerFrame = 1024;
    static constexpr int    snapHop      = 128;
    static constexpr int    snapFrame    = 1024;

    static std::optional<AnalysisResult> analyze (const juce::AudioBuffer<float>& audio,
                                                  double sampleRate,
                                                  ProgressFn progress = {});

    /** Mono mixdown resampled to 44.1 kHz (libsamplerate). */
    static std::vector<double> prepareMono (const juce::AudioBuffer<float>& audio, double sampleRate);
};

} // namespace beatit
