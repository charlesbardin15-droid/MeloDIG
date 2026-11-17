#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Style.h"
#include "CarliLicenceUI.h"

class MeloDigEditor;

enum class Tool { main = 0, modulation, drift, formant, volume, split };

namespace mdui
{
    // Row of sticker pills, one selected. Each pill: title + small subtitle.
    class PillGroup : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        struct Item { juce::String title, subtitle; };
        explicit PillGroup (std::vector<Item> items);
        std::function<void (int)> onChange;
        void setSelected (int index);
        int getSelected() const { return selected; }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
    private:
        juce::Rectangle<float> cell (int i) const;
        std::vector<Item> items;
        int selected = 0, hover = -1;
    };
}

// The note editor: piano keyboard, time ruler, note blobs with their pitch curves.
class NoteView : public juce::Component
{
public:
    explicit NoteView (MeloDigEditor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool refresh();                                  // pull a new document version; true if changed
    void zoomToFit();
    void zoomBy (double factor);                     // horizontal + vertical, around the view centre
    int zoomPercent() const { return (int) std::lround (pxPerSec / 80.0 * 100.0); }
    void selectAll();
    void clearSelection() { selection.clear(); repaint(); }
    const std::vector<size_t>& getSelection() const { return selection; }
    const std::vector<melodig::Note>& getNotes() const { return notes; }
    void applyToSelection (const std::function<void (melodig::Note&)>&, bool undoable);
    juce::String describeSelection() const;

    Tool tool = Tool::main;
    juce::String hoverText;

private:
    enum class Drag { none, select, pitch, time, startEdge, endEdge, value, undecided };

    static constexpr int keyW = 46, rulerH = 26;
    juce::Rectangle<int> rollArea() const;
    float xOf (double sec) const;
    double secAt (float x) const;
    float yOf (double midi) const;
    double midiAt (float y) const;
    double sr() const;
    int hitNote (juce::Point<float>, int& edge) const;   // edge: -1 start, 0 body, +1 end
    void commit (std::vector<melodig::Note>, bool undoable);
    void rebuildCurves();
    void clampView();
    double noteOutSec (const melodig::Note&, double inputSample) const;

    MeloDigEditor& editor;
    ClipPtr clip;
    TrackPtr track;
    std::vector<melodig::Note> notes;
    int version = -1;
    melodig::EditCurves curves;
    std::vector<size_t> selection;

    double viewStart = 0.0, pxPerSec = 80.0, topMidi = 76.0;
    float pxPerSemi = 16.0f;

    Drag drag = Drag::none;
    juce::Point<float> downPos;
    std::vector<melodig::Note> dragBase;
    int dragNote = -1;
    juce::Rectangle<float> band;
    bool gestureOpen = false;
};

class MeloDigEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MeloDigEditor (MeloDigProcessor&);
    ~MeloDigEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    MeloDigProcessor& proc;
    void selectionChanged();
    void refreshNow() { timerCallback(); }   // one UI update without waiting for the timer (tests)

private:
    void timerCallback() override;
    struct Card { juce::Rectangle<int> r; juce::String title; juce::Colour colour; };
    void drawCard (juce::Graphics&, const Card&, int number) const;
    struct ValueRow
    {
        juce::Label label;
        juce::Slider slider;
    };
    void setupRow (ValueRow&, const juce::String& name, double lo, double hi, double step, const juce::String& suffix, double def);
    void updateInspector();

    mdui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image pageImage;

    // header
    juce::TextButton transferButton, playButton, beforeButton, afterButton, undoButton, redoButton;
    juce::TextButton zoomOutButton, zoomInButton, fitButton;
    juce::Rectangle<int> zoomReadout, statusArea;
    void setListenOriginal (bool);
    juce::String statusText;

    // tools / key
    mdui::PillGroup tools;
    juce::ComboBox snapBox, rootBox, scaleBox;
    juce::TextButton detectButton;

    NoteView view { *this };

    // inspector + macros
    juce::Label selectionLabel;
    ValueRow pitchRow, modRow, driftRow, formantRow, gainRow;
    bool updatingInspector = false;
    juce::Slider corrCentre, corrDrift;
    juce::Label corrCentreLabel, corrDriftLabel, corrHelp;
    juce::TextButton corrApply;
    juce::ComboBox gridBox;
    juce::Slider quantAmount;
    juce::Label gridLabel, quantLabel, quantHelp;
    juce::TextButton quantApply;

    std::vector<Card> cards;
    juce::Rectangle<int> bandArea, logoArea, headerButtons;
    int lastVersion = -1;
    float pulse = 0.0f;

    // Essai / activation
    int barHeight() const { return bar.isVisible() ? carlito::LicenceBar::height : 0; }
    void updateLicenceUi();
    carlito::LicenceTheme licenceTheme;
    carlito::LicenceBar bar { proc.licence, licenceTheme };
    carlito::ActivationPanel activation { proc.licence, licenceTheme };
    int licenceTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeloDigEditor)
};
