#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "KeyEngine.h"

// Swell Key: finds the key and tempo of a dropped file, or of what plays through it (LISTEN).
// Audio passes through untouched; the chord / scale / metronome previews are mixed on top.
class SwellKeyProcessor : public juce::AudioProcessor
{
public:
    SwellKeyProcessor();
    ~SwellKeyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Swell Key"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    //==============================================================================
    // FILE mode
    struct FileResult
    {
        SwellKey::Result result;
        juce::String fileName;
        double seconds = 0.0, analysisMs = 0.0;
        std::vector<float> overview;   // peak per slice, for the waveform strip
        bool ok = false;
        juce::String error;
    };
    void analyseFile (const juce::File&);
    bool isAnalysing() const { return analysing.load(); }
    FileResult getFileResult() const;
    int getFileVersion() const { return fileVersion.load(); }

    // LISTEN mode
    void setListening (bool);
    bool isListening() const { return listening.load(); }
    float getListenSeconds() const;
    void updateListenAnalysis();   // call regularly from the message thread
    bool getListenResult (SwellKey::Result&) const;
    int getListenVersion() const { return listenVersion.load(); }
    float getInputLevel() const { return inputLevel.load(); }

    // Previews mixed into the output
    enum class Preview { none, chord, scale, metronome };
    void setPreview (Preview, int tonic, bool minor, double bpm);
    Preview getPreview() const { return (Preview) preview.load(); }

private:
    void renderPreview (juce::AudioBuffer<float>&, int numSamples);

    juce::ThreadPool pool { 1 };
    juce::AudioFormatManager formats;

    mutable juce::CriticalSection resultLock;
    FileResult fileResult;
    std::atomic<int> fileVersion { 0 };
    std::atomic<bool> analysing { false };

    // LISTEN: mono ring buffer of the last 60 s of input
    std::vector<float> ring;
    std::atomic<int> ringWrite { 0 }, ringFilled { 0 };
    std::atomic<bool> listening { false }, listenBusy { false };
    SwellKey::Result listenResult;
    bool hasListenResult = false;
    std::atomic<int> listenVersion { 0 };
    std::atomic<juce::int64> heardSamples { 0 };
    juce::int64 lastAnalysedAt = 0;
    std::atomic<float> inputLevel { 0.0f };

    // Previews
    std::atomic<int> preview { 0 }, previewTonic { 0 };
    std::atomic<bool> previewMinor { true };
    std::atomic<double> previewBpm { 120.0 };
    double sr = 44100.0;
    double phase[4] {};
    double previewClock = 0.0;
    int lastPreview = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellKeyProcessor)
};
