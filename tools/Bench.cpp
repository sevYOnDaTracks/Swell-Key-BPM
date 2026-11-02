// Swell Key test bench.
//   SwellKeyBench analyse <file> [file...]   key + BPM of real files
//   SwellKeyBench synth <count> [seed]        score the engine on generated beats with known key/BPM
//   SwellKeyBench dataset <audio> <keys|-> <bpms|->   score on an annotated dataset (GiantSteps layout)
//   SwellKeyBench features <audio> <keys> <out.csv>   export chroma features for profile learning

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/KeyEngine.h"

namespace
{
using namespace SwellKey;

//==============================================================================
// Beat generator: chords, melody, 808 on the chord roots, drums in a given style
struct Truth { int tonic; bool minor; double bpm; int style; float cents; };

const char* const styleNames[] = { "trap", "drill", "rnb", "boombap", "afro" };
juce::String progLabel;   // chord degrees of the last generated beat, for error reports

struct Synth
{
    double sr = 44100.0;
    juce::Random rng;
    std::vector<float> out;
    float cents = 0.0f;

    double hz (double midi) const { return 440.0 * std::pow (2.0, (midi - 69.0 + cents / 100.0) / 12.0); }

    void tone (double start, double dur, double midi, float gain, int harmonics, double decay)
    {
        const int s0 = (int) (start * sr), n = (int) (dur * sr);
        const double f = hz (midi);
        for (int i = 0; i < n && s0 + i < (int) out.size(); ++i)
        {
            const double t = i / sr;
            const double env = std::min (1.0, t / 0.008) * std::exp (-t / decay) * std::min (1.0, (dur - t) / 0.02);
            double v = 0.0;
            for (int h = 1; h <= harmonics; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * f * h * t) / h;
            out[(size_t) (s0 + i)] += (float) (v * env * gain);
        }
    }

    void bass808 (double start, double dur, double midi, float gain)
    {
        const int s0 = (int) (start * sr), n = (int) (dur * sr);
        const double f = hz (midi);
        double ph = 0.0;
        for (int i = 0; i < n && s0 + i < (int) out.size(); ++i)
        {
            const double t = i / sr;
            const double fi = f * (1.0 + 0.6 * std::exp (-t / 0.015));   // short pitch drop at the attack
            ph += juce::MathConstants<double>::twoPi * fi / sr;
            const double env = std::min (1.0, t / 0.003) * std::exp (-t / 0.6) * std::min (1.0, (dur - t) / 0.02);
            out[(size_t) (s0 + i)] += (float) (std::tanh (1.8 * std::sin (ph)) * env * gain);
        }
    }

    void kick (double start)
    {
        const int s0 = (int) (start * sr);
        double ph = 0.0;
        for (int i = 0; i < (int) (0.25 * sr) && s0 + i < (int) out.size(); ++i)
        {
            const double t = i / sr;
            ph += juce::MathConstants<double>::twoPi * (50.0 + 110.0 * std::exp (-t / 0.03)) / sr;
            out[(size_t) (s0 + i)] += (float) (std::sin (ph) * std::exp (-t / 0.08) * 0.9);
        }
    }

    void noiseHit (double start, double dur, double decay, float gain, bool bright, double toneHz = 0.0)
    {
        const int s0 = (int) (start * sr);
        float prev = 0.0f;
        for (int i = 0; i < (int) (dur * sr) && s0 + i < (int) out.size(); ++i)
        {
            const double t = i / sr;
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            const float v = bright ? w - prev : w;   // crude high-pass for hats
            prev = w;
            double s = v * std::exp (-t / decay);
            if (toneHz > 0.0)
                s += 0.5 * std::sin (juce::MathConstants<double>::twoPi * toneHz * t) * std::exp (-t / 0.05);
            out[(size_t) (s0 + i)] += (float) (s * gain);
        }
    }
};

juce::AudioBuffer<float> makeBeat (const Truth& tr, int seed, double seconds)
{
    Synth s;
    s.rng.setSeed (seed);
    s.cents = tr.cents;
    s.out.assign ((size_t) (seconds * s.sr), 0.0f);
    auto& rng = s.rng;

    static const int majorScale[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorScale[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* scale = tr.minor ? minorScale : majorScale;
    const bool harmonicV = tr.minor && rng.nextFloat() < 0.3f;

    static const std::vector<std::vector<int>> minorProgs { { 0, 5, 2, 6 }, { 0, 3, 4, 0 }, { 0, 5, 3, 4 }, { 0, 6, 5, 6 }, { 0, 0, 5, 5 }, { 0, 3, 0, 4 }, { 0, 2, 5, 4 } };
    static const std::vector<std::vector<int>> majorProgs { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 0, 3, 4, 0 }, { 5, 3, 0, 4 }, { 0, 2, 3, 4 }, { 3, 4, 0, 0 } };
    const auto& progs = tr.minor ? minorProgs : majorProgs;
    const auto prog = progs[(size_t) rng.nextInt ((int) progs.size())];
    progLabel.clear();
    for (int d : prog) progLabel << (d + 1) << " ";

    auto degreeMidi = [&] (int degree, int baseOctaveMidi)
    {
        const int d = ((degree % 7) + 7) % 7;
        int pc = scale[d];
        if (harmonicV && d == 6) pc = 11;   // leading tone in the V chord
        return baseOctaveMidi + tr.tonic + pc + 12 * (degree / 7);
    };

    const double beat = 60.0 / tr.bpm;
    const double barLen = beat * 4.0;
    const int bars = (int) (seconds / barLen);
    const float keysGain = 0.08f + 0.06f * rng.nextFloat();
    const float bassGain = 0.25f + 0.2f * rng.nextFloat();

    for (int bar = 0; bar < bars; ++bar)
    {
        const double t0 = bar * barLen;
        const int degree = prog[(size_t) (bar % 4)];
        const bool seventh = rng.nextFloat() < 0.3f;

        // chord
        for (int i = 0; i < (seventh ? 4 : 3); ++i)
            s.tone (t0, barLen, degreeMidi (degree + 2 * i, 48), keysGain, 5, 1.5);

        // melody: 8th notes, chord tones favoured
        for (int e = 0; e < 8; ++e)
            if (rng.nextFloat() < 0.45f)
            {
                const int deg = rng.nextFloat() < 0.6f ? degree + 2 * rng.nextInt (3) : rng.nextInt (7);
                s.tone (t0 + e * beat * 0.5, beat * 0.5, degreeMidi (deg, 60), keysGain * 0.9f, 3, 0.25);
            }

        // 808 on the chord root, drums by style
        const int root = degreeMidi (degree, 24);
        const int bassMidi = root < 31 ? root + 12 : root;
        std::vector<double> kickBeats;
        switch (tr.style)
        {
            case 0: kickBeats = { 0.0, 1.5, 2.75 }; break;            // trap
            case 1: kickBeats = { 0.0, 1.75, 2.5 }; break;            // drill
            case 2: kickBeats = { 0.0, 2.5 }; break;                  // r&b
            case 3: kickBeats = { 0.0, 1.75, 2.5 }; break;            // boom bap
            default: kickBeats = { 0.0, 1.0, 2.0, 3.0 }; break;       // afro
        }
        for (size_t k = 0; k < kickBeats.size(); ++k)
        {
            const double t = t0 + kickBeats[k] * beat;
            const double len = (k + 1 < kickBeats.size() ? kickBeats[k + 1] : 4.0) * beat - kickBeats[k] * beat;
            s.kick (t);
            s.bass808 (t, len, bassMidi, bassGain);
        }

        if (tr.style <= 1)
            s.noiseHit (t0 + 2.0 * beat, 0.25, 0.08, 0.5f, false, 190.0);            // half-time snare
        else
            for (double b : { 1.0, 3.0 })
                s.noiseHit (t0 + b * beat, 0.25, 0.08, 0.45f, false, 190.0);          // 2 and 4

        const double hatStep = tr.style == 1 ? beat / 3.0 : (tr.style == 0 || tr.style == 4) ? beat / 4.0 : beat / 2.0;
        for (double t = 0.0; t < barLen - 1e-6; t += hatStep)
            s.noiseHit (t0 + t, 0.05, 0.012, 0.12f, true);
    }

    juce::AudioBuffer<float> buf (2, (int) s.out.size());
    float peak = 0.0f;
    for (auto v : s.out) peak = std::max (peak, std::abs (v));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (c, i, s.out[(size_t) i] / (peak + 1e-9f) * 0.9f);
    return buf;
}

//==============================================================================
enum class KeyError { exact, relative, fifth, parallel, other };

KeyError compareKeys (const Key& got, int tonic, bool minor)
{
    if (got.tonic == tonic && got.minor == minor) return KeyError::exact;
    const int rel = minor ? (tonic + 3) % 12 : (tonic + 9) % 12;
    if (got.minor != minor && got.tonic == rel) return KeyError::relative;
    if (got.minor == minor && (got.tonic == (tonic + 7) % 12 || got.tonic == (tonic + 5) % 12)) return KeyError::fifth;
    if (got.minor != minor && got.tonic == tonic) return KeyError::parallel;
    return KeyError::other;
}

void printResult (const juce::String& name, const Result& r)
{
    std::cout << name << "\n"
              << "   KEY  " << r.key.name() << "  (" << juce::roundToInt (r.keyConfidence * 100) << " %)   alt: " << r.altKey.name() << "\n"
              << "   BPM  " << r.bpm << "  (" << juce::roundToInt (r.bpmConfidence * 100) << " %)   alt: " << r.altBpm << "\n"
              << "   tuning " << juce::roundToInt (r.tuningCents) << " cents\n";
}

int runSynth (int count, int seed)
{
    struct Config { Options opt; const char* label; };
    std::vector<Config> configs;
    const char* const profNames[] = { "Temperley", "Krumhansl", "Shaath", "Diatonic" };
    for (int p : { 0, 2, 3 })
        for (float mb : { 0.0f, 0.05f })
            for (float tp : { 0.0f, 0.1f, 0.2f, 0.4f })
            {
                Options o; o.profile = p; o.bassWeight = 0.5f; o.minorBias = mb; o.tonicPresence = tp;
                configs.push_back ({ o, profNames[p] });
            }

    std::vector<std::array<int, 5>> keyStats (configs.size(), std::array<int, 5> {});
    int bpmExact = 0, bpmOctave = 0;
    std::array<int, 5> styleTotal {}, styleBpmOk {};
    juce::Random rng (seed);

    for (int n = 0; n < count; ++n)
    {
        Truth tr;
        tr.tonic = rng.nextInt (12);
        tr.minor = rng.nextFloat() < 0.65f;
        tr.style = rng.nextInt (5);
        static const int lo[] = { 130, 138, 60, 82, 98 }, hi[] = { 165, 146, 90, 98, 118 };
        tr.bpm = lo[tr.style] + rng.nextInt (hi[tr.style] - lo[tr.style] + 1);
        tr.cents = rng.nextFloat() < 0.25f ? (rng.nextFloat() * 60.0f - 30.0f) : 0.0f;

        const auto audio = makeBeat (tr, seed * 1000 + n, 40.0);

        // tempo + chroma once, key scored for every configuration
        double fs = 0.0, frameRate = 0.0;
        const auto x = detail::prepare (audio, 44100.0, fs);
        Result r;
        const auto onsets = detail::onsetEnvelope (x, fs, frameRate);
        detail::detectTempo (onsets, frameRate, r);
        std::array<double, 12> upper {}, bass {};
        float tuning = 0.0f;
        detail::chroma (x, fs, upper, bass, tuning);

        ++styleTotal[(size_t) tr.style];
        const bool verbose = juce::SystemStats::getEnvironmentVariable ("SWELL_VERBOSE", {}).isNotEmpty();
        if (std::abs (r.bpm - tr.bpm) <= 0.5) { ++bpmExact; ++styleBpmOk[(size_t) tr.style]; }
        else
        {
            if (std::abs (r.altBpm - tr.bpm) <= 0.5 || std::abs (r.bpm * 2.0 - tr.bpm) <= 1.0 || std::abs (r.bpm / 2.0 - tr.bpm) <= 0.5)
                ++bpmOctave;
            if (verbose)
                std::cout << "  bpm  " << styleNames[tr.style] << " " << tr.bpm << " -> " << r.bpm << " (alt " << r.altBpm << ")\n";
        }
        if (verbose)
        {
            Result kr;
            detail::detectKey (upper, bass, Options {}, kr);
            if (compareKeys (kr.key, tr.tonic, tr.minor) != KeyError::exact)
            {
                Key t; t.tonic = tr.tonic; t.minor = tr.minor;
                std::cout << "  key  " << styleNames[tr.style] << " " << t.name() << " -> " << kr.key.name()
                          << " (" << juce::roundToInt (kr.keyConfidence * 100) << " %)  prog " << progLabel << "\n";
            }
        }

        for (size_t c = 0; c < configs.size(); ++c)
        {
            Result kr;
            detail::detectKey (upper, bass, configs[c].opt, kr);
            ++keyStats[c][(size_t) compareKeys (kr.key, tr.tonic, tr.minor)];
        }
    }

    std::cout << "\n=== " << count << " generated beats ===\n\n";
    std::cout << "BPM exact: " << bpmExact << "/" << count << "  (" << juce::roundToInt (100.0 * bpmExact / count) << " %)"
              << "   half/double: " << bpmOctave << "   wrong: " << (count - bpmExact - bpmOctave) << "\n";
    for (int st = 0; st < 5; ++st)
        std::cout << "   " << styleNames[st] << ": " << styleBpmOk[(size_t) st] << "/" << styleTotal[(size_t) st] << "\n";

    std::cout << "\nKEY                               exact  relative  fifth  parallel  other\n";
    for (size_t c = 0; c < configs.size(); ++c)
    {
        const auto& k = keyStats[c];
        std::cout << juce::String (configs[c].label).paddedRight (' ', 10)
                  << " minor+ " << juce::String (configs[c].opt.minorBias, 2) << " tonic " << juce::String (configs[c].opt.tonicPresence, 2) << "   "
                  << juce::String (juce::roundToInt (100.0 * k[0] / count)).paddedLeft (' ', 4) << " %"
                  << juce::String (k[1]).paddedLeft (' ', 9) << juce::String (k[2]).paddedLeft (' ', 7)
                  << juce::String (k[3]).paddedLeft (' ', 9) << juce::String (k[4]).paddedLeft (' ', 7) << "\n";
    }
    return 0;
}

//==============================================================================
// Annotated datasets (GiantSteps layout: audio/<id>.mp3, <id>.key = "C minor", <id>.bpm = "127.0")
bool parseKey (juce::String s, int& tonic, bool& minor)
{
    s = s.trim().toLowerCase();
    if (s.isEmpty()) return false;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    const juce::juce_wchar letter = s[0];
    if (letter < 'a' || letter > 'g') return false;
    tonic = base[letter - 'a'];
    int i = 1;
    if (s[i] == '#') { ++tonic; ++i; }
    else if (s[i] == 'b' && s.length() > 2 && s[i + 1] != 'a') { --tonic; ++i; }   // "b" flat, not "bass"
    tonic = (tonic + 12) % 12;
    const auto rest = s.substring (i).trim();
    if (rest.startsWith ("min") || rest == "m") minor = true;
    else if (rest.startsWith ("maj") || rest.isEmpty()) minor = false;
    else return false;
    return true;
}

struct ChromaVariant { int harmonics; float decay, maxHz, threshold, tuningTrust; };

const std::vector<ChromaVariant>& chromaVariants()
{
    static const std::vector<ChromaVariant> v {
        { 4, 0.8f, 3500.0f, 0.05f, 0.15f },   // current engine
    };
    return v;
}

// every chroma variant crossed with every key-scoring setting
std::vector<std::pair<Options, juce::String>> keyConfigs()
{
    std::vector<std::pair<Options, juce::String>> c;
    const char* const profNames[] = { "Temperley", "Krumhansl", "Shaath", "Diatonic", "Learned" };
    for (const auto& cv : chromaVariants())
        for (int p = 0; p < 5; ++p)
            for (float bw : { 0.3f, 1.0f })
                for (float mb : { 0.0f, 0.06f })
                {
                    Options o; o.profile = p; o.bassWeight = bw; o.minorBias = mb;
                    o.harmonics = cv.harmonics; o.harmonicDecay = cv.decay; o.maxHz = cv.maxHz; o.peakThreshold = cv.threshold; o.tuningTrust = cv.tuningTrust;
                    c.push_back ({ o, "h" + juce::String (cv.harmonics) + "/" + juce::String (cv.decay, 1) + " " + juce::String ((int) cv.maxHz).paddedLeft (' ', 4)
                                      + "Hz tune " + juce::String (cv.tuningTrust, 2) + "  " + juce::String (profNames[p]).paddedRight (' ', 9)
                                      + " bass " + juce::String (bw, 1) + " minor+ " + juce::String (mb, 2) });
                }
    return c;
}

int runDataset (const juce::File& audioDir, const juce::File& keyDir, const juce::File& bpmDir)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const bool verbose = juce::SystemStats::getEnvironmentVariable ("SWELL_VERBOSE", {}).isNotEmpty();
    const auto configs = keyConfigs();

    std::vector<std::array<int, 5>> keyStats (configs.size(), std::array<int, 5> {});
    std::vector<double> mirex (configs.size(), 0.0);
    int keyCount = 0, bpmCount = 0, acc1 = 0, acc2 = 0, ourExact = 0;
    double msTotal = 0.0;

    auto files = audioDir.findChildFiles (juce::File::findFiles, false, "*.mp3;*.wav;*.flac;*.aif;*.aiff");
    files.sort();
    for (const auto& f : files)
    {
        const auto id = f.getFileNameWithoutExtension();
        int tonic = 0; bool minor = false;
        const auto keyFile = keyDir.getChildFile (id + ".key");
        const bool hasKey = keyDir.isDirectory() && keyFile.existsAsFile() && parseKey (keyFile.loadFileAsString(), tonic, minor);
        const auto bpmFile = bpmDir.getChildFile (id + ".bpm");
        const double bpmTruth = bpmDir.isDirectory() && bpmFile.existsAsFile() ? bpmFile.loadFileAsString().trim().getDoubleValue() : 0.0;
        if (! hasKey && bpmTruth <= 0.0)
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr) { std::cout << "cannot read " << f.getFileName() << "\n"; continue; }
        juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        double fs = 0.0, frameRate = 0.0;
        const auto x = detail::prepare (buf, reader->sampleRate, fs);

        if (bpmTruth > 0.0)
        {
            Result r;
            const auto onsets = detail::onsetEnvelope (x, fs, frameRate);
            detail::detectTempo (onsets, frameRate, r);
            ++bpmCount;
            const bool a1 = std::abs (r.bpm - bpmTruth) <= 0.04 * bpmTruth;
            bool a2 = a1;
            for (double m : { 2.0, 0.5, 3.0, 1.0 / 3.0 })
                a2 = a2 || std::abs (r.bpm * m - bpmTruth) <= 0.04 * bpmTruth;
            acc1 += a1 ? 1 : 0;
            acc2 += a2 ? 1 : 0;
            ourExact += std::abs (r.bpm - bpmTruth) <= 1.0 ? 1 : 0;
            if (verbose && ! a1)
                std::cout << "  bpm  " << id << "  " << bpmTruth << " -> " << r.bpm << " (alt " << r.altBpm << ")\n";
        }

        if (hasKey)
        {
            std::array<double, 12> upper {}, bass {};
            float tuning = 0.0f;
            ++keyCount;
            const Options* chromaOf = nullptr;
            for (size_t c = 0; c < configs.size(); ++c)
            {
                const auto& o = configs[c].first;
                if (chromaOf == nullptr || o.harmonics != chromaOf->harmonics || o.harmonicDecay != chromaOf->harmonicDecay
                    || o.maxHz != chromaOf->maxHz || o.peakThreshold != chromaOf->peakThreshold || o.tuningTrust != chromaOf->tuningTrust)
                {
                    detail::chroma (x, fs, upper, bass, tuning, o);   // configs are grouped by chroma variant
                    chromaOf = &o;
                }
                Result kr;
                detail::detectKey (upper, bass, o, kr);
                const auto e = compareKeys (kr.key, tonic, minor);
                ++keyStats[c][(size_t) e];
                mirex[c] += e == KeyError::exact ? 1.0 : e == KeyError::fifth ? 0.5 : e == KeyError::relative ? 0.3 : e == KeyError::parallel ? 0.2 : 0.0;
            }
            if (verbose)
            {
                Result kr;
                detail::chroma (x, fs, upper, bass, tuning, Options {});
                detail::detectKey (upper, bass, Options {}, kr);
                if (compareKeys (kr.key, tonic, minor) != KeyError::exact)
                {
                    Key t; t.tonic = tonic; t.minor = minor;
                    std::cout << "  key  " << id << "  " << t.name() << " -> " << kr.key.name()
                              << "  [+" << ((kr.key.tonic - tonic + 12) % 12) << " st]  (alt " << kr.altKey.name()
                              << ", " << juce::roundToInt (kr.keyConfidence * 100) << " %, tuning " << juce::roundToInt (tuning * 100) << ")\n";
                }
            }
        }
        msTotal += juce::Time::getMillisecondCounterHiRes() - t0;
    }

    std::cout << "\n=== " << audioDir.getParentDirectory().getFileName() << " ===\n";
    if (bpmCount > 0)
        std::cout << "TEMPO  " << bpmCount << " files   Accuracy1 (4 %): " << juce::roundToInt (100.0 * acc1 / bpmCount) << " %"
                  << "   Accuracy2 (x2, x3): " << juce::roundToInt (100.0 * acc2 / bpmCount) << " %"
                  << "   within 1 BPM: " << juce::roundToInt (100.0 * ourExact / bpmCount) << " %\n";
    if (keyCount > 0)
    {
        std::cout << "KEY    " << keyCount << " files  (15 best settings by MIREX score, then the current engine)\n"
                  << juce::String().paddedRight (' ', 62) << "exact  MIREX  same notes   relative  fifth  parallel  other\n";
        std::vector<size_t> order (configs.size());
        std::iota (order.begin(), order.end(), (size_t) 0);
        std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return mirex[a] > mirex[b]; });
        order.resize (juce::jmin ((size_t) 15, order.size()));
        order.push_back (0);
        for (size_t c : order)
        {
            const auto& k = keyStats[c];
            std::cout << configs[c].second << "   "
                      << juce::String (juce::roundToInt (100.0 * k[0] / keyCount)).paddedLeft (' ', 4) << " %"
                      << juce::String (juce::roundToInt (100.0 * mirex[c] / keyCount)).paddedLeft (' ', 5) << " %"
                      << juce::String (juce::roundToInt (100.0 * (k[0] + k[1]) / keyCount)).paddedLeft (' ', 9) << " %"
                      << juce::String (k[1]).paddedLeft (' ', 9) << juce::String (k[2]).paddedLeft (' ', 7)
                      << juce::String (k[3]).paddedLeft (' ', 9) << juce::String (k[4]).paddedLeft (' ', 7) << "\n";
        }
    }
    const int n = juce::jmax (bpmCount, keyCount);
    if (n > 0)
        std::cout << "average analysis time: " << juce::roundToInt (msTotal / n) << " ms per file\n";
    return 0;
}

// Writes one CSV row per annotated file: id, tonic, minor, 12 upper chroma, 12 bass chroma, tuning
int runFeatures (const juce::File& audioDir, const juce::File& keyDir, const juce::File& outFile)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    outFile.deleteFile();
    juce::FileOutputStream out (outFile);
    if (! out.openedOk()) { std::cerr << "cannot write " << outFile.getFullPathName() << "\n"; return 1; }

    auto files = audioDir.findChildFiles (juce::File::findFiles, false, "*.mp3;*.wav;*.flac;*.aif;*.aiff");
    files.sort();
    int n = 0;
    for (const auto& f : files)
    {
        const auto id = f.getFileNameWithoutExtension();
        int tonic = 0; bool minor = false;
        const auto keyFile = keyDir.getChildFile (id + ".key");
        if (! keyFile.existsAsFile() || ! parseKey (keyFile.loadFileAsString(), tonic, minor))
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr) continue;
        juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);

        double fs = 0.0;
        const auto x = detail::prepare (buf, reader->sampleRate, fs);
        std::array<double, 12> upper {}, bass {};
        float tuning = 0.0f;
        detail::chroma (x, fs, upper, bass, tuning, Options {});

        juce::String row;
        row << id << "," << tonic << "," << (minor ? 1 : 0);
        for (auto v : upper) row << "," << v;
        for (auto v : bass) row << "," << v;
        row << "," << tuning << "\n";
        out.writeText (row, false, false, nullptr);
        ++n;
    }
    std::cout << n << " files written to " << outFile.getFullPathName() << "\n";
    return 0;
}

std::unique_ptr<juce::AudioFormatReader> openAudio (juce::AudioFormatManager& fm, const juce::File& f)
{
    return std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (f));
}

Result analyseFile (juce::AudioFormatManager& fm, const juce::File& f, bool& ok)
{
    ok = false;
    auto reader = openAudio (fm, f);
    if (reader == nullptr)
        return {};
    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
    ok = true;
    return analyse (buf, reader->sampleRate);
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: SwellKeyBench analyse <file...> | synth <count> [seed]\n";
        return 1;
    }

    const juce::String mode (argv[1]);
    if (mode == "synth")
        return runSynth (argc > 2 ? juce::String (argv[2]).getIntValue() : 100, argc > 3 ? juce::String (argv[3]).getIntValue() : 1);

    if (mode == "dataset" && argc > 4)
    {
        auto dir = [] (const char* p) { return juce::String (p) == "-" ? juce::File() : juce::File (juce::String::fromUTF8 (p)); };
        return runDataset (dir (argv[2]), dir (argv[3]), dir (argv[4]));
    }

    if (mode == "features" && argc > 4)
        return runFeatures (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])),
                            juce::File (juce::String::fromUTF8 (argv[4])));

    if (mode == "analyse")
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        for (int i = 2; i < argc; ++i)
        {
            const juce::File f (juce::String::fromUTF8 (argv[i]));
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            bool ok = false;
            const auto r = analyseFile (fm, f, ok);
            if (! ok) { std::cout << f.getFileName() << ": cannot read\n"; continue; }
            printResult (f.getFileName(), r);
            std::cout << "   (" << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - t0) << " ms)\n";
        }
        return 0;
    }

    std::cerr << "Unknown mode\n";
    return 1;
}
