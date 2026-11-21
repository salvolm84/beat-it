#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** Colour themes. Code reads the active palette through the theme:: references
    below, so switching theme (theme::set) recolours everything on the next repaint. */
namespace theme
{
    struct Palette
    {
        juce::Colour background, panel, panelRaised, outline, text, textDim;
        juce::Colour accent;        ///< bar lines, tempo curve, primary actions
        juce::Colour accentAlt;     ///< selection, highlights
        juce::Colour waveform, waveformDim, beatLine, playhead, loop, loopEdge, danger;
        juce::Colour preCount;      ///< count-in region
        juce::Colour displayTop, displayBottom, grid;   ///< waveform / tempo displays
        juce::Colour knob[5];       ///< colour-coded knobs
        juce::Colour onAccent;      ///< text on accent-filled buttons
        float glow = 0.0f;          ///< 0 = flat look, 1 = full glow on curves and lines
    };

    enum class Id { studio, classic };

    /** Deep-navy studio look in the style of modern EQ plug-ins (default). */
    inline Palette makeStudio()
    {
        Palette p;
        p.background    = juce::Colour (0xff0f1218);
        p.panel         = juce::Colour (0xff151a22);
        p.panelRaised   = juce::Colour (0xff222935);
        p.outline       = juce::Colour (0xff2c3441);
        p.text          = juce::Colour (0xffd9dfe8);
        p.textDim       = juce::Colour (0xff7d8898);
        p.accent        = juce::Colour (0xffffc94a);   // gold
        p.accentAlt     = juce::Colour (0xff38c8ff);   // cyan
        p.waveform      = juce::Colour (0xff4a8fd8);
        p.waveformDim   = juce::Colour (0xff1f3c63);
        p.beatLine      = juce::Colour (0x30ffffff);
        p.playhead      = juce::Colour (0xffffffff);
        p.loop          = juce::Colour (0x2260e08c);
        p.loopEdge      = juce::Colour (0xff60e08c);
        p.danger        = juce::Colour (0xffff5a6e);
        p.preCount      = juce::Colour (0xffa982ff);   // violet
        p.displayTop    = juce::Colour (0xff1a2232);
        p.displayBottom = juce::Colour (0xff080b11);
        p.grid          = juce::Colour (0x0fffffff);
        p.knob[0]       = juce::Colour (0xff38c8ff);   // cyan
        p.knob[1]       = juce::Colour (0xffffc94a);   // gold
        p.knob[2]       = juce::Colour (0xff60e08c);   // green
        p.knob[3]       = juce::Colour (0xffa982ff);   // violet
        p.knob[4]       = juce::Colour (0xffff9a48);   // orange
        p.onAccent      = juce::Colour (0xff1b1606);
        p.glow          = 1.0f;
        return p;
    }

    /** The original Beat It look. */
    inline Palette makeClassic()
    {
        Palette p;
        p.background    = juce::Colour (0xff121419);
        p.panel         = juce::Colour (0xff1b1e25);
        p.panelRaised   = juce::Colour (0xff242833);
        p.outline       = juce::Colour (0xff2f3440);
        p.text          = juce::Colour (0xffe8eaee);
        p.textDim       = juce::Colour (0xff8b919c);
        p.accent        = juce::Colour (0xffff8a3d);
        p.accentAlt     = juce::Colour (0xff4cc2ff);
        p.waveform      = juce::Colour (0xff5a8fd6);
        p.waveformDim   = juce::Colour (0xff2b4a73);
        p.beatLine      = juce::Colour (0x66ffffff);
        p.playhead      = juce::Colour (0xffffffff);
        p.loop          = juce::Colour (0x2240d39a);
        p.loopEdge      = juce::Colour (0xff40d39a);
        p.danger        = juce::Colour (0xffff5a5a);
        p.preCount      = juce::Colour (0xffff8a3d);
        p.displayTop    = juce::Colour (0xff16191f);
        p.displayBottom = juce::Colour (0xff121419);
        p.grid          = juce::Colour (0x0fffffff);
        for (auto& k : p.knob) k = p.accent;
        p.onAccent      = juce::Colours::black;
        p.glow          = 0.0f;
        return p;
    }

    inline Palette& active()      { static Palette p = makeStudio(); return p; }
    inline Id& activeIdStorage()  { static Id id = Id::studio; return id; }
    inline Id activeId()          { return activeIdStorage(); }
    inline void set (Id id)       { active() = (id == Id::classic ? makeClassic() : makeStudio()); activeIdStorage() = id; }

    inline const juce::Colour& background    = active().background;
    inline const juce::Colour& panel         = active().panel;
    inline const juce::Colour& panelRaised   = active().panelRaised;
    inline const juce::Colour& outline       = active().outline;
    inline const juce::Colour& text          = active().text;
    inline const juce::Colour& textDim       = active().textDim;
    inline const juce::Colour& accent        = active().accent;
    inline const juce::Colour& accentAlt     = active().accentAlt;
    inline const juce::Colour& waveform      = active().waveform;
    inline const juce::Colour& waveformDim   = active().waveformDim;
    inline const juce::Colour& beatLine      = active().beatLine;
    inline const juce::Colour& playhead      = active().playhead;
    inline const juce::Colour& loop          = active().loop;
    inline const juce::Colour& loopEdge      = active().loopEdge;
    inline const juce::Colour& danger        = active().danger;
    inline const juce::Colour& preCount      = active().preCount;
    inline const juce::Colour& displayTop    = active().displayTop;
    inline const juce::Colour& displayBottom = active().displayBottom;
    inline const juce::Colour& grid          = active().grid;
    inline const juce::Colour& onAccent      = active().onAccent;
    inline float glow()                      { return active().glow; }
    inline juce::Colour knob (int i)         { return active().knob[juce::jlimit (0, 4, i)]; }

    /** Fills r with the display gradient (waveform / tempo backgrounds). */
    inline void fillDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (displayTop, 0.0f, r.getY(), displayBottom, 0.0f, r.getBottom(), false));
        g.fillRect (r);
    }

    /** A line with a soft halo when the theme uses glow. */
    inline void glowLine (juce::Graphics& g, juce::Rectangle<float> line, juce::Colour c, float haloWidth = 6.0f)
    {
        if (glow() > 0.0f)
        {
            g.setColour (c.withMultipliedAlpha (0.10f * glow()));
            g.fillRect (line.withSizeKeepingCentre (line.getWidth() + haloWidth, line.getHeight()));
            g.setColour (c.withMultipliedAlpha (0.18f * glow()));
            g.fillRect (line.withSizeKeepingCentre (line.getWidth() + haloWidth * 0.4f, line.getHeight()));
        }
        g.setColour (c);
        g.fillRect (line);
    }

    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
    {
        if (glow() > 0.0f)
        {
            g.setColour (c.withMultipliedAlpha (0.12f * glow()));
            g.strokePath (p, juce::PathStrokeType (width + 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c.withMultipliedAlpha (0.22f * glow()));
            g.strokePath (p, juce::PathStrokeType (width + 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
/** Component properties understood by the look and feel:
      Label  "role"     = "dim" | "text" | "error"   text colour from the active theme
      Button "primary"  = true                       accent-filled call-to-action button
      Slider "knob"     = 0..4                       colour of a rotary knob */
class BeatItLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BeatItLookAndFeel()  { applyPalette(); }

    void applyPalette()
    {
        using namespace theme;
        setColourScheme ({ background, panel, panelRaised, outline, text,
                           accent, onAccent, accent, text });

        setColour (juce::TextButton::buttonColourId, panelRaised);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, accent);
        setColour (juce::ComboBox::backgroundColourId, panelRaised);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::PopupMenu::backgroundColourId, panelRaised.darker (0.15f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.20f));
        setColour (juce::PopupMenu::highlightedTextColourId, accent);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, outline);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.8f));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Label::backgroundWhenEditingColourId, displayBottom);
        setColour (juce::Label::textWhenEditingColourId, text);
        setColour (juce::TextEditor::backgroundColourId, displayBottom);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId, outline);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.25f));
        setColour (juce::ProgressBar::foregroundColourId, accent);
        setColour (juce::ProgressBar::backgroundColourId, displayBottom);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, outline);
        setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, outline);
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::DocumentWindow::backgroundColourId, background);
        setColour (juce::ListBox::backgroundColourId, displayBottom);
        setColour (juce::ListBox::textColourId, text);
        setColour (juce::GroupComponent::outlineColourId, outline);
        setColour (juce::GroupComponent::textColourId, textDim);
    }

    static bool isPrimary (const juce::Component& c)  { return (bool) c.getProperties().getWithDefault ("primary", false); }

    //==============================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        using namespace theme;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = 5.0f;
        const bool on = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.4f;

        if (isPrimary (b))
        {
            auto top = accent.brighter (highlighted ? 0.25f : 0.12f), bottom = accent.darker (down ? 0.35f : 0.12f);
            g.setGradientFill (juce::ColourGradient (top.withMultipliedAlpha (alpha), 0.0f, r.getY(),
                                                     bottom.withMultipliedAlpha (alpha), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setColour (juce::Colours::white.withAlpha (0.25f * alpha));
            g.drawLine (r.getX() + radius, r.getY() + 1.0f, r.getRight() - radius, r.getY() + 1.0f, 1.0f);
            return;
        }

        auto base = panelRaised;
        if (down)             base = base.darker (0.2f);
        else if (highlighted) base = base.brighter (0.12f);

        g.setGradientFill (juce::ColourGradient (base.brighter (0.06f).withMultipliedAlpha (alpha), 0.0f, r.getY(),
                                                 base.darker (0.10f).withMultipliedAlpha (alpha), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);

        if (on)
        {
            g.setColour (accent.withAlpha (0.16f * alpha));
            g.fillRoundedRectangle (r, radius);
        }

        g.setColour (juce::Colours::white.withAlpha (0.05f * alpha));
        g.drawLine (r.getX() + radius, r.getY() + 1.0f, r.getRight() - radius, r.getY() + 1.0f, 1.0f);
        g.setColour ((on ? accent.withAlpha (0.8f) : outline).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace theme;
        auto c = isPrimary (b) ? onAccent : b.getToggleState() ? accent : text;
        if (! b.isEnabled()) c = isPrimary (b) ? onAccent.withAlpha (0.6f) : textDim.withAlpha (0.7f);

        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (c);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.8f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.5f, (float) h * 0.52f), isPrimary (b) ? juce::Font::bold : juce::Font::plain));
    }

    //==============================================================================
    /** Toggle buttons are drawn as small switches. */
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        using namespace theme;
        const bool on = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.4f;
        const float h = 14.0f, w = 26.0f;
        auto sw = juce::Rectangle<float> (1.0f, ((float) b.getHeight() - h) * 0.5f, w, h);

        g.setColour ((on ? accent.withAlpha (0.85f) : displayBottom).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (sw, h * 0.5f);
        g.setColour ((on ? accent : outline.brighter (highlighted ? 0.3f : 0.1f)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (sw, h * 0.5f, 1.0f);

        const float d = h - 4.0f;
        auto knobR = juce::Rectangle<float> (on ? sw.getRight() - d - 2.0f : sw.getX() + 2.0f, sw.getY() + 2.0f, d, d);
        g.setColour ((on ? onAccent : textDim.brighter (highlighted ? 0.3f : 0.0f)).withMultipliedAlpha (alpha));
        g.fillEllipse (knobR);

        g.setColour ((on ? text : text.withAlpha (0.85f)).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) w + 9),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    //==============================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace theme;
        const auto colour = knob ((int) s.getProperties().getWithDefault ("knob", 0));
        const float alpha = s.isEnabled() ? 1.0f : 0.4f;
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float ringR = radius - 2.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // track ring
        juce::Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (displayBottom.withMultipliedAlpha (alpha));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc (bipolar knobs grow from 12 o'clock)
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (endAngle - startAngle)
                                   : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, std::min (from, angle), std::max (from, angle), true);
            glowStroke (g, arc, colour.withMultipliedAlpha (alpha), 2.5f);
        }

        // body
        const float bodyR = ringR - 5.0f;
        auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.fillEllipse (body.translated (0.0f, 1.5f));
        g.setGradientFill (juce::ColourGradient (panelRaised.brighter (0.22f).withMultipliedAlpha (alpha), c.x, body.getY(),
                                                 panelRaised.darker (0.35f).withMultipliedAlpha (alpha), c.x, body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (outline.brighter (0.2f).withMultipliedAlpha (alpha));
        g.drawEllipse (body, 1.0f);

        // pointer
        juce::Path ptr;
        ptr.addRoundedRectangle (-1.25f, -bodyR + 3.0f, 2.5f, bodyR * 0.5f, 1.2f);
        g.setColour (text.withMultipliedAlpha (alpha));
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace theme;
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::TwoValueHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, s);
            return;
        }

        const float alpha = s.isEnabled() ? 1.0f : 0.4f;
        const float cy = (float) y + (float) height * 0.5f;
        auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
        g.setColour (displayBottom.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (track, 2.0f, 1.0f);

        const bool two = style == juce::Slider::TwoValueHorizontal;
        const float a = two ? minSliderPos : (float) x, b = two ? maxSliderPos : sliderPos;
        const auto colour = accent.withMultipliedAlpha (alpha);
        auto fill = juce::Rectangle<float> (std::min (a, b), cy - 1.5f, std::abs (b - a), 3.0f);
        theme::glowLine (g, fill, colour, 0.0f);
        if (glow() > 0.0f)
        {
            g.setColour (colour.withAlpha (0.15f * alpha));
            g.fillRoundedRectangle (fill.expanded (0.0f, 3.0f), 3.0f);
        }

        auto thumb = [&] (float px)
        {
            auto t = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ px, cy });
            g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
            g.fillEllipse (t.translated (0.0f, 1.0f));
            g.setColour (panelRaised.brighter (0.25f).withMultipliedAlpha (alpha));
            g.fillEllipse (t);
            g.setColour (colour);
            g.drawEllipse (t.reduced (0.5f), 1.6f);
        };

        if (two) { thumb (minSliderPos); thumb (maxSliderPos); }
        else     thumb (sliderPos);
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        return s.isRotary() ? LookAndFeel_V4::getSliderThumbRadius (s) : 7;
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::FontOptions (12.5f));
        return l;
    }

    //==============================================================================
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace theme;
        const float alpha = box.isEnabled() ? 1.0f : 0.4f;
        auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        const bool hot = box.isMouseOver (true);
        auto base = panelRaised.brighter (hot ? 0.1f : 0.0f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.05f).withMultipliedAlpha (alpha), 0.0f, 0.0f,
                                                 base.darker (0.12f).withMultipliedAlpha (alpha), 0.0f, (float) height, false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour ((box.hasKeyboardFocus (true) ? accent : outline).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        juce::Path chevron;
        const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.5f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (textDim.withMultipliedAlpha (alpha));
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (juce::FontOptions (13.0f));
    }

    //==============================================================================
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        using namespace theme;
        g.fillAll (panelRaised.darker (0.2f));
        g.setColour (outline);
        g.drawRect (0, 0, width, height, 1);
    }

    juce::Font getPopupMenuFont() override  { return juce::Font (juce::FontOptions (14.0f)); }

    //==============================================================================
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        using namespace theme;
        g.fillAll (label.findColour (juce::Label::backgroundColourId));
        if (label.isBeingEdited())
            return;

        const auto role = label.getProperties().getWithDefault ("role", {}).toString();
        auto c = role == "dim"   ? textDim
               : role == "text"  ? text
               : role == "error" ? danger
               : role == "accent"? accent
                                 : label.findColour (juce::Label::textColourId);
        if (! label.isEnabled()) c = c.withMultipliedAlpha (0.5f);

        g.setColour (c);
        g.setFont (getLabelFont (label));
        auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (label.getText(), area, label.getJustificationType(),
                          juce::jmax (1, (int) ((float) area.getHeight() / label.getFont().getHeight())),
                          label.getMinimumHorizontalScale());
    }

    //==============================================================================
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize, bool over, bool down) override
    {
        using namespace theme;
        juce::ignoreUnused (bar);
        auto track = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (displayBottom.withAlpha (0.6f));
        g.fillRoundedRectangle (track.reduced (isVertical ? 1.5f : 0.0f, isVertical ? 0.0f : 1.5f), 3.0f);

        auto thumb = isVertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize).toFloat().reduced (1.5f, 1.0f)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height).toFloat().reduced (1.0f, 1.5f);
        g.setColour (outline.brighter (down ? 0.6f : over ? 0.4f : 0.2f));
        g.fillRoundedRectangle (thumb, std::min (thumb.getWidth(), thumb.getHeight()) * 0.5f);
    }

    //==============================================================================
    void drawTooltip (juce::Graphics& g, const juce::String& txt, int width, int height) override
    {
        using namespace theme;
        auto r = juce::Rectangle<float> ((float) width, (float) height);
        g.setColour (panelRaised.darker (0.1f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (outline.brighter (0.15f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (txt, juce::FontOptions (13.0f), text);
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, (float) width - 16.0f);
        layout.draw (g, r.reduced (8.0f, 5.0f));
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& txt, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::AttributedString s;
        s.append (txt, juce::FontOptions (13.0f));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 320.0f);
        const int w = (int) std::ceil (layout.getWidth()) + 18, h = (int) std::ceil (layout.getHeight()) + 12;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 8, w, h)
                   .constrainedWithin (parent);
    }

    //==============================================================================
    void drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                          double progress, const juce::String&) override
    {
        using namespace theme;
        juce::ignoreUnused (bar);
        auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f, (float) height * 0.3f);
        g.setColour (displayBottom);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (outline);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        if (progress >= 0.0 && progress <= 1.0)
        {
            auto f = r.withWidth (r.getWidth() * (float) progress);
            g.setGradientFill (juce::ColourGradient (accent.darker (0.2f), f.getX(), 0.0f, accent.brighter (0.2f), f.getRight(), 0.0f, false));
            g.fillRoundedRectangle (f, r.getHeight() * 0.5f);
        }
        else
        {
            // indeterminate: a sliding highlight
            const double t = (double) juce::Time::getMillisecondCounter() / 900.0;
            const float seg = r.getWidth() * 0.3f;
            const float x0 = r.getX() + (float) std::fmod (t, 1.0) * (r.getWidth() + seg) - seg;
            g.saveState();
            g.reduceClipRegion (r.toNearestInt());
            g.setColour (accent.withAlpha (0.8f));
            g.fillRoundedRectangle ({ x0, r.getY(), seg, r.getHeight() }, r.getHeight() * 0.5f);
            g.restoreState();   // ProgressBar keeps repainting while indeterminate
        }
    }

};
