#include "Style.h"
#include "BinaryData.h"

// ---------------------------------------------------------------------------
// Fonts (Space Grotesk, Bagel Fat One, Anton: SIL Open Font License, embedded as binary data).
namespace
{
    juce::Typeface::Ptr typeface (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    juce::Typeface::Ptr grotesk()     { static auto t = typeface (BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize); return t; }
    juce::Typeface::Ptr groteskBold() { static auto t = typeface (BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize); return t; }
    juce::Typeface::Ptr bagel()       { static auto t = typeface (BinaryData::BagelFatOneLatin_ttf, BinaryData::BagelFatOneLatin_ttfSize); return t; }
    juce::Typeface::Ptr anton()       { static auto t = typeface (BinaryData::AntonRegular_ttf, BinaryData::AntonRegular_ttfSize); return t; }

    using PST = juce::PathStrokeType;
    PST roundStroke (float w) { return PST (w, PST::curved, PST::rounded); }
}

juce::FontOptions ui::font (float size, bool bold) { return juce::FontOptions (bold ? groteskBold() : grotesk()).withHeight (size); }
juce::FontOptions ui::display (float size)         { return juce::FontOptions (bagel()).withHeight (size); }
juce::FontOptions ui::caps (float size)            { return juce::FontOptions (anton()).withHeight (size).withKerningFactor (0.04f); }

float ui::textWidth (const juce::FontOptions& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth (juce::Font (f), s);
}

void ui::sticker (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill,
                  juce::Colour outline, juce::Colour shadow, float offset)
{
    if (offset > 0.0f && ! shadow.isTransparent())
    {
        g.setColour (shadow);
        g.fillRoundedRectangle (r.translated (offset, offset), radius);
    }
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    if (! outline.isTransparent())
    {
        g.setColour (outline);
        g.drawRoundedRectangle (r.reduced (ui::stroke / 2.0f), radius, ui::stroke);
    }
}

// ---------------------------------------------------------------------------
// Doodles
void doodle::sparkle (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour col)
{
    const float s = size / 2.0f, k = s * 0.16f;
    juce::Path p;
    p.startNewSubPath (c.x, c.y - s);
    p.quadraticTo (c.x + k, c.y - k, c.x + s, c.y);
    p.quadraticTo (c.x + k, c.y + k, c.x, c.y + s);
    p.quadraticTo (c.x - k, c.y + k, c.x - s, c.y);
    p.quadraticTo (c.x - k, c.y - k, c.x, c.y - s);
    p.closeSubPath();
    g.setColour (col);
    g.fillPath (p);
}

void doodle::note (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour col, bool twin)
{
    g.setColour (col);
    const float st = juce::jmax (1.5f, size * 0.09f);
    auto head = [&] (juce::Point<float> h)
    {
        juce::Path e;
        e.addEllipse (-size * 0.2f, -size * 0.14f, size * 0.4f, size * 0.28f);
        e.applyTransform (juce::AffineTransform::rotation (-0.4f).translated (h));
        g.fillPath (e);
    };
    const juce::Point<float> h1 (c.x - size * 0.22f, c.y + size * 0.32f);
    head (h1);
    const float sx1 = h1.x + size * 0.16f, top = c.y - size * 0.45f;
    g.drawLine (sx1, h1.y - size * 0.04f, sx1, top, st);
    if (! twin)
    {
        juce::Path f;
        f.startNewSubPath (sx1, top);
        f.cubicTo (sx1 + size * 0.08f, top + size * 0.16f, sx1 + size * 0.36f, top + size * 0.2f, sx1 + size * 0.24f, top + size * 0.46f);
        g.strokePath (f, roundStroke (st));
    }
    else
    {
        const juce::Point<float> h2 (h1.x + size * 0.46f, h1.y - size * 0.1f);
        head (h2);
        const float sx2 = h2.x + size * 0.16f;
        g.drawLine (sx2, h2.y - size * 0.04f, sx2, top - size * 0.1f, st);
        juce::Path beam;
        beam.startNewSubPath (sx1, top);
        beam.lineTo (sx2, top - size * 0.1f);
        g.strokePath (beam, roundStroke (st * 2.4f));
    }
}

void doodle::squiggle (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col, float thickness)
{
    juce::Path p;
    const int steps = 40;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        const float x = r.getX() + t * r.getWidth();
        const float y = r.getCentreY() + std::sin (t * juce::MathConstants<float>::twoPi * 2.5f) * r.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col);
    g.strokePath (p, roundStroke (thickness));
}

void doodle::arrows (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour col)
{
    const float s = size / 2.0f, st = juce::jmax (1.6f, size * 0.11f);
    g.setColour (col);
    for (int dir = -1; dir <= 1; dir += 2)
    {
        const float x = c.x + dir * s * 0.42f;
        const float tip = c.y + dir * s, tail = c.y - dir * s * 0.55f;
        juce::Path a;
        a.startNewSubPath (x, tail);
        a.lineTo (x, tip);
        a.startNewSubPath (x - s * 0.3f, tip - dir * s * 0.32f);
        a.lineTo (x, tip);
        a.lineTo (x + s * 0.3f, tip - dir * s * 0.32f);
        g.strokePath (a, roundStroke (st));
    }
}

void doodle::mascot (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float w = r.getWidth();
    ui::sticker (g, r.reduced (1.0f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f), (w - 4.0f) / 2.0f,
                 ui::card, ui::card, ui::greenDeep, 3.0f);

    juce::Graphics::ScopedSaveState save (g);
    const auto c = r.getCentre().translated (-1.5f, -1.5f);
    g.addTransform (juce::AffineTransform::rotation (-0.12f, c.x, c.y));

    // A fat note "blob" (thick in the middle, thin at the ends) with a happy face.
    juce::Path blob;
    const float x0 = c.x - w * 0.31f, x1 = c.x + w * 0.31f, cy = c.y + w * 0.08f;
    const int steps = 24;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / steps;
        const float h = w * (0.07f + 0.19f * std::sin (juce::MathConstants<float>::pi * std::pow (t, 0.8f)));
        const float x = x0 + t * (x1 - x0);
        if (i == 0) blob.startNewSubPath (x, cy - h); else blob.lineTo (x, cy - h);
    }
    for (int i = steps; i >= 0; --i)
    {
        const float t = (float) i / steps;
        const float h = w * (0.07f + 0.19f * std::sin (juce::MathConstants<float>::pi * std::pow (t, 0.8f)));
        blob.lineTo (x0 + t * (x1 - x0), cy + h);
    }
    blob.closeSubPath();
    g.setColour (ui::green);
    g.fillPath (blob);

    // pitch line across the blob
    juce::Path line;
    for (int i = 0; i <= 30; ++i)
    {
        const float t = (float) i / 30;
        const float x = x0 - w * 0.04f + t * (x1 - x0 + w * 0.08f);
        const float y = cy - w * 0.33f + std::sin (t * 9.0f) * w * 0.025f * t;
        if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    g.strokePath (line, roundStroke (juce::jmax (1.4f, w * 0.03f)));

    g.setColour (ui::card);
    const float ew = w * 0.04f, fx = c.x + w * 0.02f;
    for (int s = -1; s <= 1; s += 2)
    {
        juce::Path eye;
        const float ex = fx + s * w * 0.075f, ey = cy - w * 0.03f;
        eye.startNewSubPath (ex - ew, ey + ew * 0.5f);
        eye.quadraticTo (ex, ey - ew * 0.9f, ex + ew, ey + ew * 0.5f);
        g.strokePath (eye, roundStroke (juce::jmax (1.4f, w * 0.03f)));
    }
    juce::Path smile;
    smile.startNewSubPath (fx - w * 0.055f, cy + w * 0.035f);
    smile.quadraticTo (fx, cy + w * 0.095f, fx + w * 0.055f, cy + w * 0.035f);
    g.strokePath (smile, roundStroke (juce::jmax (1.4f, w * 0.03f)));

    doodle::sparkle (g, { c.x + w * 0.3f, c.y - w * 0.25f }, w * 0.13f, ui::green);
}

juce::Image doodle::pattern (int size, juce::Colour col)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    juce::Random rnd (9);
    const int cells = 4;
    const float cell = (float) size / cells;
    for (int cy = 0; cy < cells; ++cy)
        for (int cx = 0; cx < cells; ++cx)
        {
            const juce::Point<float> c ((cx + 0.5f) * cell + (rnd.nextFloat() - 0.5f) * cell * 0.3f,
                                        (cy + 0.5f) * cell + (rnd.nextFloat() - 0.5f) * cell * 0.3f);
            const float s = cell * (0.32f + 0.12f * rnd.nextFloat());
            switch ((cx + cy * 3 + (int) rnd.nextInt (2)) % 5)
            {
                case 0: doodle::sparkle (g, c, s, col); break;
                case 1: doodle::note (g, c, s, col); break;
                case 2: doodle::arrows (g, c, s * 0.8f, col); break;
                case 3: doodle::squiggle (g, juce::Rectangle<float> (s * 1.2f, s * 0.35f).withCentre (c), col, 2.0f); break;
                default:
                    g.setColour (col);
                    g.drawEllipse (juce::Rectangle<float> (s * 0.45f, s * 0.45f).withCentre (c), 2.0f);
                    break;
            }
        }
    return img;
}

juce::Image doodle::grain (int size)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Random rnd (11);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float v = rnd.nextFloat();
            float a = std::abs (v - 0.5f) * 0.05f;
            if (rnd.nextFloat() > 0.99f) a += 0.08f;
            img.setPixelAt (x, y, (v > 0.5f ? juce::Colours::white : ui::ink).withAlpha (a));
        }
    return img;
}

// ---------------------------------------------------------------------------
mdui::LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypeface (grotesk());
    setColour (juce::Slider::textBoxTextColourId, ui::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId, ui::green);
    setColour (juce::Label::textColourId, ui::inkSoft);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId, ui::green);
    setColour (juce::ComboBox::textColourId, ui::green);
    setColour (juce::ComboBox::arrowColourId, ui::green);
    setColour (juce::PopupMenu::backgroundColourId, ui::card);
    setColour (juce::PopupMenu::textColourId, ui::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::green);
    setColour (juce::PopupMenu::highlightedTextColourId, ui::card);
    setColour (juce::TextButton::textColourOffId, ui::green);
    setColour (juce::TextButton::textColourOnId, ui::card);
    setColour (juce::ToggleButton::textColourId, ui::ink);
    setColour (juce::TextEditor::backgroundColourId, ui::card);
    setColour (juce::TextEditor::textColourId, ui::ink);
    setColour (juce::TextEditor::highlightColourId, ui::tint);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, ui::green);
    setColour (juce::CaretComponent::caretColourId, ui::green);
    setColour (juce::TooltipWindow::backgroundColourId, ui::green);
    setColour (juce::TooltipWindow::textColourId, ui::card);
}

void mdui::LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float trackW = juce::jmax (4.0f, radius * 0.15f);
    const float arcR = radius - trackW / 2.0f;
    const auto accent = slider.findColour (juce::Slider::thumbColourId);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (ui::tint);
    g.strokePath (track, roundStroke (trackW));

    // Ticks for stepped knobs (the big PITCH knob: one per octave).
    if (slider.getProperties().getWithDefault ("ticks", 0).operator int() > 0)
    {
        const int n = slider.getProperties()["ticks"];
        g.setColour (ui::green.withAlpha (0.5f));
        for (int i = 0; i <= n; ++i)
        {
            const float a = startAngle + (float) i / n * (endAngle - startAngle);
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (radius + 2.0f, a), c.getPointOnCircumference (radius + 7.0f, a)), 2.0f);
        }
    }

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zero = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float a0 = startAngle + zero * (endAngle - startAngle);
    const float a1 = startAngle + pos * (endAngle - startAngle);
    if (std::abs (a1 - a0) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (slider.isEnabled() ? accent : ui::inkSoft);
        g.strokePath (value, roundStroke (trackW));
    }

    // Flat sticker knob with a hard shadow.
    const float bodyR = arcR - trackW - 2.5f;
    const float sh = juce::jmax (2.5f, bodyR * 0.07f);
    g.setColour (ui::green);
    g.fillEllipse (c.x - bodyR + sh, c.y - bodyR + sh, bodyR * 2, bodyR * 2);
    g.setColour (ui::card);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (ui::green);
    g.drawEllipse (c.x - bodyR + 1.0f, c.y - bodyR + 1.0f, bodyR * 2 - 2.0f, bodyR * 2 - 2.0f, ui::stroke);

    // Pointer: a line from the centre, or (knobs with a readout inside) a short notch at the rim.
    const bool rim = slider.getProperties().getWithDefault ("rimPointer", false);
    const auto p0 = c.getPointOnCircumference (bodyR * (rim ? 0.72f : 0.2f), a1);
    const auto p1 = c.getPointOnCircumference (bodyR * (rim ? 0.9f : 0.72f), a1);
    juce::Path pointer;
    pointer.startNewSubPath (p0);
    pointer.lineTo (p1);
    g.strokePath (pointer, roundStroke (juce::jmax (3.0f, bodyR * (rim ? 0.07f : 0.12f))));
}

void mdui::LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float cy = (float) y + (float) h / 2.0f;
    auto track = juce::Rectangle<float> ((float) x, cy - 4.0f, (float) w, 8.0f);
    const auto col = slider.findColour (juce::Slider::thumbColourId);
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    float zeroX = track.getX();
    if (bipolar) zeroX = track.getX() + (float) slider.valueToProportionOfLength (0.0) * track.getWidth();
    else if (slider.getMinimum() < 100.0 && slider.getMaximum() > 100.0 && slider.getSkewFactor() != 1.0)
        zeroX = track.getX() + (float) slider.valueToProportionOfLength (100.0) * track.getWidth();

    g.setColour (ui::card);
    g.fillRoundedRectangle (track, 4.0f);
    g.setColour (col);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zeroX, pos), track.getY(), std::abs (pos - zeroX), track.getHeight()), 4.0f);
    g.setColour (ui::green);
    g.drawRoundedRectangle (track.reduced (0.75f), 4.0f, 1.5f);
    if (bipolar) g.fillRect (zeroX - 1.0f, cy - 7.0f, 2.0f, 14.0f);
    g.fillEllipse (pos - 7.0f, cy - 6.0f, 16.0f, 16.0f);
    g.setColour (ui::card);
    g.fillEllipse (pos - 9.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour (ui::green);
    g.drawEllipse (pos - 8.0f, cy - 7.0f, 14.0f, 14.0f, ui::stroke);
}

void mdui::LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float h = 18.0f, w = 34.0f;
    auto sw = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - h / 2.0f, w, h);
    const float alpha = b.isEnabled() ? 1.0f : 0.4f;
    g.setColour ((on ? ui::green : (highlighted ? ui::tint : ui::card)).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (sw, h / 2.0f);
    g.setColour (ui::green.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (sw.reduced (1.0f), h / 2.0f, ui::stroke);
    const float d = h - 8.0f;
    g.setColour ((on ? ui::card : ui::green).withMultipliedAlpha (alpha));
    g.fillEllipse (on ? sw.getRight() - d - 4.0f : sw.getX() + 4.0f, sw.getY() + 4.0f, d, d);
    g.setColour (ui::ink.withMultipliedAlpha (alpha));
    g.setFont (ui::font (12.5f, true));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (w + 8.0f), juce::Justification::centredLeft);
}

// Buttons: "style" property "band" = on the green header; default = on cards / page.
void mdui::LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const bool on = b.getToggleState();
    const bool band = b.getProperties().getWithDefault ("style", "").toString() == "band";
    const float off = down ? 1.0f : 3.0f;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
    if (down) r = r.translated (2.0f, 2.0f);
    const float rad = juce::jmin (r.getHeight() / 2.0f, 11.0f);
    const float alpha = b.isEnabled() ? 1.0f : 0.45f;

    if (band)
    {
        if (on) ui::sticker (g, r, rad, ui::card, ui::card, ui::greenDeep, off);
        else    ui::sticker (g, r, rad, highlighted ? ui::greenDeep : ui::green, ui::card.withAlpha (0.85f), {}, 0.0f);
        return;
    }
    const juce::Colour fill = on ? ui::green : (highlighted && b.isEnabled() ? ui::tint : ui::card);
    ui::sticker (g, r, rad, fill.withMultipliedAlpha (alpha), ui::green.withMultipliedAlpha (alpha),
                 (on ? ui::ink : ui::tint).withMultipliedAlpha (alpha), on ? off : 2.0f);
}

void mdui::LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const bool on = b.getToggleState();
    const bool band = b.getProperties().getWithDefault ("style", "").toString() == "band";
    juce::Colour c = band ? (on ? ui::green : ui::card) : (on ? ui::card : ui::green);
    if (! b.isEnabled()) c = c.withAlpha (0.4f);
    g.setColour (c);
    const float size = b.getProperties().getWithDefault ("fontSize", b.getHeight() < 30 ? 13.0f : 15.0f);
    g.setFont (ui::caps (size));
    auto r = b.getLocalBounds().reduced (1).withTrimmedRight (3).withTrimmedBottom (3);
    if (down) r = r.translated (2, 2);
    g.drawFittedText (b.getButtonText(), r.reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

void mdui::LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
    ui::sticker (g, r, 11.0f, ui::card, ui::green, ui::tint, 3.0f);
    juce::Path chevron;
    const float cx = r.getRight() - 16.0f, cy = r.getCentreY();
    chevron.startNewSubPath (cx - 4.5f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.8f);
    chevron.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (ui::green);
    g.strokePath (chevron, roundStroke (2.4f));
}

juce::Font mdui::LookAndFeel::getComboBoxFont (juce::ComboBox&) { return ui::font (13.5f, true); }
juce::Font mdui::LookAndFeel::getPopupMenuFont() { return ui::font (14.0f, true); }

void mdui::LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 0, box.getWidth() - 34, box.getHeight() - 3);
    label.setFont (getComboBoxFont (box));
}

juce::Label* mdui::LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (ui::font (s.getSliderStyle() == juce::Slider::LinearHorizontal ? 12.5f : 13.5f, true));
    l->setColour (juce::Label::textColourId, ui::ink);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::backgroundColourId, ui::card);
    l->setColour (juce::TextEditor::textColourId, ui::ink);
    l->setColour (juce::TextEditor::highlightColourId, ui::tint);
    l->setColour (juce::CaretComponent::caretColourId, ui::green);
    return l;
}

void mdui::LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (ui::green);
    g.setColour (ui::card);
    g.setFont (ui::font (12.5f, true));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
}
