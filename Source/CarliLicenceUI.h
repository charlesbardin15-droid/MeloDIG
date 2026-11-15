// Carlito Audio : bandeau d'essai + fenêtre d'activation, habillés par chaque plugin (LicenceTheme).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CarliLicence.h"

namespace carlito
{
struct LicenceTheme
{
    juce::Colour backdrop { 0xd8000000 }, panel { 0xff161616 }, outline { 0xff2e2e2e },
                 accent { 0xffffdd00 }, onAccent { 0xff0b0b0b }, ink { 0xfff2f0e6 }, inkSoft { 0xff8c8a80 },
                 alert { 0xffff5a3c }, field { 0xff0b0b0b },
                 barFill, barInk;          // bandeau : transparent = couleurs accent / onAccent
    juce::FontOptions title { 28.0f }, caps { 14.0f }, body { 14.0f }, bodyBold { 14.0f, juce::Font::bold };
    float radius = 0.0f;           // coins des panneaux et des boutons
    juce::String productName;      // "CarliFX"
};

namespace detail
{
    inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    class FlatButton : public juce::Button
    {
    public:
        FlatButton (const LicenceTheme& t, const juce::String& text, bool filledStyle)
            : juce::Button (text), theme (t), filled (filledStyle)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (down) r = r.translated (1.0f, 1.0f);
            const bool solid = filled != over;
            g.setColour (solid ? textColour() : juce::Colours::transparentBlack);
            if (solid) g.fillRoundedRectangle (r, theme.radius);
            g.setColour (textColour());
            g.drawRoundedRectangle (r, theme.radius, 1.5f);
            g.setColour (solid ? background() : textColour());
            g.setFont (theme.caps);
            g.drawText (getButtonText(), r, juce::Justification::centred, false);
        }
        juce::Colour onColour, offColour;   // optional overrides (bar on an accent background)
    private:
        juce::Colour textColour() const { return onColour.isTransparent() ? theme.accent : onColour; }
        juce::Colour background() const { return offColour.isTransparent() ? theme.onAccent : offColour; }
        const LicenceTheme& theme;
        const bool filled;
    };
}

// Bandeau fin en haut de l'interface : "ESSAI GRATUIT · 12 JOURS RESTANTS   [ACTIVER]". Caché une fois activé.
class LicenceBar : public juce::Component
{
public:
    static constexpr int height = 34;

    LicenceBar (Licence& l, const LicenceTheme& t) : licence (l), theme (t), button (t, detail::u8 ("ACTIVER"), true)
    {
        button.onColour = ink(); button.offColour = fill();
        button.onClick = [this] { if (onActivate) onActivate(); };
        addAndMakeVisible (button);
    }
    std::function<void()> onActivate;

    void paint (juce::Graphics& g) override
    {
        const bool expired = licence.getState() == Licence::State::expired;
        auto r = getLocalBounds().toFloat();
        g.setColour (expired ? theme.alert : fill());
        g.fillRect (r);
        g.setColour (expired ? juce::Colours::white : ink());
        auto text = r.reduced (16.0f, 0.0f).withTrimmedRight ((float) button.getWidth() + 16.0f);
        g.setFont (theme.caps);
        const int d = licence.getDaysLeft();
        juce::String left = expired ? detail::u8 ("ESSAI TERMIN\xc3\x89")
                                    : detail::u8 ("ESSAI GRATUIT \xc2\xb7 ") + juce::String (d) + (d > 1 ? " JOURS RESTANTS" : " JOUR RESTANT");
        g.drawText (left, text, juce::Justification::centredLeft, false);
        const float w = juce::GlyphArrangement::getStringWidth (juce::Font (theme.caps), left);
        g.setFont (theme.body);
        g.drawText (expired ? detail::u8 ("Le son passe sans effet. Entre ta cl\xc3\xa9 pour continuer.")
                            : detail::u8 ("Toutes les fonctions sont ouvertes pendant l'essai."),
                    text.withTrimmedLeft (w + 18.0f), juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        button.setBounds (getLocalBounds().removeFromRight (128).reduced (12, 5));
        const bool expired = licence.getState() == Licence::State::expired;
        button.onColour = expired ? juce::Colours::white : ink();
        button.offColour = expired ? theme.alert : fill();
        button.repaint();
    }
    void update() { resized(); repaint(); }

private:
    juce::Colour fill() const { return theme.barFill.isTransparent() ? theme.accent : theme.barFill; }
    juce::Colour ink() const { return theme.barInk.isTransparent() ? theme.onAccent : theme.barInk; }
    Licence& licence;
    const LicenceTheme& theme;
    detail::FlatButton button;
};

// Fenêtre d'activation posée par-dessus l'interface.
class ActivationPanel : public juce::Component
{
public:
    ActivationPanel (Licence& l, const LicenceTheme& t)
        : licence (l), theme (t),
          activate (t, detail::u8 ("ACTIVER"), true), later (t, detail::u8 ("PLUS TARD"), false)
    {
        for (auto* e : { &email, &key })
        {
            e->setIndents (10, 8);
            e->onReturnKey = [this] { tryActivate(); };
            addAndMakeVisible (*e);
        }
        key.setMultiLine (true, true);
        key.setReturnKeyStartsNewLine (false);
        applyTheme();
        activate.onClick = [this] { tryActivate(); };
        later.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (activate);
        addAndMakeVisible (later);
        setWantsKeyboardFocus (true);
    }
    std::function<void()> onClose, onActivated;

    void show() { applyTheme(); message = {}; failed = false; setVisible (true); toFront (true); email.grabKeyboardFocus(); repaint(); }

    // Le thème est souvent rempli après la construction : on le réapplique à chaque ouverture.
    void applyTheme()
    {
        for (auto* e : { &email, &key })
        {
            e->setColour (juce::TextEditor::backgroundColourId, theme.field);
            e->setColour (juce::TextEditor::textColourId, theme.ink);
            e->setColour (juce::TextEditor::outlineColourId, theme.outline);
            e->setColour (juce::TextEditor::focusedOutlineColourId, theme.accent);
            e->setColour (juce::TextEditor::highlightColourId, theme.accent.withAlpha (0.35f));
            e->setColour (juce::CaretComponent::caretColourId, theme.accent);
            e->applyFontToAllText (juce::Font (theme.body));
            e->setFont (juce::Font (theme.body));
        }
        email.setTextToShowWhenEmpty (detail::u8 ("ton-adresse@mail.com"), theme.inkSoft);
        key.setTextToShowWhenEmpty (detail::u8 ("Colle ici la cl\xc3\xa9 re\xc3\xa7ue par e-mail (XXXXX-XXXXX-\xe2\x80\xa6)"), theme.inkSoft);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.backdrop);
        auto c = card().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (c.translated (8.0f, 8.0f), theme.radius);
        g.setColour (theme.panel);
        g.fillRoundedRectangle (c, theme.radius);
        g.setColour (theme.accent);
        g.drawRoundedRectangle (c, theme.radius, 2.0f);

        auto r = c.reduced (34.0f, 28.0f);
        const bool expired = licence.getState() == Licence::State::expired;
        g.setColour (theme.accent);
        g.setFont (theme.caps);
        g.drawText (expired ? detail::u8 ("L'ESSAI EST TERMIN\xc3\x89")
                            : detail::u8 ("ESSAI \xc2\xb7 ") + juce::String (licence.getDaysLeft()) + detail::u8 (" JOURS RESTANTS"),
                    r.removeFromTop (20.0f), juce::Justification::centredLeft, false);
        g.setFont (theme.title);
        g.drawText ("ACTIVER " + theme.productName.toUpperCase(), r.removeFromTop (44.0f), juce::Justification::centredLeft, false);
        g.setColour (theme.ink);
        g.setFont (theme.body);
        g.drawFittedText (detail::u8 ("Entre l'adresse e-mail utilis\xc3\xa9""e pour l'achat et la cl\xc3\xa9 re\xc3\xa7ue. "
                                      "L'activation se fait sur cet ordinateur, sans connexion internet. "
                                      "Une cl\xc3\xa9 du pack d\xc3\xa9""bloque les quatre plugins."),
                          r.removeFromTop (48.0f).toNearestInt(), juce::Justification::topLeft, 3);

        g.setColour (theme.inkSoft);
        g.setFont (theme.caps);
        g.drawText ("E-MAIL", labelFor (email), juce::Justification::bottomLeft, false);
        g.drawText (detail::u8 ("CL\xc3\x89 DE LICENCE"), labelFor (key), juce::Justification::bottomLeft, false);

        if (message.isNotEmpty())
        {
            g.setColour (failed ? theme.alert : theme.accent);
            g.setFont (theme.bodyBold);
            g.drawText (message, messageArea, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = card().reduced (34, 28);
        r.removeFromTop (20 + 44 + 48 + 22);
        email.setBounds (r.removeFromTop (40));
        r.removeFromTop (30);
        key.setBounds (r.removeFromTop (86));
        r.removeFromTop (16);
        auto row = r.removeFromBottom (42);
        activate.setBounds (row.removeFromRight (150));
        row.removeFromRight (10);
        later.setBounds (row.removeFromRight (130));
        messageArea = row.withTrimmedRight (10);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! card().contains (e.getPosition()) && onClose) onClose();
    }

private:
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (560, 410); }
    juce::Rectangle<int> labelFor (const juce::Component& c) const { return c.getBounds().translated (0, -24).withHeight (20); }

    void tryActivate()
    {
        if (licence.activate (email.getText(), key.getText()))
        {
            failed = false;
            message = detail::u8 ("Activ\xc3\xa9 pour ") + licence.getOwner() + ". Merci !";
            repaint();
            if (onActivated) onActivated();
            return;
        }
        failed = true;
        message = email.isEmpty() || key.isEmpty() ? detail::u8 ("Remplis l'e-mail et la cl\xc3\xa9.")
                                                   : detail::u8 ("Cl\xc3\xa9 refus\xc3\xa9""e : v\xc3\xa9rifie l'e-mail et la cl\xc3\xa9.");
        repaint();
    }

    Licence& licence;
    const LicenceTheme& theme;
    juce::TextEditor email, key;
    detail::FlatButton activate, later;
    juce::String message;
    bool failed = false;
    juce::Rectangle<int> messageArea;
};
}
