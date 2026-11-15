/*
  Supreme Drums
  Copyright (c) 2026 Giulia Casu. All rights reserved.
  Created by Giulia Casu on 12 November 2026.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>

// An automatable parameter whose name follows the kit: "01 Kick · Volume" instead of "Pad C1 volume".
// The name can change while the plugin runs (the host is told with updateHostDisplay).
class SDParam : public juce::AudioParameterFloat
{
public:
    SDParam (const juce::String& id, const juce::String& initialName, juce::NormalisableRange<float> range, float def,
             std::function<juce::String (float, int)> toText, std::function<float (const juce::String&)> fromText = nullptr)
        : juce::AudioParameterFloat (juce::ParameterID { id, 1 }, initialName, range, def,
                                     juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText))
                                                                          .withValueFromStringFunction (std::move (fromText))),
          dynName (initialName) {}

    juce::String getName (int maximumStringLength) const override
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        return dynName.substring (0, maximumStringLength);
    }
    bool setDynamicName (const juce::String& n)
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        if (n == dynName) return false;
        dynName = n;
        return true;
    }

private:
    mutable juce::SpinLock nameLock;
    juce::String dynName;
};

// Groove settings shared with the interface (same formulas as the web UI).
struct Groove
{
    double swing = 0.0, shuffle = 0.0, lay = 0.0, humT = 0.0, humV = 0.0, dyn = 1.0;
};

struct Pattern
{
    int bars = 1;
    double bpm = 120.0;
    bool hostSync = false;
    Groove groove;
    std::vector<std::vector<int>> steps; // one row per pad, levels 0..3
    std::vector<float> mixDb;            // per pad
    std::vector<int> lay;                // 1 = snare/clap/rim (laid-back applies)
    std::vector<float> lvel, lvar, ltr, lnudge; // per line: velocity scale, velocity variation, soft transients, nudge (ms)
    std::vector<float> pan;              // per pad, -1 (left) .. +1 (right)
    std::vector<std::vector<int>> trip;  // per pad, per beat: 1 = triplet beat
    std::vector<std::vector<int>> roll;  // per pad, per step: repeats inside the step (0/1 = none)
    float masterDb = 0.0f;
};

using SampleData = std::shared_ptr<const std::vector<float>>;

struct Slot
{
    juce::String name;
    int choke = 0;
    SampleData data;
    SampleData alt;   // second recorded hit, alternated with the main one (round robin)
    SampleData soft;  // soft recorded hit, used for ghost notes
    int rr = 0;
};

class SupremeDrumsProcessor : public juce::AudioProcessor
{
public:
    SupremeDrumsProcessor();
    ~SupremeDrumsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Supreme Drums"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called on the message thread by the editor.
    void handleUiMessage (const juce::var& msg);
    juce::String getProjectJson() const;
    int getCurrentStep() const { return currentStep.load(); }
    bool isSequencerRunning() const { return seqRunning.load(); }
    double getHostBpm() const { return hostTempo.load(); }

    static double grooveBeats (int s, const Groove& g);
    static double hrand (long long a, long long b);

private:
    struct Voice
    {
        SampleData data;
        double pos = 0.0;
        float gain = 0.0f, fade = 1.0f, soft = 0.0f;
        int softPos = 0, softLen = 0;
        int slot = -1, choke = 0, delay = 0;
        float ic1 = 0.0f, ic2 = 0.0f;   // per-voice low-pass filter state (pad filter)
        bool releasing = false, active = false;
        juce::uint64 age = 0;
    };

    void trigger (int slot, float gain, int sampleOffset, float soft = 0.0f, int level = 0);
    void startVoice (SampleData data, float gain, int sampleOffset, int slot, int choke, float soft = 0.0f);
    void runSequencer (double ppqStart, double ppqEnd, int numSamples, double bpm);
    double eventPpq (long long k, int s, size_t slot, double bpm) const;
    void applyKitMessage (const juce::var& msg);
    void applyPatternMessage (const juce::var& msg);
    static SampleData decodeSamples (const juce::String& b64);

    SDParam* volume = nullptr;
    // Automatable in the host (Ableton): per pad and for the whole kit.
    static constexpr int numAutoPads = 16;
    std::array<SDParam*, numAutoPads> padVol {}, padPan {}, padTune {}, padDecay {}, padCut {};
    SDParam* masterFilter = nullptr;
    SDParam* masterDrive = nullptr;
    SDParam* swingAdd = nullptr;
public:
    // Names of pads and words for the host (sent by the interface, in its language).
    void setParamNames (const juce::var& msg);
    void refreshParamNames();
private:
    juce::StringArray padNames;
    juce::StringArray paramWords { "Volume", "Pan", "Tune", "Length", "Filter", "Master", "DJ filter", "Saturation", "Extra swing", "empty" };
    juce::IIRFilter djFilter[2];
    float lastFilter = -9.0f;
public:
    void setParamFromUi (const juce::String& id, float value, bool gesture = true);
    juce::var paramSnapshot() const;
private:

    juce::SpinLock audioLock;               // guards slots, pattern, pending triggers
    std::vector<Slot> slots;
    Pattern pattern;
    struct PendingTrigger { int slot = 0; float gain = 1.0f; int level = 0; };
    std::vector<PendingTrigger> pendingTriggers;
    // Message-thread copy of the pads: edited without touching the audio lock.
    std::vector<Slot> uiSlots;
    // Sample buffers replaced while a voice may still play them are released here, on the message thread.
    std::vector<SampleData> retired;
    // Host notes that arrive while the UI holds the lock are kept and played at the start of the next block.
    struct HeldNote { int slot = 0; float gain = 0.0f, soft = 0.0f; };
    std::array<HeldNote, 128> heldNotes {};
    int numHeldNotes = 0;
public:
    void collectGarbage();
private:
    SampleData audition;
    bool auditionPending = false;

    std::array<Voice, 64> voices;
    juce::uint64 voiceCounter = 0;
    double currentSampleRate = 44100.0;
    double previewPpq = 0.0;
    std::atomic<bool> previewOn { false };
    std::atomic<bool> previewRestart { false };
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> seqRunning { false };
    std::atomic<double> hostTempo { 0.0 };
    std::array<float, 64> mixGainL {}, mixGainR {};
    float masterGain = 1.0f;
    bool isTripletStep (size_t slot, int s) const;

    mutable juce::CriticalSection stateLock;
    juce::String projectJson, patternJson;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupremeDrumsProcessor)
};
