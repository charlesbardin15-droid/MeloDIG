// MeloDIG engine checks: note detection, transparency, pitch / time / formant / volume edits.
#include "MeloEngine.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

using namespace melodig;

static int failures = 0;
static void check (bool ok, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0)
{
    failures += ! ok;
    std::printf ("%-4s ", ok ? "OK" : "FAIL");
    std::printf (fmt, a, b, c, d);
    std::printf ("\n");
}

struct TrueNote { double start, end, midi; };

struct Reso
{
    double a1, a2, b0, y1 = 0, y2 = 0;
    Reso (double f, double bw, double sr) { const double r = std::exp (-kPi * bw / sr); a1 = 2 * r * std::cos (2 * kPi * f / sr); a2 = -r * r; b0 = 1 - r; }
    double p (double x) { const double y = b0 * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; }
};

// A sung phrase with human imperfections (scoops, vibrato, drift, off-key notes).
// legato = notes glide into each other without gaps; consonants = a noise burst before some notes.
static std::vector<float> singer (double sr, unsigned seed, bool legato, bool consonants, std::vector<TrueNote>& truth)
{
    const int notes[] = { 57, 60, 62, 64, 62, 60, 59, 57, 64, 65, 64, 62, 60, 57 };
    const double durs[] = { .6, .45, .45, .9, .4, .4, .5, .8, .5, .5, .5, .45, .45, 1.0 };
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0, 1);
    std::uniform_real_distribution<double> u (-1, 1);
    std::vector<float> out ((size_t) (0.3 * sr), 0.0f);
    double phase = 0, drift = 0, prevGl = 0, lastMidi = notes[0];
    Reso f1 (700, 90, sr), f2 (1150, 110, sr), f3 (2600, 160, sr), f4 (3400, 200, sr);
    for (int k = 0; k < 14; ++k)
    {
        if (consonants && k % 3 == 1)   // "s"-like burst
        {
            Biquad hp; hp.lowpass (sr, 6000, 0.7);
            for (int i = 0; i < (int) (0.07 * sr); ++i)
            {
                const double n = g (rng);
                out.push_back ((float) ((n - hp.process (n)) * 0.05 * std::sin (kPi * i / (0.07 * sr))));
            }
        }
        const double off = 0.30 * u (rng);
        const double scoop = legato ? 0.0 : -0.6 - 0.4 * std::abs (u (rng));
        const int len = (int) (durs[k] * sr);
        TrueNote tn { (double) out.size(), (double) out.size() + len, 0 };
        double sum = 0;
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            drift += (g (rng) * 0.02 - drift * 0.002);
            const double sc = scoop * std::exp (-t / 0.05);
            const double glide = legato ? (lastMidi - notes[k]) * std::exp (-t / 0.03) : 0.0;
            const double vib = (t > 0.25 ? std::min (1.0, (t - 0.25) / 0.2) : 0.0) * 0.35 * std::sin (2 * kPi * 5.6 * t);
            const double midi = notes[k] + off + sc + vib + glide + 0.15 * std::tanh (drift);
            if (i > len * 0.15 && i < len * 0.85) sum += midi - vib;
            phase += midiToHz (midi) * (1.0 + 0.0012 * g (rng)) / sr;
            if (phase >= 1) phase -= 1;
            const double op = 0.6, cl = 0.15;
            const double gl = phase < op ? 0.5 * (1 - std::cos (kPi * phase / op)) : (phase < op + cl ? std::cos (kPi * (phase - op) / (2 * cl)) : 0.0);
            const double src = (gl - prevGl) * 8.0 + 0.02 * g (rng); prevGl = gl;
            const double y = f1.p (src) + f2.p (src) * 0.6 + f3.p (src) * 0.3 + f4.p (src) * 0.15;
            const double env = legato ? 1.0 : std::min (1.0, t / 0.03) * std::min (1.0, (durs[k] - t) / 0.05);
            out.push_back ((float) (y * env * 0.5));
        }
        tn.midi = sum / std::max (1.0, len * 0.7);
        truth.push_back (tn);
        lastMidi = notes[k] + off;
        if (! legato) for (int i = 0; i < (int) (0.04 * sr); ++i) out.push_back ((float) (0.002 * g (rng)));
    }
    for (int i = 0; i < (int) (0.3 * sr); ++i) out.push_back (0.0f);
    float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
    for (float& v : out) v *= 0.4f / pk;
    return out;
}

static double rms (const std::vector<float>& x, double a, double b)
{
    double e = 0; int n = 0;
    for (long i = std::max (0L, (long) a); i < std::min ((long) x.size(), (long) b); ++i) { e += (double) x[(size_t) i] * x[(size_t) i]; ++n; }
    return std::sqrt (e / std::max (1, n));
}

// Matches detected notes to the true ones (by overlap).
static int matchNote (const std::vector<Note>& notes, const TrueNote& tn)
{
    int best = -1; double bo = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const double o = std::min (notes[i].voicedEnd, tn.end) - std::max (notes[i].voicedStart, tn.start);
        if (o > bo) { bo = o; best = (int) i; }
    }
    return bo > 0.5 * (tn.end - tn.start) ? best : -1;
}

// Pitch of a span of audio (median of voiced frames, detected afresh).
static double spanPitch (const std::vector<float>& x, double sr, double a, double b)
{
    const auto t = trackPitch (x, sr);
    std::vector<float> v;
    for (int f = (int) (a / t.hop); f < (int) (b / t.hop) && f < t.numFrames(); ++f) if (t.midi[(size_t) f] > 0) v.push_back (t.midi[(size_t) f]);
    if (v.empty()) return 0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

static double spanPitchSpread (const std::vector<float>& x, double sr, double a, double b)
{
    const auto t = trackPitch (x, sr);
    double s = 0, s2 = 0; int n = 0;
    for (int f = (int) (a / t.hop); f < (int) (b / t.hop) && f < t.numFrames(); ++f)
        if (t.midi[(size_t) f] > 0) { s += t.midi[(size_t) f]; s2 += (double) t.midi[(size_t) f] * t.midi[(size_t) f]; ++n; }
    if (n < 2) return 0;
    const double m = s / n;
    return std::sqrt (std::max (0.0, s2 / n - m * m)) * 100.0;   // cents
}

static double centroid (const std::vector<float>& x, double sr, double a, double b)
{
    // spectral centroid over a span via a plain DFT on a few frames
    double num = 0, den = 0;
    const int N = 2048;
    FFT fft (N);
    for (long st = (long) a; st + N < (long) b; st += N)
    {
        std::vector<std::complex<double>> buf ((size_t) N);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[(size_t) (st + i)] * (0.5 - 0.5 * std::cos (2 * kPi * i / (N - 1)));
        fft.forward (buf);
        for (int k = 1; k < N / 2; ++k) { const double p = std::norm (buf[(size_t) k]); num += p * k * sr / N; den += p; }
    }
    return den > 0 ? num / den : 0;
}

int main()
{
    const double sr = 48000;
    for (int legato = 0; legato < 2; ++legato)
    {
        std::vector<TrueNote> truth;
        const auto mono = singer (sr, 3 + legato, legato == 1, legato == 0, truth);
        std::vector<std::vector<float>> audio { mono, mono };
        const auto t0 = std::chrono::steady_clock::now();
        auto an = analyse (audio, sr);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const char* name = legato ? "legato" : "detached+consonants";
        std::printf ("\n== %s phrase: %.1f s of audio, analysed in %.3f s, %d notes found ==\n",
                     name, mono.size() / sr, secs, (int) an.notes.size());
        check (secs < 1.0, "analysis speed (%.3f s)", secs);

        int found = 0; double worst = 0;
        for (const auto& tn : truth)
        {
            const int i = matchNote (an.notes, tn);
            if (i < 0) continue;
            ++found;
            worst = std::max (worst, std::abs (an.notes[(size_t) i].centre - tn.midi) * 100.0);
        }
        check (found == 14 && (int) an.notes.size() >= 14 && (int) an.notes.size() <= 16,
               "notes: %.0f of 14 sung notes found, %.0f detected", found, (double) an.notes.size());
        check (worst < 25, "pitch centres within %.1f cents of what was sung", worst);

        {
            const Scale k = estimateKey (an.notes);
            check ((k.root == 9 && k.type == 2) || (k.root == 0 && k.type == 1), "key detected: root %.0f type %.0f (A minor / C major expected)", k.root, k.type);
        }

        // 1. no edits -> bit-identical
        std::vector<std::vector<float>> out;
        RenderInput in { &audio, &an.track, &an.notes };
        auto never = [] { return false; };
        Renderer::render (in, out, never);
        double maxDiff = 0;
        for (size_t i = 0; i < mono.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (out[0][i] - mono[i]));
        check (maxDiff == 0.0, "no edits: output is bit-identical (max diff %.1e)", maxDiff);

        // 2. move note 4 (index of the 4th true note) up 2 semitones
        {
            auto notes = an.notes;
            const int i = matchNote (notes, truth[3]);
            notes[(size_t) i].pitchOffset = 2.0f;
            RenderInput in2 { &audio, &an.track, &notes };
            const auto t1 = std::chrono::steady_clock::now();
            Renderer::render (in2, out, never);
            const double rs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
            const double p0 = spanPitch (mono, sr, notes[(size_t) i].voicedStart, notes[(size_t) i].voicedEnd);
            const double p1 = spanPitch (out[0], sr, notes[(size_t) i].voicedStart, notes[(size_t) i].voicedEnd);
            check (std::abs ((p1 - p0) - 2.0) < 0.06, "note moved +2 st: measured %+.3f st", p1 - p0);
            const double l0 = rms (mono, notes[(size_t) i].voicedStart, notes[(size_t) i].voicedEnd);
            const double l1 = rms (out[0], notes[(size_t) i].voicedStart, notes[(size_t) i].voicedEnd);
            check (std::abs (20 * std::log10 (l1 / l0)) < 2.0, "moved note keeps its level (%+.2f dB)", 20 * std::log10 (l1 / l0));
            // far-away notes untouched
            const auto& far = truth[10];
            double d = 0;
            for (long s = (long) far.start; s < (long) far.end; ++s) d = std::max (d, (double) std::abs (out[0][(size_t) s] - mono[(size_t) s]));
            check (d == 0.0, "other notes untouched (max diff %.1e)", d);
            check (rs < 0.5, "render time %.3f s", rs);
        }

        // 3. "correct pitch" 100 % centre + 100 % drift on all notes
        {
            auto notes = an.notes;
            std::vector<size_t> all (notes.size()); std::iota (all.begin(), all.end(), 0);
            Scale sc; sc.root = 9; sc.type = 2;   // A minor
            correctPitch (notes, all, sc, 1.0f, 1.0f);
            RenderInput in3 { &audio, &an.track, &notes };
            Renderer::render (in3, out, never);
            double worstC = 0;
            for (const auto& tn : truth)
            {
                const int i = matchNote (notes, tn);
                if (i < 0) continue;
                const auto& n = notes[(size_t) i];
                const double a = n.voicedStart + 0.2 * (n.voicedEnd - n.voicedStart), b = n.voicedEnd - 0.2 * (n.voicedEnd - n.voicedStart);
                const double p = spanPitch (out[0], sr, a, b);
                worstC = std::max (worstC, std::abs (p - std::round (p)) * 100.0);
            }
            check (worstC < 12, "correct pitch 100%%: every note within %.1f cents of the A-minor scale", worstC);
        }

        // 4. modulation 0 flattens vibrato
        {
            auto notes = an.notes;
            const int i = matchNote (notes, truth[13]);   // the long last note has vibrato
            auto& n = notes[(size_t) i];
            const double a = n.voicedStart + 0.45 * (n.voicedEnd - n.voicedStart), b = n.voicedEnd - 0.1 * (n.voicedEnd - n.voicedStart);
            const double before = spanPitchSpread (mono, sr, a, b);
            n.modulation = 0.0f;
            RenderInput in4 { &audio, &an.track, &notes };
            Renderer::render (in4, out, never);
            const double after = spanPitchSpread (out[0], sr, a, b);
            check (after < before * 0.4, "modulation 0: vibrato %.0f -> %.0f cents spread", before, after);
        }

        // 5. formant +3 st: brighter, same pitch
        {
            auto notes = an.notes;
            const int i = matchNote (notes, truth[3]);
            auto& n = notes[(size_t) i];
            n.formant = 3.0f;
            RenderInput in5 { &audio, &an.track, &notes };
            Renderer::render (in5, out, never);
            const double c0 = centroid (mono, sr, n.voicedStart, n.voicedEnd), c1 = centroid (out[0], sr, n.voicedStart, n.voicedEnd);
            const double p0 = spanPitch (mono, sr, n.voicedStart, n.voicedEnd), p1 = spanPitch (out[0], sr, n.voicedStart, n.voicedEnd);
            check (c1 > c0 * 1.08 && std::abs (p1 - p0) < 0.08, "formant +3 st: centroid %.0f -> %.0f Hz, pitch change %+.3f st", c0, c1, p1 - p0);
        }

        // 6. stretch a note by 150 ms: its end and every later note move 150 ms later
        {
            auto notes = an.notes;
            const int i = matchNote (notes, truth[7]);
            const double add = 0.15 * sr;
            notes[(size_t) i].newEnd += add;
            for (size_t k = (size_t) i + 1; k < notes.size(); ++k) { notes[k].newStart += add; notes[k].newEnd += add; }
            const auto& n = notes[(size_t) i];
            RenderInput in6 { &audio, &an.track, &notes };
            Renderer::render (in6, out, never);
            const auto tr = trackPitch (out[0], sr);
            const auto map = TimeMap::build (notes, (double) mono.size());
            const double vs = map.toOutput (n.voicedStart), ve = map.toOutput (n.voicedEnd);
            int voiced = 0;
            for (int f = (int) (vs / tr.hop); f < (int) (ve / tr.hop); ++f) voiced += tr.midi[(size_t) f] > 0;
            const double outLen = voiced * (double) tr.hop / sr, inLen = (n.voicedEnd - n.voicedStart) / sr;
            const double p0 = spanPitch (mono, sr, n.voicedStart, n.voicedEnd), p1 = spanPitch (out[0], sr, vs, ve);
            // a later note, moved without stretching: same pitch, 150 ms later
            const auto& m = notes[(size_t) matchNote (notes, truth[11])];
            const double q0 = spanPitch (mono, sr, m.voicedStart, m.voicedEnd), q1 = spanPitch (out[0], sr, m.voicedStart + add, m.voicedEnd + add);
            check (outLen > inLen + 0.12 && std::abs (p1 - p0) < 0.08 && std::abs (q1 - q0) < 0.05,
                   "time: note stretched %.3f s -> %.3f s (pitch %+.3f st), later note moved (pitch %+.3f st)", inLen, outLen, p1 - p0, q1 - q0);
        }

        // 7. volume -6 dB and mute
        {
            auto notes = an.notes;
            const int i = matchNote (notes, truth[5]), j = matchNote (notes, truth[9]);
            notes[(size_t) i].gainDb = -6.0f;
            notes[(size_t) j].muted = true;
            RenderInput in7 { &audio, &an.track, &notes };
            Renderer::render (in7, out, never);
            const auto& n = notes[(size_t) i];
            const double r = 20 * std::log10 (rms (out[0], n.voicedStart + 0.02 * sr, n.voicedEnd - 0.02 * sr) / rms (mono, n.voicedStart + 0.02 * sr, n.voicedEnd - 0.02 * sr));
            const auto& m = notes[(size_t) j];
            const double rm = rms (out[0], m.start + 0.02 * sr, m.end - 0.02 * sr);
            check (std::abs (r + 6.0) < 0.2 && rm < 1e-6, "volume -6 dB measured %+.2f dB, muted note rms %.1e", r, rm);
        }

        // 8. split + merge keep the sound unchanged
        {
            auto notes = an.notes;
            const int i = matchNote (notes, truth[3]);
            const double mid = (notes[(size_t) i].voicedStart + notes[(size_t) i].voicedEnd) / 2;
            const bool sp = splitNote (notes, (size_t) i, mid, an.track);
            RenderInput in8 { &audio, &an.track, &notes };
            Renderer::render (in8, out, never);
            double d = 0; for (size_t s = 0; s < mono.size(); ++s) d = std::max (d, (double) std::abs (out[0][s] - mono[s]));
            const bool mg = mergeNotes (notes, (size_t) i, an.track);
            check (sp && mg && d < 1e-6 && notes.size() == an.notes.size(), "split then merge: sound unchanged (max diff %.1e)", d);
        }

        // 9. quantize time keeps notes ordered
        {
            auto notes = an.notes;
            std::vector<size_t> all (notes.size()); std::iota (all.begin(), all.end(), 0);
            quantizeTime (notes, all, sr * 60.0 / 120.0 / 4.0, 0.0, 1.0f, (double) mono.size());
            bool ordered = true;
            for (size_t k = 1; k < notes.size(); ++k) ordered &= notes[k].newStart >= notes[k - 1].newEnd - 1e-6;
            RenderInput in9 { &audio, &an.track, &notes };
            const bool okR = Renderer::render (in9, out, never);
            bool fin = true; for (float v : out[0]) fin &= std::isfinite (v) && std::abs (v) < 2.0f;
            check (ordered && okR && fin, "quantize time (1/16 at 120 BPM): notes stay in order, output clean");
        }
    }

    // Lifecycle / edge cases
    {
        std::vector<std::vector<float>> empty;
        auto a = analyse (empty, 48000);
        check (a.notes.empty(), "empty clip: no crash");
        std::vector<std::vector<float>> silent { std::vector<float> (48000, 0.0f) };
        auto b = analyse (silent, 48000);
        std::vector<std::vector<float>> out;
        RenderInput in { &silent, &b.track, &b.notes };
        Renderer::render (in, out, [] { return false; });
        check (b.notes.empty() && out[0].size() == 48000, "silent clip: no notes, renders");
        for (double r : { 44100.0, 96000.0 })
        {
            std::vector<TrueNote> truth;
            auto mono = singer (r, 7, false, false, truth);
            std::vector<std::vector<float>> audio { mono };
            auto an = analyse (audio, r);
            int found = 0; for (auto& tn : truth) found += matchNote (an.notes, tn) >= 0;
            if (! an.notes.empty()) an.notes[2].pitchOffset = -3.0f;
            RenderInput in2 { &audio, &an.track, &an.notes };
            Renderer::render (in2, out, [] { return false; });
            const double p0 = spanPitch (mono, r, an.notes[2].voicedStart, an.notes[2].voicedEnd);
            const double p1 = spanPitch (out[0], r, an.notes[2].voicedStart, an.notes[2].voicedEnd);
            check (found == 14 && std::abs (p1 - p0 + 3.0) < 0.08, "%.0f Hz: %.0f/14 notes, -3 st edit measured %+.3f st", r, found, p1 - p0);
        }
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nAll checks passed\n", failures);
    return failures ? 1 : 0;
}
