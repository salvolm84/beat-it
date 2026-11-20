#include "MidiExporter.h"

#include <cmath>

namespace beatit
{

juce::MidiFile MidiExporter::build (const BeatGrid& grid, const MidiExportOptions& options)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (options.ppq);

    if (grid.isEmpty())
        return file;

    const auto layout = grid.computeLayout();
    const int den = juce::jlimit (1, 32, grid.denominator);
    const int bpb = std::max (1, grid.beatsPerBar);
    const double ticksPerBeat = options.ppq * 4.0 / den;
    const double quartersPerBeat = 4.0 / den;

    // ---- full beat list: virtual pre-roll beats + detected beats
    std::vector<double> times;
    times.reserve (grid.beats.size() + (size_t) layout.preRollBeats);
    for (int k = 0; k < layout.preRollBeats; ++k)
        times.push_back (grid.beats[0] * k / layout.preRollBeats);
    times.insert (times.end(), grid.beats.begin(), grid.beats.end());

    if (layout.firstBeatSnappedToZero)
        times[0] = 0.0;

    auto tickOf = [&] (size_t fullIndex) { return (double) juce::roundToInt ((double) fullIndex * ticksPerBeat); };

    // ---- conductor track
    juce::MidiMessageSequence conductor;

    {
        auto name = juce::MidiMessage::textMetaEvent (3, options.songName.isNotEmpty() ? options.songName + " tempo map"
                                                                                         : juce::String ("Tempo map"));
        name.setTimeStamp (0.0);
        conductor.addEvent (name);
    }

    // time signature(s): partial pickup bar first if needed
    if (layout.pickupBeats > 0)
    {
        auto ts = juce::MidiMessage::timeSignatureMetaEvent (layout.pickupBeats, den);
        ts.setTimeStamp (0.0);
        conductor.addEvent (ts);

        auto ts2 = juce::MidiMessage::timeSignatureMetaEvent (bpb, den);
        ts2.setTimeStamp (tickOf ((size_t) layout.pickupBeats));
        conductor.addEvent (ts2);
    }
    else
    {
        auto ts = juce::MidiMessage::timeSignatureMetaEvent (bpb, den);
        ts.setTimeStamp (0.0);
        conductor.addEvent (ts);
    }

    // tempo per beat, with error feedback so integer-microsecond rounding never drifts
    double elapsedMicros = 0.0;
    int lastTempo = -1;

    for (size_t i = 0; i + 1 < times.size(); ++i)
    {
        const double targetMicros = times[i + 1] * 1.0e6;
        const double beatMicros = std::max (1.0, targetMicros - elapsedMicros);

        // microseconds per quarter note; clamp to 3.6 .. 960 BPM (MIDI 24-bit limit / Reaper max)
        const int usPerQuarter = juce::jlimit (62500, 0xFFFFFF, (int) std::lround (beatMicros / quartersPerBeat));
        elapsedMicros += usPerQuarter * quartersPerBeat;

        if (usPerQuarter != lastTempo)
        {
            auto tempo = juce::MidiMessage::tempoMetaEvent (usPerQuarter);
            tempo.setTimeStamp (tickOf (i));
            conductor.addEvent (tempo);
            lastTempo = usPerQuarter;
        }
    }

    if (options.includeBarMarkers)
    {
        for (size_t i = 0; i < times.size(); ++i)
        {
            auto [bar, beat] = layout.barAndBeat ((int) i);
            if (beat == 1)
            {
                auto marker = juce::MidiMessage::textMetaEvent (6, "Bar " + juce::String (bar));
                marker.setTimeStamp (tickOf (i));
                conductor.addEvent (marker);
            }
        }
    }

    const double endTick = tickOf (times.size());
    conductor.addEvent (juce::MidiMessage::endOfTrack(), endTick);
    conductor.updateMatchedPairs();
    file.addTrack (conductor);

    // ---- optional click track (GM percussion, channel 10)
    if (options.includeClickTrack)
    {
        juce::MidiMessageSequence click;
        auto name = juce::MidiMessage::textMetaEvent (3, "Click");
        name.setTimeStamp (0.0);
        click.addEvent (name);

        const auto noteLength = std::max (1.0, ticksPerBeat / 4.0);

        for (size_t i = 0; i < grid.beats.size(); ++i)
        {
            const size_t full = i + (size_t) layout.preRollBeats;
            const bool down = layout.barAndBeat ((int) full).second == 1;
            const int note = down ? 76 : 77;        // hi / low wood block
            const auto vel = (juce::uint8) (down ? 120 : 90);
            const double t = tickOf (full);

            click.addEvent (juce::MidiMessage::noteOn (10, note, vel), t);
            click.addEvent (juce::MidiMessage::noteOff (10, note), t + noteLength);
        }

        click.addEvent (juce::MidiMessage::endOfTrack(), endTick);
        click.updateMatchedPairs();
        file.addTrack (click);
    }

    return file;
}

juce::Result MidiExporter::exportToFile (const BeatGrid& grid, const MidiExportOptions& options, const juce::File& file)
{
    if (grid.isEmpty())
        return juce::Result::fail ("There are no beats to export. Analyze the song first.");

    auto midi = build (grid, options);

    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk())
            return juce::Result::fail ("Could not write to " + file.getFullPathName());

        if (! midi.writeTo (out, 1))
            return juce::Result::fail ("Failed to write MIDI data.");
    }

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result MidiExporter::exportCsv (const BeatGrid& grid, const juce::File& file)
{
    if (grid.isEmpty())
        return juce::Result::fail ("There are no beats to export.");

    const auto layout = grid.computeLayout();
    juce::String csv ("index,time_seconds,bar,beat,bpm\n");

    for (size_t i = 0; i < grid.beats.size(); ++i)
    {
        auto [bar, beat] = grid.barAndBeatOfDetected ((int) i, layout);
        csv << (int) i << "," << juce::String (grid.beats[i], 6) << "," << bar << "," << beat << ","
            << juce::String (grid.bpmOfInterval ((int) i), 3) << "\n";
    }

    return file.replaceWithText (csv) ? juce::Result::ok()
                                      : juce::Result::fail ("Could not write " + file.getFullPathName());
}

} // namespace beatit
