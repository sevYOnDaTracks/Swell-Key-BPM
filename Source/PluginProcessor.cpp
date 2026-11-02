#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SwellLink.h"

SwellKeyProcessor::SwellKeyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
}

SwellKeyProcessor::~SwellKeyProcessor()
{
    pool.removeAllJobs (true, 10000);
}

bool SwellKeyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void SwellKeyProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    ring.assign ((size_t) (sampleRate * 60.0), 0.0f);
    ringWrite = 0;
    ringFilled = 0;
    heardSamples = 0;
    lastAnalysedAt = 0;
}

void SwellKeyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    inputLevel.store (juce::jmax (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n)));

    // LISTEN: keep the last 60 s of input (mono), before the previews are added
    if (listening.load() && ! ring.empty())
    {
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        const int size = (int) ring.size();
        int w = ringWrite.load();
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) w] = 0.5f * (l[i] + r[i]);
            if (++w >= size) w = 0;
        }
        ringWrite.store (w);
        ringFilled.store (juce::jmin (size, ringFilled.load() + n));
        heardSamples += n;
    }

    renderPreview (buffer, n);
}

//==============================================================================
void SwellKeyProcessor::setPreview (Preview p, int tonic, bool minor, double bpm)
{
    previewTonic.store (tonic);
    previewMinor.store (minor);
    previewBpm.store (juce::jlimit (40.0, 300.0, bpm));
    preview.store ((int) p);
}

void SwellKeyProcessor::renderPreview (juce::AudioBuffer<float>& buffer, int n)
{
    const int p = preview.load();
    if (p != lastPreview)
    {
        lastPreview = p;
        previewClock = 0.0;
        for (auto& ph : phase) ph = 0.0;
    }
    if (p == (int) Preview::none)
        return;

    const int tonic = previewTonic.load();
    const bool minor = previewMinor.load();
    const double twoPi = juce::MathConstants<double>::twoPi;
    auto hz = [] (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); };
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    static const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2 };
    static const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10, 12, 10, 8, 7, 5, 3, 2 };

    for (int i = 0; i < n; ++i)
    {
        double v = 0.0;
        const double t = previewClock;

        if (p == (int) Preview::chord)
        {
            // root, third, fifth and octave of the key's tonic chord, soft attack
            const double root = 48 + tonic;
            const double notes[] = { root, root + (minor ? 3 : 4), root + 7, root + 12 };
            const double env = juce::jmin (1.0, t / 0.03);
            for (int k = 0; k < 4; ++k)
            {
                phase[k] += twoPi * hz (notes[k]) / sr;
                v += (std::sin (phase[k]) + 0.3 * std::sin (2.0 * phase[k])) * 0.07 * env;
            }
        }
        else if (p == (int) Preview::scale)
        {
            // the scale up and down, one note every 0.28 s
            const double step = 0.28;
            const int idx = (int) (t / step) % 14;
            const double local = std::fmod (t, step);
            const double midi = 60 + tonic + (minor ? minorSteps[idx] : majorSteps[idx]);
            phase[0] += twoPi * hz (midi) / sr;
            const double env = juce::jmin (1.0, local / 0.005) * std::exp (-local * 5.0);
            v = (std::sin (phase[0]) + 0.25 * std::sin (2.0 * phase[0])) * 0.22 * env;
        }
        else
        {
            // metronome, accent on the first beat of the bar
            const double beat = 60.0 / previewBpm.load();
            const double local = std::fmod (t, beat);
            const bool accent = ((int) (t / beat) % 4) == 0;
            if (local < 0.04)
                v = std::sin (twoPi * (accent ? 1600.0 : 1000.0) * local) * std::exp (-local * 120.0) * 0.35;
        }

        l[i] += (float) v;
        r[i] += (float) v;
        previewClock += 1.0 / sr;
    }
}

//==============================================================================
void SwellKeyProcessor::analyseFile (const juce::File& file)
{
    if (analysing.exchange (true))
        return;

    pool.addJob ([this, file]
    {
        FileResult fr;
        fr.fileName = file.getFileName();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            fr.error = "Cannot read this file";
        }
        else
        {
            juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            fr.seconds = audio.getNumSamples() / reader->sampleRate;

            // waveform overview for the file strip (RMS per slice: peaks are flat on mastered beats)
            constexpr int slices = 110;
            const int per = juce::jmax (1, audio.getNumSamples() / slices);
            float loudest = 1.0e-6f;
            for (int s = 0; s < slices; ++s)
            {
                float m = 0.0f;
                for (int c = 0; c < audio.getNumChannels(); ++c)
                    m = juce::jmax (m, audio.getRMSLevel (c, s * per, juce::jmin (per, audio.getNumSamples() - s * per)));
                fr.overview.push_back (m);
                loudest = juce::jmax (loudest, m);
            }
            for (auto& m : fr.overview) m = std::pow (m / loudest, 1.5f);

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            fr.result = SwellKey::analyse (audio, reader->sampleRate);
            fr.analysisMs = juce::Time::getMillisecondCounterHiRes() - t0;
            fr.ok = true;

            // share the result with the other Swell plugins (Chord Motion's SWELL KEY button)
            SwellLink::KeyInfo k;
            k.tonic = fr.result.key.tonic;
            k.minor = fr.result.key.minor;
            k.bpm = fr.result.bpm;
            k.name = fr.result.key.name();
            k.source = fr.fileName;
            k.time = juce::Time::currentTimeMillis();
            SwellLink::write (k);
        }

        {
            const juce::ScopedLock sl (resultLock);
            fileResult = std::move (fr);
        }
        ++fileVersion;
        analysing.store (false);
    });
}

SwellKeyProcessor::FileResult SwellKeyProcessor::getFileResult() const
{
    const juce::ScopedLock sl (resultLock);
    return fileResult;
}

//==============================================================================
void SwellKeyProcessor::setListening (bool on)
{
    if (on && ! listening.load())
    {
        ringFilled = 0;
        heardSamples = 0;
        lastAnalysedAt = 0;
        const juce::ScopedLock sl (resultLock);
        hasListenResult = false;
        ++listenVersion;
    }
    listening.store (on);
}

float SwellKeyProcessor::getListenSeconds() const
{
    return (float) (ringFilled.load() / sr);
}

void SwellKeyProcessor::updateListenAnalysis()
{
    if (! listening.load() || listenBusy.load() || ring.empty())
        return;

    // a new estimate every 2 s once 4 s have been heard (up to the last 60 s)
    const int filled = ringFilled.load();
    const auto heard = heardSamples.load();
    if (filled < (int) (sr * 4.0) || heard - lastAnalysedAt < (juce::int64) (sr * 2.0))
        return;
    lastAnalysedAt = heard;

    const int size = (int) ring.size();
    const int start = (ringWrite.load() - filled + size) % size;
    auto audio = std::make_shared<juce::AudioBuffer<float>> (1, filled);
    for (int i = 0; i < filled; ++i)
        audio->setSample (0, i, ring[(size_t) ((start + i) % size)]);

    listenBusy.store (true);
    const double rate = sr;
    pool.addJob ([this, audio, rate]
    {
        const auto r = SwellKey::analyse (*audio, rate);
        {
            const juce::ScopedLock sl (resultLock);
            listenResult = r;
            hasListenResult = true;
        }
        ++listenVersion;
        listenBusy.store (false);
    });
}

bool SwellKeyProcessor::getListenResult (SwellKey::Result& out) const
{
    const juce::ScopedLock sl (resultLock);
    out = listenResult;
    return hasListenResult;
}

juce::AudioProcessorEditor* SwellKeyProcessor::createEditor()
{
    return new SwellKeyEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwellKeyProcessor();
}
