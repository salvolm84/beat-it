#include "WaveformView.h"
#include "Theme.h"

#include <cmath>

WaveformView::WaveformView()
{
    thumbnail.addChangeListener (this);
    setWantsKeyboardFocus (false);
    setRepaintsOnMouseActivity (false);
}

WaveformView::~WaveformView()
{
    thumbnail.removeChangeListener (this);
}

void WaveformView::setAudio (const juce::AudioBuffer<float>* buffer, double rate)
{
    audio = buffer;
    sampleRate = rate;
    thumbnail.clear();

    if (buffer != nullptr && buffer->getNumSamples() > 0)
    {
        thumbnail.reset (buffer->getNumChannels(), rate, buffer->getNumSamples());
        thumbnail.addBlock (0, *buffer, 0, buffer->getNumSamples());
    }

    hoveredBeat = selectedBeat = -1;
    repaint();
}

void WaveformView::setAudioOffset (double seconds)
{
    if (! juce::exactlyEqual (seconds, audioOffset))
    {
        audioOffset = std::max (0.0, seconds);
        repaint();
    }
}

void WaveformView::setGrid (const beatit::BeatGrid* g, const beatit::GridLayout* l, int locked)
{
    grid = g;
    layout = l;
    lockedBeats = std::max (0, locked);
    if (selectedBeat < lockedBeats) selectedBeat = -1;
    if (hoveredBeat < lockedBeats)  hoveredBeat = -1;
    if (grid == nullptr || selectedBeat >= (int) grid->beats.size()) selectedBeat = -1;
    if (grid == nullptr || hoveredBeat >= (int) grid->beats.size())  hoveredBeat = -1;
    repaint();
}

void WaveformView::setVisibleRange (double s, double e)
{
    if (! juce::exactlyEqual (s, viewStart) || ! juce::exactlyEqual (e, viewEnd))
    {
        viewStart = s;
        viewEnd = std::max (s + 1.0e-3, e);
        repaint();
    }
}

void WaveformView::setPlayhead (double t)
{
    if (std::abs (timeToX (t) - timeToX (playhead)) >= 0.5f)
    {
        const auto oldX = timeToX (playhead), newX = timeToX (t);
        playhead = t;
        repaint (juce::Rectangle<float> (std::min (oldX, newX) - 8.0f, 0.0f, std::abs (newX - oldX) + 16.0f, (float) getHeight()).toNearestInt());
    }
}

void WaveformView::setLoop (double s, double e, bool enabled)
{
    loopStart = s; loopEnd = e; loopEnabled = enabled;
    repaint();
}

void WaveformView::setSelectedBeat (int index)
{
    selectedBeat = index;
    repaint();
}

double WaveformView::xToTime (float x) const noexcept
{
    return viewStart + (viewEnd - viewStart) * (double) x / std::max (1, getWidth());
}

float WaveformView::timeToX (double t) const noexcept
{
    return (float) ((t - viewStart) / (viewEnd - viewStart) * getWidth());
}

int WaveformView::beatUnder (float x) const
{
    if (grid == nullptr || grid->beats.empty())
        return -1;

    const double tolerance = (viewEnd - viewStart) * 6.0 / std::max (1, getWidth());
    const int b = grid->nearestBeat (xToTime (x), tolerance);
    return b >= lockedBeats ? b : -1;   // pre-count beats are not editable
}

//==============================================================================
void WaveformView::drawWaveformDirect (juce::Graphics& g, juce::Rectangle<int> area)
{
    // When zoomed in further than the thumbnail resolution, draw min/max
    // straight from the sample data for a crisp view of the transients.
    const int numCh = audio->getNumChannels();
    const int total = audio->getNumSamples();
    const float chHeight = (float) area.getHeight() / (float) numCh;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* d = audio->getReadPointer (ch);
        const float mid = (float) area.getY() + chHeight * ((float) ch + 0.5f);
        const float half = chHeight * 0.46f;

        juce::Path p;
        bool started = false;
        std::vector<std::pair<float, float>> cols ((size_t) area.getWidth());

        for (int x = 0; x < area.getWidth(); ++x)
        {
            const double a = (xToTime ((float) x) - audioOffset) * sampleRate;
            const double b = (xToTime ((float) x + 1.0f) - audioOffset) * sampleRate;
            if (b < 0.0 || a >= (double) total) { cols[(size_t) x] = { 0.0f, 0.0f }; continue; }

            const int s0 = juce::jlimit (0, total - 1, (int) a);
            const int s1 = juce::jlimit (0, total - 1, (int) b);

            float lo = d[s0], hi = d[s0];
            for (int s = s0 + 1; s <= s1; ++s) { lo = std::min (lo, d[s]); hi = std::max (hi, d[s]); }
            cols[(size_t) x] = { lo, hi };
        }

        for (int x = 0; x < area.getWidth(); ++x)
        {
            const float y = mid - cols[(size_t) x].second * half;
            if (! started) { p.startNewSubPath ((float) x, y); started = true; }
            else p.lineTo ((float) x, y);
        }
        for (int x = area.getWidth() - 1; x >= 0; --x)
            p.lineTo ((float) x, mid - cols[(size_t) x].first * half);
        p.closeSubPath();

        g.setGradientFill (waveGradient (mid, half));
        g.fillPath (p);
        g.setColour (theme::waveform.brighter (0.4f).withAlpha (0.9f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
}

juce::ColourGradient WaveformView::waveGradient (float mid, float half) const
{
    // brighter towards the peaks, dimmer at the centre line (analyser-style fill)
    const auto w = theme::waveform;
    juce::ColourGradient grad (w.brighter (0.35f).withAlpha (0.95f), 0.0f, mid - half,
                               w.brighter (0.35f).withAlpha (0.95f), 0.0f, mid + half, false);
    grad.addColour (0.5, w.withAlpha (theme::glow() > 0.0f ? 0.45f : 0.9f));
    return grad;
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    auto ruler = bounds.withHeight (rulerHeight);
    auto wave = bounds.withTrimmedTop (rulerHeight);

    theme::fillDisplay (g, wave.toFloat());

    if (audio == nullptr || audio->getNumSamples() == 0)
    {
        g.setColour (theme::panelRaised);
        g.fillRect (ruler);

        // drop zone
        auto zone = wave.toFloat().reduced (40.0f, 30.0f);
        zone = zone.withSizeKeepingCentre (std::min (zone.getWidth(), 520.0f), std::min (zone.getHeight(), 260.0f));
        juce::Path outline;
        outline.addRoundedRectangle (zone, 14.0f);
        juce::Path dashed;
        const float dashes[] = { 7.0f, 6.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (theme::outline.brighter (0.4f));
        g.fillPath (dashed);

        // tray + arrow icon
        const auto c = zone.getCentre().translated (0.0f, -30.0f);
        juce::Path icon;
        icon.startNewSubPath (c.x - 22.0f, c.y + 4.0f);
        icon.lineTo (c.x - 22.0f, c.y + 16.0f);
        icon.lineTo (c.x + 22.0f, c.y + 16.0f);
        icon.lineTo (c.x + 22.0f, c.y + 4.0f);
        icon.startNewSubPath (c.x, c.y - 20.0f);
        icon.lineTo (c.x, c.y + 6.0f);
        icon.startNewSubPath (c.x - 9.0f, c.y - 3.0f);
        icon.lineTo (c.x, c.y + 6.0f);
        icon.lineTo (c.x + 9.0f, c.y - 3.0f);
        theme::glowStroke (g, icon, theme::accent, 2.2f);

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (17.0f));
        auto lines = emptyMessage;
        g.drawFittedText (lines.upToFirstOccurrenceOf ("\n", false, false),
                          zone.withTrimmedTop (zone.getHeight() * 0.5f + 4.0f).withHeight (24.0f).toNearestInt(), juce::Justification::centred, 1);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (lines.fromFirstOccurrenceOf ("\n", false, false),
                          zone.withTrimmedTop (zone.getHeight() * 0.5f + 30.0f).withHeight (36.0f).toNearestInt(), juce::Justification::centredTop, 2);
        return;
    }

    // ---- loop region
    if (loopEnabled)
    {
        const float x0 = timeToX (loopStart), x1 = timeToX (loopEnd);
        g.setColour (theme::loop);
        g.fillRect (juce::Rectangle<float> (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight()));
        g.setColour (theme::loopEdge.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (x0, (float) wave.getY(), 1.0f, (float) wave.getHeight()));
        g.fillRect (juce::Rectangle<float> (x1 - 1.0f, (float) wave.getY(), 1.0f, (float) wave.getHeight()));
    }

    // ---- pre-count region (silence added before the audio)
    if (audioOffset > 0.0)
    {
        const float x0 = timeToX (0.0), x1 = timeToX (audioOffset);
        if (x1 > 0.0f && x0 < (float) getWidth())
        {
            const auto r = juce::Rectangle<float> (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight());
            g.setGradientFill (juce::ColourGradient (theme::preCount.withAlpha (0.14f), 0.0f, r.getY(),
                                                     theme::preCount.withAlpha (0.03f), 0.0f, r.getBottom(), false));
            g.fillRect (r);
            theme::glowLine (g, r.withX (x1 - 1.0f).withWidth (1.5f), theme::preCount.withAlpha (0.8f));
            if (r.getWidth() > 80.0f)
            {
                g.setColour (theme::preCount);
                g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.15f));
                g.drawText ("COUNT-IN", r.reduced (10.0f, 8.0f).withHeight (14.0f), juce::Justification::centredLeft, true);
            }
        }
    }

    // ---- waveform
    const double samplesPerPixel = (viewEnd - viewStart) * sampleRate / std::max (1, getWidth());
    {
        auto area = wave.reduced (0, 8);
        if (samplesPerPixel < 256.0)
        {
            drawWaveformDirect (g, area);
        }
        else
        {
            // thumbnail times are file times; only draw where the audio exists on the timeline
            const int xStart = juce::jlimit (area.getX(), area.getRight(), (int) std::ceil (timeToX (audioOffset)));
            const auto sub = area.withLeft (xStart);
            if (sub.getWidth() > 0)
            {
                const int nCh = audio->getNumChannels();
                const float chH = (float) sub.getHeight() / (float) nCh;
                for (int ch = 0; ch < nCh; ++ch)
                {
                    const float mid = (float) sub.getY() + chH * ((float) ch + 0.5f);
                    g.setGradientFill (waveGradient (mid, chH * 0.5f));
                    thumbnail.drawChannel (g, sub.withY ((int) ((float) sub.getY() + chH * (float) ch)).withHeight ((int) chH),
                                           xToTime ((float) xStart) - audioOffset, viewEnd - audioOffset, ch, 1.0f);
                }
            }
        }

        // channel centre lines
        g.setColour (theme::grid);
        const int nCh = audio->getNumChannels();
        for (int ch = 0; ch < nCh; ++ch)
            g.fillRect (area.getX(), area.getY() + (int) (((float) ch + 0.5f) * (float) area.getHeight() / (float) nCh), area.getWidth(), 1);
        if (nCh > 1)
            g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1);
    }

    // ---- ruler background
    g.setGradientFill (juce::ColourGradient (theme::panelRaised.brighter (0.05f), 0.0f, 0.0f,
                                             theme::panelRaised.darker (0.15f), 0.0f, (float) rulerHeight, false));
    g.fillRect (ruler);
    g.setColour (theme::outline);
    g.fillRect (ruler.removeFromBottom (1));

    // ---- beat grid
    if (grid != nullptr && grid->beats.size() >= 2 && layout != nullptr)
    {
        const auto& beats = grid->beats;
        const double pxPerBeat = getWidth() / ((viewEnd - viewStart) / std::max (1.0e-3, grid->averageBpm() > 0 ? 60.0 / grid->averageBpm() : 0.5));
        const bool drawAllBeats = pxPerBeat >= 5.0;
        const int barLabelEvery = std::max (1, (int) std::ceil (36.0 / std::max (1.0e-3, pxPerBeat * grid->beatsPerBar)));
        // crowded bar lines fade and lose their glow so the waveform stays readable
        const double pxPerBar = pxPerBeat * std::max (1, grid->beatsPerBar);
        const float barAlpha = (float) juce::jlimit (0.3, 0.9, pxPerBar / 45.0);
        const bool barGlow = pxPerBar > 24.0;

        auto first = std::lower_bound (beats.begin(), beats.end(), viewStart - 1.0);
        g.setFont (juce::FontOptions (11.5f));

        for (auto it = first; it != beats.end() && *it <= viewEnd + 1.0; ++it)
        {
            const int i = (int) std::distance (beats.begin(), it);
            const float x = timeToX (*it);
            const bool down = grid->isDownbeat (i);
            const bool locked = i < lockedBeats;

            if (! down && ! drawAllBeats && i != selectedBeat && i != hoveredBeat)
                continue;

            juce::Colour c = down ? (locked ? theme::preCount : theme::accent).withAlpha (barAlpha) : theme::beatLine;
            float w = down ? 1.4f : 1.0f;
            if (locked && ! down)  c = c.withMultipliedAlpha (0.6f);
            if (i == hoveredBeat)  { c = (down ? theme::accent : theme::text).withAlpha (0.95f); w += 0.6f; }
            if (i == selectedBeat) { c = theme::accentAlt; w = 2.0f; }

            const auto line = juce::Rectangle<float> (x - w * 0.5f, (float) wave.getY(), w, (float) wave.getHeight());
            if ((down && barGlow) || i == selectedBeat || i == hoveredBeat) theme::glowLine (g, line, c);
            else { g.setColour (c); g.fillRect (line); }

            // drag handle on the hovered / selected beat
            if (i == hoveredBeat || i == selectedBeat)
            {
                const auto node = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, (float) wave.getY() + 12.0f });
                if (theme::glow() > 0.0f)
                {
                    g.setColour (c.withAlpha (0.25f));
                    g.fillEllipse (node.expanded (4.0f));
                }
                g.setColour (theme::displayBottom);
                g.fillEllipse (node);
                g.setColour (c);
                g.drawEllipse (node.reduced (0.8f), 2.0f);
            }

            // ruler ticks + bar numbers
            if (down)
            {
                auto [bar, beat] = grid->barAndBeatOfDetected (i, *layout);
                juce::ignoreUnused (beat);
                g.setColour (locked ? theme::preCount : theme::accent);
                g.fillRect (juce::Rectangle<float> (x - 0.75f, 9.0f, 1.5f, (float) rulerHeight - 9.0f));

                if (bar % barLabelEvery == 0 || barLabelEvery == 1)
                {
                    g.setColour (locked ? theme::preCount : theme::text);
                    g.drawText (juce::String (bar), juce::Rectangle<float> (x + 4.0f, 2.0f, 40.0f, 14.0f), juce::Justification::left, false);
                }
            }
            else if (drawAllBeats)
            {
                g.setColour (theme::textDim.withAlpha (0.7f));
                g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) rulerHeight - 6.0f, 1.0f, 5.0f));
            }
        }
    }

    // ---- cursor readout (time, bar.beat) and editing hint
    if (hoverX >= 0.0f && drag == Drag::none)
    {
        const double t = xToTime (hoverX);
        g.setColour (theme::text.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (hoverX, (float) wave.getY(), 1.0f, (float) wave.getHeight()));

        juce::String txt = formatReadoutTime (t);
        if (grid != nullptr && layout != nullptr && grid->beats.size() >= 2)
        {
            const auto p = grid->positionAt (t, *layout);
            if (p.valid)
                txt << "   " << p.bar << "." << p.beat;
        }
        const juce::Font f (juce::FontOptions (12.0f));
        const float tw = juce::GlyphArrangement::getStringWidth (f, txt) + 16.0f;
        auto box = juce::Rectangle<float> (hoverX + 10.0f, (float) wave.getY() + 6.0f, tw, 20.0f);
        if (box.getRight() > (float) getWidth() - 4.0f) box.setX (hoverX - 10.0f - tw);
        g.setColour (theme::panelRaised.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (theme::outline.brighter (0.2f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);

        if (grid != nullptr && grid->beats.size() >= 2 && getWidth() > 600)
        {
            g.setColour (theme::textDim.withAlpha (0.8f));
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (juce::CharPointer_UTF8 ("Drag a beat to move it (\xe2\x8c\x98 fine)  \xc2\xb7  double-click to add  \xc2\xb7  "
                                                "\xe2\x8c\xa5-click to delete  \xc2\xb7  shift-drag to loop  \xc2\xb7  right-click for more"),
                        wave.reduced (10, 6).removeFromBottom (16), juce::Justification::bottomRight, true);
        }
    }

    // ---- playhead
    {
        const float x = timeToX (playhead);
        theme::glowLine (g, juce::Rectangle<float> (x - 0.75f, 0.0f, 1.5f, (float) getHeight()), theme::playhead, 5.0f);
        juce::Path tri;
        tri.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 9.0f);
        g.setColour (theme::playhead);
        g.fillPath (tri);
    }
}

juce::String WaveformView::formatReadoutTime (double t)
{
    t = std::max (0.0, t);
    const int minutes = (int) (t / 60.0);
    return juce::String (minutes) + ":" + juce::String (t - minutes * 60.0, 3).paddedLeft ('0', 6);
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const int b = e.y > rulerHeight ? beatUnder ((float) e.x) : -1;
    if (b != hoveredBeat)
    {
        hoveredBeat = b;
        setMouseCursor (b >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    hoverX = audio != nullptr ? (float) e.x : -1.0f;
    repaint();
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    hoveredBeat = -1;
    hoverX = -1.0f;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        showContextMenu (e);
        return;
    }

    const double t = xToTime ((float) e.x);

    if (e.mods.isShiftDown())
    {
        drag = Drag::loop;
        dragAnchor = t;
        return;
    }

    const int b = e.y > rulerHeight ? beatUnder ((float) e.x) : -1;

    if (b >= 0 && e.mods.isAltDown())
    {
        if (onDeleteBeat) onDeleteBeat (b);
        hoveredBeat = -1;
        return;
    }

    if (b >= 0)
    {
        drag = Drag::beat;
        selectedBeat = b;
        dragAnchor = grid->beats[(size_t) b];
        if (onEditBegin) onEditBegin();
        repaint();
        return;
    }

    selectedBeat = -1;
    drag = Drag::scrub;
    if (onSeek) onSeek (t);
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    const double t = xToTime ((float) e.x);

    switch (drag)
    {
        case Drag::beat:
        {
            // fine adjustment with Cmd held: 1/10 speed
            double target = t;
            if (e.mods.isCommandDown())
                target = dragAnchor + (t - xToTime ((float) e.getMouseDownX())) * 0.1;

            if (onMoveBeat) onMoveBeat (selectedBeat, target);
            break;
        }
        case Drag::scrub:
            if (onSeek) onSeek (juce::jmax (0.0, t));
            break;
        case Drag::loop:
            if (onLoopSet) onLoopSet (std::min (dragAnchor, t), std::max (dragAnchor, t));
            break;
        case Drag::none:
            break;
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::beat && onEditEnd)
        onEditEnd();

    drag = Drag::none;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (audio == nullptr || e.y <= rulerHeight || e.mods.isAnyModifierKeyDown())
        return;

    if (beatUnder ((float) e.x) < 0 && onAddBeat)
        onAddBeat (xToTime ((float) e.x));
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (audio == nullptr || ! onRangeRequested)
        return;

    const double span = viewEnd - viewStart;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = xToTime ((float) e.x);
        const double factor = std::pow (0.5, (double) w.deltaY * 2.0);
        const double newSpan = span * factor;
        const double ratio = (anchor - viewStart) / span;
        onRangeRequested (anchor - ratio * newSpan, anchor + (1.0 - ratio) * newSpan);
        return;
    }

    const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
    const double shift = (double) delta * span * 0.5;
    onRangeRequested (viewStart + shift, viewEnd + shift);
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    if (audio == nullptr || ! onRangeRequested || scale <= 0.0f)
        return;

    const double span = viewEnd - viewStart;
    const double anchor = xToTime ((float) e.x);
    const double newSpan = span / scale;
    const double ratio = (anchor - viewStart) / span;
    onRangeRequested (anchor - ratio * newSpan, anchor + (1.0 - ratio) * newSpan);
}

void WaveformView::showContextMenu (const juce::MouseEvent& e)
{
    const double t = xToTime ((float) e.x);
    const int b = beatUnder ((float) e.x);

    juce::PopupMenu m;
    if (b >= 0)
    {
        m.addSectionHeader ("Beat " + juce::String (b + 1));
        m.addItem (2, "Delete beat");
        m.addItem (3, "Set as bar start (downbeat)");
        m.addSeparator();
    }
    else
    {
        m.addItem (1, "Add beat here");
    }

    m.addItem (4, "Play from here");
    if (grid != nullptr && grid->beats.size() >= 2)
    {
        m.addSeparator();
        m.addItem (5, "Loop this bar");
    }
    if (loopEnabled)
        m.addItem (6, "Clear loop");

    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe, t, b] (int result)
    {
        if (safe == nullptr) return;
        auto& v = *safe;

        switch (result)
        {
            case 1: if (v.onAddBeat) v.onAddBeat (t); break;
            case 2: if (v.onDeleteBeat) v.onDeleteBeat (b); break;
            case 3: if (v.onSetDownbeat) v.onSetDownbeat (b); break;
            case 4: if (v.onPlayFrom) v.onPlayFrom (t); break;
            case 5:
            {
                // find the bar containing t
                const auto& beats = v.grid->beats;
                int i = std::max (0, v.grid->beatIndexAtOrBefore (t));
                while (i > 0 && ! v.grid->isDownbeat (i)) --i;
                int j = i + 1;
                while (j < (int) beats.size() - 1 && ! v.grid->isDownbeat (j)) ++j;
                if (v.onLoopSet) v.onLoopSet (beats[(size_t) i], beats[(size_t) j]);
                break;
            }
            case 6: if (v.onLoopCleared) v.onLoopCleared(); break;
            default: break;
        }
    });
}
