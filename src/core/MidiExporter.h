#pragma once

#include "TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace beatit
{

struct MidiExportOptions
{
    int  ppq = 960;
    bool includeClickTrack = true;   ///< a GM drum track with one note per beat
    bool includeBarMarkers = false;  ///< marker meta event at each bar ("Bar N")
    juce::String songName;
};

/** Writes a Standard MIDI File (format 1) whose conductor track contains one
    tempo event per beat, so that every beat of the audio lands exactly on a
    beat of the DAW grid when the file's tempo map is imported (e.g. Reaper:
    Insert > Media file..., answer "Yes" to "import tempo map").

    Tick 0 corresponds to the very start of the audio file. */
class MidiExporter
{
public:
    static juce::MidiFile build (const BeatGrid& grid, const MidiExportOptions& options);

    static juce::Result exportToFile (const BeatGrid& grid, const MidiExportOptions& options,
                                      const juce::File& file);

    /** Beats as CSV: index, time, bar, beat, bpm. */
    static juce::Result exportCsv (const BeatGrid& grid, const juce::File& file);
};

} // namespace beatit
