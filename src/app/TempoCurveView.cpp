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
    theme::fillDisplay (g, getLocalBounds().toFloat());

    // horizontal grid
    const double span = bpmHi - bpmLo;
    const double step = span > 120 ? 20.0 : span > 60 ? 10.0 : span > 24 ? 5.0 : span > 10 ? 2.0 : 1.0;
    g.setFont (juce::FontOptions (11.0f));

    for (double b = std::ceil (bpmLo / step) * step; b <= bpmHi; b += step)
    {
        const float y = bpmToY (b);
        g.setColour (theme::grid);
        g.fillRect (juce::Rectangle<float> ((float) axisWidth, y, (float) getWidth(), 1.0f));
    }

    // axis labels go on top of the curve, on a dark backing so they stay readable
    auto drawAxisLabels = [&]
    {
        g.setFont (juce::FontOptions (11.0f));
        for (double b = std::ceil (bpmLo / step) * step; b <= bpmHi; b += step)
        {
            const float y = bpmToY (b);
            const auto r = juce::Rectangle<float> (4.0f, y - 7.0f, (float) axisWidth - 8.0f, 14.0f);
            g.setColour (theme::displayBottom.withAlpha (0.75f));
            g.fillRoundedRectangle (r.withTrimmedLeft (r.getWidth() - 26.0f).expanded (2.0f, 0.0f), 3.0f);
            g.setColour (theme::textDim.withAlpha (0.9f));
            g.drawText (juce::String ((int) b), r, juce::Justification::right, false);
        }
    };

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
            const auto curve = theme::glow() > 0.0f ? theme::accent : theme::accentAlt;
            g.setGradientFill (juce::ColourGradient (curve.withAlpha (0.28f), 0.0f, 0.0f,
                                                     curve.withAlpha (0.0f), 0.0f, bottom, false));
            g.fillPath (fill);
            theme::glowStroke (g, line, curve, 1.6f);
        }
        drawAxisLabels();

        // hover readout
        if (hoverX >= 0.0f)
        {
            const double t = xToTime (hoverX);
            const int bi = grid->beatIndexAtOrBefore (t);
            if (bi >= 0 && bi + 1 < (int) beats.size())
            {
                const double bpm = grid->bpmOfInterval (bi);
                g.setColour (theme::text.withAlpha (0.2f));
                g.fillRect (juce::Rectangle<float> (hoverX, 0.0f, 1.0f, (float) getHeight()));

                const float y = bpmToY (bpm);
                const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ hoverX, y });
                g.setColour (theme::displayBottom);
                g.fillEllipse (dot);
                g.setColour (theme::glow() > 0.0f ? theme::accent : theme::accentAlt);
                g.drawEllipse (dot, 1.8f);

                const juce::String txt = juce::String (bpm, 2) + " BPM";
                auto box = juce::Rectangle<float> (hoverX + 8.0f, 5.0f, 88.0f, 20.0f);
                if (box.getRight() > (float) getWidth()) box.setX (hoverX - 96.0f);
                g.setColour (theme::panelRaised.withAlpha (0.92f));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (theme::outline.brighter (0.2f));
                g.drawRoundedRectangle (box, 4.0f, 1.0f);
                g.setColour (theme::text);
                g.drawText (txt, box, juce::Justification::centred, false);
            }
        }
    }
    else
    {
        g.setColour (theme::textDim);
        g.drawText ("Tempo curve appears after analysis", getLocalBounds(), juce::Justification::centred, false);
        drawAxisLabels();
    }

    // playhead
    theme::glowLine (g, juce::Rectangle<float> (timeToX (playhead) - 0.6f, 0.0f, 1.2f, (float) getHeight()),
                     theme::playhead.withAlpha (0.85f), 4.0f);

    g.setColour (theme::textDim.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.12f));
    g.drawText ("TEMPO", juce::Rectangle<float> ((float) axisWidth + 8.0f, 4.0f, 80.0f, 14.0f), juce::Justification::left, false);
}

void TempoCurveView::mouseDown (const juce::MouseEvent& e)  { if (onSeek) onSeek (std::max (0.0, xToTime ((float) e.x))); }
void TempoCurveView::mouseDrag (const juce::MouseEvent& e)  { mouseDown (e); hoverX = (float) e.x; repaint(); }
void TempoCurveView::mouseMove (const juce::MouseEvent& e)  { hoverX = (float) e.x; repaint(); }
void TempoCurveView::mouseExit (const juce::MouseEvent&)    { hoverX = -1.0f; repaint(); }
