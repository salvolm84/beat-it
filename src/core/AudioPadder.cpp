#include "AudioPadder.h"

namespace beatit
{

juce::Array<juce::File> AudioPadder::findAudioFiles (juce::AudioFormatManager& formats, const juce::File& folder)
{
    juce::Array<juce::File> result;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false))
        if (! f.isHidden() && ! f.getFileName().startsWithChar ('.') && formats.findFormatForFileExtension (f.getFileExtension()) != nullptr)
            result.add (f);

    result.sort();
    return result;
}

juce::Array<juce::File> AudioPadder::destinationsFor (const juce::Array<juce::File>& sources, const juce::File& destFolder)
{
    juce::StringArray stems;
    for (const auto& f : sources)
        stems.add (f.getFileNameWithoutExtension().toLowerCase());

    juce::Array<juce::File> result;
    for (int i = 0; i < sources.size(); ++i)
    {
        const auto& f = sources.getReference (i);
        auto name = f.getFileNameWithoutExtension();

        int sameStem = 0;
        for (const auto& s : stems)
            sameStem += s == stems[i] ? 1 : 0;
        if (sameStem > 1)
            name << " (" << f.getFileExtension().substring (1) << ")";

        result.add (destFolder.getChildFile (name + ".wav"));
    }
    return result;
}

juce::Result AudioPadder::padFile (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& dest,
                                   const PreCount& preCount, const std::function<bool (float)>& progress)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
        return juce::Result::fail ("Can't read " + source.getFileName());

    const int channels = (int) reader->numChannels;
    const auto length = reader->lengthInSamples;
    const auto silence = preCount.silenceSamplesAt (reader->sampleRate);

    const bool isPcmSource = reader->getFormatName().containsIgnoreCase ("wav") || reader->getFormatName().containsIgnoreCase ("aiff")
                          || reader->getFormatName().containsIgnoreCase ("flac");
    const bool useFloat = reader->usesFloatingPointData || ! isPcmSource;
    const int bits = useFloat ? 32 : (reader->bitsPerSample == 8 || reader->bitsPerSample == 16 || reader->bitsPerSample == 24
                                      || reader->bitsPerSample == 32 ? (int) reader->bitsPerSample : 24);

    juce::TemporaryFile temp (dest);
    std::unique_ptr<juce::OutputStream> stream = temp.getFile().createOutputStream();
    if (stream == nullptr)
        return juce::Result::fail ("Can't write " + dest.getFullPathName());

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (reader->sampleRate)
                                                   .withNumChannels (channels)
                                                   .withBitsPerSample (bits)
                                                   .withSampleFormat (useFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                               : juce::AudioFormatWriterOptions::SampleFormat::integral));
    if (writer == nullptr)
        return juce::Result::fail ("Can't create a WAV writer for " + source.getFileName());

    constexpr int blockSize = 65536;
    juce::AudioBuffer<float> block (channels, blockSize);
    const auto total = silence + length;

    auto report = [&] (juce::int64 done) { return ! progress || progress (total > 0 ? (float) ((double) done / (double) total) : 1.0f); };

    block.clear();
    for (juce::int64 pos = 0; pos < silence; pos += blockSize)
    {
        const int n = (int) std::min ((juce::int64) blockSize, silence - pos);
        if (! writer->writeFromAudioSampleBuffer (block, 0, n))
            return juce::Result::fail ("Error writing " + dest.getFileName());
        if (! report (pos + n))
            return juce::Result::fail ("Cancelled");
    }

    for (juce::int64 pos = 0; pos < length; pos += blockSize)
    {
        const int n = (int) std::min ((juce::int64) blockSize, length - pos);
        if (! reader->read (&block, 0, n, pos, true, true))
            return juce::Result::fail ("Error reading " + source.getFileName());
        if (! writer->writeFromAudioSampleBuffer (block, 0, n))
            return juce::Result::fail ("Error writing " + dest.getFileName());
        if (! report (silence + pos + n))
            return juce::Result::fail ("Cancelled");
    }

    writer.reset();   // flush + close
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Can't replace " + dest.getFullPathName());

    return juce::Result::ok();
}

} // namespace beatit
