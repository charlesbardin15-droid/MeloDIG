#include "PluginEditor.h"

using namespace melodig;

static juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

static juce::String noteName (int midi)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

static juce::String num (double v, int decimals)
{
    return juce::String (v, decimals).replaceCharacter ('.', ',');
}

// ===========================================================================
// Pills
mdui::PillGroup::PillGroup (std::vector<Item> i) : items (std::move (i)) {}

void mdui::PillGroup::setSelected (int index)
{
    index = juce::jlimit (0, (int) items.size() - 1, index);
    if (index != selected) { selected = index; repaint(); }
}

juce::Rectangle<float> mdui::PillGroup::cell (int i) const
{
    const float gap = 6.0f;
    const float w = ((float) getWidth() - gap * (float) (items.size() - 1)) / (float) items.size();
    return { (w + gap) * (float) i, 0.0f, w, (float) getHeight() };
}

void mdui::PillGroup::paint (juce::Graphics& g)
{
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const bool on = i == selected;
        auto r = cell (i).reduced (1.0f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
        ui::sticker (g, r, juce::jmin (12.0f, r.getHeight() / 2.0f), on ? ui::green : (i == hover ? ui::tint : ui::card),
                     ui::green, on ? ui::ink : ui::tint, on ? 3.0f : 2.0f);
        auto t = r.reduced (8.0f, 3.0f);
        const auto& it = items[(size_t) i];
        const bool two = it.subtitle.isNotEmpty() && r.getHeight() > 36.0f;
        g.setColour (on ? ui::card : ui::green);
        g.setFont (ui::caps (two ? 15.0f : 13.5f));
        g.drawFittedText (it.title, (two ? t.removeFromTop (t.getHeight() * 0.56f) : t).toNearestInt(),
                          two ? juce::Justification::centredBottom : juce::Justification::centred, 1, 0.75f);
        if (two)
        {
            g.setColour (on ? ui::card.withAlpha (0.85f) : ui::inkSoft);
            g.setFont (ui::font (10.5f, true));
            g.drawFittedText (it.subtitle, t.toNearestInt(), juce::Justification::centredTop, 1, 0.75f);
        }
    }
}

void mdui::PillGroup::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (cell (i).contains (e.position)) { setSelected (i); if (onChange) onChange (i); return; }
}

void mdui::PillGroup::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < (int) items.size(); ++i) if (cell (i).contains (e.position)) h = i;
    if (h != hover) { hover = h; repaint(); }
}

void mdui::PillGroup::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

// ===========================================================================
// NoteView
NoteView::NoteView (MeloDigEditor& e) : editor (e)
{
    setWantsKeyboardFocus (false);
}

double NoteView::sr() const { return clip != nullptr ? clip->sampleRate : 48000.0; }
juce::Rectangle<int> NoteView::rollArea() const { return getLocalBounds().withTrimmedLeft (keyW).withTrimmedTop (rulerH); }
float NoteView::xOf (double sec) const { return (float) (rollArea().getX() + (sec - viewStart) * pxPerSec); }
double NoteView::secAt (float x) const { return viewStart + (x - rollArea().getX()) / pxPerSec; }
float NoteView::yOf (double midi) const { return (float) (rollArea().getY() + (topMidi - midi) * pxPerSemi); }
double NoteView::midiAt (float y) const { return topMidi - (y - rollArea().getY()) / pxPerSemi; }

double NoteView::noteOutSec (const Note& n, double u) const
{
    const double span = juce::jmax (1.0, n.end - n.start);
    return (n.newStart + (u - n.start) / span * (n.newEnd - n.newStart)) / sr();
}

void NoteView::resized() { clampView(); }

void NoteView::clampView()
{
    const double len = clip != nullptr ? clip->lengthSeconds() : 10.0;
    const double visible = rollArea().getWidth() / pxPerSec;
    viewStart = juce::jlimit (-0.5, juce::jmax (-0.5, len - visible + 0.5), viewStart);
    const double rows = rollArea().getHeight() / pxPerSemi;
    topMidi = juce::jlimit (24.0 + rows, 108.0, topMidi);
}

bool NoteView::refresh()
{
    auto& p = editor.proc;
    const int v = p.getNotesVersion();
    auto c = p.getClip();
    if (v == version && c == clip) return false;
    const bool newClip = c != clip;
    clip = c;
    track = p.getTrack();
    notes = p.getNotes();
    version = v;
    for (auto it = selection.begin(); it != selection.end();)
        if (*it >= notes.size()) it = selection.erase (it); else ++it;
    rebuildCurves();
    if (newClip || (notes.size() > 0 && version >= 0 && dragBase.empty() && newClip)) zoomToFit();
    repaint();
    return true;
}

void NoteView::rebuildCurves()
{
    if (track != nullptr) curves = buildCurves (*track, notes);
    else curves = {};
}

void NoteView::zoomToFit()
{
    const auto r = rollArea();
    if (clip == nullptr || r.getWidth() < 10) return;
    pxPerSec = juce::jlimit (8.0, 2000.0, (r.getWidth() - 20) / juce::jmax (1.0, clip->lengthSeconds()));
    viewStart = -10.0 / pxPerSec;
    if (! notes.empty())
    {
        float lo = 200, hi = 0;
        for (auto& n : notes) { lo = juce::jmin (lo, n.targetCentre()); hi = juce::jmax (hi, n.targetCentre()); }
        const double rows = juce::jmax (12.0, (double) (hi - lo) + 8.0);
        pxPerSemi = (float) juce::jlimit (6.0, 30.0, r.getHeight() / rows);
        topMidi = (hi + lo) / 2.0 + r.getHeight() / pxPerSemi / 2.0;
    }
    clampView();
    repaint();
}

void NoteView::selectAll()
{
    selection.resize (notes.size());
    std::iota (selection.begin(), selection.end(), (size_t) 0);
    editor.selectionChanged();
    repaint();
}

int NoteView::hitNote (juce::Point<float> p, int& edge) const
{
    edge = 0;
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(size_t) i];
        const float x0 = xOf (n.newStart / sr()), x1 = xOf (n.newEnd / sr());
        const float yc = yOf (n.targetCentre());
        const float hh = juce::jmax (7.0f, pxPerSemi * 0.7f);
        if (p.y < yc - hh || p.y > yc + hh || p.x < x0 - 4 || p.x > x1 + 4) continue;
        if (x1 - x0 > 18)
        {
            if (std::abs (p.x - x0) < 6) edge = -1;
            else if (std::abs (p.x - x1) < 6) edge = 1;
        }
        return i;
    }
    return -1;
}

void NoteView::commit (std::vector<Note> ns, bool undoable)
{
    notes = ns;
    rebuildCurves();
    editor.proc.setNotes (std::move (ns), undoable);
    version = editor.proc.getNotesVersion();
    repaint();
}

void NoteView::applyToSelection (const std::function<void (Note&)>& fn, bool undoable)
{
    if (notes.empty()) return;
    auto ns = notes;
    if (selection.empty()) for (auto& n : ns) fn (n);
    else for (size_t i : selection) if (i < ns.size()) fn (ns[i]);
    commit (std::move (ns), undoable);
}

juce::String NoteView::describeSelection() const
{
    if (clip == nullptr) return fr ("Aucune prise");
    if (notes.empty()) return editor.proc.isAnalysing() ? fr ("Analyse des notes…") : fr ("Aucune note détectée");
    if (selection.empty()) return juce::String ((int) notes.size()) + fr (" notes · rien de sélectionné (les réglages s'appliquent à tout)");
    if (selection.size() == 1)
    {
        const auto& n = notes[selection[0]];
        const double t = n.targetCentre();
        const int k = (int) std::lround (t);
        const int ct = (int) std::lround ((t - k) * 100.0);
        return noteName (k) + "  " + (ct >= 0 ? "+" : "") + juce::String (ct) + " ct   ·   " + num ((n.newEnd - n.newStart) / sr(), 2) + " s"
             + fr ("   ·   chanté : ") + noteName ((int) std::lround (n.centre)) + " " + (n.centre - std::round (n.centre) >= 0 ? "+" : "")
             + juce::String ((int) std::lround ((n.centre - std::round (n.centre)) * 100)) + " ct";
    }
    return juce::String ((int) selection.size()) + fr (" notes sélectionnées");
}

// ---------------------------------------------------------------------------
void NoteView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds();
    const auto roll = rollArea();
    auto& p = editor.proc;
    const auto& sc = p.scale;

    // rows
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (roll);
        g.fillAll (ui::card);
        const int lo = (int) std::floor (midiAt ((float) roll.getBottom())), hi = (int) std::ceil (topMidi);
        for (int m = lo; m <= hi; ++m)
        {
            const float y0 = yOf (m + 0.5), y1 = yOf (m - 0.5);
            const bool black = (m % 12 == 1 || m % 12 == 3 || m % 12 == 6 || m % 12 == 8 || m % 12 == 10);
            const bool inScale = sc.type == 0 ? ! black : sc.contains (m);
            g.setColour (inScale ? ui::card : ui::cardDeep);
            g.fillRect (juce::Rectangle<float> ((float) roll.getX(), y0, (float) roll.getWidth(), y1 - y0));
            g.setColour (ui::tint.withAlpha (m % 12 == 0 ? 0.9f : 0.45f));
            g.fillRect ((float) roll.getX(), y1 - (m % 12 == 0 ? 1.5f : 0.75f), (float) roll.getWidth(), m % 12 == 0 ? 1.5f : 0.75f);
        }
    }

    // time grid + ruler
    {
        g.setColour (ui::cardDeep);
        g.fillRect (full.withHeight (rulerH));
        g.setColour (ui::green);
        g.fillRect (full.withHeight (rulerH).removeFromBottom (2));
        const double bpm = clip != nullptr ? clip->bpm : p.getHostBpm();
        const double ppq0 = clip != nullptr ? clip->hostStartPpq : 0.0;
        const double secPerBeat = 60.0 / juce::jmax (20.0, bpm);
        const double t0 = secAt ((float) roll.getX()), t1 = secAt ((float) roll.getRight());
        // choose beat subdivision from zoom
        double step = 1.0;   // beats
        for (double s : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }) if (s * secPerBeat * pxPerSec >= 18) { step = s; break; }
        const double firstBeat = std::ceil ((ppq0 + t0 / secPerBeat) / step) * step;
        g.setFont (ui::caps (11.5f));
        for (double b = firstBeat; ; b += step)
        {
            const double t = (b - ppq0) * secPerBeat;
            if (t > t1) break;
            const float x = xOf (t);
            const bool bar = std::abs (std::fmod (b, 4.0)) < 1e-6;
            g.setColour (ui::tint.withAlpha (bar ? 1.0f : 0.5f));
            g.fillRect (x, (float) roll.getY(), bar ? 1.5f : 0.75f, (float) roll.getHeight());
            if (bar || step >= 1.0)
            {
                g.setColour (bar ? ui::green : ui::inkSoft);
                const int barNum = (int) std::floor (b / 4.0) + 1;
                const int beatNum = (int) std::floor (std::fmod (b, 4.0)) + 1;
                g.drawText (bar ? juce::String (barNum) : juce::String (barNum) + "." + juce::String (beatNum),
                            juce::Rectangle<float> (x + 3, 2, 40, rulerH - 4.0f), juce::Justification::centredLeft);
            }
        }
    }

    // keyboard
    {
        auto kb = full.withWidth (keyW).withTrimmedTop (rulerH);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (kb);
        g.setColour (ui::card);
        g.fillRect (kb);
        const int lo = (int) std::floor (midiAt ((float) kb.getBottom())), hi = (int) std::ceil (topMidi);
        for (int m = lo; m <= hi; ++m)
        {
            const bool black = (m % 12 == 1 || m % 12 == 3 || m % 12 == 6 || m % 12 == 8 || m % 12 == 10);
            const float y0 = yOf (m + 0.5), y1 = yOf (m - 0.5);
            if (black)
            {
                g.setColour (ui::green);
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) kb.getX(), y0 + 1, keyW * 0.62f, y1 - y0 - 2), 2.0f);
            }
            g.setColour (ui::tint);
            if (m % 12 == 0 || m % 12 == 5) g.fillRect ((float) kb.getX(), y1 - 1, (float) keyW, 1.0f);
            if (m % 12 == 0 && pxPerSemi >= 7)
            {
                g.setColour (ui::green);
                g.setFont (ui::caps (11.0f));
                g.drawText (noteName (m), juce::Rectangle<float> ((float) kb.getX(), y0, keyW - 4.0f, y1 - y0), juce::Justification::centredRight);
            }
        }
        g.setColour (ui::green);
        g.fillRect (kb.getRight() - 2, kb.getY(), 2, kb.getHeight());
    }
    g.setColour (ui::cardDeep);
    g.fillRect (0, 0, keyW, rulerH);

    // empty / status states
    if (clip == nullptr || notes.empty())
    {
        auto box = roll.withSizeKeepingCentre (juce::jmin (620, roll.getWidth() - 40), 150).toFloat();
        ui::sticker (g, box, 16.0f, ui::cardDeep, ui::green, ui::green, 4.0f);
        auto t = box.reduced (24, 14);
        g.setColour (ui::green);
        g.setFont (ui::display (26.0f));
        juce::String title, body;
        const auto tr = p.getTransferState();
        if (tr == MeloDigProcessor::Transfer::armed) { title = fr ("prêt à enregistrer"); body = fr ("Lance la lecture dans Ableton : MeloDIG enregistre la piste. Arrête la lecture quand c'est fini."); }
        else if (tr == MeloDigProcessor::Transfer::recording) { title = fr ("enregistrement… ") + num (p.getTransferSeconds(), 1) + " s"; body = fr ("Laisse jouer jusqu'au bout de la partie, puis arrête la lecture."); }
        else if (clip != nullptr && p.isAnalysing()) { title = fr ("analyse des notes…"); body = fr ("MeloDIG cherche les notes et leur hauteur."); }
        else if (clip != nullptr) { title = fr ("pas de note trouvée"); body = fr ("La prise semble vide ou trop bruitée. Retransfère une voix seule (une note à la fois)."); }
        else { title = fr ("transfère ta voix"); body = fr ("1. Clique sur TRANSFÉRER (en haut).\n2. Lance la lecture dans Ableton : la piste est enregistrée.\n3. Arrête la lecture : les notes apparaissent, tu peux les éditer."); }
        g.drawText (title, t.removeFromTop (34), juce::Justification::centredLeft);
        g.setColour (ui::ink);
        g.setFont (ui::font (14.0f, true));
        g.drawFittedText (body, t.toNearestInt(), juce::Justification::topLeft, 4);
        doodle::note (g, { box.getRight() - 40, box.getY() + 32 }, 30.0f, ui::green, true);
        doodle::sparkle (g, { box.getRight() - 74, box.getY() + 22 }, 14.0f, ui::green);
        if (clip == nullptr) return;
    }

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (roll);

    // waveform hint behind the blobs (very light)
    if (clip != nullptr && ! clip->peak.empty())
    {
        const float mid = (float) roll.getBottom() - 30.0f;
        juce::Path w;
        for (int x = roll.getX(); x < roll.getRight(); x += 2)
        {
            const double t = secAt ((float) x);
            const long k = (long) (t * clip->sampleRate / 256.0);
            if (k < 0 || k >= (long) clip->peak.size()) continue;
            const float h = clip->peak[(size_t) k] * 26.0f;
            w.addRectangle ((float) x, mid - h, 1.5f, h * 2 + 0.5f);
        }
        g.setColour (ui::green.withAlpha (0.12f));
        g.fillPath (w);
    }

    // blobs
    if (track != nullptr)
    {
        float peakDb = -120;
        for (float l : track->levelDb) peakDb = juce::jmax (peakDb, l);
        const double hop = track->hop;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            const float x0 = xOf (n.newStart / sr()), x1 = xOf (n.newEnd / sr());
            if (x1 < roll.getX() || x0 > roll.getRight()) continue;
            const bool sel = std::find (selection.begin(), selection.end(), i) != selection.end();
            const float yc = yOf (n.targetCentre());
            const float maxH = juce::jmax (4.0f, pxPerSemi * 0.62f);
            juce::Path blob;
            std::vector<juce::Point<float>> top, bottom;
            const float stepPx = 2.0f;
            for (float x = x0; x <= x1 + 0.01f; x += stepPx)
            {
                const double outSec = secAt (juce::jmin (x, x1));
                const double frac = (outSec * sr() - n.newStart) / juce::jmax (1.0, n.newEnd - n.newStart);
                const double u = n.start + frac * (n.end - n.start);
                const int f = juce::jlimit (0, track->numFrames() - 1, (int) (u / hop));
                const float lvl = track->levelDb[(size_t) f];
                float h = maxH * juce::jlimit (0.12f, 1.0f, (lvl - (peakDb - 42.0f)) / 38.0f);
                const float edgeT = (float) juce::jmin (frac, 1.0 - frac);
                h *= juce::jlimit (0.35f, 1.0f, edgeT * 12.0f + 0.35f);
                top.push_back ({ juce::jmin (x, x1), yc - h });
                bottom.push_back ({ juce::jmin (x, x1), yc + h });
            }
            if (top.size() < 2) { top.push_back ({ x1, yc - 3 }); bottom.push_back ({ x1, yc + 3 }); }
            blob.startNewSubPath (top.front());
            for (auto& q : top) blob.lineTo (q);
            for (auto it = bottom.rbegin(); it != bottom.rend(); ++it) blob.lineTo (*it);
            blob.closeSubPath();
            const auto fill = n.muted ? ui::card : (sel ? ui::brown : ui::green);
            g.setColour (ui::ink.withAlpha (0.25f));
            g.fillPath (blob, juce::AffineTransform::translation (2.0f, 2.0f));
            g.setColour (fill);
            g.fillPath (blob);
            g.setColour (sel ? ui::ink : ui::greenDeep);
            if (n.muted)
            {
                juce::Path dashed;
                const float dash[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, blob, dash, 2);
                g.fillPath (dashed);
            }
            else g.strokePath (blob, juce::PathStrokeType (1.2f));

            // label
            if (x1 - x0 > 46 && pxPerSemi >= 10)
            {
                const int k = (int) std::lround (n.targetCentre());
                g.setColour (n.muted ? ui::inkSoft : ui::card);
                g.setFont (ui::caps (10.5f));
                g.drawText (noteName (k), juce::Rectangle<float> (x0 + 6, yc - 7, 40, 14), juce::Justification::centredLeft);
            }
            // edited marks: small dots for formant / volume
            if (std::abs (n.formant) > 0.01f || std::abs (n.gainDb) > 0.01f)
                doodle::sparkle (g, { x1 - 5, yc - maxH - 4 }, 8.0f, ui::brown);
        }

        // pitch curves (what you will hear), original curve faint where edited
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            const float x0 = xOf (n.newStart / sr()), x1 = xOf (n.newEnd / sr());
            if (x1 < roll.getX() || x0 > roll.getRight() || n.muted) continue;
            juce::Path target, orig;
            bool tOpen = false, oOpen = false;
            const bool edited = n.pitchEdited() || n.timeEdited();
            const int fa = (int) std::ceil (n.voicedStart / hop), fb = (int) std::floor (n.voicedEnd / hop);
            for (int f = fa; f < fb && f < track->numFrames(); ++f)
            {
                const float m = track->midi[(size_t) f];
                if (m <= 0) { tOpen = oOpen = false; continue; }
                const float x = xOf (noteOutSec (n, f * hop));
                const float d = f < (int) curves.delta.size() ? curves.delta[(size_t) f] : 0.0f;
                const juce::Point<float> pt (x, yOf (m + d));
                if (! tOpen) { target.startNewSubPath (pt); tOpen = true; } else target.lineTo (pt);
                if (edited)
                {
                    const juce::Point<float> po (xOf (f * hop / sr()), yOf (m));
                    if (! oOpen) { orig.startNewSubPath (po); oOpen = true; } else orig.lineTo (po);
                }
            }
            if (edited)
            {
                g.setColour (ui::inkSoft.withAlpha (0.35f));
                g.strokePath (orig, juce::PathStrokeType (1.0f));
            }
            g.setColour (ui::ink.withAlpha (0.85f));
            g.strokePath (target, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // selection band
    if (drag == Drag::select)
    {
        g.setColour (ui::green.withAlpha (0.12f));
        g.fillRect (band);
        g.setColour (ui::green);
        g.drawRect (band, 1.5f);
    }

    // play line
    const double ps = p.getPlaySecondsInClip();
    if ((p.isHostPlaying() || p.isPreviewing()) && ps >= -0.5)
    {
        const float x = xOf (ps);
        g.setColour (ui::brown);
        g.fillRect (x - 1.0f, (float) roll.getY(), 2.0f, (float) roll.getHeight());
    }
}

// ---------------------------------------------------------------------------
void NoteView::mouseMove (const juce::MouseEvent& e)
{
    int edge = 0;
    const int i = hitNote (e.position, edge);
    auto cur = juce::MouseCursor::NormalCursor;
    if (i >= 0)
    {
        if (tool == Tool::main) cur = edge != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
        else if (tool == Tool::split) cur = juce::MouseCursor::IBeamCursor;
        else cur = juce::MouseCursor::UpDownResizeCursor;
    }
    setMouseCursor (cur);
    juce::String h;
    if (i >= 0)
    {
        const auto& n = notes[(size_t) i];
        const double t = n.targetCentre();
        const int k = (int) std::lround (t);
        h = noteName (k) + " " + ((t - k) >= 0 ? "+" : "") + juce::String ((int) std::lround ((t - k) * 100)) + " ct";
        if (std::abs (n.modulation - 1) > 0.01f) h << "  ·  modulation " << juce::String ((int) std::lround (n.modulation * 100)) << " %";
        if (std::abs (n.drift - 1) > 0.01f) h << fr ("  ·  dérive ") << juce::String ((int) std::lround (n.drift * 100)) << " %";
        if (std::abs (n.formant) > 0.01f) h << "  ·  formant " << num (n.formant, 1) << " st";
        if (std::abs (n.gainDb) > 0.01f) h << "  ·  " << num (n.gainDb, 1) << " dB";
    }
    if (h != hoverText) { hoverText = h; }
}

void NoteView::mouseDown (const juce::MouseEvent& e)
{
    editor.grabKeyboardFocus();
    drag = Drag::none;
    gestureOpen = false;
    downPos = e.position;
    if (clip == nullptr) return;

    // ruler: preview from here (host stopped)
    if (e.position.y < rulerH && e.position.x > keyW)
    {
        if (! editor.proc.isHostPlaying()) editor.proc.setPreviewing (true, juce::jmax (0.0, secAt (e.position.x)));
        return;
    }
    if (! rollArea().contains (e.getPosition())) return;

    int edge = 0;
    const int i = hitNote (e.position, edge);
    dragNote = i;
    if (i < 0)
    {
        if (! e.mods.isShiftDown()) selection.clear();
        drag = Drag::select;
        band = { e.position, e.position };
        editor.selectionChanged();
        repaint();
        return;
    }
    const bool isSel = std::find (selection.begin(), selection.end(), (size_t) i) != selection.end();
    if (e.mods.isShiftDown())
    {
        if (isSel) selection.erase (std::find (selection.begin(), selection.end(), (size_t) i));
        else selection.push_back ((size_t) i);
    }
    else if (! isSel) selection = { (size_t) i };
    editor.selectionChanged();

    if (tool == Tool::split)
    {
        const auto& n = notes[(size_t) i];
        const double outS = secAt (e.position.x) * sr();
        const double u = n.start + (outS - n.newStart) / juce::jmax (1.0, n.newEnd - n.newStart) * (n.end - n.start);
        auto ns = notes;
        if (track != nullptr && splitNote (ns, (size_t) i, u, *track))
        {
            selection = { (size_t) i + 1 };
            commit (std::move (ns), true);
            editor.selectionChanged();
        }
        return;
    }
    dragBase = notes;
    if (tool == Tool::main) drag = edge < 0 ? Drag::startEdge : edge > 0 ? Drag::endEdge : Drag::undecided;
    else drag = Drag::value;
    repaint();
}

void NoteView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || clip == nullptr) return;
    const auto d = e.position - downPos;
    if (drag == Drag::select)
    {
        band = juce::Rectangle<float> (downPos, e.position);
        std::vector<size_t> sel;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            const juce::Rectangle<float> r (xOf (n.newStart / sr()), yOf (n.targetCentre()) - 4, xOf (n.newEnd / sr()) - xOf (n.newStart / sr()), 8);
            if (r.intersects (band)) sel.push_back (i);
        }
        if (e.mods.isShiftDown()) for (size_t s : selection) if (std::find (sel.begin(), sel.end(), s) == sel.end()) sel.push_back (s);
        selection = sel;
        editor.selectionChanged();
        repaint();
        return;
    }
    if (drag == Drag::undecided)
    {
        if (d.getDistanceFromOrigin() < 5) return;
        drag = std::abs (d.y) >= std::abs (d.x) ? Drag::pitch : Drag::time;
    }
    if (! gestureOpen) { editor.proc.beginGesture(); gestureOpen = true; }

    auto ns = dragBase;
    const double len = (double) clip->numSamples();
    auto& p = editor.proc;
    switch (drag)
    {
        case Drag::pitch:
        {
            const double dy = -d.y / pxPerSemi;
            const bool free = e.mods.isAltDown() || p.snapMode == 0;
            for (size_t i : selection)
            {
                auto& n = ns[i];
                double target = dragBase[i].targetCentre() + dy;
                if (! free) target = p.snapMode == 2 ? (double) p.scale.nearest (target) : std::round (target);
                n.pitchOffset = (float) (target - n.centre);
            }
            break;
        }
        case Drag::time:
        {
            double dt = d.x / pxPerSec * sr();
            for (size_t i : selection)
            {
                const auto& b = dragBase[i];
                const bool prevSel = i > 0 && std::find (selection.begin(), selection.end(), i - 1) != selection.end();
                const bool nextSel = i + 1 < ns.size() && std::find (selection.begin(), selection.end(), i + 1) != selection.end();
                const double lo = i == 0 ? 0.0 : (prevSel ? -1e18 : dragBase[i - 1].newEnd);
                const double hi = i + 1 >= ns.size() ? len : (nextSel ? 1e18 : dragBase[i + 1].newStart);
                dt = juce::jlimit (lo - b.newStart, hi - b.newEnd, dt);
            }
            for (size_t i : selection) { ns[i].newStart = dragBase[i].newStart + dt; ns[i].newEnd = dragBase[i].newEnd + dt; }
            break;
        }
        case Drag::startEdge:
        case Drag::endEdge:
        {
            if (dragNote < 0) break;
            const size_t i = (size_t) dragNote;
            const double dt = d.x / pxPerSec * sr();
            const double minLen = 0.04 * sr();
            auto& n = ns[i];
            if (drag == Drag::startEdge)
            {
                const double lo = i == 0 ? 0.0 : ns[i - 1].newEnd;
                n.newStart = juce::jlimit (lo, n.newEnd - minLen, dragBase[i].newStart + dt);
            }
            else
            {
                const double hi = i + 1 >= ns.size() ? len : ns[i + 1].newStart;
                n.newEnd = juce::jlimit (n.newStart + minLen, hi, dragBase[i].newEnd + dt);
            }
            break;
        }
        case Drag::value:
        {
            const double v = -d.y;
            for (size_t i : selection)
            {
                auto& n = ns[i];
                const auto& b = dragBase[i];
                switch (tool)
                {
                    case Tool::modulation: n.modulation = (float) juce::jlimit (0.0, 2.0, b.modulation + v / 120.0); break;
                    case Tool::drift:      n.drift = (float) juce::jlimit (0.0, 2.0, b.drift + v / 120.0); break;
                    case Tool::formant:    n.formant = (float) juce::jlimit (-12.0, 12.0, std::round ((b.formant + v / 15.0) * 10.0) / 10.0); break;
                    case Tool::volume:     n.gainDb = (float) juce::jlimit (-36.0, 12.0, std::round ((b.gainDb + v / 6.0) * 10.0) / 10.0); break;
                    default: break;
                }
            }
            break;
        }
        default: break;
    }
    commit (std::move (ns), false);
    editor.selectionChanged();
}

void NoteView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::select) repaint();
    drag = Drag::none;
    dragBase.clear();
    gestureOpen = false;
}

void NoteView::mouseDoubleClick (const juce::MouseEvent& e)
{
    int edge = 0;
    const int i = hitNote (e.position, edge);
    if (i < 0 || track == nullptr) return;
    auto ns = notes;
    auto& p = editor.proc;
    auto& n = ns[(size_t) i];
    switch (tool)
    {
        case Tool::main:
        {
            // snap the pitch centre onto the nearest note (of the scale in "scale" mode)
            const double t = n.targetCentre();
            const int target = p.snapMode == 2 ? p.scale.nearest (t) : (int) std::lround (t);
            n.pitchOffset = (float) (target - n.centre);
            break;
        }
        case Tool::modulation: n.modulation = n.modulation < 0.5f ? 1.0f : 0.0f; break;
        case Tool::drift:      n.drift = n.drift < 0.5f ? 1.0f : 0.0f; break;
        case Tool::formant:    n.formant = 0.0f; break;
        case Tool::volume:     n.muted = ! n.muted; break;
        case Tool::split:
        {
            // double-click near a cut between two touching notes joins them
            const double s = secAt (e.position.x) * sr();
            size_t a = (size_t) i;
            if (std::abs (s - n.newStart) < std::abs (s - n.newEnd) && i > 0) a = (size_t) i - 1;
            if (! mergeNotes (ns, a, *track)) return;
            selection = { a };
            break;
        }
    }
    commit (std::move (ns), true);
    editor.selectionChanged();
}

void NoteView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double at = secAt (e.position.x);
        pxPerSec = juce::jlimit (8.0, 3000.0, pxPerSec * std::exp (w.deltaY * 1.5));
        viewStart = at - (e.position.x - rollArea().getX()) / pxPerSec;
    }
    else if (e.mods.isAltDown())
    {
        const double at = midiAt (e.position.y);
        pxPerSemi = (float) juce::jlimit (5.0, 40.0, pxPerSemi * std::exp (w.deltaY * 1.5));
        topMidi = at + (e.position.y - rollArea().getY()) / pxPerSemi;
    }
    else
    {
        const float dx = w.deltaX != 0.0f ? w.deltaX : (e.mods.isShiftDown() ? w.deltaY : 0.0f);
        const float dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
        viewStart -= dx * 400.0 / pxPerSec;
        topMidi += dy * 30.0;
    }
    clampView();
    repaint();
}

// ===========================================================================
// Editor
MeloDigEditor::MeloDigEditor (MeloDigProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      tools ({ { "PRINCIPAL", fr ("hauteur · temps") }, { "MODULATION", "vibrato" }, { fr ("DÉRIVE"), "lente" },
               { "FORMANT", "timbre" }, { "VOLUME", "dB" }, { fr ("SÉPARER"), "couper" } })
{
    setLookAndFeel (&lnf);
    pagePattern = doodle::pattern (240, ui::green.withAlpha (0.07f));
    bandPattern = doodle::pattern (240, ui::card.withAlpha (0.10f));
    grainImage = doodle::grain (160);

    auto band = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip)
    {
        b.setButtonText (text);
        b.getProperties().set ("style", "band");
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    band (transferButton, fr ("TRANSFÉRER"), fr ("Enregistre la piste pendant qu'Ableton joue (arrête la lecture pour finir)"));
    band (playButton, fr ("PRÉ-ÉCOUTE"), fr ("Écoute la prise corrigée sans lancer Ableton (clic sur la règle = écouter à partir de là)"));
    band (undoButton, "ANNULER", fr ("Annuler (Cmd+Z)"));
    band (redoButton, fr ("RÉTABLIR"), fr ("Rétablir (Maj+Cmd+Z)"));
    band (fitButton, "TOUT VOIR", fr ("Zoom pour voir toute la prise (Cmd+molette = zoom, Alt+molette = zoom vertical)"));
    transferButton.onClick = [this]
    {
        if (proc.getTransferState() == MeloDigProcessor::Transfer::idle) proc.startTransfer();
        else proc.stopTransfer();
    };
    playButton.onClick = [this]
    {
        proc.setPreviewing (! proc.isPreviewing(), juce::jmax (0.0, proc.isPreviewing() ? 0.0 : 0.0));
    };
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    fitButton.onClick = [this] { view.zoomToFit(); };

    tools.onChange = [this] (int i) { view.tool = (Tool) i; };
    tools.setTooltip (fr ("PRINCIPAL : glisser = changer la note ou la déplacer, bords = étirer, double-clic = recaler sur la note juste.\n"
                          "MODULATION / DÉRIVE : glisser vers le bas = réduire le vibrato / les variations lentes.\n"
                          "FORMANT / VOLUME : glisser vers le haut ou le bas. SÉPARER : clic = couper, double-clic sur une coupe = recoller."));
    addAndMakeVisible (tools);

    snapBox.addItemList ({ "Aimant : libre", "Aimant : demi-tons", "Aimant : gamme" }, 1);
    snapBox.setSelectedItemIndex (proc.snapMode, juce::dontSendNotification);
    snapBox.onChange = [this] { proc.snapMode = snapBox.getSelectedItemIndex(); };
    snapBox.setTooltip (fr ("Quand tu montes ou descends une note : libre, au demi-ton, ou seulement sur les notes de la gamme (Alt = libre)"));
    for (int k = 0; k < 12; ++k) rootBox.addItem (juce::String (std::array<const char*, 12> { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }[(size_t) k]), k + 1);
    rootBox.setSelectedItemIndex (proc.scale.root, juce::dontSendNotification);
    rootBox.onChange = [this] { proc.scale.root = rootBox.getSelectedItemIndex(); view.repaint(); };
    scaleBox.addItemList ({ "Chromatique", "Majeur", "Mineur" }, 1);
    scaleBox.setSelectedItemIndex (proc.scale.type, juce::dontSendNotification);
    scaleBox.onChange = [this] { proc.scale.type = scaleBox.getSelectedItemIndex(); view.repaint(); };
    detectButton.setButtonText (fr ("DÉTECTER"));
    detectButton.setTooltip (fr ("Trouve la tonalité de la prise"));
    detectButton.onClick = [this]
    {
        const auto& ns = view.getNotes();
        if (ns.empty()) return;
        const auto k = estimateKey (ns);
        proc.scale = k;
        rootBox.setSelectedItemIndex (k.root, juce::dontSendNotification);
        scaleBox.setSelectedItemIndex (k.type, juce::dontSendNotification);
        view.repaint();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &snapBox, &rootBox, &scaleBox, &detectButton }) addAndMakeVisible (c);

    addAndMakeVisible (view);

    // inspector
    selectionLabel.setFont (ui::font (13.0f, true));
    selectionLabel.setColour (juce::Label::textColourId, ui::ink);
    selectionLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (selectionLabel);
    setupRow (pitchRow,   "HAUTEUR",    -24.0, 24.0, 0.01, " st", 0.0);
    setupRow (modRow,     "MODULATION", 0.0, 200.0, 1.0, " %", 100.0);
    setupRow (driftRow,   fr ("DÉRIVE"), 0.0, 200.0, 1.0, " %", 100.0);
    setupRow (formantRow, "FORMANT",    -12.0, 12.0, 0.1, " st", 0.0);
    setupRow (gainRow,    "VOLUME",     -36.0, 12.0, 0.1, " dB", 0.0);
    pitchRow.slider.setTooltip (fr ("Décalage de la note par rapport à ce qui a été chanté"));
    modRow.slider.setTooltip (fr ("Vibrato et petites variations rapides : 0 % = note droite, 100 % = comme chanté"));
    driftRow.slider.setTooltip (fr ("Variations lentes autour de la note : 0 % = parfaitement stable, 100 % = comme chanté"));
    auto bindRow = [this] (ValueRow& row, std::function<void (Note&, double)> set)
    {
        row.slider.onDragStart = [this] { proc.beginGesture(); };
        row.slider.onValueChange = [this, &row, set]
        {
            if (updatingInspector) return;
            const double v = row.slider.getValue();
            view.applyToSelection ([&] (Note& n) { set (n, v); }, ! row.slider.isMouseButtonDown());
            updateInspector();
        };
    };
    bindRow (pitchRow,   [] (Note& n, double v) { n.pitchOffset = (float) v; });
    bindRow (modRow,     [] (Note& n, double v) { n.modulation = (float) (v / 100.0); });
    bindRow (driftRow,   [] (Note& n, double v) { n.drift = (float) (v / 100.0); });
    bindRow (formantRow, [] (Note& n, double v) { n.formant = (float) v; });
    bindRow (gainRow,    [] (Note& n, double v) { n.gainDb = (float) v; });

    // macros
    auto macroSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double def)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20);
        s.setRange (0.0, 100.0, 1.0);
        s.setValue (def, juce::dontSendNotification);
        s.setTextValueSuffix (" %");
        s.setColour (juce::Slider::thumbColourId, ui::green);
        l.setText (name, juce::dontSendNotification);
        l.setFont (ui::caps (12.5f));
        l.setColour (juce::Label::textColourId, ui::inkSoft);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    macroSlider (corrCentre, corrCentreLabel, "CENTRE", 100.0);
    macroSlider (corrDrift, corrDriftLabel, fr ("DÉRIVE"), 50.0);
    corrHelp.setText (fr ("Recale le centre des notes sur la gamme et lisse leurs variations. Sélection, ou toutes les notes."), juce::dontSendNotification);
    corrHelp.setFont (ui::font (11.5f, true));
    corrHelp.setColour (juce::Label::textColourId, ui::inkSoft);
    corrHelp.setJustificationType (juce::Justification::topLeft);
    corrHelp.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (corrHelp);
    corrApply.setButtonText ("CORRIGER");
    corrApply.onClick = [this]
    {
        auto ns = view.getNotes();
        if (ns.empty()) return;
        std::vector<size_t> sel = view.getSelection();
        if (sel.empty()) { sel.resize (ns.size()); std::iota (sel.begin(), sel.end(), (size_t) 0); }
        Scale sc = proc.scale;
        if (proc.snapMode != 2) sc.type = 0;   // "gamme" only when the scale snap is chosen
        correctPitch (ns, sel, sc, (float) corrCentre.getValue() / 100.0f, (float) corrDrift.getValue() / 100.0f);
        proc.setNotes (std::move (ns), true);
        view.refresh();
        updateInspector();
    };
    addAndMakeVisible (corrApply);

    gridBox.addItemList ({ "Grille : 1/4", "Grille : 1/8", "Grille : 1/16", "Grille : 1/32" }, 1);
    gridBox.setSelectedItemIndex (2, juce::dontSendNotification);
    addAndMakeVisible (gridBox);
    macroSlider (quantAmount, quantLabel, "FORCE", 100.0);
    quantHelp.setText (fr ("Cale le début des notes sur la grille du projet (tempo d'Ableton)."), juce::dontSendNotification);
    quantHelp.setFont (ui::font (11.5f, true));
    quantHelp.setColour (juce::Label::textColourId, ui::inkSoft);
    quantHelp.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (quantHelp);
    quantApply.setButtonText ("QUANTIFIER");
    quantApply.onClick = [this]
    {
        auto c = proc.getClip();
        auto ns = view.getNotes();
        if (c == nullptr || ns.empty()) return;
        std::vector<size_t> sel = view.getSelection();
        if (sel.empty()) { sel.resize (ns.size()); std::iota (sel.begin(), sel.end(), (size_t) 0); }
        const double beats[4] = { 1.0, 0.5, 0.25, 0.125 };
        const double secPerBeat = 60.0 / juce::jmax (20.0, c->bpm);
        const double grid = beats[juce::jlimit (0, 3, gridBox.getSelectedItemIndex())] * secPerBeat * c->sampleRate;
        // grid lines in clip samples: beat b is at (b - hostStartPpq) * secPerBeat
        const double offset = std::fmod (-c->hostStartPpq * secPerBeat * c->sampleRate, grid);
        quantizeTime (ns, sel, grid, offset, (float) quantAmount.getValue() / 100.0f, (double) c->numSamples());
        proc.setNotes (std::move (ns), true);
        view.refresh();
    };
    addAndMakeVisible (quantApply);

    setLookAndFeel (nullptr);   // re-apply so every child rebuilds its text boxes with our look
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (960, 640, 2400, 1500);

    licenceTheme.productName = "MeloDIG";
    licenceTheme.accent = ui::green; licenceTheme.onAccent = ui::card;
    licenceTheme.barFill = ui::card; licenceTheme.barInk = ui::green;
    licenceTheme.ink = ui::ink; licenceTheme.inkSoft = ui::inkSoft;
    licenceTheme.panel = ui::card; licenceTheme.outline = ui::green; licenceTheme.field = juce::Colours::white;
    licenceTheme.backdrop = ui::greenDeep.withAlpha (0.82f); licenceTheme.alert = juce::Colour (0xffc0392b);
    licenceTheme.title = ui::display (30.0f); licenceTheme.caps = ui::caps (14.0f);
    licenceTheme.body = ui::font (13.0f); licenceTheme.bodyBold = ui::font (13.0f, true);
    licenceTheme.radius = 14.0f;
    addChildComponent (bar);
    addChildComponent (activation);
    bar.onActivate = [this] { activation.show(); };
    activation.onClose = [this] { activation.setVisible (false); grabKeyboardFocus(); };
    activation.onActivated = [this] { juce::Timer::callAfterDelay (1400, [sp = juce::Component::SafePointer<MeloDigEditor> (this)] { if (sp) { sp->activation.setVisible (false); sp->updateLicenceUi(); } }); };
    bar.setVisible (proc.licence.getState() != carlito::Licence::State::licensed);
    setSize (1180, 760 + barHeight());
    if (proc.licence.getState() == carlito::Licence::State::expired) activation.show();
    timerCallback();
    startTimerHz (30);
}

MeloDigEditor::~MeloDigEditor() { setLookAndFeel (nullptr); }

void MeloDigEditor::setupRow (ValueRow& row, const juce::String& name, double lo, double hi, double step, const juce::String& suffix, double def)
{
    row.label.setText (name, juce::dontSendNotification);
    row.label.setFont (ui::caps (12.5f));
    row.label.setColour (juce::Label::textColourId, ui::inkSoft);
    row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 20);
    row.slider.setRange (lo, hi, step);
    row.slider.setTextValueSuffix (suffix);
    row.slider.setDoubleClickReturnValue (true, def);
    row.slider.setValue (def, juce::dontSendNotification);
    row.slider.setColour (juce::Slider::thumbColourId, ui::green);
    addAndMakeVisible (row.label);
    addAndMakeVisible (row.slider);
}

void MeloDigEditor::selectionChanged() { updateInspector(); }

void MeloDigEditor::updateInspector()
{
    const juce::ScopedValueSetter<bool> svs (updatingInspector, true);
    selectionLabel.setText (view.describeSelection(), juce::dontSendNotification);
    const auto& ns = view.getNotes();
    const auto& sel = view.getSelection();
    const Note* n = nullptr;
    if (! sel.empty() && sel[0] < ns.size()) n = &ns[sel[0]];
    else if (! ns.empty()) n = &ns[0];
    const bool enabled = ! ns.empty();
    for (auto* r : { &pitchRow, &modRow, &driftRow, &formantRow, &gainRow }) r->slider.setEnabled (enabled);
    if (n == nullptr) return;
    if (! pitchRow.slider.isMouseButtonDown()) pitchRow.slider.setValue (n->pitchOffset, juce::dontSendNotification);
    if (! modRow.slider.isMouseButtonDown()) modRow.slider.setValue (n->modulation * 100.0, juce::dontSendNotification);
    if (! driftRow.slider.isMouseButtonDown()) driftRow.slider.setValue (n->drift * 100.0, juce::dontSendNotification);
    if (! formantRow.slider.isMouseButtonDown()) formantRow.slider.setValue (n->formant, juce::dontSendNotification);
    if (! gainRow.slider.isMouseButtonDown()) gainRow.slider.setValue (n->gainDb, juce::dontSendNotification);
}

bool MeloDigEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k.getKeyCode() == 'Z' && mods.isCommandDown()) { if (mods.isShiftDown()) proc.redo(); else proc.undo(); return true; }
    if (k.getKeyCode() == 'A' && mods.isCommandDown()) { view.selectAll(); return true; }
    if (k == juce::KeyPress::escapeKey) { view.clearSelection(); updateInspector(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        if (view.getSelection().empty()) return true;
        view.applyToSelection ([] (Note& n) { n.muted = ! n.muted; }, true);
        return true;
    }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        if (view.getSelection().empty()) return true;
        const float d = (k == juce::KeyPress::upKey ? 1.0f : -1.0f) * (mods.isShiftDown() ? 12.0f : 1.0f);
        view.applyToSelection ([d] (Note& n) { n.pitchOffset += d; }, true);
        updateInspector();
        return true;
    }
    return false;
}

void MeloDigEditor::updateLicenceUi()
{
    const bool show = proc.licence.getState() != carlito::Licence::State::licensed;
    if (show != bar.isVisible()) { bar.setVisible (show); resized(); repaint(); }
    bar.update();
}

void MeloDigEditor::timerCallback()
{
    if (++licenceTicks >= 30 * 60) { licenceTicks = 0; proc.licence.refresh(); updateLicenceUi(); }   // chaque minute
    const bool changed = view.refresh();
    if (changed || proc.getNotesVersion() != lastVersion) { lastVersion = proc.getNotesVersion(); updateInspector(); }

    const auto tr = proc.getTransferState();
    transferButton.setToggleState (tr != MeloDigProcessor::Transfer::idle, juce::dontSendNotification);
    transferButton.setButtonText (tr == MeloDigProcessor::Transfer::idle ? fr ("TRANSFÉRER")
                                  : tr == MeloDigProcessor::Transfer::armed ? fr ("EN ATTENTE…") : fr ("STOP ") + num (proc.getTransferSeconds(), 1) + " s");
    playButton.setToggleState (proc.isPreviewing(), juce::dontSendNotification);
    playButton.setButtonText (proc.isPreviewing() ? "STOP" : fr ("PRÉ-ÉCOUTE"));
    playButton.setEnabled (proc.getClip() != nullptr && tr == MeloDigProcessor::Transfer::idle);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());

    juce::String s;
    if (tr == MeloDigProcessor::Transfer::armed) s = fr ("lance la lecture dans Ableton");
    else if (tr == MeloDigProcessor::Transfer::recording) s = fr ("enregistrement en cours");
    else if (proc.isAnalysing()) s = fr ("analyse des notes…");
    else if (proc.isRendering()) s = fr ("calcul du son…");
    else if (view.hoverText.isNotEmpty()) s = view.hoverText;
    else if (auto c = proc.getClip()) s = fr ("prise : ") + num (c->lengthSeconds(), 1) + " s  ·  " + juce::String ((int) view.getNotes().size()) + " notes";
    else s = fr ("aucune prise");
    if (proc.getLastError().isNotEmpty() && tr == MeloDigProcessor::Transfer::idle && proc.getClip() == nullptr) s = proc.getLastError();
    if (s != statusText) { statusText = s; repaint (bandArea); }

    pulse += 0.15f;
    if (tr != MeloDigProcessor::Transfer::idle || proc.isHostPlaying() || proc.isPreviewing() || proc.isAnalysing()) view.repaint();
}

void MeloDigEditor::drawCard (juce::Graphics& g, const Card& c, int number) const
{
    const auto r = c.r.toFloat();
    ui::sticker (g, r, 14.0f, ui::card, ui::green, ui::green, 4.0f);
    if (c.title.isEmpty()) return;
    auto t = c.r.reduced (14, 8).removeFromTop (20).toFloat();
    const juce::String title = "[ " + c.title.toUpperCase() + " ]";
    const auto f = ui::caps (16.0f);
    g.setColour (c.colour);
    g.setFont (f);
    g.drawText (title, t, juce::Justification::centredLeft);
    g.setColour (ui::inkSoft);
    g.setFont (ui::caps (12.5f));
    g.drawText ("/0" + juce::String (number), t, juce::Justification::centredRight);
    const float x = t.getX() + ui::textWidth (f, title) + 14.0f, y = t.getCentreY();
    if (x + 22.0f > t.getRight() - 28.0f) return;
    switch (number % 4)
    {
        case 1:  doodle::sparkle (g, { x, y }, 13.0f, c.colour); break;
        case 2:  doodle::note (g, { x, y }, 16.0f, c.colour); break;
        case 3:  doodle::squiggle (g, { x - 4.0f, y - 3.0f, 26.0f, 6.0f }, c.colour, 2.0f); break;
        default: doodle::arrows (g, { x + 2.0f, y }, 14.0f, c.colour); break;
    }
}

void MeloDigEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.fillAll (ui::bg);
    g.setTiledImageFill (pagePattern, 0, 0, 1.0f);
    g.fillRect (b);

    juce::Path bandPath;
    {
        const float base = (float) bandArea.getBottom() - 4.0f;
        bandPath.startNewSubPath (0, 0);
        bandPath.lineTo (b.getRight(), 0);
        for (float x = b.getRight(); x >= 0.0f; x -= 4.0f) bandPath.lineTo (x, base + 4.0f * std::sin (x * 0.045f));
        bandPath.closeSubPath();
    }
    g.setColour (ui::green);
    g.fillPath (bandPath);
    g.setTiledImageFill (bandPattern, 0, 0, 1.0f);
    g.fillPath (bandPath);
    g.setTiledImageFill (grainImage, 0, 0, 1.0f);
    g.fillRect (b);

    doodle::mascot (g, logoArea.withWidth (logoArea.getHeight()).toFloat());
    auto textArea = juce::Rectangle<int> (logoArea.getX() + logoArea.getHeight() + 12, logoArea.getY() - 4, 280, logoArea.getHeight() + 8);
    const auto logoFont = ui::display (40.0f);
    g.setColour (ui::card);
    g.setFont (logoFont);
    g.drawText ("melodig", textArea.removeFromTop (44), juce::Justification::bottomLeft);
    doodle::sparkle (g, { (float) textArea.getX() + ui::textWidth (logoFont, "melodig") + 10.0f, (float) textArea.getY() - 34.0f }, 15.0f, ui::card);
    g.setColour (ui::card.withAlpha (0.75f));
    g.setFont (ui::caps (12.0f));
    g.drawText (fr ("[ ÉDITION DE LA VOIX NOTE PAR NOTE ]"), textArea, juce::Justification::topLeft);

    // status under the header buttons
    g.setColour (ui::card.withAlpha (0.85f));
    g.setFont (ui::font (12.0f, true));
    g.drawText (statusText, headerButtons.withY (headerButtons.getBottom() + 3).withHeight (16), juce::Justification::centredLeft);
    if (proc.getTransferState() == MeloDigProcessor::Transfer::recording)
    {
        g.setColour (ui::brown.brighter (0.4f).withAlpha (0.5f + 0.5f * std::sin (pulse)));
        g.fillEllipse ((float) transferButton.getX() - 16.0f, (float) transferButton.getBounds().getCentreY() - 5.0f, 10.0f, 10.0f);
    }

    int n = 0;
    for (auto& c : cards) drawCard (g, c, c.title.isEmpty() ? 0 : ++n);
}

void MeloDigEditor::resized()
{
    cards.clear();
    const int top = barHeight();
    bar.setBounds (0, 0, getWidth(), top);
    activation.setBounds (getLocalBounds());
    auto full = getLocalBounds().withTrimmedTop (top);
    bandArea = full.withHeight (92);
    auto area = full.reduced (18, 0);
    auto header = area.removeFromTop (92).reduced (0, 13);
    logoArea = header.removeFromLeft (330).withHeight (58).withY (header.getY() + 1);
    headerButtons = header.removeFromRight (juce::jmin (header.getWidth(), 640)).withHeight (38).withY (header.getY() + 3);
    {
        auto hb = headerButtons;
        transferButton.setBounds (hb.removeFromLeft (150).reduced (2, 0));
        playButton.setBounds (hb.removeFromLeft (130).reduced (2, 0));
        hb.removeFromLeft (12);
        undoButton.setBounds (hb.removeFromLeft (110).reduced (2, 0));
        redoButton.setBounds (hb.removeFromLeft (110).reduced (2, 0));
        fitButton.setBounds (hb.reduced (2, 0));
    }

    area.removeFromTop (10);
    area.removeFromBottom (16);

    // tool row
    auto toolRow = area.removeFromTop (46);
    {
        auto r = toolRow;
        auto right = r.removeFromRight (juce::jmin (470, r.getWidth() / 2));
        tools.setBounds (r.withTrimmedRight (14));
        detectButton.setBounds (right.removeFromRight (100).reduced (0, 5));
        right.removeFromRight (6);
        scaleBox.setBounds (right.removeFromRight (118).reduced (0, 6));
        right.removeFromRight (6);
        rootBox.setBounds (right.removeFromRight (70).reduced (0, 6));
        right.removeFromRight (6);
        snapBox.setBounds (right.reduced (0, 6));
    }
    area.removeFromTop (12);

    // bottom cards
    auto bottom = area.removeFromBottom (176);
    area.removeFromBottom (14);
    cards.push_back ({ area, {}, ui::green });
    view.setBounds (area.reduced (6).withTrimmedRight (3).withTrimmedBottom (3));

    auto noteCard = bottom.removeFromLeft (bottom.getWidth() * 44 / 100);
    bottom.removeFromLeft (14);
    auto corrCard = bottom.removeFromLeft ((bottom.getWidth() - 14) / 2);
    bottom.removeFromLeft (14);
    auto quantCard = bottom;
    cards.push_back ({ noteCard, "note", ui::green });
    cards.push_back ({ corrCard, "corriger la hauteur", ui::brown });
    cards.push_back ({ quantCard, "quantifier le temps", ui::green });

    {
        auto r = noteCard.reduced (14, 0).withTrimmedTop (32).withTrimmedBottom (10);
        selectionLabel.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        const int rowH = r.getHeight() / 5;
        for (auto* row : { &pitchRow, &modRow, &driftRow, &formantRow, &gainRow })
        {
            auto rr = r.removeFromTop (rowH);
            row->label.setBounds (rr.removeFromLeft (92));
            row->slider.setBounds (rr);
        }
    }
    {
        auto r = corrCard.reduced (14, 0).withTrimmedTop (32).withTrimmedBottom (12);
        corrApply.setBounds (r.removeFromBottom (34).removeFromLeft (juce::jmin (160, r.getWidth())));
        r.removeFromBottom (6);
        auto r1 = r.removeFromTop (26);
        corrCentreLabel.setBounds (r1.removeFromLeft (66));
        corrCentre.setBounds (r1);
        auto r2 = r.removeFromTop (26);
        corrDriftLabel.setBounds (r2.removeFromLeft (66));
        corrDrift.setBounds (r2);
        corrHelp.setBounds (r.withTrimmedTop (2));
    }
    {
        auto r = quantCard.reduced (14, 0).withTrimmedTop (32).withTrimmedBottom (12);
        quantApply.setBounds (r.removeFromBottom (34).removeFromLeft (juce::jmin (160, r.getWidth())));
        r.removeFromBottom (6);
        gridBox.setBounds (r.removeFromTop (30).removeFromLeft (juce::jmin (170, r.getWidth())));
        auto r2 = r.removeFromTop (28);
        quantLabel.setBounds (r2.removeFromLeft (56));
        quantAmount.setBounds (r2);
        quantHelp.setBounds (r.withTrimmedTop (2));
    }
}
