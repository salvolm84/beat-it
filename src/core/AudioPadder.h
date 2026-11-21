#pragma once

#include "TempoMap.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace beatit
{

/** Prepends the pre-count silence to audio files, so stems recorded with the
    song line up with the tempo map and metronome exported with that pre-count. */
namespace AudioPadder
{
    /** Readable audio files directly inside `folder` (sub-folders are not searched), sorted by name. */
    juce::Array<juce::File> findAudioFiles (juce::AudioFormatManager& formats, const juce::File& folder);

    /** Output file for each source inside destFolder: same name with a .wav extension
        (the source extension is kept in the name if two sources would collide). */
    juce::Array<juce::File> destinationsFor (const juce::Array<juce::File>& sources, const juce::File& destFolder);

    /** Writes `source` to `dest` as WAV with the pre-count silence in front. Sample rate,
        channel count and bit depth are kept (compressed sources are written as 32-bit float).
        `progress` gets 0..1 and returns false to cancel. */
    juce::Result padFile (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& dest,
                          const PreCount& preCount, const std::function<bool (float)>& progress = {});
}

} // namespace beatit
