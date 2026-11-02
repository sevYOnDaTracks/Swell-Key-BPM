#include "PluginEditor.h"
#include "SwellLink.h"

using namespace SwellColours;

namespace
{
const juce::Colour cardBg { 0xff1b1c20 }, cardBorder { 0xff26272c }, labelCol { 0xffc8c6cd }, soft { 0xffb4b2ba };
const juce::Colour sureCol { 0xff7fd18b }, likelyCol { 0xfff5c46b }, checkCol { 0xffff9d7a };

void card (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (cardBg);
    g.fillRoundedRectangle (r.toFloat(), 14.0f);
    g.setColour (cardBorder);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
}

void caption (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c = labelCol)
{
    g.setColour (c);
    g.setFont (swellFont (12.0f, true));
    g.drawText (t, r, juce::Justification::centredLeft);
}

void badge (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> rightEdge)
{
    g.setFont (swellFont (11.0f, true));
    const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (swellFont (11.0f, true), t)) + 16;
    auto b = rightEdge.removeFromRight (w).withSizeKeepingCentre (w, 20);
    g.setColour (juce::Colour (0xff2e2117));
    g.fillRoundedRectangle (b.toFloat(), 5.0f);
    g.setColour (orange);
    g.drawText (t, b, juce::Justification::centred);
}

void meter (juce::Graphics& g, juce::Rectangle<int> r, float value)
{
    g.setColour (juce::Colour (0xff2c2d33));
    g.fillRoundedRectangle (r.toFloat(), 3.0f);
    g.setColour (orange);
    g.fillRoundedRectangle (r.toFloat().withWidth ((float) r.getWidth() * juce::jlimit (0.0f, 1.0f, value)), 3.0f);
}

// "Sure" / "Likely" / "Check by ear" from a calibrated confidence
std::pair<juce::String, juce::Colour> confidenceWord (float c)
{
    if (c >= 0.75f) return { "Sure", sureCol };
    if (c >= 0.6f)  return { "Likely", likelyCol };
    return { "Check by ear", checkCol };
}

void setupButton (juce::TextButton& b, const juce::String& style = {})
{
    if (style.isNotEmpty())
        b.getProperties().set ("style", style);
    b.setColour (juce::TextButton::textColourOnId, style == "soft" ? orange : background);
}
} // namespace

//==============================================================================
void AltKeyButton::setInfo (const juce::String& name, const juce::String& camelot, const juce::String& note)
{
    keyName = name;
    camelotCode = camelot;
    relation = note;
    repaint();
}

void AltKeyButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (over ? juce::Colour (0xff222328) : juce::Colour (0xff17181c));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (over ? orange.withAlpha (0.6f) : borderHi);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    auto t = getLocalBounds().reduced (12, 0);
    g.setColour (SwellColours::text);
    g.setFont (swellFont (13.5f, true));
    const int nameW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (swellFont (13.5f, true), keyName));
    g.drawText (keyName, t.removeFromLeft (nameW + 2), juce::Justification::centredLeft);
    g.setColour (soft);
    g.setFont (swellFont (12.5f));
    g.drawText ("  " + camelotCode, t, juce::Justification::centredLeft);
    g.setFont (swellFont (11.0f));
    g.drawText (relation, t, juce::Justification::centredRight);
}

void DropZone::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (dragOver ? juce::Colour (0xff221a14) : juce::Colour (0xff17181c));
    g.fillRoundedRectangle (r, 16.0f);

    juce::Path outline;
    outline.addRoundedRectangle (r, 16.0f);
    juce::Path dashed;
    const float dashes[] = { 8.0f, 6.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (dragOver ? orange : juce::Colour (0xff4a3424));
    g.fillPath (dashed);

    const auto c = r.getCentre();
    auto icon = juce::Drawable::parseSVGPath ("M12 3v12M7 10l5 5 5-5M4 15v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4");
    icon.applyTransform (juce::AffineTransform::scale (2.4f).translated (c.x - 28.8f, c.y - 100.0f));
    g.setColour (orange);
    g.strokePath (icon, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (text);
    g.setFont (swellFont (24.0f, true));
    g.drawText ("Drop your beat here", r.withY (c.y - 22.0f).withHeight (30.0f), juce::Justification::centred);
    g.setColour (labelCol);
    g.setFont (swellFont (14.0f));
    g.drawText (juce::String::fromUTF8 ("WAV, MP3, AIFF, FLAC  \xc2\xb7  or click to browse"), r.withY (c.y + 16.0f).withHeight (20.0f), juce::Justification::centred);
    g.setColour (hint.startsWith ("Cannot") ? checkCol : soft);
    g.setFont (swellFont (12.0f));
    g.drawText (hint.isNotEmpty() ? hint : "Key and BPM in about one second", r.withY (c.y + 44.0f).withHeight (18.0f), juce::Justification::centred);
}

//==============================================================================
SwellKeyEditor::SwellKeyEditor (SwellKeyProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (auto* t : { &fileTab, &listenTab })
    {
        t->setClickingTogglesState (false);
        t->getProperties().set ("corner", 7.0f);
        setupButton (*t);
        addAndMakeVisible (t);
    }
    fileTab.onClick = [this] { setMode (Mode::file); };
    listenTab.onClick = [this] { setMode (Mode::listen); };

    dropZone.onClick = [this] { browse(); };
    addChildComponent (dropZone);

    newBeat.onClick = [this]
    {
        setPreview (SwellKeyProcessor::Preview::none);
        result = {};
        updateVisibility();
        repaint();
    };
    addChildComponent (newBeat);

    alt1.onClick = [this] { selected = altIndex[0]; refreshResult(); };
    alt2.onClick = [this] { selected = altIndex[1]; refreshResult(); };
    addChildComponent (alt1);
    addChildComponent (alt2);

    for (auto* b : { &chordButton, &scaleButton, &metroButton })
    {
        setupButton (*b, "soft");
        b->setClickingTogglesState (false);
        addChildComponent (b);
    }
    chordButton.onClick = [this] { setPreview (proc.getPreview() == SwellKeyProcessor::Preview::chord ? SwellKeyProcessor::Preview::none : SwellKeyProcessor::Preview::chord); };
    scaleButton.onClick = [this] { setPreview (proc.getPreview() == SwellKeyProcessor::Preview::scale ? SwellKeyProcessor::Preview::none : SwellKeyProcessor::Preview::scale); };
    metroButton.onClick = [this] { setPreview (proc.getPreview() == SwellKeyProcessor::Preview::metronome ? SwellKeyProcessor::Preview::none : SwellKeyProcessor::Preview::metronome); };

    halfButton.onClick = [this] { tappedBpm = 0.0; bpmFactor *= 0.5; refreshResult(); };
    doubleButton.onClick = [this] { tappedBpm = 0.0; bpmFactor *= 2.0; refreshResult(); };
    tapButton.onClick = [this] { tap(); };
    for (auto* b : { &halfButton, &doubleButton, &tapButton })
    {
        setupButton (*b);
        addChildComponent (b);
    }

    setupButton (copyButton, "primary");
    copyButton.setColour (juce::TextButton::textColourOffId, background);
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (selectedKey().name() + juce::String::fromUTF8 (" \xc2\xb7 ") + formatBpm (shownBpm()) + " BPM");
        copiedUntil = juce::Time::getMillisecondCounter() + 1500;
        refreshResult();
    };
    sendButton.onClick = [this] { sendToChordMotion (selectedKey(), shownBpm()); sentUntil = juce::Time::getMillisecondCounter() + 1500; refreshResult(); };
    setupButton (sendButton);
    addChildComponent (copyButton);
    addChildComponent (sendButton);

    setupButton (listenButton, "soft");
    listenButton.onClick = [this] { proc.setListening (! proc.isListening()); lastListenVersion = -1; repaint(); };
    setupButton (listenSendButton);
    listenSendButton.onClick = [this]
    {
        if (hasListenResult)
        {
            sendToChordMotion (listenResult.key, listenResult.bpm);
            listenSentUntil = juce::Time::getMillisecondCounter() + 1500;
        }
    };
    addChildComponent (listenButton);
    addChildComponent (listenSendButton);

    setSize (760, 620);
    setMode (Mode::file);
    startTimerHz (30);
}

SwellKeyEditor::~SwellKeyEditor()
{
    proc.setPreview (SwellKeyProcessor::Preview::none, 0, false, 120.0);
    setLookAndFeel (nullptr);
}

//==============================================================================
SwellKey::Key SwellKeyEditor::selectedKey() const
{
    const auto& r = result.result;
    return selected == 1 ? r.altKey : selected == 2 ? r.thirdKey : r.key;
}

double SwellKeyEditor::shownBpm() const
{
    return tappedBpm > 0.0 ? tappedBpm : result.result.bpm * bpmFactor;
}

juce::String SwellKeyEditor::formatBpm (double b)
{
    return std::abs (b - std::round (b)) < 0.05 ? juce::String (juce::roundToInt (b)) : juce::String (b, 1);
}

juce::String SwellKeyEditor::relationTo (const SwellKey::Key& d, const SwellKey::Key& o)
{
    if (d.tonic == o.tonic && d.minor == o.minor) return "detected";
    const int rel = d.minor ? (d.tonic + 3) % 12 : (d.tonic + 9) % 12;
    if (o.minor != d.minor && o.tonic == rel) return "same notes";
    if (o.minor == d.minor && (o.tonic == (d.tonic + 7) % 12 || o.tonic == (d.tonic + 5) % 12)) return "close key (fifth)";
    if (o.minor != d.minor && o.tonic == d.tonic) return "same root";
    return {};
}

void SwellKeyEditor::setMode (Mode m)
{
    mode = m;
    fileTab.setToggleState (m == Mode::file, juce::dontSendNotification);
    listenTab.setToggleState (m == Mode::listen, juce::dontSendNotification);
    if (m == Mode::file)
        proc.setListening (false);
    setPreview (SwellKeyProcessor::Preview::none);
    updateVisibility();
    repaint();
}

void SwellKeyEditor::startAnalysis (const juce::File& f)
{
    setPreview (SwellKeyProcessor::Preview::none);
    selected = 0;
    bpmFactor = 1.0;
    tappedBpm = 0.0;
    taps.clear();
    proc.analyseFile (f);
    updateVisibility();
    repaint();
}

void SwellKeyEditor::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a beat", juce::File(), "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  startAnalysis (fc.getResult());
                          });
}

bool SwellKeyEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;ogg"))
            return true;
    return false;
}

void SwellKeyEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropZone.dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;ogg"))
        {
            setMode (Mode::file);
            startAnalysis (juce::File (f));
            return;
        }
}

void SwellKeyEditor::setPreview (SwellKeyProcessor::Preview p)
{
    const auto k = mode == Mode::listen && hasListenResult ? listenResult.key : selectedKey();
    proc.setPreview (p, k.tonic, k.minor, mode == Mode::listen ? listenResult.bpm : shownBpm());
    chordButton.setToggleState (p == SwellKeyProcessor::Preview::chord, juce::dontSendNotification);
    scaleButton.setToggleState (p == SwellKeyProcessor::Preview::scale, juce::dontSendNotification);
    metroButton.setToggleState (p == SwellKeyProcessor::Preview::metronome, juce::dontSendNotification);
}

void SwellKeyEditor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! taps.empty() && now - taps.back() > 2.5)
        taps.clear();
    taps.push_back (now);
    if (taps.size() > 8)
        taps.erase (taps.begin());
    if (taps.size() >= 3)
        tappedBpm = 60.0 * (double) (taps.size() - 1) / (taps.back() - taps.front());
    refreshResult();
}

void SwellKeyEditor::sendToChordMotion (const SwellKey::Key& k, double bpm)
{
    SwellLink::KeyInfo info;
    info.tonic = k.tonic;
    info.minor = k.minor;
    info.bpm = bpm;
    info.name = k.name();
    info.source = result.fileName;
    info.time = juce::Time::currentTimeMillis();
    SwellLink::write (info);
}

//==============================================================================
void SwellKeyEditor::refreshResult()
{
    const auto& r = result.result;
    const SwellKey::Key keys[] = { r.key, r.altKey, r.thirdKey };
    int slot = 0;
    for (int i = 0; i < 3 && slot < 2; ++i)
    {
        if (i == selected) continue;
        altIndex[slot] = i;
        (slot == 0 ? alt1 : alt2).setInfo (keys[i].name(), keys[i].camelot(), relationTo (r.key, keys[i]));
        ++slot;
    }

    const double bpm = shownBpm();
    halfButton.setButtonText (juce::String::fromUTF8 ("\xc2\xbd  \xc2\xb7  ") + formatBpm (bpm * 0.5));
    doubleButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97" "2  \xc2\xb7  ") + formatBpm (bpm * 2.0));

    const bool copied = juce::Time::getMillisecondCounter() < copiedUntil;
    copyButton.setButtonText (copied ? "COPIED!" : "COPY  " + selectedKey().name().toUpperCase() + juce::String::fromUTF8 ("  \xc2\xb7  ") + formatBpm (bpm) + " BPM");
    sendButton.setButtonText (juce::Time::getMillisecondCounter() < sentUntil ? "SENT!  Click SWELL KEY in Chord Motion" : "SEND TO CHORD MOTION");

    // keep a playing preview in the chosen key / tempo
    if (proc.getPreview() != SwellKeyProcessor::Preview::none)
        setPreview (proc.getPreview());
    repaint();
}

void SwellKeyEditor::updateVisibility()
{
    const bool fileMode = mode == Mode::file;
    const bool busy = proc.isAnalysing();
    const bool done = fileMode && ! busy && result.ok;

    dropZone.hint = result.error;
    dropZone.setVisible (fileMode && ! busy && ! result.ok);
    for (auto* c : std::initializer_list<juce::Component*> { &newBeat, &alt1, &alt2, &chordButton, &scaleButton, &halfButton, &doubleButton,
                                                             &metroButton, &tapButton, &copyButton, &sendButton })
        c->setVisible (done);
    listenButton.setVisible (! fileMode);
    listenSendButton.setVisible (! fileMode);
}

void SwellKeyEditor::timerCallback()
{
    ++anim;

    // Dev helper for README pictures: SWELL_SNAPSHOT=<png> [SWELL_SNAPSHOT_FILE=<beat>] [SWELL_SNAPSHOT_MODE=listen|empty]
    if (! snapshotChecked)
    {
        snapshotChecked = true;
        const auto out = juce::SystemStats::getEnvironmentVariable ("SWELL_SNAPSHOT", {});
        if (out.isNotEmpty())
        {
            const auto beat = juce::SystemStats::getEnvironmentVariable ("SWELL_SNAPSHOT_FILE", {});
            const auto m = juce::SystemStats::getEnvironmentVariable ("SWELL_SNAPSHOT_MODE", {});
            if (m == "listen") setMode (Mode::listen);
            else if (beat.isNotEmpty() && m != "empty") startAnalysis (juce::File (beat));
            juce::Timer::callAfterDelay (4000, [sp = juce::Component::SafePointer<SwellKeyEditor> (this), out]
            {
                if (sp == nullptr) return;
                juce::FileOutputStream stream { juce::File (out) };
                if (stream.openedOk())
                {
                    stream.setPosition (0);
                    stream.truncate();
                    juce::PNGImageFormat().writeImageToStream (sp->createComponentSnapshot (sp->getLocalBounds(), true, 2.0f), stream);
                }
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
    }

    if (const int v = proc.getFileVersion(); v != lastFileVersion)
    {
        lastFileVersion = v;
        if (v > 0)
            result = proc.getFileResult();
        refreshResult();
        updateVisibility();
    }

    if (mode == Mode::listen)
    {
        proc.updateListenAnalysis();
        if (const int v = proc.getListenVersion(); v != lastListenVersion)
        {
            lastListenVersion = v;
            hasListenResult = proc.getListenResult (listenResult);
        }
        listenButton.setButtonText (proc.isListening() ? "STOP" : "START LISTENING");
        listenButton.setToggleState (proc.isListening(), juce::dontSendNotification);
        listenSendButton.setEnabled (hasListenResult);
        listenSendButton.setButtonText (juce::Time::getMillisecondCounter() < listenSentUntil ? "SENT!" : "SEND TO CHORD MOTION");
        repaint();
    }
    else if (proc.isAnalysing())
    {
        updateVisibility();
        repaint();
    }

    if ((copiedUntil != 0 && juce::Time::getMillisecondCounter() > copiedUntil) || (sentUntil != 0 && juce::Time::getMillisecondCounter() > sentUntil))
    {
        copiedUntil = sentUntil = 0;
        refreshResult();
    }
}

//==============================================================================
void SwellKeyEditor::resized()
{
    fileTab.setBounds (286, 15, 92, 34);
    listenTab.setBounds (382, 15, 100, 34);

    auto body = juce::Rectangle<int> (20, 82, 720, 486);
    dropZone.setBounds (body);

    // FILE result
    stripArea = juce::Rectangle<int> (20, 82, 720, 62);
    newBeat.setBounds (stripArea.getRight() - 14 - 116, stripArea.getY() + 12, 116, 38);
    keyCard = juce::Rectangle<int> (20, 158, 353, 346);
    tempoCard = juce::Rectangle<int> (387, 158, 353, 346);

    const auto k = keyCard.reduced (18);
    alt1.setBounds (k.getX(), k.getY() + 148, k.getWidth(), 36);
    alt2.setBounds (k.getX(), k.getY() + 190, k.getWidth(), 36);
    const int half = (k.getWidth() - 8) / 2;
    chordButton.setBounds (k.getX(), k.getBottom() - 40, half, 40);
    scaleButton.setBounds (k.getRight() - half, k.getBottom() - 40, half, 40);

    const auto t = tempoCard.reduced (18);
    halfButton.setBounds (t.getX(), t.getY() + 164, (t.getWidth() - 6) / 2, 36);
    doubleButton.setBounds (t.getRight() - (t.getWidth() - 6) / 2, t.getY() + 164, (t.getWidth() - 6) / 2, 36);
    metroButton.setBounds (t.getX(), t.getBottom() - 40, half, 40);
    tapButton.setBounds (t.getRight() - half, t.getBottom() - 40, half, 40);

    copyButton.setBounds (20, 518, 486, 46);
    sendButton.setBounds (516, 518, 224, 46);

    // LISTEN
    listenCard = juce::Rectangle<int> (20, 82, 353, 486);
    listenKeyCard = juce::Rectangle<int> (387, 82, 353, 236);
    listenTempoCard = juce::Rectangle<int> (387, 332, 353, 236);
    listenButton.setBounds (listenCard.getCentreX() - 90, listenCard.getBottom() - 70, 180, 42);
    listenSendButton.setBounds (listenKeyCard.getX() + 18, listenKeyCard.getBottom() - 54, listenKeyCard.getWidth() - 36, 36);
}

void SwellKeyEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Header
    g.setColour (juce::Colour (0xff17181c));
    g.fillRect (0, 0, getWidth(), 64);
    g.setColour (cardBorder);
    g.drawHorizontalLine (64, 0.0f, (float) getWidth());
    drawSwellLogo (g, { 20.0f, 14.0f, 36.0f, 36.0f });
    g.setColour (text);
    g.setFont (swellFont (20.0f, true));
    g.drawText ("SWELL", 66, 13, 150, 22, juce::Justification::centredLeft);
    g.setColour (orange);
    g.setFont (swellFont (10.5f, true));
    g.drawText ("K E Y   &   B P M", 66, 35, 150, 14, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff0f1012));
    g.fillRoundedRectangle (282.0f, 11.0f, 204.0f, 42.0f, 10.0f);
    g.setColour (cardBorder);
    g.drawRoundedRectangle (282.5f, 11.5f, 203.0f, 41.0f, 10.0f, 1.0f);

    if (mode == Mode::listen)
        paintListen (g);
    else if (proc.isAnalysing())
        paintBusy (g);
    else if (result.ok)
        paintFileResult (g);

    // Footer
    g.setColour (cardBorder);
    g.drawHorizontalLine (586, 0.0f, (float) getWidth());
    g.setColour (soft);
    g.setFont (swellFont (12.0f));
    g.drawText (juce::String::fromUTF8 ("SWELL Key  \xc2\xb7  v") + JucePlugin_VersionString, 20, 586, 300, 34, juce::Justification::centredLeft);
    drawSwellLogo (g, { (float) getWidth() - 40.0f, 594.0f, 20.0f, 18.0f });
    g.drawText ("by @sevyondatracks", getWidth() - 250, 586, 202, 34, juce::Justification::centredRight);
}

void SwellKeyEditor::paintBusy (juce::Graphics& g)
{
    const auto body = juce::Rectangle<int> (20, 82, 720, 486);
    card (g, body);
    const auto c = body.getCentre();
    for (int i = 0; i < 9; ++i)
    {
        const float h = 14.0f + 46.0f * (0.5f + 0.5f * std::sin ((float) anim * 0.25f + (float) i * 0.7f));
        g.setColour (orange.withAlpha (0.55f + 0.05f * (float) (i % 3)));
        g.fillRoundedRectangle ((float) c.x - 58.0f + (float) i * 13.0f, (float) c.y - 30.0f - h * 0.5f, 8.0f, h, 4.0f);
    }
    g.setColour (text);
    g.setFont (swellFont (20.0f, true));
    g.drawText (juce::String::fromUTF8 ("Analysing\xe2\x80\xa6"), body.withY (c.y + 20).withHeight (26), juce::Justification::centred);
    g.setColour (labelCol);
    g.setFont (swellFont (13.0f));
    g.drawText ("listening to the whole track", body.withY (c.y + 50).withHeight (20), juce::Justification::centred);
}

void SwellKeyEditor::paintFileResult (juce::Graphics& g)
{
    const auto& r = result.result;

    // file strip: waveform overview, name and details
    card (g, stripArea);
    {
        auto wave = juce::Rectangle<int> (stripArea.getX() + 14, stripArea.getY() + 13, 220, 36);
        const auto n = result.overview.size();
        g.setColour (orange.withAlpha (0.85f));
        for (size_t i = 0; i < n; ++i)
        {
            const float x = (float) wave.getX() + (float) wave.getWidth() * (float) i / (float) n;
            const float h = 3.0f + (float) (wave.getHeight() - 3) * result.overview[i];
            g.fillRect (x, (float) wave.getCentreY() - h * 0.5f, 1.4f, h);
        }
        const int infoX = wave.getRight() + 14;
        g.setColour (text);
        g.setFont (swellFont (14.0f, true));
        g.drawText (result.fileName, infoX, stripArea.getY() + 12, 330, 20, juce::Justification::centredLeft);
        const int secs = juce::roundToInt (result.seconds);
        const auto tuning = juce::roundToInt (r.tuningCents);
        g.setColour (soft);
        g.setFont (swellFont (12.0f));
        g.drawText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                        + juce::String::fromUTF8 ("  \xc2\xb7  analysed in ") + juce::String (result.analysisMs / 1000.0, 1) + " s"
                        + juce::String::fromUTF8 ("  \xc2\xb7  tuning ") + (tuning >= 0 ? "+" : "") + juce::String (tuning) + " ct",
                    infoX, stripArea.getY() + 32, 330, 18, juce::Justification::centredLeft);
    }

    // KEY card
    card (g, keyCard);
    {
        const auto k = keyCard.reduced (18);
        const auto key = selectedKey();
        caption (g, "KEY", k.withHeight (20));
        badge (g, "Camelot " + key.camelot(), k.withHeight (20));
        g.setColour (text);
        g.setFont (swellFont (44.0f, true));
        g.drawText (key.name(), k.getX(), k.getY() + 26, k.getWidth(), 52, juce::Justification::centredLeft);

        const auto [word, colour] = selected != 0 ? std::pair<juce::String, juce::Colour> { "Your pick", orange } : confidenceWord (r.keyConfidence);
        g.setFont (swellFont (12.0f, true));
        g.setColour (colour);
        g.drawText (word, k.getX(), k.getY() + 88, 200, 16, juce::Justification::centredLeft);
        g.setColour (labelCol);
        g.drawText (selected != 0 ? "chosen" : juce::String (juce::roundToInt (r.keyConfidence * 100.0f)) + " %", k.getX(), k.getY() + 88, k.getWidth(), 16, juce::Justification::centredRight);
        meter (g, { k.getX(), k.getY() + 108, k.getWidth(), 6 }, selected != 0 ? 1.0f : r.keyConfidence);
        caption (g, "OTHER POSSIBILITIES", { k.getX(), k.getY() + 126, k.getWidth(), 16 }, soft);
    }

    // TEMPO card
    card (g, tempoCard);
    {
        const auto t = tempoCard.reduced (18);
        caption (g, "TEMPO", t.withHeight (20));
        badge (g, "4/4", t.withHeight (20));
        const auto bpmText = formatBpm (shownBpm());
        g.setColour (text);
        g.setFont (swellFont (58.0f, true));
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (swellFont (58.0f, true), bpmText));
        g.drawText (bpmText, t.getX(), t.getY() + 22, w + 4, 62, juce::Justification::centredLeft);
        g.setColour (labelCol);
        g.setFont (swellFont (16.0f, true));
        g.drawText (tappedBpm > 0.0 ? "BPM (tapped)" : "BPM", t.getX() + w + 12, t.getY() + 50, 160, 24, juce::Justification::centredLeft);

        const auto [word, colour] = confidenceWord (r.bpmConfidence);
        g.setFont (swellFont (12.0f, true));
        g.setColour (colour);
        g.drawText (word, t.getX(), t.getY() + 88, 200, 16, juce::Justification::centredLeft);
        g.setColour (labelCol);
        g.drawText (juce::String (juce::roundToInt (r.bpmConfidence * 100.0f)) + " %", t.getX(), t.getY() + 88, t.getWidth(), 16, juce::Justification::centredRight);
        meter (g, { t.getX(), t.getY() + 108, t.getWidth(), 6 }, r.bpmConfidence);
        caption (g, "HALF OR DOUBLE?", { t.getX(), t.getY() + 126, t.getWidth(), 16 }, soft);
        g.setColour (soft);
        g.setFont (swellFont (12.0f));
        g.drawFittedText ("Trap at 140 and R&B at 70 share the same groove: pick the one your DAW is set to.",
                          { t.getX(), t.getY() + 208, t.getWidth(), 36 }, juce::Justification::topLeft, 2, 1.0f);
    }
}

void SwellKeyEditor::paintListen (juce::Graphics& g)
{
    const bool on = proc.isListening();

    card (g, listenCard);
    {
        const auto c = juce::Point<float> ((float) listenCard.getCentreX(), (float) listenCard.getY() + 150.0f);
        g.setColour (on ? juce::Colour (0xff2a1d14) : juce::Colour (0xff17181c));
        g.fillEllipse (c.x - 80.0f, c.y - 80.0f, 160.0f, 160.0f);
        g.setColour (on ? orange : borderHi);
        g.drawEllipse (c.x - 80.0f, c.y - 80.0f, 160.0f, 160.0f, 2.0f);
        const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.getInputLevel()) * 1.6f);
        for (int i = 0; i < 9; ++i)
        {
            const float wob = 0.5f + 0.5f * std::sin ((float) anim * 0.3f + (float) i * 0.9f);
            const float h = on ? 10.0f + 50.0f * level * wob : 8.0f;
            g.setColour (orange.withAlpha (on ? 1.0f : 0.4f));
            g.fillRoundedRectangle (c.x - 50.0f + (float) i * 11.5f, c.y - h * 0.5f, 6.0f, h, 3.0f);
        }
        g.setColour (text);
        g.setFont (swellFont (20.0f, true));
        g.drawText (on ? juce::String::fromUTF8 ("Listening\xe2\x80\xa6") : juce::String ("Ready to listen"),
                    listenCard.withY ((int) c.y + 100).withHeight (26), juce::Justification::centred);
        g.setColour (labelCol);
        g.setFont (swellFont (13.0f));
        g.drawFittedText ("Put Swell Key on a mixer track\nand press play in your DAW",
                          listenCard.withY ((int) c.y + 132).withHeight (40).reduced (20, 0), juce::Justification::centredTop, 2);
    }

    const float heard = proc.getListenSeconds();
    card (g, listenKeyCard);
    {
        const auto k = listenKeyCard.reduced (18);
        caption (g, "KEY SO FAR", k.withHeight (20));
        if (hasListenResult)
            badge (g, "Camelot " + listenResult.key.camelot(), k.withHeight (20));
        g.setColour (hasListenResult ? text : soft);
        g.setFont (swellFont (36.0f, true));
        g.drawText (hasListenResult ? listenResult.key.name() : juce::String::fromUTF8 ("\xe2\x80\x94"), k.getX(), k.getY() + 28, k.getWidth(), 44, juce::Justification::centredLeft);
        g.setColour (soft);
        g.setFont (swellFont (12.0f));
        const auto msg = hasListenResult
            ? confidenceWord (listenResult.keyConfidence).first + "  " + juce::String (juce::roundToInt (listenResult.keyConfidence * 100)) + juce::String::fromUTF8 (" %  \xc2\xb7  ") + juce::String (juce::roundToInt (heard)) + " s heard"
            : juce::String ("needs about 4 s of music");
        g.drawText (msg, k.getX(), k.getY() + 80, k.getWidth(), 18, juce::Justification::centredLeft);
        meter (g, { k.getX(), k.getY() + 106, k.getWidth(), 6 }, juce::jmin (1.0f, heard / 20.0f));
    }

    card (g, listenTempoCard);
    {
        const auto t = listenTempoCard.reduced (18);
        caption (g, "TEMPO", t.withHeight (20));
        g.setColour (hasListenResult ? text : soft);
        g.setFont (swellFont (36.0f, true));
        g.drawText (hasListenResult ? formatBpm (listenResult.bpm) + " BPM" : juce::String::fromUTF8 ("\xe2\x80\x94"), t.getX(), t.getY() + 28, t.getWidth(), 44, juce::Justification::centredLeft);
        g.setColour (soft);
        g.setFont (swellFont (12.0f));
        g.drawText (hasListenResult ? "or " + formatBpm (listenResult.altBpm) + " BPM" : juce::String ("press START and play"), t.getX(), t.getY() + 80, t.getWidth(), 18, juce::Justification::centredLeft);
        meter (g, { t.getX(), t.getY() + 106, t.getWidth(), 6 }, hasListenResult ? listenResult.bpmConfidence : 0.0f);
    }
}
