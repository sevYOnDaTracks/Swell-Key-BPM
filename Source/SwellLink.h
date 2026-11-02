#pragma once

#include <juce_core/juce_core.h>

// Shared between Swell plugins: Swell Key writes its last result here, others (Chord Motion) read it.
// File: <user app data>/Swell/swell-key.json  (Windows: AppData\Roaming, Mac: ~/Library)
namespace SwellLink
{
struct KeyInfo
{
    int tonic = -1;           // 0 = C ... 11 = B
    bool minor = false;
    double bpm = 0.0;
    juce::String name;        // e.g. "C# minor"
    juce::String source;      // analysed file name
    juce::int64 time = 0;     // milliseconds since 1970

    bool isValid() const { return juce::isPositiveAndBelow (tonic, 12); }
};

inline juce::File file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Swell").getChildFile ("swell-key.json");
}

inline bool write (const KeyInfo& k)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("tonic", k.tonic);
    obj->setProperty ("minor", k.minor);
    obj->setProperty ("bpm", k.bpm);
    obj->setProperty ("name", k.name);
    obj->setProperty ("source", k.source);
    obj->setProperty ("time", k.time);
    const auto f = file();
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

inline KeyInfo read()
{
    KeyInfo k;
    const auto json = juce::JSON::parse (file());
    if (auto* obj = json.getDynamicObject())
    {
        k.tonic = (int) obj->getProperty ("tonic");
        k.minor = (bool) obj->getProperty ("minor");
        k.bpm = (double) obj->getProperty ("bpm");
        k.name = obj->getProperty ("name").toString();
        k.source = obj->getProperty ("source").toString();
        k.time = (juce::int64) obj->getProperty ("time");
    }
    return k;
}
} // namespace SwellLink
