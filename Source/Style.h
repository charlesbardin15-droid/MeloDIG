#pragma once
// MeloDIG look: same family as FreeTune and FreePitch. Green monochrome, flat "sticker" cards with
// thick outlines and hard shadows, chunky display type and small hand-drawn doodles.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
    const juce::Colour green     { 0xff1e4631 };   // primary: deep green
    const juce::Colour greenDeep { 0xff112e1f };
    const juce::Colour greenSoft { 0xff4f7a3a };   // secondary green (markers)
    const juce::Colour tint      { 0xffc9d4b0 };   // light sage
    const juce::Colour bg        { 0xffdde4c7 };   // page
    const juce::Colour card      { 0xfff1eddb };   // cream
    const juce::Colour cardDeep  { 0xffe5e0c8 };   // darker cream (wells, lanes)
    const juce::Colour ink       { 0xff17201a };
    const juce::Colour inkSoft   { 0xff56634f };
    const juce::Colour brown     { 0xff9a5530 };   // pitch accent (coffee)
    constexpr float stroke = 2.0f;

    juce::FontOptions font (float size, bool bold = false);   // Space Grotesk
    juce::FontOptions display (float size);                   // Bagel Fat One
    juce::FontOptions caps (float size);                      // Anton

    void sticker (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour fill,
                  juce::Colour outline, juce::Colour shadow, float offset);

    float textWidth (const juce::FontOptions&, const juce::String&);
}

namespace doodle
{
    void sparkle (juce::Graphics&, juce::Point<float> c, float size, juce::Colour);
    void note (juce::Graphics&, juce::Point<float> c, float size, juce::Colour, bool twin = false);
    void squiggle (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float thickness = 2.0f);
    void arrows (juce::Graphics&, juce::Point<float> c, float size, juce::Colour);   // up + down arrows
    void mascot (juce::Graphics&, juce::Rectangle<float>);   // happy note blob with its pitch line
    juce::Image pattern (int size, juce::Colour);
    juce::Image grain (int size);
}

namespace mdui
{
    // Colour roles used by the editor (all from the ui palette).
    namespace colours
    {
        const juce::Colour bg       = ui::cardDeep;   // wells: waveform lanes
        const juce::Colour panel    = ui::card;
        const juce::Colour panel2   = ui::cardDeep;
        const juce::Colour track    = ui::tint;
        const juce::Colour accent   = ui::green;      // tempo / main
        const juce::Colour pitch    = ui::brown;      // pitch
        const juce::Colour morph    = ui::greenSoft;  // Time-Morph markers
        const juce::Colour wave     { 0xff8a9878 };
        const juce::Colour waveOut  = ui::green;
        const juce::Colour text     = ui::ink;
        const juce::Colour textDim  = ui::inkSoft;
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getPopupMenuFont() override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    };
}
