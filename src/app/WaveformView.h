#pragma once

#include "TempoMap.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

/** Waveform display with beat grid overlay and beat editing. */
class WaveformView : public juce::Component,
                     private juce::ChangeListener
{
public:
    WaveformView();
    ~WaveformView() override;

    void setAudio (const juce::AudioBuffer<float>* buffer, double sampleRate);
    /** lockedBeats: leading beats (a pre-count) that are drawn dimmed and can't be edited. */
    void setGrid (const beatit::BeatGrid* grid, const beatit::GridLayout* layout, int lockedBeats = 0);
    /** Time on the timeline where the audio starts (pre-count silence before it). */
    void setAudioOffset (double seconds);
    void setVisibleRange (double start, double end);
    void setPlayhead (double seconds);
    void setLoop (double start, double end, bool enabled);
    void setSelectedBeat (int index);
    int  getSelectedBeat() const noexcept     { return selectedBeat; }
    void setEmptyMessage (const juce::String& m) { emptyMessage = m; repaint(); }

    //==============================================================================
    std::function<void (double)>          onSeek;
    std::function<void (double, double)>  onRangeRequested;   ///< zoom / scroll
    std::function<void()>                 onEditBegin;        ///< take an undo snapshot
    std::function<void (int, double)>     onMoveBeat;         ///< during drag
    std::function<void()>                 onEditEnd;
    std::function<void (double)>          onAddBeat;
    std::function<void (int)>             onDeleteBeat;
    std::function<void (int)>             onSetDownbeat;
    std::function<void (double, double)>  onLoopSet;
    std::function<void()>                 onLoopCleared;
    std::function<void (double)>          onPlayFrom;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    static constexpr int rulerHeight = 24;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    double xToTime (float x) const noexcept;
    float  timeToX (double t) const noexcept;
    int    beatUnder (float x) const;
    void   showContextMenu (const juce::MouseEvent&);
    void   drawWaveformDirect (juce::Graphics&, juce::Rectangle<int> area);

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbCache { 2 };
    juce::AudioThumbnail thumbnail { 256, formatManager, thumbCache };
    const juce::AudioBuffer<float>* audio = nullptr;
    double sampleRate = 44100.0;
    double audioOffset = 0.0;
    int lockedBeats = 0;

    const beatit::BeatGrid* grid = nullptr;
    const beatit::GridLayout* layout = nullptr;

    double viewStart = 0.0, viewEnd = 1.0;
    double playhead = 0.0;
    double loopStart = 0.0, loopEnd = 0.0;
    bool loopEnabled = false;

    int hoveredBeat = -1, selectedBeat = -1;
    enum class Drag { none, beat, scrub, loop } drag = Drag::none;
    double dragAnchor = 0.0;
    juce::String emptyMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
