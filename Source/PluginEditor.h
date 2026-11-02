#pragma once

#include "PluginProcessor.h"
#include "SwellLookAndFeel.h"

// One of the other possible keys: name + Camelot code, and how it relates to the detected key
class AltKeyButton : public juce::Button
{
public:
    AltKeyButton() : juce::Button ("alt") {}
    void setInfo (const juce::String& name, const juce::String& camelot, const juce::String& note);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::String keyName, camelotCode, relation;
};

// "Drop your beat here" area; a click opens the file browser
class DropZone : public juce::Component
{
public:
    std::function<void()> onClick;
    juce::String hint;
    bool dragOver = false;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

class SwellKeyEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit SwellKeyEditor (SwellKeyProcessor&);
    ~SwellKeyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropZone.dragOver = true; dropZone.repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropZone.dragOver = false; dropZone.repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    enum class Mode { file, listen };

    void timerCallback() override;
    void setMode (Mode);
    void startAnalysis (const juce::File&);
    void browse();
    void refreshResult();
    void updateVisibility();
    void setPreview (SwellKeyProcessor::Preview);
    void tap();
    void sendToChordMotion (const SwellKey::Key&, double bpm);

    SwellKey::Key selectedKey() const;
    double shownBpm() const;
    static juce::String formatBpm (double);
    static juce::String relationTo (const SwellKey::Key& detected, const SwellKey::Key& other);

    void paintFileResult (juce::Graphics&);
    void paintBusy (juce::Graphics&);
    void paintListen (juce::Graphics&);

    SwellKeyProcessor& proc;
    SwellLookAndFeel lnf;
    Mode mode = Mode::file;

    juce::TextButton fileTab { "FILE" }, listenTab { "LISTEN" };
    DropZone dropZone;
    juce::TextButton newBeat { "NEW BEAT" };
    AltKeyButton alt1, alt2;
    int altIndex[2] { 1, 2 };
    juce::TextButton chordButton { "PLAY CHORD" }, scaleButton { "PLAY SCALE" };
    juce::TextButton halfButton, doubleButton, metroButton { "METRONOME" }, tapButton { "TAP TEMPO" };
    juce::TextButton copyButton, sendButton { "SEND TO CHORD MOTION" };
    juce::TextButton listenButton { "START LISTENING" }, listenSendButton { "SEND TO CHORD MOTION" };
    std::unique_ptr<juce::FileChooser> chooser;

    SwellKeyProcessor::FileResult result;
    int lastFileVersion = -1, lastListenVersion = -1;
    int selected = 0;               // 0 = detected key, 1 / 2 = the other possibilities
    double bpmFactor = 1.0, tappedBpm = 0.0;
    std::vector<double> taps;
    juce::uint32 copiedUntil = 0, sentUntil = 0, listenSentUntil = 0;
    SwellKey::Result listenResult;
    bool hasListenResult = false;
    int anim = 0;
    bool snapshotChecked = false;

    juce::Rectangle<int> stripArea, keyCard, tempoCard, listenCard, listenKeyCard, listenTempoCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellKeyEditor)
};
