#pragma once

#include "TempoMap.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** BPM over time (one step per beat interval, exactly what the MIDI export writes). */
class TempoCurveView : public juce::Component
{
public:
    TempoCurveView();

    void setGrid (const beatit::BeatGrid* grid);
    void setVisibleRange (double start, double end);
    void setPlayhead (double seconds);

    std::function<void (double)> onSeek;
    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
    std::function<void (const juce::MouseEvent&, float)> onMagnify;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (onWheel) onWheel (e, w); }
    void mouseMagnify (const juce::MouseEvent& e, float s) override                          { if (onMagnify) onMagnify (e, s); }

private:
    void updateRange();
    float timeToX (double t) const noexcept;
    double xToTime (float x) const noexcept;
    float bpmToY (double bpm) const noexcept;

    const beatit::BeatGrid* grid = nullptr;
    double viewStart = 0.0, viewEnd = 1.0, playhead = 0.0;
    double bpmLo = 60.0, bpmHi = 180.0;
    float hoverX = -1.0f;

    static constexpr int axisWidth = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoCurveView)
};
