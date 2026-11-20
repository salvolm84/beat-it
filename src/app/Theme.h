#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    const juce::Colour background   { 0xff121419 };
    const juce::Colour panel        { 0xff1b1e25 };
    const juce::Colour panelRaised  { 0xff242833 };
    const juce::Colour outline      { 0xff2f3440 };
    const juce::Colour text         { 0xffe8eaee };
    const juce::Colour textDim      { 0xff8b919c };
    const juce::Colour accent       { 0xffff8a3d };   // downbeats, primary actions
    const juce::Colour accentAlt    { 0xff4cc2ff };   // selection, tempo curve
    const juce::Colour waveform     { 0xff5a8fd6 };
    const juce::Colour waveformDim  { 0xff2b4a73 };
    const juce::Colour beatLine     { 0x66ffffff };
    const juce::Colour playhead     { 0xffffffff };
    const juce::Colour loop         { 0x2240d39a };
    const juce::Colour loopEdge     { 0xff40d39a };
    const juce::Colour danger       { 0xffff5a5a };
}

class BeatItLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BeatItLookAndFeel()
    {
        using namespace theme;
        setColourScheme ({ background, panel, panelRaised, outline, text,
                           accent, juce::Colours::black, accent, text });

        setColour (juce::TextButton::buttonColourId, panelRaised);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, panelRaised);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, panelRaised);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, outline);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxTextColourId, textDim);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3a404d));
        setColour (juce::ProgressBar::foregroundColourId, accent);
        setColour (juce::ProgressBar::backgroundColourId, panelRaised);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::ResizableWindow::backgroundColourId, background);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bg;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        else if (down)       c = c.brighter (0.15f);
        else if (highlighted) c = c.brighter (0.08f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (theme::outline.withAlpha (b.getToggleState() ? 0.0f : 1.0f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) h * 0.55f)));
    }
};
