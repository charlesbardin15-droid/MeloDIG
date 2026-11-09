#pragma once
// MeloDIG engine: note-based editing of a monophonic recording (voice, solo instrument).
// No JUCE dependency, so it can be tested on its own.
//
//   analyse()  : pitch track (YIN, FFT-based) + segmentation into notes ("blobs").
//   Note       : what was sung (span, pitch centre, curve) + what the user changed
//                (pitch offset, vibrato/modulation, drift, formant, volume, timing).
//   render()   : offline pitch-synchronous overlap-add over the whole clip. Wherever nothing
//                was changed the original samples are used untouched, so an unedited clip is
//                bit-identical to the recording.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <numeric>
#include <vector>

namespace melodig {

constexpr double kPi = 3.14159265358979323846;

inline double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline double midiToHz (double m)  { return 440.0 * std::exp2 ((m - 69.0) / 12.0); }

// ---------------------------------------------------------------------------
// Small radix-2 FFT (in place).
class FFT
{
public:
    explicit FFT (int n = 1024) { setSize (n); }
    void setSize (int n)
    {
        size = n;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0, -2.0 * kPi * i / n);
    }
    int getSize() const { return size; }
    void forward (std::vector<std::complex<double>>& a) const { run (a, false); }
    void inverse (std::vector<std::complex<double>>& a) const
    {
        run (a, true);
        for (auto& v : a) v /= (double) size;
    }
private:
    void run (std::vector<std::complex<double>>& a, bool inv) const
    {
        const int n = size;
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    auto w = tw[(size_t) (k * step)];
                    if (inv) w = std::conj (w);
                    const auto u = a[(size_t) (i + k)], v = a[(size_t) (i + k + len / 2)] * w;
                    a[(size_t) (i + k)] = u + v;
                    a[(size_t) (i + k + len / 2)] = u - v;
                }
        }
    }
    int size = 0;
    std::vector<std::complex<double>> tw;
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double sr, double fc, double q)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * q), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; z1 = z2 = 0;
    }
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// ---------------------------------------------------------------------------
// Pitch track: one frame every `hop` input samples (frame i is centred on sample i*hop).
struct PitchTrack
{
    double sampleRate = 48000.0;
    int hop = 96;
    std::vector<float> midi;    // 0 = unvoiced
    std::vector<float> conf;    // periodicity 0..1
    std::vector<float> levelDb; // frame RMS

    int numFrames() const { return (int) midi.size(); }
    bool voicedAt (double sample) const
    {
        const int f = (int) std::lround (sample / hop);
        return f >= 0 && f < numFrames() && midi[(size_t) f] > 0.0f;
    }
    // Interpolated pitch (midi) at a sample position, 0 if unvoiced.
    double midiAt (double sample) const
    {
        const double fp = sample / hop;
        const int f0 = (int) std::floor (fp);
        if (f0 < 0 || f0 >= numFrames()) return 0.0;
        const double a = midi[(size_t) f0];
        if (f0 + 1 >= numFrames()) return a;
        const double b = midi[(size_t) f0 + 1];
        if (a <= 0 || b <= 0) return (fp - f0 < 0.5 ? a : b);
        return a + (b - a) * (fp - f0);
    }
};

struct AnalysisSettings
{
    double minHz = 55.0, maxHz = 1200.0;
    double gateDb = -50.0;        // relative to the clip's loudest frame
};

inline PitchTrack trackPitch (const std::vector<float>& mono, double sr, const AnalysisSettings& set = {})
{
    PitchTrack t;
    t.sampleRate = sr;
    const int decim = std::max (1, (int) std::lround (sr / 16000.0));
    const double srD = sr / decim;
    const int hopD = std::max (1, (int) std::lround (srD * 0.002));   // ~2 ms
    t.hop = hopD * decim;

    // Decimated, low-passed copy.
    std::vector<double> x;
    {
        Biquad f1, f2;
        f1.lowpass (sr, 0.42 * srD, 0.5412);
        f2.lowpass (sr, 0.42 * srD, 1.3066);
        x.reserve (mono.size() / (size_t) decim + 1);
        for (size_t i = 0; i < mono.size(); ++i)
        {
            const double y = f2.process (f1.process (mono[i]));
            if (i % (size_t) decim == 0) x.push_back (y);
        }
    }
    const int nD = (int) x.size();
    const int minLag = std::max (2, (int) std::floor (srD / set.maxHz));
    const int maxLag = (int) std::ceil (srD / set.minHz);
    const int W = std::max ((int) std::lround (srD * 0.025), maxLag);   // integration window
    int N = 1; while (N < W + maxLag + 2) N <<= 1;
    FFT fft (N);

    // Energy prefix sums for the sliding terms.
    std::vector<double> pre ((size_t) nD + 1, 0.0);
    for (int i = 0; i < nD; ++i) pre[(size_t) i + 1] = pre[(size_t) i] + x[(size_t) i] * x[(size_t) i];
    auto energy = [&] (int a, int len)
    {
        const int lo = std::clamp (a, 0, nD), hi = std::clamp (a + len, 0, nD);
        return pre[(size_t) hi] - pre[(size_t) lo];
    };
    auto xs = [&] (int i) { return (i >= 0 && i < nD) ? x[(size_t) i] : 0.0; };

    const int frames = (int) (mono.size() / (size_t) t.hop) + 1;
    t.midi.assign ((size_t) frames, 0.0f);
    t.conf.assign ((size_t) frames, 0.0f);
    t.levelDb.assign ((size_t) frames, -120.0f);

    std::vector<std::complex<double>> A ((size_t) N), B ((size_t) N);
    std::vector<double> d ((size_t) maxLag + 2), cm ((size_t) maxLag + 2);
    std::vector<float> rawMidi ((size_t) frames, 0.0f);
    float peakDb = -120.0f;

    for (int fr = 0; fr < frames; ++fr)
    {
        const int centre = fr * hopD;
        const int start = centre - W / 2 - maxLag / 4;
        // level over a 20 ms window centred on the frame
        {
            const int half = (int) (srD * 0.01);
            const double e = energy (centre - half, 2 * half) / std::max (1, 2 * half);
            t.levelDb[(size_t) fr] = (float) (10.0 * std::log10 (e + 1e-12));
            peakDb = std::max (peakDb, t.levelDb[(size_t) fr]);
        }
        for (int i = 0; i < N; ++i)
        {
            A[(size_t) i] = i < W ? xs (start + i) : 0.0;
            B[(size_t) i] = i < W + maxLag ? xs (start + i) : 0.0;
        }
        fft.forward (A);
        fft.forward (B);
        for (int i = 0; i < N; ++i) A[(size_t) i] = std::conj (A[(size_t) i]) * B[(size_t) i];
        fft.inverse (A);
        const double e0 = energy (start, W);
        if (e0 < 1e-10) continue;
        d[0] = 0; cm[0] = 1;
        double run = 0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const double dt = std::max (0.0, e0 + energy (start + tau, W) - 2.0 * A[(size_t) tau].real());
            d[(size_t) tau] = dt;
            run += dt;
            cm[(size_t) tau] = run > 0 ? dt * tau / run : 1.0;
        }
        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (cm[(size_t) tau] < 0.15)
            {
                while (tau + 1 <= maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            double m = 1e9;
            for (int tau = minLag; tau <= maxLag; ++tau) if (cm[(size_t) tau] < m) { m = cm[(size_t) tau]; best = tau; }
            if (m > 0.35) best = -1;
        }
        if (best < 0) continue;
        double tf = best;
        if (best > 1 && best < maxLag)
        {
            const double s0 = d[(size_t) best - 1], s1 = d[(size_t) best], s2 = d[(size_t) best + 1], den = s0 - 2 * s1 + s2;
            if (std::abs (den) > 1e-12) tf += 0.5 * (s0 - s2) / den;
        }
        rawMidi[(size_t) fr] = (float) hzToMidi (srD / tf);
        t.conf[(size_t) fr] = (float) std::clamp (1.0 - cm[(size_t) best], 0.0, 1.0);
    }

    // Gate, median smoothing, octave-jump repair, short-run removal.
    const float gate = peakDb + (float) set.gateDb;
    for (int i = 0; i < frames; ++i) if (t.levelDb[(size_t) i] < gate) rawMidi[(size_t) i] = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        if (rawMidi[(size_t) i] <= 0) continue;
        float w[5]; int n = 0;
        for (int k = -2; k <= 2; ++k)
        {
            const int j = i + k;
            if (j >= 0 && j < frames && rawMidi[(size_t) j] > 0) w[n++] = rawMidi[(size_t) j];
        }
        std::nth_element (w, w + n / 2, w + n);
        float v = rawMidi[(size_t) i];
        const float med = w[n / 2];
        if (std::abs (v - med) > 6.0f) v += 12.0f * std::round ((med - v) / 12.0f);   // octave error
        else if (std::abs (v - med) > 0.8f) v = med;
        t.midi[(size_t) i] = v;
    }
    const int minRun = std::max (2, (int) std::lround (0.025 * sr / t.hop));
    const int maxGap = std::max (1, (int) std::lround (0.02 * sr / t.hop));
    // fill short unvoiced gaps between similar pitches
    for (int i = 0; i < frames;)
    {
        if (t.midi[(size_t) i] > 0) { ++i; continue; }
        int j = i; while (j < frames && t.midi[(size_t) j] <= 0) ++j;
        if (i > 0 && j < frames && j - i <= maxGap && std::abs (t.midi[(size_t) i - 1] - t.midi[(size_t) j]) < 1.0f)
            for (int k = i; k < j; ++k)
                t.midi[(size_t) k] = t.midi[(size_t) i - 1] + (t.midi[(size_t) j] - t.midi[(size_t) i - 1]) * float (k - i + 1) / float (j - i + 1);
        i = j;
    }
    for (int i = 0; i < frames;)
    {
        if (t.midi[(size_t) i] <= 0) { ++i; continue; }
        int j = i; while (j < frames && t.midi[(size_t) j] > 0) ++j;
        if (j - i < minRun) for (int k = i; k < j; ++k) t.midi[(size_t) k] = 0.0f;
        i = j;
    }
    return t;
}

// ---------------------------------------------------------------------------
// A note: analysis results (orig*) + user edits.
struct Note
{
    // analysis (input samples)
    double start = 0, end = 0;          // span, including an attached consonant lead / tail
    double voicedStart = 0, voicedEnd = 0;
    float centre = 60.0f;               // pitch centre (midi)

    // edits
    float pitchOffset = 0.0f;           // semitones
    float modulation = 1.0f;            // vibrato / fast deviation scale (0 = flat, 1 = as sung)
    float drift = 1.0f;                 // slow deviation scale
    float formant = 0.0f;               // semitones
    float gainDb = 0.0f;
    bool muted = false;
    double newStart = 0, newEnd = 0;    // where [start, end) is played (output samples)

    bool pitchEdited() const { return std::abs (pitchOffset) > 1e-4f || std::abs (modulation - 1) > 1e-4f || std::abs (drift - 1) > 1e-4f; }
    bool timeEdited() const { return std::abs (newStart - start) > 0.5 || std::abs (newEnd - end) > 0.5; }
    float targetCentre() const { return centre + pitchOffset; }
};

// Pitch centre: level-weighted mean of the middle 70 % of the voiced frames.
inline float noteCentre (const PitchTrack& t, double vs, double ve)
{
    const int f0 = (int) std::ceil (vs / t.hop), f1 = (int) std::floor (ve / t.hop);
    const int n = f1 - f0;
    if (n <= 0) return (float) std::max (0.0, t.midiAt ((vs + ve) / 2));
    const int a = f0 + (int) (n * 0.15), b = std::max (a + 1, f1 - (int) (n * 0.15));
    double s = 0, w = 0;
    for (int f = a; f < b; ++f)
    {
        if (f < 0 || f >= t.numFrames() || t.midi[(size_t) f] <= 0) continue;
        const double wt = std::pow (10.0, t.levelDb[(size_t) f] / 20.0);
        s += wt * t.midi[(size_t) f]; w += wt;
    }
    return w > 0 ? (float) (s / w) : (float) t.midiAt ((vs + ve) / 2);
}

inline std::vector<Note> segmentNotes (const PitchTrack& t, double totalSamples)
{
    std::vector<Note> notes;
    const int frames = t.numFrames();
    const double fps = t.sampleRate / t.hop;
    const int smoothHalf = std::max (1, (int) std::lround (0.06 * fps));
    const int sustain = std::max (2, (int) std::lround (0.045 * fps));
    const int minLen = std::max (3, (int) std::lround (0.09 * fps));
    const double tau = 0.15 * fps;

    float peak = -120.0f;
    for (float l : t.levelDb) peak = std::max (peak, l);

    for (int i = 0; i < frames;)
    {
        if (t.midi[(size_t) i] <= 0) { ++i; continue; }
        int j = i; while (j < frames && t.midi[(size_t) j] > 0) ++j;
        // voiced run [i, j): smoothed curve
        std::vector<double> s ((size_t) (j - i));
        for (int k = i; k < j; ++k)
        {
            double sum = 0; int n = 0;
            for (int q = std::max (i, k - smoothHalf); q <= std::min (j - 1, k + smoothHalf); ++q) { sum += t.midi[(size_t) q]; ++n; }
            s[(size_t) (k - i)] = sum / n;
        }
        std::vector<int> cuts { i };
        std::vector<int> pitchCuts;
        const int settle = std::min (j - i - 1, (int) (1.5 * smoothHalf));
        double ref = s[(size_t) settle];
        int count = 0, since = 0;
        for (int k = i + 1; k < j; ++k, ++since)
        {
            const double v = s[(size_t) (k - i)];
            if (std::abs (v - ref) > 0.45) ++count; else count = 0;
            if (count >= sustain && since > minLen)
            {
                // cut at the steepest raw slope in the last stretch
                int best = k - count, bestSlope = -1; double bs = -1;
                for (int q = std::max (cuts.back() + 2, k - count - sustain); q <= k; ++q)
                {
                    const double sl = std::abs (t.midi[(size_t) q] - t.midi[(size_t) q - 1]);
                    if (sl > bs) { bs = sl; bestSlope = q; }
                }
                if (bestSlope > 0) best = bestSlope;
                cuts.push_back (best);
                pitchCuts.push_back (best);
                ref = s[(size_t) (std::min (j - 1, best + (int) (1.5 * smoothHalf)) - i)];
                count = 0; since = k - best;
                continue;
            }
            if (std::abs (v - ref) < 0.25) ref += (v - ref) / tau;   // follow slow drift, not the next note
        }
        // amplitude re-articulations on one pitch: level dip then rise
        {
            const int w150 = (int) std::lround (0.15 * fps);
            for (int k = i + minLen; k < j - minLen; ++k)
            {
                const float l = t.levelDb[(size_t) k];
                if (l > t.levelDb[(size_t) k - 1] || l > t.levelDb[(size_t) k + 1]) continue;
                float before = -200, after = -200;
                for (int q = std::max (i, k - w150); q < k; ++q) before = std::max (before, t.levelDb[(size_t) q]);
                for (int q = k + 1; q <= std::min (j - 1, k + w150); ++q) after = std::max (after, t.levelDb[(size_t) q]);
                if (before - l > 9.0f && after - l > 6.0f)
                {
                    bool near = false;
                    for (int c : cuts) if (std::abs (c - k) < minLen) near = true;
                    if (! near) cuts.push_back (k);
                }
            }
            std::sort (cuts.begin(), cuts.end());
        }
        cuts.push_back (j);
        // undo pitch cuts between pieces that end up at the same pitch (glides, scoops)
        auto pieceMedian = [&] (int a, int b)
        {
            std::vector<float> v;
            for (int q = a + (b - a) / 5; q < b - (b - a) / 5; ++q) v.push_back (t.midi[(size_t) q]);
            if (v.empty()) for (int q = a; q < b; ++q) v.push_back (t.midi[(size_t) q]);
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            return (double) v[v.size() / 2];
        };
        for (bool changed = true; changed;)
        {
            changed = false;
            for (size_t c = 1; c + 1 < cuts.size(); ++c)
            {
                if (std::find (pitchCuts.begin(), pitchCuts.end(), cuts[c]) == pitchCuts.end()) continue;
                if (std::abs (pieceMedian (cuts[c - 1], cuts[c]) - pieceMedian (cuts[c], cuts[c + 1])) < 0.35)
                {
                    cuts.erase (cuts.begin() + (long) c);
                    changed = true;
                    break;
                }
            }
        }
        // merge too-short pieces into the neighbour with the closer pitch
        for (bool changed = true; changed && cuts.size() > 2;)
        {
            changed = false;
            for (size_t c = 0; c + 1 < cuts.size(); ++c)
            {
                if (cuts[c + 1] - cuts[c] >= minLen) continue;
                if (cuts.size() == 2) break;
                if (c == 0) cuts.erase (cuts.begin() + 1);
                else if (c + 2 == cuts.size()) cuts.erase (cuts.begin() + (long) c);
                else
                {
                    const double mid = t.midi[(size_t) (cuts[c] + cuts[c + 1]) / 2];
                    const double pl = t.midi[(size_t) (cuts[c - 1] + cuts[c]) / 2], pr = t.midi[(size_t) (cuts[c + 1] + cuts[c + 2]) / 2];
                    if (std::abs (mid - pl) < std::abs (mid - pr)) cuts.erase (cuts.begin() + (long) c);
                    else cuts.erase (cuts.begin() + (long) c + 1);
                }
                changed = true;
                break;
            }
        }
        for (size_t c = 0; c + 1 < cuts.size(); ++c)
        {
            Note n;
            n.voicedStart = n.start = (double) cuts[c] * t.hop;
            n.voicedEnd = n.end = (double) cuts[c + 1] * t.hop;
            notes.push_back (n);
        }
        i = j;
    }

    // Attach audible unvoiced material (consonants, breaths before / sibilant tails after).
    const float gate = peak - 45.0f;
    auto audible = [&] (int f) { return f >= 0 && f < frames && t.midi[(size_t) f] <= 0 && t.levelDb[(size_t) f] > gate; };
    const int maxLead = (int) std::lround (0.2 * fps), maxTail = (int) std::lround (0.15 * fps);
    for (size_t k = 0; k < notes.size(); ++k)
    {
        auto& n = notes[k];
        const double prevEnd = k > 0 ? notes[k - 1].end : 0.0;
        int f = (int) (n.voicedStart / t.hop) - 1, steps = 0;
        while (steps < maxLead && audible (f) && f * t.hop > prevEnd) { --f; ++steps; }
        n.start = std::max (prevEnd, (double) (f + 1) * t.hop);
        const double nextStart = k + 1 < notes.size() ? notes[k + 1].voicedStart : totalSamples;
        f = (int) (n.voicedEnd / t.hop); steps = 0;
        while (steps < maxTail && audible (f) && (f + 1) * t.hop < nextStart) { ++f; ++steps; }
        n.end = std::min (nextStart, (double) f * t.hop);
        n.end = std::max (n.end, n.voicedEnd);
        n.end = std::min (n.end, totalSamples);
    }
    for (auto& n : notes)
    {
        n.centre = noteCentre (t, n.voicedStart, n.voicedEnd);
        n.newStart = n.start;
        n.newEnd = n.end;
    }
    return notes;
}

// Split / merge (separation tool).
inline bool splitNote (std::vector<Note>& notes, size_t index, double atSample, const PitchTrack& t)
{
    if (index >= notes.size()) return false;
    Note a = notes[index];
    if (atSample <= a.voicedStart + 0.03 * t.sampleRate || atSample >= a.voicedEnd - 0.03 * t.sampleRate) return false;
    Note b = a;
    // a time-edited note keeps its scaling: map the cut into output time
    const double frac = (atSample - a.start) / std::max (1.0, a.end - a.start);
    const double cutOut = a.newStart + frac * (a.newEnd - a.newStart);
    a.end = a.voicedEnd = atSample; a.newEnd = cutOut;
    b.start = b.voicedStart = atSample; b.newStart = cutOut;
    a.centre = noteCentre (t, a.voicedStart, a.voicedEnd);
    b.centre = noteCentre (t, b.voicedStart, b.voicedEnd);
    // both parts keep the edit of the original note, so splitting changes nothing audible
    notes[index] = a;
    notes.insert (notes.begin() + (long) index + 1, b);
    return true;
}

inline bool mergeNotes (std::vector<Note>& notes, size_t index, const PitchTrack& t)   // index with index+1
{
    if (index + 1 >= notes.size()) return false;
    Note& a = notes[index];
    const Note& b = notes[index + 1];
    if (b.start - a.end > 1.0) return false;   // only touching notes
    a.end = b.end; a.voicedEnd = b.voicedEnd; a.newEnd = b.newEnd;
    a.centre = noteCentre (t, a.voicedStart, a.voicedEnd);
    notes.erase (notes.begin() + (long) index + 1);
    return true;
}

// ---------------------------------------------------------------------------
// Scales and macros
struct Scale
{
    int root = 0;          // 0 = C
    int type = 0;          // 0 chromatic, 1 major, 2 minor
    bool contains (int midi) const
    {
        static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
        if (type == 0) return true;
        const int pc = ((midi - root) % 12 + 12) % 12;
        const int* s = type == 1 ? major : minor;
        for (int i = 0; i < 7; ++i) if (s[i] == pc) return true;
        return false;
    }
    int nearest (double midi) const
    {
        const int r = (int) std::lround (midi);
        for (int d = 0; d < 7; ++d)
        {
            const int lo = r - d, hi = r + d;
            const bool a = contains (lo), b = contains (hi);
            if (a && b) return std::abs (midi - lo) <= std::abs (midi - hi) ? lo : hi;
            if (a) return lo;
            if (b) return hi;
        }
        return r;
    }
};

// "Correct pitch": pull pitch centres toward the scale (centreAmount) and flatten drift (driftAmount).
inline void correctPitch (std::vector<Note>& notes, const std::vector<size_t>& sel, const Scale& scale,
                          float centreAmount, float driftAmount)
{
    for (size_t i : sel)
    {
        if (i >= notes.size()) continue;
        auto& n = notes[i];
        const double cur = n.targetCentre();
        const int target = scale.nearest (cur);
        n.pitchOffset += (float) ((target - cur) * std::clamp (centreAmount, 0.0f, 1.0f));
        n.drift = std::clamp (n.drift * (1.0f - std::clamp (driftAmount, 0.0f, 1.0f)), 0.0f, 2.0f);
    }
}

// "Quantize time": move note starts toward a grid (output samples), keeping their length.
inline void quantizeTime (std::vector<Note>& notes, const std::vector<size_t>& sel, double gridSamples,
                          double gridOffset, float amount, double totalSamples)
{
    if (gridSamples <= 1) return;
    for (size_t i : sel)
    {
        if (i >= notes.size()) continue;
        auto& n = notes[i];
        const double anchor = n.newStart + (n.voicedStart - n.start) * (n.newEnd - n.newStart) / std::max (1.0, n.end - n.start);
        const double q = gridOffset + std::round ((anchor - gridOffset) / gridSamples) * gridSamples;
        double d = (q - anchor) * std::clamp (amount, 0.0f, 1.0f);
        const double lo = i > 0 ? notes[i - 1].newEnd : 0.0;
        const double hi = i + 1 < notes.size() ? notes[i + 1].newStart : totalSamples;
        d = std::clamp (d, lo - n.newStart, hi - n.newEnd);
        n.newStart += d; n.newEnd += d;
    }
}

// Key estimate (Krumhansl-Schmuckler) from the notes' pitch classes, weighted by duration.
inline Scale estimateKey (const std::vector<Note>& notes)
{
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double h[12] = {};
    for (const auto& n : notes) h[(((int) std::lround (n.targetCentre())) % 12 + 12) % 12] += n.voicedEnd - n.voicedStart;
    auto corr = [&] (const double* prof, int root)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += h[(i + root) % 12]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = h[(i + root) % 12] - mx, y = prof[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : -1.0;
    };
    Scale best; double bc = -2;
    for (int r = 0; r < 12; ++r)
    {
        const double a = corr (maj, r), b = corr (mnr, r);
        if (a > bc) { bc = a; best.root = r; best.type = 1; }
        if (b > bc) { bc = b; best.root = r; best.type = 2; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Output -> input time map (piecewise linear, monotonic).
struct TimeMap
{
    std::vector<std::pair<double, double>> pts;   // (out, in)
    bool identity = true;

    static TimeMap build (const std::vector<Note>& notes, double total)
    {
        TimeMap m;
        m.pts.push_back ({ 0.0, 0.0 });
        for (const auto& n : notes)
        {
            if (n.timeEdited()) m.identity = false;
            m.pts.push_back ({ n.newStart, n.start });
            m.pts.push_back ({ n.newEnd, n.end });
        }
        m.pts.push_back ({ total, total });
        std::stable_sort (m.pts.begin(), m.pts.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        // enforce monotonic input
        for (size_t i = 1; i < m.pts.size(); ++i) m.pts[i].second = std::max (m.pts[i].second, m.pts[i - 1].second);
        return m;
    }
    double toInput (double out) const
    {
        if (identity) return out;
        auto it = std::upper_bound (pts.begin(), pts.end(), out, [] (double v, const auto& p) { return v < p.first; });
        if (it == pts.begin()) return out;
        if (it == pts.end()) return pts.back().second + (out - pts.back().first);
        const auto& b = *it; const auto& a = *(it - 1);
        if (b.first - a.first < 1e-9) return b.second;
        return a.second + (b.second - a.second) * (out - a.first) / (b.first - a.first);
    }
    double toOutput (double in) const
    {
        if (identity) return in;
        for (size_t i = 1; i < pts.size(); ++i)
            if (in <= pts[i].second)
            {
                const auto& a = pts[i - 1]; const auto& b = pts[i];
                if (b.second - a.second < 1e-9) return a.first;
                return a.first + (b.first - a.first) * (in - a.second) / (b.second - a.second);
            }
        return in;
    }
};

// ---------------------------------------------------------------------------
// Per-frame edit curves (input time): pitch delta (semitones), formant (semitones), gain (dB).
struct EditCurves
{
    std::vector<float> delta, formant, gainDb, gain;   // gain: linear, smoothed
    std::vector<uint8_t> muted;
};

// Target pitch of one frame of a note: centre + offset + drift*slow + modulation*fast.
inline void noteDeviation (const PitchTrack& t, const Note& n, std::vector<double>& slow, std::vector<double>& fast, int& f0)
{
    f0 = (int) std::ceil (n.voicedStart / t.hop);
    const int f1 = std::min (t.numFrames(), (int) std::floor (n.voicedEnd / t.hop));
    const int len = std::max (0, f1 - f0);
    std::vector<double> dev ((size_t) len, 0.0);
    std::vector<uint8_t> ok ((size_t) len, 0);
    for (int k = 0; k < len; ++k)
    {
        const float m = t.midi[(size_t) (f0 + k)];
        if (m > 0) { dev[(size_t) k] = m - n.centre; ok[(size_t) k] = 1; }
    }
    const int half = std::max (1, (int) std::lround (0.1 * t.sampleRate / t.hop));   // ~200 ms box: removes vibrato
    slow.assign ((size_t) len, 0.0);
    fast.assign ((size_t) len, 0.0);
    for (int k = 0; k < len; ++k)
    {
        double s = 0; int c = 0;
        for (int q = std::max (0, k - half); q <= std::min (len - 1, k + half); ++q) if (ok[(size_t) q]) { s += dev[(size_t) q]; ++c; }
        slow[(size_t) k] = c ? s / c : 0.0;
        fast[(size_t) k] = ok[(size_t) k] ? dev[(size_t) k] - slow[(size_t) k] : 0.0;
    }
}

inline EditCurves buildCurves (const PitchTrack& t, const std::vector<Note>& notes)
{
    EditCurves c;
    const int frames = t.numFrames();
    c.delta.assign ((size_t) frames, 0.0f);
    c.formant.assign ((size_t) frames, 0.0f);
    c.gainDb.assign ((size_t) frames, 0.0f);
    c.muted.assign ((size_t) frames, 0);
    std::vector<int> owner ((size_t) frames, -1);
    std::vector<float> offset ((size_t) frames, 0.0f), dev ((size_t) frames, 0.0f);

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const int a = std::max (0, (int) std::ceil (n.start / t.hop)), b = std::min (frames, (int) std::ceil (n.end / t.hop));
        for (int f = a; f < b; ++f)
        {
            owner[(size_t) f] = (int) i;
            offset[(size_t) f] = n.pitchOffset;
            c.formant[(size_t) f] = n.formant;
            c.gainDb[(size_t) f] = n.muted ? -120.0f : n.gainDb;
            c.muted[(size_t) f] = n.muted ? 1 : 0;
        }
        if (std::abs (n.modulation - 1) > 1e-4f || std::abs (n.drift - 1) > 1e-4f)
        {
            std::vector<double> slow, fast; int f0;
            noteDeviation (t, n, slow, fast, f0);
            for (size_t k = 0; k < slow.size(); ++k)
            {
                const int f = f0 + (int) k;
                if (f < 0 || f >= frames || t.midi[(size_t) f] <= 0) continue;
                dev[(size_t) f] = (float) ((n.drift - 1.0) * slow[k] + (n.modulation - 1.0) * fast[k]);
            }
        }
    }

    // Pitch transition between two touching notes: raised-cosine blend over ~35 ms each side.
    // (A note next to silence / unvoiced sound keeps its own pitch right from its start.)
    auto blendBoundaries = [&] (std::vector<float>& v, int r)
    {
        std::vector<float> out = v;
        for (int f = 1; f < frames; ++f)
        {
            const int oa = owner[(size_t) f - 1], ob = owner[(size_t) f];
            if (oa < 0 || ob < 0 || oa == ob) continue;
            const float left = v[(size_t) f - 1], right = v[(size_t) f];
            if (std::abs (left - right) < 1e-6f) continue;
            int lo = f - r, hi = f + r;
            for (int q = f - 1; q >= f - r; --q) if (q < 0 || owner[(size_t) q] != oa) { lo = q + 1; break; }
            for (int q = f; q < f + r; ++q) if (q >= frames || owner[(size_t) q] != ob) { hi = q; break; }
            for (int q = lo; q < hi; ++q)
            {
                const float x = (float) (q - lo + 0.5f) / (float) (hi - lo);
                const float w = 0.5f - 0.5f * std::cos ((float) kPi * x);
                out[(size_t) q] = left + (right - left) * w;
            }
        }
        v.swap (out);
    };
    const int tr = std::max (1, (int) std::lround (0.035 * t.sampleRate / t.hop));
    blendBoundaries (offset, tr);
    blendBoundaries (c.formant, tr);
    for (int f = 0; f < frames; ++f) c.delta[(size_t) f] = offset[(size_t) f] + dev[(size_t) f];

    // Volume as a linear gain, smoothed with a 20 ms moving average (only near changes).
    {
        c.gain.assign ((size_t) frames, 1.0f);
        bool any = false;
        for (int f = 0; f < frames; ++f)
        {
            c.gain[(size_t) f] = c.muted[(size_t) f] ? 0.0f : (float) std::pow (10.0, c.gainDb[(size_t) f] / 20.0);
            if (c.gain[(size_t) f] != 1.0f) any = true;
        }
        if (any)
        {
            const int gr = std::max (1, (int) std::lround (0.010 * t.sampleRate / t.hop));
            std::vector<double> pre ((size_t) frames + 1, 0.0);
            for (int f = 0; f < frames; ++f) pre[(size_t) f + 1] = pre[(size_t) f] + c.gain[(size_t) f];
            std::vector<float> g ((size_t) frames);
            for (int f = 0; f < frames; ++f)
            {
                const int lo = std::max (0, f - gr), hi = std::min (frames, f + gr + 1);
                g[(size_t) f] = (float) ((pre[(size_t) hi] - pre[(size_t) lo]) / (hi - lo));
            }
            c.gain.swap (g);
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// Offline renderer.
struct RenderInput
{
    const std::vector<std::vector<float>>* audio = nullptr;   // [channel][sample], the recording
    const PitchTrack* track = nullptr;
    const std::vector<Note>* notes = nullptr;
};

class Renderer
{
public:
    // Fills `out` (same size as the input). Returns false if cancelled.
    template <typename CancelFn>
    static bool render (const RenderInput& in, std::vector<std::vector<float>>& out, CancelFn cancelled)
    {
        const auto& audio = *in.audio;
        const auto& t = *in.track;
        const auto& notes = *in.notes;
        const int channels = (int) audio.size();
        const int len = channels > 0 ? (int) audio[0].size() : 0;
        out = audio;
        if (len == 0) return true;

        const EditCurves curves = buildCurves (t, notes);
        const TimeMap map = TimeMap::build (notes, (double) len);

        auto frameAt = [&] (double s) { return std::clamp ((int) std::lround (s / t.hop), 0, std::max (0, t.numFrames() - 1)); };

        // Which output samples need resynthesis (pitch, formant or timing changed)?
        std::vector<float> wet ((size_t) len, 0.0f);
        bool any = false;
        for (int s = 0; s < len; s += 16)
        {
            const double u = map.toInput (s);
            const int f = frameAt (u);
            const bool moved = ! map.identity && std::abs (u - s) > 0.5;
            const bool edited = std::abs (curves.delta[(size_t) f]) > 1e-4f || std::abs (curves.formant[(size_t) f]) > 1e-4f;
            if (moved || edited) { for (int k = s; k < std::min (len, s + 16); ++k) wet[(size_t) k] = 1.0f; any = true; }
        }
        if (! map.identity)   // anything between two moved anchors is stretched
            for (size_t i = 1; i < map.pts.size(); ++i)
                if (std::abs ((map.pts[i].first - map.pts[i - 1].first) - (map.pts[i].second - map.pts[i - 1].second)) > 0.5)
                    for (int k = std::max (0, (int) map.pts[i - 1].first); k < std::min (len, (int) map.pts[i].first); ++k) { wet[(size_t) k] = 1.0f; any = true; }

        if (any)
        {
            // dilate + ramp (15 ms) so the switch between original and resynthesis is smooth
            const int ramp = std::max (1, (int) (0.015 * t.sampleRate));
            std::vector<float> w2 ((size_t) len, 0.0f);
            int last = -1000000000;
            for (int s = 0; s < len; ++s) { if (wet[(size_t) s] > 0) last = s; w2[(size_t) s] = s - last < ramp ? 1.0f - float (s - last) / ramp : 0.0f; }
            last = 2000000000;
            for (int s = len - 1; s >= 0; --s) { if (wet[(size_t) s] > 0) last = s; const float v = last - s < ramp ? 1.0f - float (last - s) / ramp : 0.0f; w2[(size_t) s] = std::max (w2[(size_t) s], v); }
            for (auto& v : w2) v = 0.5f - 0.5f * std::cos ((float) kPi * v);
            wet.swap (w2);

            std::vector<std::vector<float>> syn;
            if (! psola (audio, t, curves, map, syn, cancelled)) return false;
            levelMatch (audio, notes, map, syn, t.sampleRate);
            for (int ch = 0; ch < channels; ++ch)
                for (int s = 0; s < len; ++s)
                {
                    const float w = wet[(size_t) s];
                    if (w > 0) out[(size_t) ch][(size_t) s] = audio[(size_t) ch][(size_t) s] * (1 - w) + syn[(size_t) ch][(size_t) s] * w;
                }
        }

        // Volume / mute (output time -> the note playing there).
        bool gains = false;
        for (float g : curves.gain) if (std::abs (g - 1.0f) > 1e-6f) { gains = true; break; }
        if (gains)
        {
            for (int s = 0; s < len; ++s)
            {
                const double fp = map.toInput (s) / t.hop;
                const int f0 = std::clamp ((int) std::floor (fp), 0, t.numFrames() - 1), f1 = std::min (t.numFrames() - 1, f0 + 1);
                const float g = (float) (curves.gain[(size_t) f0] + (curves.gain[(size_t) f1] - curves.gain[(size_t) f0]) * (fp - f0));
                if (g == 1.0f) continue;
                for (int ch = 0; ch < channels; ++ch) out[(size_t) ch][(size_t) s] *= g;
            }
        }
        return true;
    }

private:
    // Overlap-add changes the loudness a little (fewer / more pulses per second, wider grains).
    // Like a careful engineer would, bring each edited note back to the level it was sung at.
    static void levelMatch (const std::vector<std::vector<float>>& audio, const std::vector<Note>& notes,
                            const TimeMap& map, std::vector<std::vector<float>>& syn, double sr)
    {
        const int len = (int) audio[0].size();
        std::vector<float> comp ((size_t) len, 1.0f);
        bool any = false;
        auto energy = [&] (const std::vector<std::vector<float>>& x, double a, double b)
        {
            double e = 0; long n = 0;
            for (long s = std::max (0L, (long) a); s < std::min ((long) len, (long) b); ++s)
            { for (auto& ch : x) e += (double) ch[(size_t) s] * ch[(size_t) s]; ++n; }
            return n > 0 ? e / n : 0.0;
        };
        for (const auto& n : notes)
        {
            if (! (n.pitchEdited() || std::abs (n.formant) > 1e-4f || n.timeEdited())) continue;
            const double vs = map.toOutput (n.voicedStart), ve = map.toOutput (n.voicedEnd);
            const double e0 = energy (audio, n.voicedStart, n.voicedEnd), e1 = energy (syn, vs, ve);
            if (e0 <= 1e-12 || e1 <= 1e-12) continue;
            const float g = (float) std::clamp (std::sqrt (e0 / e1), 0.5, 2.0);
            if (std::abs (g - 1.0f) < 0.01f) continue;
            const double os = map.toOutput (n.start), oe = map.toOutput (n.end);
            const double ramp = 0.01 * sr;
            for (long s = std::max (0L, (long) (os - ramp)); s < std::min ((long) len, (long) (oe + ramp)); ++s)
            {
                double w = 1.0;
                if (s < os) w = 1.0 - (os - s) / ramp; else if (s > oe) w = 1.0 - (s - oe) / ramp;
                comp[(size_t) s] = (float) (comp[(size_t) s] * (1.0 + (g - 1.0) * std::clamp (w, 0.0, 1.0)));
            }
            any = true;
        }
        if (any) for (auto& ch : syn) for (int s = 0; s < len; ++s) ch[(size_t) s] *= comp[(size_t) s];
    }

    struct Mark { double pos; double period; bool voiced; };

    static float readAt (const std::vector<float>& x, double pos)
    {
        const long n = (long) x.size();
        const long i1 = (long) std::floor (pos);
        const float fr = (float) (pos - (double) i1);
        auto at = [&] (long i) { return (i >= 0 && i < n) ? x[(size_t) i] : 0.0f; };
        if (fr == 0.0f) return at (i1);
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);   // cubic Hermite
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + y1;
    }

    // Pitch marks: one per period in voiced parts, aligned on the waveform's main peak.
    static std::vector<Mark> pitchMarks (const std::vector<std::vector<float>>& audio, const PitchTrack& t)
    {
        const int len = (int) audio[0].size();
        std::vector<float> lp ((size_t) len);
        {
            Biquad f; f.lowpass (t.sampleRate, 1200.0, 0.707);
            for (int s = 0; s < len; ++s)
            {
                double v = 0; for (auto& ch : audio) v += ch[(size_t) s];
                lp[(size_t) s] = (float) f.process (v / (double) audio.size());
            }
            // compensate the filter delay roughly (~0.13 ms) by ignoring it: peaks only need consistency
        }
        std::vector<Mark> marks;
        const double U = t.sampleRate * 0.005;
        double s = 0;
        while (s < len)
        {
            if (! t.voicedAt (s)) { marks.push_back ({ s, U, false }); s += U; continue; }
            // voiced run: walk period by period
            double T = t.sampleRate / midiToHz (t.midiAt (s));
            double p = s;
            {   // first mark: peak within the first period
                int best = (int) p; float bv = -1e9f;
                for (int k = (int) p; k < std::min (len, (int) (p + T)); ++k) if (lp[(size_t) k] > bv) { bv = lp[(size_t) k]; best = k; }
                p = best;
            }
            while (p < len && t.voicedAt (p))
            {
                T = t.sampleRate / midiToHz (std::max (20.0, t.midiAt (p)));
                marks.push_back ({ p, T, true });
                const double pred = p + T;
                int best = (int) std::lround (pred); float bv = -1e9f;
                for (int k = (int) (pred - 0.2 * T); k <= (int) (pred + 0.2 * T); ++k)
                    if (k >= 0 && k < len && lp[(size_t) k] > bv) { bv = lp[(size_t) k]; best = k; }
                p = best > p ? best : pred;
            }
            s = p;
        }
        return marks;
    }

    template <typename CancelFn>
    static bool psola (const std::vector<std::vector<float>>& audio, const PitchTrack& t, const EditCurves& c,
                       const TimeMap& map, std::vector<std::vector<float>>& out, CancelFn cancelled)
    {
        const int channels = (int) audio.size();
        const int len = (int) audio[0].size();
        out.assign ((size_t) channels, std::vector<float> ((size_t) len, 0.0f));
        std::vector<float> wsum ((size_t) len, 0.0f);
        std::vector<uint8_t> down ((size_t) len, 0);

        const auto marks = pitchMarks (audio, t);
        if (marks.empty()) { out = audio; return true; }
        std::vector<double> mpos (marks.size());
        for (size_t i = 0; i < marks.size(); ++i) mpos[i] = marks[i].pos;

        auto curveAt = [&] (const std::vector<float>& v, double s)
        {
            const double fp = s / t.hop;
            const int f0 = std::clamp ((int) std::floor (fp), 0, (int) v.size() - 1), f1 = std::min ((int) v.size() - 1, f0 + 1);
            return (double) v[(size_t) f0] + (v[(size_t) f1] - v[(size_t) f0]) * (fp - f0);
        };
        auto nearestMark = [&] (double u)
        {
            auto it = std::lower_bound (mpos.begin(), mpos.end(), u);
            size_t i = (size_t) (it - mpos.begin());
            if (i >= mpos.size()) return mpos.size() - 1;
            if (i > 0 && u - mpos[i - 1] < mpos[i] - u) return i - 1;
            return i;
        };

        double tau = 0.0;
        int counter = 0;
        while (tau < len)
        {
            if ((++counter & 1023) == 0 && cancelled()) return false;
            const double u = map.toInput (tau);
            const size_t k = nearestMark (u);
            const Mark& m = marks[k];
            double step, half, readRate = 1.0, centreIn;
            bool isDown = false;
            if (m.voiced && std::abs (m.pos - u) < m.period)
            {
                const double next = k + 1 < marks.size() && marks[k + 1].voiced ? marks[k + 1].pos - m.pos : m.period;
                const double delta = curveAt (c.delta, m.pos);
                const double ratio = std::exp2 (delta / 12.0);
                readRate = std::exp2 (curveAt (c.formant, m.pos) / 12.0);
                step = next / ratio;
                half = m.period / readRate;
                centreIn = m.pos;
                isDown = ratio < 0.999 || readRate > 1.001;
                // Entering a voiced stretch from unvoiced material: land on the mark (keeps identity exact).
                if (! map.identity || std::abs (delta) > 1e-6) {} else tau = map.toOutput (m.pos);
            }
            else
            {
                step = t.sampleRate * 0.005;
                half = step;
                centreIn = u;
            }
            const int L = std::max (2, (int) std::ceil (half));
            const long ct = (long) std::lround (tau);
            const double frac = tau - (double) ct;
            for (int j = -L; j <= L; ++j)
            {
                const long o = ct + j;
                if (o < 0 || o >= len) continue;
                const double jj = (double) j - frac;
                if (std::abs (jj) >= half + 1.0) continue;
                const float w = (float) (0.5 + 0.5 * std::cos (kPi * jj / (half + 1.0)));
                const double pos = centreIn + jj * readRate;
                for (int ch = 0; ch < channels; ++ch) out[(size_t) ch][(size_t) o] += w * readAt (audio[(size_t) ch], pos);
                wsum[(size_t) o] += w;
                if (isDown && w > 0.05f) down[(size_t) o] = 1;
            }
            tau += std::max (8.0, step);
        }
        for (int s = 0; s < len; ++s)
        {
            const float ws = wsum[(size_t) s];
            const float floorW = down[(size_t) s] ? 1.0f : 0.05f;
            const float g = ws > floorW ? 1.0f / ws : (ws > 1e-6f ? 1.0f / floorW : 0.0f);
            for (int ch = 0; ch < channels; ++ch) out[(size_t) ch][(size_t) s] *= g;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Convenience: whole analysis of a clip.
struct Analysis
{
    PitchTrack track;
    std::vector<Note> notes;
};

inline Analysis analyse (const std::vector<std::vector<float>>& audio, double sr, const AnalysisSettings& set = {})
{
    Analysis a;
    if (audio.empty() || audio[0].empty()) return a;
    std::vector<float> mono (audio[0].size(), 0.0f);
    for (auto& ch : audio) for (size_t i = 0; i < mono.size(); ++i) mono[i] += ch[i] / (float) audio.size();
    a.track = trackPitch (mono, sr, set);
    a.notes = segmentNotes (a.track, (double) mono.size());
    return a;
}

} // namespace melodig
