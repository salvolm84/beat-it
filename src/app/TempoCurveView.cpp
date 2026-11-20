#include "TempoCurveView.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

TempoCurveView::TempoCurveView()
{
    setWantsKeyboardFocus (false);
}

void TempoCurveView::setGrid (const beatit::BeatGrid* g)
{
    grid = g;
    updateRange();
    repaint();
}

void TempoCurveView::setVisibleRange (double s, double e)
{
    if (! juce::exactlyEqual (s, viewStart) || ! juce::exactlyEqual (e, viewEnd))
    {
        viewStart = s;
        viewEnd = std::max (s + 1.0e-3, e);
        repaint();
    }
}

void TempoCurveView::setPlayhead (double t)
{
    if (std::abs (timeToX (t) - timeToX (playhead)) >= 0.5f)
    {
        const auto oldX = timeToX (playhead), newX = timeToX (t);
        playhead = t;
        repaint (juce::Rectangle<float> (std::min (oldX, newX) - 4.0f, 0.0f, std::abs (newX - oldX) + 8.0f, (float) getHeight()).toNearestInt());
    }
}

void TempoCurveView::updateRange()
{
    if (grid == nullptr || grid->beats.size() < 3)
    {
        bpmLo = 60.0; bpmHi = 180.0;
        return;
    }

    // robust range (2nd..98th percentile) so a single bad beat doesn't squash the curve
    std::vector<double> bpms;
    for (int i = 0; i + 1 < (int) grid->beats.size(); ++i)
        bpms.push_back (grid->bpmOfInterval (i));
    std::sort (bpms.begin(), bpms.end());

    const double lo = bpms[(size_t) ((double) (bpms.size() - 1) * 0.02)];
    const double hi = bpms[(size_t) ((double) (bpms.size() - 1) * 0.98)];
    const double pad = std::max (4.0, (hi - lo) * 0.25);
    bpmLo = std::max (0.0, lo - pad);
    bpmHi = hi + pad;
}

float TempoCurveView::timeToX (double t) const noexcept
{
    return (float) ((t - viewStart) / (viewEnd - viewStart) * getWidth());
}

double TempoCurveView::xToTime (float x) const noexcept
{
    return viewStart + (viewEnd - viewStart) * (double) x / std::max (1, getWidth());
}

float TempoCurveView::bpmToY (double bpm) const noexcept
{
    const float top = 8.0f, bottom = (float) getHeight() - 8.0f;
    return (float) juce::jmap (bpm, bpmLo, bpmHi, (double) bottom, (double) top);
}

void TempoCurveView::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);

    // horizontal grid
    const double span = bpmHi - bpmLo;
    const double step = span > 120 ? 20.0 : span > 60 ? 10.0 : span > 24 ? 5.0 : span > 10 ? 2.0 : 1.0;
    g.setFont (juce::FontOptions (11.0f));

    for (double b = std::ceil (bpmLo / step) * step; b <= bpmHi; b += step)
    {
        const float y = bpmToY (b);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float> ((float) axisWidth, y, (float) getWidth(), 1.0f));
        g.setColour (theme::textDim);
        g.drawText (juce::String ((int) b), juce::Rectangle<float> (4.0f, y - 7.0f, (float) axisWidth - 8.0f, 14.0f), juce::Justification::right, false);
    }

    if (grid != nullptr && grid->beats.size() >= 2)
    {
        const auto& beats = grid->beats;
        auto first = std::lower_bound (beats.begin(), beats.end(), viewStart);
        int i0 = std::max (0, (int) std::distance (beats.begin(), first) - 1);

        juce::Path line, fill;
        bool started = false;
        const float bottom = (float) getHeight();

        for (int i = i0; i + 1 < (int) beats.size(); ++i)
        {
            const float x0 = timeToX (beats[(size_t) i]);
            const float x1 = timeToX (beats[(size_t) i + 1]);
            const float y = bpmToY (grid->bpmOfInterval (i));

            if (! started) { line.startNewSubPath (x0, y); fill.startNewSubPath (x0, bottom); fill.lineTo (x0, y); started = true; }
            else           { line.lineTo (x0, y); fill.lineTo (x0, y); }

            line.lineTo (x1, y);
            fill.lineTo (x1, y);

            if (x0 > (float) getWidth())
                break;
        }

        if (started)
        {
            fill.lineTo (line.getCurrentPosition().x, bottom);
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (theme::accentAlt.withAlpha (0.25f), 0.0f, 0.0f,
                                                     theme::accentAlt.withAlpha (0.02f), 0.0f, bottom, false));
            g.fillPath (fill);
            g.setColour (theme::accentAlt);
            g.strokePath (line, juce::PathStrokeType (1.6f));
        }

        // hover readout
        if (hoverX >= 0.0f)
        {
            const double t = xToTime (hoverX);
            const int bi = grid->beatIndexAtOrBefore (t);
            if (bi >= 0 && bi + 1 < (int) beats.size())
            {
                const double bpm = grid->bpmOfInterval (bi);
                g.setColour (juce::Colours::white.withAlpha (0.3f));
                g.fillRect (juce::Rectangle<float> (hoverX, 0.0f, 1.0f, (float) getHeight()));

                const juce::String txt = juce::String (bpm, 2) + " BPM";
                auto box = juce::Rectangle<float> (hoverX + 6.0f, 4.0f, 86.0f, 18.0f);
                if (box.getRight() > (float) getWidth()) box.setX (hoverX - 92.0f);
                g.setColour (theme::panelRaised.withAlpha (0.95f));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (theme::text);
                g.drawText (txt, box, juce::Justification::centred, false);
            }
        }
    }
    else
    {
        g.setColour (theme::textDim);
        g.drawText ("Tempo curve appears after analysis", getLocalBounds(), juce::Justification::centred, false);
    }

    // playhead
    g.setColour (theme::playhead.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (timeToX (playhead) - 0.5f, 0.0f, 1.0f, (float) getHeight()));

    g.setColour (theme::outline);
    g.drawRect (getLocalBounds().removeFromTop (1));
}

void TempoCurveView::mouseDown (const juce::MouseEvent& e)  { if (onSeek) onSeek (std::max (0.0, xToTime ((float) e.x))); }
void TempoCurveView::mouseDrag (const juce::MouseEvent& e)  { mouseDown (e); hoverX = (float) e.x; repaint(); }
void TempoCurveView::mouseMove (const juce::MouseEvent& e)  { hoverX = (float) e.x; repaint(); }
void TempoCurveView::mouseExit (const juce::MouseEvent&)    { hoverX = -1.0f; repaint(); }
