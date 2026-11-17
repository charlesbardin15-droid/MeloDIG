#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MeloEngine.h"
#include "CarliLicence.h"

// The recorded take, at the host sample rate.
struct Clip
{
    std::vector<std::vector<float>> data;   // [channel][sample]
    double sampleRate = 48000.0;
    long long hostStart = 0;                // timeline position of sample 0 (host samples)
    double hostStartPpq = 0.0;              // same, in quarter notes (for the bar grid)
    double bpm = 120.0;
    juce::File file;
    std::vector<float> peak;                // mono peak summary, one value per 256 samples (drawing)
    int numSamples() const { return data.empty() ? 0 : (int) data[0].size(); }
    double lengthSeconds() const { return numSamples() / sampleRate; }
};
using ClipPtr = std::shared_ptr<const Clip>;
using TrackPtr = std::shared_ptr<const melodig::PitchTrack>;
using AudioPtr = std::shared_ptr<const std::vector<std::vector<float>>>;

class MeloDigProcessor : public juce::AudioProcessor
{
public:
    MeloDigProcessor();
    ~MeloDigProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------- message thread API (editor) ----------------
    enum class Transfer { idle, armed, recording };
    Transfer getTransferState() const { return transfer.load(); }
    void startTransfer();                 // arm: recording starts when the host plays
    void stopTransfer();                  // finish now (also happens when the host stops)
    double getTransferSeconds() const;

    ClipPtr getClip() const;
    TrackPtr getTrack() const;
    AudioPtr getRendered() const;
    std::vector<melodig::Note> getNotes() const;
    int getNotesVersion() const { return notesVersion.load(); }
    bool isAnalysing() const { return analysing.load(); }
    bool isRendering() const { return rendering.load(); }

    // Replace the notes (an edit). `undoable`: push the previous state on the undo stack first.
    void setNotes (std::vector<melodig::Note>, bool undoable);
    void beginGesture();                  // snapshot for undo once per mouse gesture
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    melodig::Scale scale;                 // edited from the UI (message thread)
    int snapMode = 1;                     // 0 free, 1 chromatic, 2 scale

    // transport (for the play line)
    bool isHostPlaying() const { return hostPlaying.load(); }
    double getPlaySecondsInClip() const;  // host or preview position relative to the clip
    void setPreviewing (bool, double fromSeconds = 0.0);
    bool isPreviewing() const { return previewing.load(); }
    void setListenOriginal (bool b) { listenOriginal.store (b); }   // AVANT / APRÈS
    bool isListeningOriginal() const { return listenOriginal.load(); }
    double getHostBpm() const { return hostBpm.load(); }
    double getCurrentSampleRate() const { return currentSampleRate.load(); }
    juce::String getLastError() const;

    static juce::File getDataFolder();
    void timerTick();                     // called by the editor's timer and the internal one

    carlito::Licence licence { "MDG" };   // essai 14 jours / clé

private:
    class Worker;
    class Ticker : public juce::Timer
    {
    public:
        explicit Ticker (MeloDigProcessor& p) : owner (p) {}
        void timerCallback() override { owner.timerTick(); }
        MeloDigProcessor& owner;
    };

    void finishTransfer();
    void newClip (std::shared_ptr<Clip>, std::vector<melodig::Note> restoredNotes, bool restored);
    void requestRender();
    void publishRendered (AudioPtr);
    static void computePeaks (Clip&);
    static juce::String notesToString (const std::vector<melodig::Note>&);
    static std::vector<melodig::Note> notesFromString (const juce::String&);

    // document (message thread, read by the worker under lock)
    mutable juce::CriticalSection docLock;
    ClipPtr clip;
    TrackPtr track;
    std::vector<melodig::Note> notes;
    std::vector<std::vector<melodig::Note>> undoStack, redoStack;
    std::atomic<int> notesVersion { 0 };
    juce::String lastError;

    // audio-thread side
    mutable juce::SpinLock playLock;
    ClipPtr playClip;
    AudioPtr rendered;

    std::atomic<Transfer> transfer { Transfer::idle };
    std::vector<std::vector<float>> captureBuffer;
    std::atomic<long long> recStart { -1 }, recMaxWritten { 0 };
    std::atomic<bool> finishRequested { false };
    double recBpm = 120.0, recPpq = 0.0;
    std::atomic<double> recPpqAtomic { 0.0 }, recBpmAtomic { 120.0 };

    std::atomic<bool> hostPlaying { false };
    std::atomic<long long> hostPos { 0 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> previewing { false };
    std::atomic<bool> listenOriginal { false };
    std::atomic<long long> previewPos { 0 };

    std::atomic<bool> analysing { false }, rendering { false };
    std::atomic<double> currentSampleRate { 48000.0 };

    std::unique_ptr<Worker> worker;
    Ticker ticker { *this };
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeloDigProcessor)
};
