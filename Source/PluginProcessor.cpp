#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace melodig;

// ---------------------------------------------------------------------------
// Background worker: analysis of a new take, then (re)rendering after every edit.
class MeloDigProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (MeloDigProcessor& p) : juce::Thread ("MeloDIG worker"), owner (p) {}

    void requestAnalysis (std::shared_ptr<Clip> c, std::vector<Note> restored, bool isRestored)
    {
        {
            const juce::ScopedLock sl (jobLock);
            pendingClip = std::move (c);
            pendingNotes = std::move (restored);
            pendingRestored = isRestored;
        }
        renderGen.fetch_add (1);   // cancel a render in progress
        notify();
    }
    void requestRender() { renderGen.fetch_add (1); notify(); }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::shared_ptr<Clip> c;
            std::vector<Note> restored;
            bool isRestored = false;
            {
                const juce::ScopedLock sl (jobLock);
                c = std::move (pendingClip);
                pendingClip.reset();
                restored = std::move (pendingNotes);
                isRestored = pendingRestored;
            }
            if (c != nullptr)
            {
                owner.analysing.store (true);
                auto a = std::make_shared<Analysis> (analyse (c->data, c->sampleRate));
                owner.analysing.store (false);
                if (threadShouldExit()) return;
                {
                    const juce::ScopedLock sl (owner.docLock);
                    if (owner.clip.get() != c.get()) continue;   // superseded by another take
                    owner.track = std::make_shared<PitchTrack> (a->track);
                    owner.notes = (isRestored && ! restored.empty()) ? restored : a->notes;
                    owner.undoStack.clear();
                    owner.redoStack.clear();
                    owner.notesVersion.fetch_add (1);
                }
                renderGen.fetch_add (1);
            }

            const int gen = renderGen.load();
            if (gen != doneGen)
            {
                ClipPtr cl; TrackPtr tr; std::vector<Note> ns;
                {
                    const juce::ScopedLock sl (owner.docLock);
                    cl = owner.clip; tr = owner.track; ns = owner.notes;
                }
                if (cl != nullptr && tr != nullptr)
                {
                    owner.rendering.store (true);
                    auto out = std::make_shared<std::vector<std::vector<float>>>();
                    RenderInput in { &cl->data, tr.get(), &ns };
                    const bool ok = Renderer::render (in, *out, [&] { return threadShouldExit() || renderGen.load() != gen; });
                    owner.rendering.store (false);
                    if (ok) owner.publishRendered (out);
                }
                doneGen = gen;
                continue;
            }
            wait (500);
        }
    }

private:
    MeloDigProcessor& owner;
    juce::CriticalSection jobLock;
    std::shared_ptr<Clip> pendingClip;
    std::vector<Note> pendingNotes;
    bool pendingRestored = false;
    std::atomic<int> renderGen { 0 };
    int doneGen = 0;
};

// ---------------------------------------------------------------------------
MeloDigProcessor::MeloDigProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    scale.root = 0;
    scale.type = 0;
    worker = std::make_unique<Worker> (*this);
    worker->startThread (juce::Thread::Priority::low);
    ticker.startTimerHz (20);
}

MeloDigProcessor::~MeloDigProcessor()
{
    ticker.stopTimer();
    worker->signalThreadShouldExit();
    worker->notify();
    worker->stopThread (5000);
}

bool MeloDigProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MeloDigProcessor::prepareToPlay (double sampleRate, int)
{
    licence.refresh();
    currentSampleRate.store (sampleRate);
    setLatencySamples (0);
}

juce::File MeloDigProcessor::getDataFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("MeloDIG");
    f.createDirectory();
    return f;
}

// ---------------------------------------------------------------------------
void MeloDigProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);

    bool playing = false, hasTime = false;
    long long time = 0;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples()) { time = *t; hasTime = true; }
            if (auto q = pos->getPpqPosition()) ppq = *q;
            if (auto b = pos->getBpm()) hostBpm.store (*b);
        }
    hostPlaying.store (playing);
    if (hasTime) hostPos.store (time);
    if (! licence.audioAllowed()) return;   // essai terminé : la piste passe sans modification

    // ---- Transfer (recording the track while the host plays) ----
    const auto tr = transfer.load();
    if (tr == Transfer::armed && playing && hasTime)
    {
        recStart.store (time);
        recPpqAtomic.store (ppq);
        recBpmAtomic.store (hostBpm.load());
        recMaxWritten.store (0);
        transfer.store (Transfer::recording);
    }
    if (transfer.load() == Transfer::recording)
    {
        if (! playing) finishRequested.store (true);
        else if (hasTime && ! captureBuffer.empty())
        {
            const long long cap = (long long) captureBuffer[0].size();
            const long long idx = time - recStart.load();
            for (int c = 0; c < (int) captureBuffer.size(); ++c)
            {
                const float* src = buffer.getReadPointer (juce::jmin (c, numIn - 1));
                auto& dst = captureBuffer[(size_t) c];
                for (int i = 0; i < n; ++i)
                {
                    const long long k = idx + i;
                    if (k >= 0 && k < cap) dst[(size_t) k] = src[i];
                }
            }
            const long long end = juce::jlimit (0LL, cap, idx + n);
            if (end > recMaxWritten.load()) recMaxWritten.store (end);
        }
        return;   // the input is heard while transferring
    }

    // ---- Playback of the edited take ----
    ClipPtr c;
    AudioPtr r;
    {
        const juce::SpinLock::ScopedTryLockType lock (playLock);
        if (lock.isLocked()) { c = playClip; r = rendered; }
    }
    if (c == nullptr || r == nullptr || r->empty()) return;
    const auto& out = *r;
    const long long len = (long long) out[0].size();
    const int rch = (int) out.size();

    if (playing && hasTime)
    {
        previewing.store (false);
        const long long base = time - c->hostStart;
        if (base + n <= 0 || base >= len) return;   // outside the take: input passes through
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* dst = buffer.getWritePointer (ch);
            const auto& src = out[(size_t) juce::jmin (ch, rch - 1)];
            for (int i = 0; i < n; ++i)
            {
                const long long k = base + i;
                if (k >= 0 && k < len) dst[i] = src[(size_t) k];
            }
        }
        return;
    }

    if (previewing.load())
    {
        long long p = previewPos.load();
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* dst = buffer.getWritePointer (ch);
            const auto& src = out[(size_t) juce::jmin (ch, rch - 1)];
            for (int i = 0; i < n; ++i) dst[i] = p + i < len ? src[(size_t) (p + i)] : 0.0f;
        }
        p += n;
        previewPos.store (p);
        if (p >= len) previewing.store (false);
    }
}

// ---------------------------------------------------------------------------
void MeloDigProcessor::startTransfer()
{
    if (transfer.load() != Transfer::idle) return;
    setPreviewing (false);
    const double sr = currentSampleRate.load();
    const int chans = juce::jlimit (1, 2, getTotalNumInputChannels());
    const size_t cap = (size_t) (sr * 60.0 * 6.0);   // up to 6 minutes
    try { captureBuffer.assign ((size_t) chans, std::vector<float> (cap, 0.0f)); }
    catch (...) { lastError = "Pas assez de memoire pour enregistrer."; return; }
    recStart.store (-1);
    recMaxWritten.store (0);
    finishRequested.store (false);
    transfer.store (Transfer::armed);
}

void MeloDigProcessor::stopTransfer()
{
    if (transfer.load() == Transfer::armed) { transfer.store (Transfer::idle); captureBuffer.clear(); captureBuffer.shrink_to_fit(); return; }
    if (transfer.load() == Transfer::recording) finishTransfer();
}

double MeloDigProcessor::getTransferSeconds() const
{
    return recMaxWritten.load() / juce::jmax (1.0, currentSampleRate.load());
}

void MeloDigProcessor::finishTransfer()
{
    finishRequested.store (false);
    transfer.store (Transfer::idle);
    juce::Thread::sleep (30);   // let a block in flight finish writing
    const long long len = recMaxWritten.load();
    if (len < (long long) (0.2 * currentSampleRate.load()))
    {
        captureBuffer.clear(); captureBuffer.shrink_to_fit();
        lastError = "Rien n'a ete enregistre : lance la lecture dans Ableton apres avoir clique sur TRANSFERER.";
        return;
    }
    auto c = std::make_shared<Clip>();
    c->sampleRate = currentSampleRate.load();
    c->hostStart = recStart.load();
    c->hostStartPpq = recPpqAtomic.load();
    c->bpm = recBpmAtomic.load();
    // a mono part on a stereo track: keep one channel
    bool same = captureBuffer.size() == 2;
    if (same)
        for (long long i = 0; i < len; i += 7)
            if (std::abs (captureBuffer[0][(size_t) i] - captureBuffer[1][(size_t) i]) > 1e-6f) { same = false; break; }
    const size_t chans = same ? 1 : captureBuffer.size();
    for (size_t ch = 0; ch < chans; ++ch)
        c->data.emplace_back (captureBuffer[ch].begin(), captureBuffer[ch].begin() + (long) len);
    captureBuffer.clear();
    captureBuffer.shrink_to_fit();

    // Keep the take on disk so the project can reopen it.
    auto folder = getDataFolder().getChildFile ("Takes");
    folder.createDirectory();
    c->file = folder.getNonexistentChildFile ("take-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"), ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (c->file.createOutputStream().release());
        if (os != nullptr)
            if (auto* w = wav.createWriterFor (os.get(), c->sampleRate, (unsigned) c->data.size(), 24, {}, 0))
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                std::vector<const float*> ptrs;
                for (auto& ch : c->data) ptrs.push_back (ch.data());
                writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) len);
            }
    }
    newClip (c, {}, false);
}

void MeloDigProcessor::computePeaks (Clip& c)
{
    const int n = c.numSamples();
    c.peak.assign ((size_t) (n / 256 + 1), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        float v = 0;
        for (auto& ch : c.data) v = juce::jmax (v, std::abs (ch[(size_t) i]));
        auto& p = c.peak[(size_t) (i / 256)];
        p = juce::jmax (p, v);
    }
}

void MeloDigProcessor::newClip (std::shared_ptr<Clip> c, std::vector<Note> restoredNotes, bool restored)
{
    computePeaks (*c);
    {
        const juce::ScopedLock sl (docLock);
        clip = c;
        track.reset();
        notes.clear();
        undoStack.clear();
        redoStack.clear();
        notesVersion.fetch_add (1);
    }
    {
        const juce::SpinLock::ScopedLockType lock (playLock);
        playClip = c;
        rendered = std::make_shared<std::vector<std::vector<float>>> (c->data);   // play the raw take until the render is ready
    }
    worker->requestAnalysis (c, std::move (restoredNotes), restored);
}

void MeloDigProcessor::publishRendered (AudioPtr r)
{
    AudioPtr old;
    {
        const juce::SpinLock::ScopedLockType lock (playLock);
        old = rendered;
        rendered = std::move (r);
    }
    // `old` may still be in use by the audio thread for this block: release it later.
    juce::MessageManager::callAsync ([old] { juce::Thread::sleep (0); });
}

void MeloDigProcessor::timerTick()
{
    if (finishRequested.load() && transfer.load() == Transfer::recording) finishTransfer();
}

// ---------------------------------------------------------------------------
ClipPtr MeloDigProcessor::getClip() const { const juce::ScopedLock sl (docLock); return clip; }
TrackPtr MeloDigProcessor::getTrack() const { const juce::ScopedLock sl (docLock); return track; }
std::vector<Note> MeloDigProcessor::getNotes() const { const juce::ScopedLock sl (docLock); return notes; }
juce::String MeloDigProcessor::getLastError() const { return lastError; }

AudioPtr MeloDigProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType lock (playLock);
    return rendered;
}

void MeloDigProcessor::beginGesture()
{
    const juce::ScopedLock sl (docLock);
    undoStack.push_back (notes);
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void MeloDigProcessor::setNotes (std::vector<Note> ns, bool undoable)
{
    {
        const juce::ScopedLock sl (docLock);
        if (undoable)
        {
            undoStack.push_back (notes);
            if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
            redoStack.clear();
        }
        notes = std::move (ns);
        notesVersion.fetch_add (1);
    }
    requestRender();
}

bool MeloDigProcessor::undo()
{
    {
        const juce::ScopedLock sl (docLock);
        if (undoStack.empty()) return false;
        redoStack.push_back (notes);
        notes = undoStack.back();
        undoStack.pop_back();
        notesVersion.fetch_add (1);
    }
    requestRender();
    return true;
}

bool MeloDigProcessor::redo()
{
    {
        const juce::ScopedLock sl (docLock);
        if (redoStack.empty()) return false;
        undoStack.push_back (notes);
        notes = redoStack.back();
        redoStack.pop_back();
        notesVersion.fetch_add (1);
    }
    requestRender();
    return true;
}

bool MeloDigProcessor::canUndo() const { const juce::ScopedLock sl (docLock); return ! undoStack.empty(); }
bool MeloDigProcessor::canRedo() const { const juce::ScopedLock sl (docLock); return ! redoStack.empty(); }

void MeloDigProcessor::requestRender() { worker->requestRender(); }

double MeloDigProcessor::getPlaySecondsInClip() const
{
    auto c = getClip();
    const double sr = c != nullptr ? c->sampleRate : currentSampleRate.load();
    if (previewing.load()) return previewPos.load() / sr;
    if (c == nullptr) return -1.0;
    return (double) (hostPos.load() - c->hostStart) / sr;
}

void MeloDigProcessor::setPreviewing (bool b, double fromSeconds)
{
    if (b)
    {
        auto c = getClip();
        if (c == nullptr) return;
        previewPos.store ((long long) juce::jmax (0.0, fromSeconds * c->sampleRate));
    }
    previewing.store (b);
}

// ---------------------------------------------------------------------------
// State: the take lives in a WAV file next to the project's data; notes are stored as text.
juce::String MeloDigProcessor::notesToString (const std::vector<Note>& ns)
{
    juce::StringArray parts;
    for (const auto& n : ns)
    {
        juce::StringArray v;
        for (double x : { n.start, n.end, n.voicedStart, n.voicedEnd, n.newStart, n.newEnd }) v.add (juce::String ((juce::int64) std::llround (x)));
        for (float x : { n.centre, n.pitchOffset, n.modulation, n.drift, n.formant, n.gainDb }) v.add (juce::String (x, 5));
        v.add (n.muted ? "1" : "0");
        parts.add (v.joinIntoString (","));
    }
    return parts.joinIntoString (";");
}

std::vector<Note> MeloDigProcessor::notesFromString (const juce::String& s)
{
    std::vector<Note> ns;
    for (auto& part : juce::StringArray::fromTokens (s, ";", ""))
    {
        auto v = juce::StringArray::fromTokens (part, ",", "");
        if (v.size() < 13) continue;
        Note n;
        n.start = v[0].getDoubleValue(); n.end = v[1].getDoubleValue();
        n.voicedStart = v[2].getDoubleValue(); n.voicedEnd = v[3].getDoubleValue();
        n.newStart = v[4].getDoubleValue(); n.newEnd = v[5].getDoubleValue();
        n.centre = v[6].getFloatValue(); n.pitchOffset = v[7].getFloatValue();
        n.modulation = v[8].getFloatValue(); n.drift = v[9].getFloatValue();
        n.formant = v[10].getFloatValue(); n.gainDb = v[11].getFloatValue();
        n.muted = v[12] == "1";
        ns.push_back (n);
    }
    return ns;
}

void MeloDigProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree st ("MeloDIG");
    st.setProperty ("version", 1, nullptr);
    st.setProperty ("scaleRoot", scale.root, nullptr);
    st.setProperty ("scaleType", scale.type, nullptr);
    st.setProperty ("snap", snapMode, nullptr);
    auto c = getClip();
    if (c != nullptr && c->file.existsAsFile())
    {
        st.setProperty ("file", c->file.getFullPathName(), nullptr);
        st.setProperty ("hostStart", (juce::int64) c->hostStart, nullptr);
        st.setProperty ("hostStartPpq", c->hostStartPpq, nullptr);
        st.setProperty ("bpm", c->bpm, nullptr);
        st.setProperty ("notes", notesToString (getNotes()), nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void MeloDigProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("MeloDIG")) return;
    auto st = juce::ValueTree::fromXml (*xml);
    scale.root = juce::jlimit (0, 11, (int) st.getProperty ("scaleRoot", 0));
    scale.type = juce::jlimit (0, 2, (int) st.getProperty ("scaleType", 0));
    snapMode = juce::jlimit (0, 2, (int) st.getProperty ("snap", 1));
    const juce::File f (st.getProperty ("file").toString());
    if (! juce::File::isAbsolutePath (f.getFullPathName()) || ! f.existsAsFile()) return;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) { lastError = "Prise introuvable : " + f.getFullPathName(); return; }
    auto c = std::make_shared<Clip>();
    c->file = f;
    c->sampleRate = reader->sampleRate;
    c->hostStart = (long long) (juce::int64) st.getProperty ("hostStart", 0);
    c->hostStartPpq = st.getProperty ("hostStartPpq", 0.0);
    c->bpm = st.getProperty ("bpm", 120.0);
    const int len = (int) reader->lengthInSamples, chans = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, chans > 1);
    for (int ch = 0; ch < chans; ++ch) c->data.emplace_back (buf.getReadPointer (ch), buf.getReadPointer (ch) + len);
    newClip (c, notesFromString (st.getProperty ("notes").toString()), true);
}

juce::AudioProcessorEditor* MeloDigProcessor::createEditor() { return new MeloDigEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MeloDigProcessor(); }
