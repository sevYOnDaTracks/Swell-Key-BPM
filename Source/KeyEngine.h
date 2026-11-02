#pragma once

#include <juce_dsp/juce_dsp.h>
#include <numeric>

// Swell Key: offline key + tempo detection on a whole file.
//  - Tempo: spectral-flux onset envelope, then a pulse-train search over the whole track
//    (sharp on fixed-tempo productions), octave chosen by the usual producer convention.
//  - Key: tuning-corrected chroma from spectral peaks (bass and upper range kept apart),
//    matched against key profiles; the bass adds a tonic bonus (808s and basslines sit on the tonic).
namespace SwellKey
{
inline const char* const noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

struct Key
{
    int tonic = 0;
    bool minor = false;
    float score = 0.0f;
    juce::String name() const { return juce::String (noteNames[tonic]) + (minor ? " minor" : " major"); }
    int index() const { return tonic + (minor ? 12 : 0); }
};

struct Result
{
    Key key, altKey;
    float keyConfidence = 0.0f;   // 0..1
    double bpm = 0.0, altBpm = 0.0;
    float bpmConfidence = 0.0f;   // 0..1
    float tuningCents = 0.0f;     // offset from A = 440 Hz
};

struct Options
{
    int profile = 4;              // see profiles()
    float bassWeight = 1.0f;      // bass chroma mixed into the key chroma (learned profile: weight of the bass match)
    float tonicBonus = 0.0f;      // weight of the bass on the tonic
    float minorBias = 0.06f;      // most rap / trap beats are minor: favour minor over its relative major
    float tonicPresence = 0.0f;   // penalty for a key whose tonic the bass (almost) never plays

    // chroma
    float maxHz = 3500.0f;        // highest frequency used for the key
    int harmonics = 4;            // credit each peak to the fundamentals it could be harmonic 1..n of
    float harmonicDecay = 0.8f;   // weight of harmonic h = decay^(h-1)
    float peakThreshold = 0.05f;  // peaks below this fraction of the frame's loudest are ignored
    float tuningTrust = 0.15f;    // peak agreement needed before moving away from A = 440 (higher = stricter)
};

//==============================================================================
namespace detail
{
    struct Profile { float major[12], minor[12]; };

    inline const std::vector<Profile>& profiles()
    {
        static const std::vector<Profile> p {
            // 0: Temperley (Kostka-Payne)
            { { 5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f },
              { 5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f } },
            // 1: Krumhansl-Kessler
            { { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f },
              { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f } },
            // 2: Sha'ath (KeyFinder)
            { { 6.6f, 2.0f, 3.5f, 2.3f, 4.6f, 4.0f, 2.5f, 5.2f, 2.4f, 3.7f, 2.3f, 3.4f },
              { 6.5f, 2.7f, 3.5f, 5.4f, 2.6f, 3.5f, 2.5f, 5.2f, 4.0f, 2.7f, 4.3f, 3.2f } },
            // 3: diatonic (flat scale tones, tonic and fifth stressed)
            { { 2.0f, 0.0f, 1.0f, 0.0f, 1.2f, 1.0f, 0.0f, 1.5f, 0.0f, 1.0f, 0.0f, 1.0f },
              { 2.0f, 0.0f, 1.0f, 1.2f, 0.0f, 1.0f, 0.0f, 1.5f, 1.0f, 0.0f, 1.0f, 0.3f } },
            // 4: learned from the GiantSteps key dataset (upper range; see tools/learn_profiles.py)
            { { 0.1453f, 0.0378f, 0.1063f, 0.0496f, 0.1196f, 0.0732f, 0.0479f, 0.1490f, 0.0434f, 0.0894f, 0.0573f, 0.0812f },
              { 0.1477f, 0.0475f, 0.0947f, 0.0964f, 0.0620f, 0.0847f, 0.0451f, 0.1534f, 0.0617f, 0.0558f, 0.0980f, 0.0531f } },
        };
        return p;
    }

    // Bass-range profiles learned alongside profile 4, matched separately against the bass chroma
    inline const Profile& learnedBassProfile()
    {
        static const Profile p {
            { 0.1378f, 0.0553f, 0.0889f, 0.0625f, 0.0927f, 0.1007f, 0.0570f, 0.1212f, 0.0582f, 0.0863f, 0.0668f, 0.0727f },
            { 0.1361f, 0.0684f, 0.0770f, 0.0963f, 0.0690f, 0.0959f, 0.0609f, 0.1055f, 0.0791f, 0.0625f, 0.0855f, 0.0639f } };
        return p;
    }

    // Mono, decimated to ~22 kHz (keys and beats need nothing above)
    inline std::vector<float> prepare (const juce::AudioBuffer<float>& in, double sr, double& fs)
    {
        const int factor = juce::jmax (1, (int) std::round (sr / 22050.0));
        fs = sr / factor;
        const int n = in.getNumSamples() / factor;
        std::vector<float> out ((size_t) juce::jmax (0, n), 0.0f);
        const float g = 1.0f / (float) (factor * juce::jmax (1, in.getNumChannels()));
        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            const float* x = in.getReadPointer (c);
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < factor; ++k)
                    out[(size_t) i] += x[i * factor + k] * g;
        }
        return out;
    }

    inline std::vector<float> hann (int n)
    {
        std::vector<float> w ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);
        return w;
    }

    //==========================================================================
    // Onset strength: log-compressed spectral flux, local mean removed
    inline std::vector<float> onsetEnvelope (const std::vector<float>& x, double fs, double& frameRate)
    {
        constexpr int order = 10, N = 1 << order, H = N / 4;
        frameRate = fs / H;
        if ((int) x.size() < N * 2)
            return {};

        juce::dsp::FFT fft (order);
        const auto win = hann (N);
        std::vector<float> buf ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f);
        const int maxBin = juce::jmin (N / 2, (int) (8000.0 / fs * N));
        const int frames = ((int) x.size() - N) / H + 1;
        std::vector<float> flux ((size_t) frames, 0.0f);

        for (int f = 0; f < frames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                buf[(size_t) i] = x[(size_t) (f * H + i)] * win[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            float s = 0.0f;
            for (int k = 1; k <= maxBin; ++k)
            {
                const float m = std::log1p (100.0f * buf[(size_t) k]);
                s += juce::jmax (0.0f, m - prev[(size_t) k]);
                prev[(size_t) k] = m;
            }
            flux[(size_t) f] = s;
        }

        // remove the local mean (+-0.5 s) and keep the positive part
        const int half = (int) (frameRate * 0.5);
        std::vector<double> cum ((size_t) frames + 1, 0.0);
        for (int f = 0; f < frames; ++f)
            cum[(size_t) f + 1] = cum[(size_t) f] + flux[(size_t) f];
        std::vector<float> o ((size_t) frames);
        for (int f = 0; f < frames; ++f)
        {
            const int a = juce::jmax (0, f - half), b = juce::jmin (frames, f + half + 1);
            const float mean = (float) ((cum[(size_t) b] - cum[(size_t) a]) / (b - a));
            o[(size_t) f] = juce::jmax (0.0f, flux[(size_t) f] - mean);
        }
        return o;
    }

    inline float at (const std::vector<float>& o, double t)
    {
        if (t < 0.0 || t >= (double) o.size() - 1.0)
            return 0.0f;
        const int i = (int) t;
        const float fr = (float) (t - i);
        return o[(size_t) i] * (1.0f - fr) + o[(size_t) i + 1] * fr;
    }

    // Mean onset strength on a beat grid of this tempo, at its best phase
    inline double pulse (const std::vector<float>& o, double frameRate, double bpm)
    {
        const double period = 60.0 * frameRate / bpm;
        double best = 0.0;
        for (double ph = 0.0; ph < period; ph += 0.5)
        {
            double s = 0.0;
            int n = 0;
            for (double t = ph; t < (double) o.size() - 1.0; t += period, ++n)
                s += at (o, t);
            if (n > 0)
                best = juce::jmax (best, s / n);
        }
        return best;
    }

    // Tempo family score: a real tempo also lines up at half speed and on its subdivision
    // (8ths, or triplets for drill-style hats)
    inline double family (const std::vector<float>& o, double frameRate, double bpm)
    {
        return pulse (o, frameRate, bpm) + pulse (o, frameRate, bpm * 0.5)
             + juce::jmax (pulse (o, frameRate, bpm * 2.0), pulse (o, frameRate, bpm * 3.0));
    }

    inline void detectTempo (const std::vector<float>& o, double frameRate, Result& r)
    {
        if (o.size() < 64)
            return;

        // coarse search over one octave (75..150), then refine
        double best = 0.0, bestBpm = 120.0;
        std::vector<double> scores;
        for (double b = 75.0; b < 150.0; b += 0.25)
        {
            const double s = family (o, frameRate, b);
            scores.push_back (s);
            if (s > best) { best = s; bestBpm = b; }
        }
        for (double b = bestBpm - 0.3; b <= bestBpm + 0.3; b += 0.01)
        {
            const double s = family (o, frameRate, b);
            if (s > best) { best = s; bestBpm = b; }
        }

        // confidence: how far the winner stands above a typical tempo
        std::sort (scores.begin(), scores.end());
        const double median = scores[scores.size() / 2];
        r.bpmConfidence = (float) juce::jlimit (0.0, 1.0, (best - median) / (best + 1.0e-9) * 1.6);

        // octave: the base family tempo is in 75..150; double it when that reads more naturally
        // (center of gravity of beat tempos ~115 BPM, upper limit 175)
        double main = bestBpm, other = bestBpm * 2.0;
        if (other < 175.0 && std::abs (std::log (other / 115.0)) < std::abs (std::log (main / 115.0)))
            std::swap (main, other);

        auto tidy = [] (double b) { return std::abs (b - std::round (b)) < 0.25 ? std::round (b) : std::round (b * 10.0) / 10.0; };
        r.bpm = tidy (main);
        r.altBpm = tidy (main >= 115.0 ? main * 0.5 : main * 2.0);
    }

    //==========================================================================
    // Chroma from spectral peaks, tuning-corrected; bass (< 250 Hz) and upper range kept apart
    inline void chroma (const std::vector<float>& x, double fs, std::array<double, 12>& upper, std::array<double, 12>& bass, float& tuning,
                        const Options& opt = {})
    {
        constexpr int order = 14, N = 1 << order, H = N / 4;
        upper.fill (0.0);
        bass.fill (0.0);
        tuning = 0.0f;
        if ((int) x.size() < N)
            return;

        juce::dsp::FFT fft (order);
        const auto win = hann (N);
        std::vector<float> buf ((size_t) N * 2);
        const double binHz = fs / N;
        const int lo = (int) (45.0 / binHz), hi = juce::jmin (N / 2 - 2, (int) (opt.maxHz / binHz));
        const int frames = ((int) x.size() - N) / H + 1;

        struct Peak { float midi, weight; };
        std::vector<std::vector<Peak>> peaks ((size_t) frames);
        std::vector<float> energy ((size_t) frames, 0.0f);

        for (int f = 0; f < frames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                buf[(size_t) i] = x[(size_t) (f * H + i)] * win[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            float maxMag = 0.0f;
            for (int k = lo; k <= hi; ++k)
                maxMag = juce::jmax (maxMag, buf[(size_t) k]);
            energy[(size_t) f] = maxMag;
            if (maxMag <= 0.0f)
                continue;

            for (int k = lo; k <= hi; ++k)
            {
                const float m = buf[(size_t) k];
                if (m < maxMag * opt.peakThreshold || m <= buf[(size_t) k - 1] || m < buf[(size_t) k + 1])
                    continue;
                // parabolic interpolation of the peak frequency
                const float a = std::log (buf[(size_t) k - 1] + 1e-9f), b = std::log (m + 1e-9f), c = std::log (buf[(size_t) k + 1] + 1e-9f);
                const float denom = a - 2.0f * b + c;
                const float d = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
                const double hz = (k + juce::jlimit (-0.5f, 0.5f, d)) * binHz;
                peaks[(size_t) f].push_back ({ (float) (69.0 + 12.0 * std::log2 (hz / 440.0)), std::sqrt (m / maxMag) });
            }
        }

        // tuning: weighted circular mean of the strong tonal peaks' offset from the 12-TET grid
        // (80 Hz - 1.5 kHz, drums and noise left out); stays at A = 440 unless the evidence is clear
        double cs = 0.0, sn = 0.0, total = 0.0;
        for (auto& fp : peaks)
            for (auto& p : fp)
            {
                if (p.weight < 0.4f || p.midi < 39.5f || p.midi > 90.5f)
                    continue;
                const double ang = juce::MathConstants<double>::twoPi * p.midi;
                cs += p.weight * std::cos (ang);
                sn += p.weight * std::sin (ang);
                total += p.weight;
            }
        const double concentration = total > 0.0 ? std::sqrt (cs * cs + sn * sn) / total : 0.0;
        tuning = (float) (std::atan2 (sn, cs) / juce::MathConstants<double>::twoPi
                          * juce::jlimit (0.0, 1.0, (concentration - opt.tuningTrust) / 0.25));

        // quiet frames (intros, breaks) count less; each loud frame votes about equally
        const float loud = *std::max_element (energy.begin(), energy.end());
        for (int f = 0; f < frames; ++f)
        {
            if (energy[(size_t) f] < loud * 0.05f)
                continue;
            std::array<double, 12> u {}, bs {};
            for (auto& p : peaks[(size_t) f])
            {
                const float m = p.midi - tuning;
                const float dev = m - std::round (m);
                if (std::abs (dev) > 0.35f)
                    continue;
                const double w = p.weight * std::pow (std::cos (juce::MathConstants<float>::pi * dev), 2.0f);
                (m < 59.0f ? bs : u)[(size_t) ((((int) std::round (m)) % 12 + 12) % 12)] += w;   // MIDI 59 ~ 247 Hz

                // this peak may also be harmonic h of a lower note: credit that fundamental too
                for (int h = 2; h <= opt.harmonics; ++h)
                {
                    const float fm = m - 12.0f * std::log2 ((float) h);
                    const float fdev = fm - std::round (fm);
                    if (std::abs (fdev) > 0.35f || fm < 28.0f)
                        continue;
                    const double hw = w * std::pow (opt.harmonicDecay, (double) (h - 1));
                    (fm < 59.0f ? bs : u)[(size_t) ((((int) std::round (fm)) % 12 + 12) % 12)] += hw;
                }
            }
            const double su = std::accumulate (u.begin(), u.end(), 0.0), sb = std::accumulate (bs.begin(), bs.end(), 0.0);
            for (int i = 0; i < 12; ++i)
            {
                if (su > 0.0) upper[(size_t) i] += u[(size_t) i] / su;
                if (sb > 0.0) bass[(size_t) i] += bs[(size_t) i] / sb;
            }
        }
    }

    inline double correlation (const std::array<double, 12>& x, const float* profile, int tonic)
    {
        double mx = 0.0, mp = 0.0;
        for (int i = 0; i < 12; ++i) { mx += x[(size_t) i]; mp += profile[i]; }
        mx /= 12.0; mp /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = x[(size_t) ((i + tonic) % 12)] - mx, b = profile[i] - mp;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    }

    inline void detectKey (const std::array<double, 12>& upper, const std::array<double, 12>& bass, const Options& opt, Result& r)
    {
        const auto& prof = profiles()[(size_t) juce::jlimit (0, (int) profiles().size() - 1, opt.profile)];

        auto norm = [] (std::array<double, 12> v)
        {
            const double s = std::accumulate (v.begin(), v.end(), 0.0);
            if (s > 0.0) for (auto& e : v) e /= s;
            return v;
        };
        const auto u = norm (upper), b = norm (bass);
        std::array<double, 12> mix {};
        for (int i = 0; i < 12; ++i)
            mix[(size_t) i] = u[(size_t) i] + opt.bassWeight * b[(size_t) i];

        std::array<double, 24> score {};
        const bool learned = opt.profile == 4;
        for (int t = 0; t < 12; ++t)
        {
            if (learned)
            {
                // learned profiles: upper and bass chroma each matched against their own profile
                const auto& bp = learnedBassProfile();
                score[(size_t) t]      = correlation (u, prof.major, t) + opt.bassWeight * correlation (b, bp.major, t);
                score[(size_t) t + 12] = correlation (u, prof.minor, t) + opt.bassWeight * correlation (b, bp.minor, t) + opt.minorBias;
                continue;
            }
            const double bassOnTonic = opt.tonicBonus * 12.0 * (b[(size_t) t] - 1.0 / 12.0)
                                     - opt.tonicPresence * (1.0 - juce::jlimit (0.0, 1.0, b[(size_t) t] / 0.15));
            score[(size_t) t]      = correlation (mix, prof.major, t) + bassOnTonic;
            score[(size_t) t + 12] = correlation (mix, prof.minor, t) + bassOnTonic + opt.minorBias;
        }

        std::array<int, 24> order {};
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (int a, int c) { return score[(size_t) a] > score[(size_t) c]; });

        auto keyOf = [&] (int idx) { Key k; k.tonic = idx % 12; k.minor = idx >= 12; k.score = (float) score[(size_t) idx]; return k; };
        r.key = keyOf (order[0]);
        r.altKey = keyOf (order[1]);

        // confidence from the gap between the two best keys
        const double gap = score[(size_t) order[0]] - score[(size_t) order[1]];
        r.keyConfidence = (float) juce::jlimit (0.0, 1.0, 0.5 + gap * 4.0);
    }
} // namespace detail

//==============================================================================
inline Result analyse (const juce::AudioBuffer<float>& audio, double sampleRate, const Options& opt = {})
{
    Result r;
    double fs = 0.0;
    const auto x = detail::prepare (audio, sampleRate, fs);

    double frameRate = 0.0;
    const auto onsets = detail::onsetEnvelope (x, fs, frameRate);
    detail::detectTempo (onsets, frameRate, r);

    std::array<double, 12> upper {}, bass {};
    float tuning = 0.0f;
    detail::chroma (x, fs, upper, bass, tuning, opt);
    r.tuningCents = tuning * 100.0f;
    detail::detectKey (upper, bass, opt, r);
    return r;
}
} // namespace SwellKey
