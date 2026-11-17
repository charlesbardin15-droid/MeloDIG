#pragma once
// Carlitaccuracy look: same family as FreeTune and FreePitch. Green monochrome, flat "sticker" cards with
// thick outlines and hard shadows, chunky display type and small hand-drawn doodles.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
    // Carlitaccuracy : BLANC : papier blanc, encre café, notes violettes, fond violet mat
    const juce::Colour green     { 0xff3b1311 };
    const juce::Colour greenDeep { 0xff230908 };
    const juce::Colour greenSoft { 0xff7d5cff };
    const juce::Colour tint      { 0xffe2dcf3 };
    const juce::Colour card      { 0xffffffff };
    const juce::Colour cardDeep  { 0xfff3f0fa };
    const juce::Colour ink       { 0xff2c0d0b };
    const juce::Colour inkSoft   { 0xff6b4a52 };
    const juce::Colour brown     { 0xff7d5cff };
    const juce::Colour lavender  { 0xffefeaff };
    const juce::Colour bg        { 0xff141329 };
    const juce::Colour night     { 0xff121227 };
    const juce::Colour violet    { 0xffb9a6ff };   // lavender page (under the gradient)
    const juce::Colour noteFill  { 0xff7d5cff };   // note blobs: lavender violet
    const juce::Colour rollBg    { 0xffffffff };   // note editor background: white
    const juce::Colour rollAlt   { 0xfff3f0fa };   // rows outside the scale / black keys
    const juce::Colour rollLine  { 0xffdcd6ef };
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
    juce::Image gradientPage (int w, int h);
    float carlitLetters (juce::Graphics&, juce::Point<float> topLeft, float height, juce::Colour, int count = 6);   // CarliFX letters, returns width
    void wordmark (juce::Graphics&, juce::Rectangle<float>);   // CARLIT letters + [ACCURACY] tag, like CarliFX   // Carlitaccuracy background: soft violet blobs + heavy grain
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
