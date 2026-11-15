/*
  Supreme Drums
  Copyright (c) 2026 Giulia Casu. All rights reserved.
  Created by Giulia Casu on 12 November 2026.
*/

#include "PluginProcessor.h"
#include <algorithm>
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr int firstNote = 36;          // C1 in Ableton = pad 1
    constexpr double sourceRate = 44100.0; // the interface renders at 44.1 kHz
    constexpr juce::uint32 stateMagic = 0x53445231; // "SDR1"
    constexpr juce::uint32 variantMagic = 0x53445632; // "SDV2": recorded variants block

    float levelGainDb (int lv, double dyn)
    {
        return (float) (lv == 1 ? -14.0 * dyn : lv == 2 ? -4.0 * dyn : 0.0);
    }

    int levelVelocity (int lv, double dyn)
    {
        const double v = lv == 1 ? 96.0 - 51.0 * dyn : lv == 2 ? 96.0 : 96.0 + 24.0 * dyn;
        return juce::jlimit (10, 127, (int) std::lround (v));
    }
}

SupremeDrumsProcessor::SupremeDrumsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    pendingTriggers.reserve (64); // never grows on the audio thread
    retired.reserve (256);
    // Text shown by the host for each value (with units), and the reverse for typed values.
    auto dbText = [] (float v, int) { return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB"; };
    auto panText = [] (float v, int) { const int p = juce::roundToInt (v * 100.0f); return p == 0 ? juce::String ("C") : juce::String (std::abs (p)) + (p < 0 ? " L" : " R"); };
    auto stText = [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st"; };
    auto pctText = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto cutHz = [] (float v) { return 20000.0 * std::pow (150.0 / 20000.0, 1.0 - (double) v); };
    auto cutText = [cutHz] (float v, int) { if (v >= 0.999f) return juce::String ("Off"); const double hz = cutHz (v); return hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; };
    auto masterDbText = [] (float v, int) { return v <= 0.0001f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB"; };
    auto djText = [] (float v, int) { if (std::abs (v) < 0.01f) return juce::String ("Off");
        const double hz = v < 0.0f ? 20000.0 * std::pow (200.0 / 20000.0, (double) -v) : 20.0 * std::pow (5000.0 / 20.0, (double) v);
        return juce::String (v < 0.0f ? "LP " : "HP ") + (hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"); };
    auto num = [] (const juce::String& s) { return s.retainCharacters ("-+0123456789.,").replaceCharacter (',', '.').getFloatValue(); };

    addParameter (volume = new SDParam ("volume", "Master · Volume", juce::NormalisableRange<float> (0.0f, 1.5f, 0.0f, 0.6f), 1.0f, masterDbText,
                                        [num] (const juce::String& s) { return juce::Decibels::decibelsToGain (num (s)); }));
    addParameter (masterFilter = new SDParam ("filter", "Master · DJ filter", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, djText));
    addParameter (masterDrive = new SDParam ("drive", "Master · Saturation", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctText,
                                             [num] (const juce::String& s) { return num (s) / 100.0f; }));
    addParameter (swingAdd = new SDParam ("swing", "Master · Extra swing", juce::NormalisableRange<float> (0.0f, 0.5f), 0.0f, pctText,
                                          [num] (const juce::String& s) { return num (s) / 100.0f; }));
    for (int i = 0; i < numAutoPads; ++i)
    {
        const juce::String pad = juce::String (i + 1).paddedLeft ('0', 2) + " Pad " + juce::String (i + 1) + " · ";
        const juce::String id = "pad" + juce::String (i + 1) + "_";
        addParameter (padVol[(size_t) i] = new SDParam (id + "vol", pad + "Volume", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.0f, 2.5f), 0.0f, dbText,
                                                        [num] (const juce::String& s) { return num (s); }));
        addParameter (padPan[(size_t) i] = new SDParam (id + "pan", pad + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, panText,
                                                        [num] (const juce::String& s) { const float v = num (s) / 100.0f; return s.containsIgnoreCase ("l") ? -std::abs (v) : v; }));
        addParameter (padTune[(size_t) i] = new SDParam (id + "tune", pad + "Tune", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, stText,
                                                         [num] (const juce::String& s) { return num (s); }));
        addParameter (padDecay[(size_t) i] = new SDParam (id + "decay", pad + "Length", juce::NormalisableRange<float> (0.05f, 1.0f), 1.0f, pctText,
                                                          [num] (const juce::String& s) { return num (s) / 100.0f; }));
    }
    // Added in version 1.1, after every older parameter (saved sets restore parameters in this order).
    for (int i = 0; i < numAutoPads; ++i)
        addParameter (padCut[(size_t) i] = new SDParam ("pad" + juce::String (i + 1) + "_cut", juce::String (i + 1).paddedLeft ('0', 2) + " Pad " + juce::String (i + 1) + " · Filter",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, cutText));
    for (int i = 0; i < numAutoPads; ++i) padNames.add ({});
}

void SupremeDrumsProcessor::setParamNames (const juce::var& msg)
{
    if (auto* w = msg.getProperty ("words", {}).getArray())
        for (int i = 0; i < juce::jmin (w->size(), paramWords.size()); ++i)
            if ((*w)[i].toString().isNotEmpty()) paramWords.set (i, (*w)[i].toString());
    if (auto* p = msg.getProperty ("pads", {}).getArray())
        for (int i = 0; i < numAutoPads; ++i)
            padNames.set (i, i < p->size() ? (*p)[i].toString() : juce::String());
    refreshParamNames();
}

void SupremeDrumsProcessor::refreshParamNames()
{
    bool changed = false;
    for (int i = 0; i < numAutoPads; ++i)
    {
        const auto nm = padNames[i].isNotEmpty() ? padNames[i] : "(" + paramWords[9] + ")";
        const auto pre = juce::String (i + 1).paddedLeft ('0', 2) + " " + nm + " · ";
        changed |= padVol[(size_t) i]->setDynamicName (pre + paramWords[0]);
        changed |= padPan[(size_t) i]->setDynamicName (pre + paramWords[1]);
        changed |= padTune[(size_t) i]->setDynamicName (pre + paramWords[2]);
        changed |= padDecay[(size_t) i]->setDynamicName (pre + paramWords[3]);
        changed |= padCut[(size_t) i]->setDynamicName (pre + paramWords[4]);
    }
    changed |= volume->setDynamicName (paramWords[5] + " · " + paramWords[0]);
    changed |= masterFilter->setDynamicName (paramWords[5] + " · " + paramWords[6]);
    changed |= masterDrive->setDynamicName (paramWords[5] + " · " + paramWords[7]);
    changed |= swingAdd->setDynamicName (paramWords[5] + " · " + paramWords[8]);
    if (changed)
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged (true));
}

void SupremeDrumsProcessor::setParamFromUi (const juce::String& id, float value, bool gesture)
{
    for (auto* p : getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
            if (f->paramID == id)
            {
                const float v = f->getNormalisableRange().getRange().clipValue (value);
                if (std::abs (f->get() - v) > 1.0e-5f)
                {
                    // gesture start/end lets Ableton record automation and select the parameter
                    if (gesture) f->beginChangeGesture();
                    f->setValueNotifyingHost (f->convertTo0to1 (v));
                    if (gesture) f->endChangeGesture();
                }
                return;
            }
}

juce::var SupremeDrumsProcessor::paramSnapshot() const
{
    juce::Array<juce::var> vol, pan, tune, decay, cut;
    for (int i = 0; i < numAutoPads; ++i)
    {
        cut.add (padCut[(size_t) i]->get());
        vol.add (padVol[(size_t) i]->get());
        pan.add (padPan[(size_t) i]->get());
        tune.add (padTune[(size_t) i]->get());
        decay.add (padDecay[(size_t) i]->get());
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("vol", vol);
    o->setProperty ("pan", pan);
    o->setProperty ("tune", tune);
    o->setProperty ("decay", decay);
    o->setProperty ("cut", cut);
    o->setProperty ("volume", volume->get());
    o->setProperty ("filter", masterFilter->get());
    o->setProperty ("drive", masterDrive->get());
    o->setProperty ("swing", swingAdd->get());
    return juce::var (o);
}

bool SupremeDrumsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SupremeDrumsProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& v : voices)
        v.active = false;
    for (auto& f : djFilter)
        f.reset();
    lastFilter = -9.0f;
    numHeldNotes = 0;
}

//==============================================================================
double SupremeDrumsProcessor::grooveBeats (int s, const Groove& g)
{
    const int q = s % 4;
    const double d8 = g.shuffle * 0.5;
    const bool half = q >= 2;
    const double inHalf = (q % 2) * 0.25;
    const double start = half ? 0.5 + d8 : 0.0;
    const double len = half ? 0.5 - d8 : 0.5 + d8;
    double pos = start + (inHalf / 0.5) * len;
    if (q % 2 == 1)
        pos += g.swing * 0.25 * (len / 0.5);
    return pos - q * 0.25;
}

double SupremeDrumsProcessor::hrand (long long a, long long b)
{
    juce::uint32 x = ((juce::uint32) a * 73856093u) ^ ((juce::uint32) b * 19349663u) ^ 0x5bd1e995u;
    x = (x ^ (x >> 15)) * 0x2c1b3c6du;
    x = (x ^ (x >> 12)) * 0x297a2d39u;
    x ^= x >> 15;
    return x / 4294967295.0 * 2.0 - 1.0;
}

bool SupremeDrumsProcessor::isTripletStep (size_t slot, int s) const
{
    return slot < pattern.trip.size() && (size_t) (s / 4) < pattern.trip[slot].size() && pattern.trip[slot][(size_t) (s / 4)] != 0;
}

double SupremeDrumsProcessor::eventPpq (long long k, int s, size_t slot, double bpm) const
{
    Groove g = pattern.groove;
    g.swing = juce::jlimit (0.0, 0.9, g.swing + (double) swingAdd->get());
    const int q = s % 4;
    // Triplet beats place their first three cells at 0, 1/3 and 2/3 of the beat (no swing).
    double t = isTripletStep (slot, s) ? (double) (k - q) * 0.25 + q / 3.0
                                       : (double) k * 0.25 + grooveBeats (s, g);
    if (slot < pattern.lay.size() && pattern.lay[slot] != 0)
        t += g.lay / 1000.0 * bpm / 60.0;
    if (g.humT > 0.0)
        t += hrand (k, (long long) slot) * g.humT / 1000.0 * bpm / 60.0;
    if (slot < pattern.lnudge.size())
        t += pattern.lnudge[slot] / 1000.0 * bpm / 60.0;
    return t;
}

//==============================================================================
void SupremeDrumsProcessor::startVoice (SampleData data, float gain, int sampleOffset, int slot, int choke, float soft)
{
    if (data == nullptr || data->size() < 2)
        return;

    if (choke > 0)
        for (auto& v : voices)
            if (v.active && v.choke == choke && v.slot != slot)
                v.releasing = true;

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age) target = &v;
    }

    target->data = std::move (data);
    target->pos = 0.0;
    target->gain = gain;
    target->fade = 1.0f;
    target->slot = slot;
    target->choke = choke;
    target->delay = juce::jmax (0, sampleOffset);
    target->releasing = false;
    target->soft = juce::jlimit (0.0f, 0.95f, soft);
    target->softPos = 0;
    target->softLen = (int) (0.006 * currentSampleRate);
    target->active = true;
    target->age = ++voiceCounter;
    target->ic1 = target->ic2 = 0.0f;
}

void SupremeDrumsProcessor::trigger (int slot, float gain, int sampleOffset, float soft, int level)
{
    if (slot < 0 || slot >= (int) slots.size())
        return;
    auto& s = slots[(size_t) slot];
    // level: 1 = ghost, 2 = normal, 3 = accent, 0 = unknown (host notes and previews: judged from the gain)
    const bool ghost = level == 1 || (level == 0 && gain < 0.3f);
    SampleData d = s.data;
    if (ghost && s.soft != nullptr)
        d = s.soft;
    else if (s.alt != nullptr && ((++s.rr) & 1) == 0)
        d = s.alt;
    startVoice (d, gain, sampleOffset, slot, s.choke, soft);
}

void SupremeDrumsProcessor::runSequencer (double ppqStart, double ppqEnd, int numSamples, double bpm)
{
    const int total = pattern.bars * 16;
    if (total <= 0 || pattern.steps.empty() || ppqEnd <= ppqStart)
        return;

    const auto& g = pattern.groove;
    const long long k0 = juce::jmax (0LL, (long long) std::floor (ppqStart * 4.0) - 3);
    const long long k1 = (long long) std::ceil (ppqEnd * 4.0) + 3;
    const size_t rows = juce::jmin (pattern.steps.size(), slots.size());

    for (long long k = k0; k <= k1; ++k)
    {
        const int s = (int) (k % total);
        for (size_t i = 0; i < rows; ++i)
        {
            const auto& row = pattern.steps[i];
            if (s >= (int) row.size()) continue;
            const int lv = row[(size_t) s];
            if (lv <= 0) continue;
            const bool trip = isTripletStep (i, s);
            if (trip && s % 4 == 3) continue; // the fourth cell of a triplet beat is not used

            const double t0 = eventPpq (k, s, i, bpm);
            int reps = (i < pattern.roll.size() && (size_t) s < pattern.roll[i].size()) ? pattern.roll[i][(size_t) s] : 1;
            reps = juce::jlimit (1, 4, reps);
            const double sub = (trip ? 1.0 / 3.0 : 0.25) / reps;

            for (int rr = 0; rr < reps; ++rr)
            {
            const double t = t0 + rr * sub;
            if (t < ppqStart || t >= ppqEnd) continue;

            const int offset = juce::jlimit (0, numSamples - 1, (int) ((t - ppqStart) / (ppqEnd - ppqStart) * numSamples));
            float gain = juce::Decibels::decibelsToGain (levelGainDb (lv, g.dyn)) * (rr > 0 ? 0.8f : 1.0f);
            if (g.humV > 0.0)
                gain *= (float) juce::jmax (0.05, 1.0 + hrand (k, (long long) i + 101) * g.humV / 100.0);
            if (i < pattern.lvel.size())
                gain *= pattern.lvel[i];
            if (i < pattern.lvar.size() && pattern.lvar[i] > 0.0f)
                gain *= (float) juce::jmax (0.05, 1.0 + hrand (k, (long long) i + 303) * pattern.lvar[i]);
            const float tr = i < pattern.ltr.size() ? pattern.ltr[i] : 0.0f;
            trigger ((int) i, gain, offset, tr * (lv == 1 ? 0.85f : lv == 2 ? 0.4f : 0.0f), lv);
            currentStep.store (s);
            }
        }
    }
}

void SupremeDrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    bool hostPlaying = false;
    double hostBpm = 120.0, hostPpq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) { hostBpm = *b; hostTempo.store (hostBpm); }
            if (auto p = pos->getPpqPosition()) hostPpq = *p;
        }

    {
        const juce::SpinLock::ScopedTryLockType lock (audioLock);
        if (! lock.isLocked())
        {
            // The UI is swapping data right now: keep the host's notes and play them next block.
            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if (m.isNoteOn() && numHeldNotes < (int) heldNotes.size())
                    heldNotes[(size_t) numHeldNotes++] = { m.getNoteNumber() - firstNote, std::pow (m.getFloatVelocity(), 1.3f), 0.0f };
            }
        }
        if (lock.isLocked())
        {
            for (int h = 0; h < numHeldNotes; ++h)
                trigger (heldNotes[(size_t) h].slot, heldNotes[(size_t) h].gain, 0, heldNotes[(size_t) h].soft);
            numHeldNotes = 0;
            // Mixer: per-pad volume and equal-power pan, plus the master volume.
            for (size_t i = 0; i < mixGainL.size(); ++i)
            {
                const bool automated = i < (size_t) numAutoPads;
                const float db = automated ? padVol[i]->get() : (i < pattern.mixDb.size() ? pattern.mixDb[i] : 0.0f);
                const float pn = automated ? padPan[i]->get() : (i < pattern.pan.size() ? juce::jlimit (-1.0f, 1.0f, pattern.pan[i]) : 0.0f);
                const float a = (pn + 1.0f) * juce::MathConstants<float>::pi * 0.25f, gdb = juce::Decibels::decibelsToGain (db);
                mixGainL[i] = gdb * std::cos (a) * juce::MathConstants<float>::sqrt2;
                mixGainR[i] = gdb * std::sin (a) * juce::MathConstants<float>::sqrt2;
            }
            masterGain = 1.0f; // the master volume is the automatable "Volume" parameter
            for (const auto& t : pendingTriggers)
                trigger (t.slot, t.gain, 0, 0.0f, t.level);
            pendingTriggers.clear();

            if (auditionPending && audition != nullptr)
            {
                startVoice (audition, 1.0f, 0, -1, 0);
                auditionPending = false;
            }

            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if (m.isNoteOn())
                {
                    const int slot = m.getNoteNumber() - firstNote;
                    const float tr = slot >= 0 && slot < (int) pattern.ltr.size() ? pattern.ltr[(size_t) slot] : 0.0f;
                    trigger (slot, std::pow (m.getFloatVelocity(), 1.3f), metadata.samplePosition, tr * (1.0f - m.getFloatVelocity()));
                }
            }

            if (pattern.hostSync && hostPlaying)
            {
                const double len = numSamples * hostBpm / 60.0 / currentSampleRate;
                runSequencer (hostPpq, hostPpq + len, numSamples, hostBpm);
                seqRunning.store (true);
            }
            else if (previewOn.load())
            {
                if (previewRestart.exchange (false))
                    previewPpq = 0.0;
                // The preview always follows the host tempo (Ableton); the UI tempo is used only without a host.
                const double tempo = hostTempo.load() > 0.0 ? hostTempo.load() : pattern.bpm;
                const double len = numSamples * tempo / 60.0 / currentSampleRate;
                runSequencer (previewPpq, previewPpq + len, numSamples, tempo);
                previewPpq += len;
                seqRunning.store (true);
            }
            else
            {
                seqRunning.store (false);
            }
        }
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const double step = sourceRate / currentSampleRate;
    const float vol = volume->get();
    const float fadeStep = 1.0f / (float) (0.006 * currentSampleRate);

    for (auto& v : voices)
    {
        if (! v.active) continue;
        const auto& d = *v.data;
        const int len = (int) d.size();
        const bool padAuto = v.slot >= 0 && v.slot < numAutoPads;
        const double vstep = step * (padAuto ? std::pow (2.0, (double) padTune[(size_t) v.slot]->get() / 12.0) : 1.0);
        const double cut = padAuto ? (double) padDecay[(size_t) v.slot]->get() * len : (double) len;
        // pad filter (12 dB/oct low-pass, TPT state variable), bypassed when fully open
        const float cutN = padAuto ? padCut[(size_t) v.slot]->get() : 1.0f;
        const bool filt = cutN < 0.999f;
        float fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f;
        if (filt)
        {
            const double fc = juce::jmin (20000.0 * std::pow (150.0 / 20000.0, 1.0 - (double) cutN), currentSampleRate * 0.45);
            const float gq = (float) std::tan (juce::MathConstants<double>::pi * fc / currentSampleRate), k = 1.4142f;
            fa1 = 1.0f / (1.0f + gq * (gq + k)); fa2 = gq * fa1; fa3 = gq * fa2;
        }
        const double fadeLen = 0.03 * sourceRate;

        for (int i = v.delay; i < numSamples; ++i)
        {
            const int ip = (int) v.pos;
            if (ip + 1 >= len) { v.active = false; break; }
            const float fr = (float) (v.pos - ip);
            float s = d[(size_t) ip] + (d[(size_t) ip + 1] - d[(size_t) ip]) * fr;
            if (filt)
            {
                const float v3 = s - v.ic2, v1 = fa1 * v.ic1 + fa2 * v3, v2 = v.ic2 + fa2 * v.ic1 + fa3 * v3;
                v.ic1 = 2.0f * v1 - v.ic1; v.ic2 = 2.0f * v2 - v.ic2; s = v2;
            }
            float g = v.gain * vol;
            if (v.pos > cut)
            {
                const float k = 1.0f - (float) ((v.pos - cut) / fadeLen);
                if (k <= 0.0f) { v.active = false; break; }
                g *= k;
            }
            if (v.softPos < v.softLen)
            {
                g *= (1.0f - v.soft) + v.soft * (float) v.softPos / (float) v.softLen;
                ++v.softPos;
            }
            if (v.releasing)
            {
                v.fade -= fadeStep;
                if (v.fade <= 0.0f) { v.active = false; break; }
                g *= v.fade;
            }
            const size_t ps = (v.slot >= 0 && v.slot < (int) mixGainL.size()) ? (size_t) v.slot : 0;
            const float gl = g * masterGain * (v.slot >= 0 ? mixGainL[ps] : 1.0f), gr = g * masterGain * (v.slot >= 0 ? mixGainR[ps] : 1.0f);
            if (right != nullptr) { left[i] += s * gl; right[i] += s * gr; }
            else left[i] += s * 0.5f * (gl + gr);
            v.pos += vstep;
        }
        v.delay = 0;
    }

    // Kit-wide automatable effects: saturation, then a DJ filter (negative = low-pass, positive = high-pass).
    const float drive = masterDrive->get(), flt = masterFilter->get();
    if (drive > 0.001f)
    {
        const float k = 1.0f + drive * 9.0f, norm = 1.0f / std::tanh (k);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                x[i] = std::tanh (x[i] * k) * norm;
        }
    }
    if (std::abs (flt) > 0.01f)
    {
        if (std::abs (flt - lastFilter) > 0.001f)
        {
            const auto co = flt < 0.0f
                ? juce::IIRCoefficients::makeLowPass (currentSampleRate, 20000.0 * std::pow (200.0 / 20000.0, (double) -flt), 0.9)
                : juce::IIRCoefficients::makeHighPass (currentSampleRate, 20.0 * std::pow (5000.0 / 20.0, (double) flt), 0.9);
            for (auto& f : djFilter)
                f.setCoefficients (co);
            lastFilter = flt;
        }
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            djFilter[ch].processSamples (buffer.getWritePointer (ch), numSamples);
    }
    else if (lastFilter != -9.0f)
    {
        for (auto& f : djFilter)
            f.reset();
        lastFilter = -9.0f;
    }
}

//==============================================================================
SampleData SupremeDrumsProcessor::decodeSamples (const juce::String& b64)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64))
        return {};
    const size_t n = mo.getDataSize() / 2;
    auto v = std::make_shared<std::vector<float>> (n + 1, 0.0f);
    const auto* p = static_cast<const juce::uint8*> (mo.getData());
    for (size_t i = 0; i < n; ++i)
    {
        const auto raw = (juce::int16) (juce::uint16) (p[2 * i] | (p[2 * i + 1] << 8));
        (*v)[i] = raw / 32768.0f;
    }
    return v;
}

void SupremeDrumsProcessor::applyKitMessage (const juce::var& msg)
{
    const int n = juce::jlimit (0, 64, (int) msg.getProperty ("n", 0));
    std::vector<Slot> next = uiSlots;
    next.resize ((size_t) n);

    if (auto* arr = msg.getProperty ("slots", {}).getArray())
        for (const auto& s : *arr)
        {
            const int i = (int) s.getProperty ("i", -1);
            if (i < 0 || i >= n) continue;
            auto& slot = next[(size_t) i];
            slot.name = s.getProperty ("name", "").toString();
            slot.choke = (int) s.getProperty ("choke", 0);
            if (auto data = decodeSamples (s.getProperty ("b64", "").toString()))
            {
                slot.data = data;
                // variants travel with the main sound: missing ones are cleared
                slot.alt = s.hasProperty ("b64a") ? decodeSamples (s.getProperty ("b64a", "").toString()) : SampleData();
                slot.soft = s.hasProperty ("b64s") ? decodeSamples (s.getProperty ("b64s", "").toString()) : SampleData();
            }
        }

    uiSlots = next;
    for (int i = 0; i < numAutoPads; ++i)
        padNames.set (i, i < (int) next.size() ? next[(size_t) i].name : juce::String());
    refreshParamNames();
    for (const auto& s : slots)
        for (const auto& d : { s.data, s.alt, s.soft })
            if (d != nullptr)
                retired.push_back (d);
    {
        const juce::SpinLock::ScopedLockType lock (audioLock);
        slots.swap (next);
    }
    collectGarbage();
}

void SupremeDrumsProcessor::collectGarbage()
{
    // A buffer is freed here (message thread) only once no voice holds it any more.
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const SampleData& d) { return d.use_count() <= 1; }), retired.end());
}

void SupremeDrumsProcessor::applyPatternMessage (const juce::var& msg)
{
    Pattern p;
    p.bars = juce::jlimit (1, 4, (int) msg.getProperty ("bars", 1));
    p.bpm = juce::jlimit (40.0, 300.0, (double) msg.getProperty ("bpm", 120.0));
    p.hostSync = (bool) msg.getProperty ("hostSync", false);

    const auto g = msg.getProperty ("groove", {});
    p.groove.swing   = (double) g.getProperty ("swing", 0.0);
    p.groove.shuffle = (double) g.getProperty ("shuffle", 0.0);
    p.groove.lay     = (double) g.getProperty ("lay", 0.0);
    p.groove.humT    = (double) g.getProperty ("humT", 0.0);
    p.groove.humV    = (double) g.getProperty ("humV", 0.0);
    p.groove.dyn     = (double) g.getProperty ("dyn", 1.0);

    if (auto* rows = msg.getProperty ("steps", {}).getArray())
        for (const auto& row : *rows)
        {
            std::vector<int> r;
            if (auto* cells = row.getArray())
                for (const auto& c : *cells)
                    r.push_back (juce::jlimit (0, 3, (int) c));
            p.steps.push_back (std::move (r));
        }

    if (auto* mix = msg.getProperty ("mix", {}).getArray())
        for (const auto& m : *mix)
            p.mixDb.push_back ((float) (double) m);

    if (auto* lay = msg.getProperty ("lay", {}).getArray())
        for (const auto& l : *lay)
            p.lay.push_back ((int) l);

    const auto readFloats = [&msg] (const char* key, std::vector<float>& dst, float lo, float hi)
    {
        if (auto* arr = msg.getProperty (key, {}).getArray())
            for (const auto& x : *arr)
                dst.push_back (juce::jlimit (lo, hi, (float) (double) x));
    };
    readFloats ("lvel", p.lvel, 0.0f, 2.0f);
    readFloats ("lvar", p.lvar, 0.0f, 1.0f);
    readFloats ("ltr", p.ltr, 0.0f, 1.0f);
    readFloats ("lnudge", p.lnudge, -50.0f, 50.0f);
    readFloats ("pan", p.pan, -1.0f, 1.0f);
    p.masterDb = juce::jlimit (-60.0f, 12.0f, (float) (double) msg.getProperty ("mvol", 0.0));
    const auto readGrid = [&msg] (const char* key, std::vector<std::vector<int>>& dst)
    {
        if (auto* rows = msg.getProperty (key, {}).getArray())
            for (const auto& r : *rows)
            {
                std::vector<int> row;
                if (auto* cells = r.getArray())
                    for (const auto& x : *cells)
                        row.push_back ((int) x);
                dst.push_back (std::move (row));
            }
    };
    readGrid ("trip", p.trip);
    readGrid ("roll", p.roll);

    {
        const juce::SpinLock::ScopedLockType lock (audioLock);
        std::swap (pattern, p); // the old pattern is destroyed after the lock is released
    }
}

void SupremeDrumsProcessor::handleUiMessage (const juce::var& msg)
{
    const auto type = msg.getProperty ("type", "").toString();

    if (type == "kit")
    {
        applyKitMessage (msg);
    }
    else if (type == "pnames")
    {
        setParamNames (msg);
    }
    else if (type == "pattern")
    {
        {
            const juce::ScopedLock sl (stateLock);
            patternJson = juce::JSON::toString (msg, true);
        }
        applyPatternMessage (msg);
    }
    else if (type == "seq")
    {
        const bool on = (bool) msg.getProperty ("on", false);
        if (on) previewRestart.store (true);
        previewOn.store (on);
    }
    else if (type == "trig")
    {
        const int i = (int) msg.getProperty ("i", -1);
        const float gain = (float) (double) msg.getProperty ("gain", 1.0);
        const int level = (int) msg.getProperty ("lv", 0);
        const juce::SpinLock::ScopedLockType lock (audioLock);
        if (pendingTriggers.size() < 64)
            pendingTriggers.push_back ({ i, gain, level });
    }
    else if (type == "audition")
    {
        auto data = decodeSamples (msg.getProperty ("b64", "").toString());
        const juce::SpinLock::ScopedLockType lock (audioLock);
        audition = data;
        auditionPending = true;
    }
    else if (type == "project")
    {
        const juce::ScopedLock sl (stateLock);
        projectJson = msg.getProperty ("json", "").toString();
    }
}

juce::String SupremeDrumsProcessor::getProjectJson() const
{
    const juce::ScopedLock sl (stateLock);
    return projectJson;
}

//==============================================================================
//==============================================================================
void SupremeDrumsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out (destData, false);
    out.writeInt ((int) stateMagic);
    {
        const juce::ScopedLock sl (stateLock);
        out.writeString (projectJson);
        out.writeString (patternJson);
    }
    out.writeFloat (volume->get());

    const std::vector<Slot>& copy = uiSlots;
    out.writeInt ((int) copy.size());
    for (const auto& s : copy)
    {
        out.writeString (s.name);
        out.writeInt (s.choke);
        const int len = s.data != nullptr ? (int) s.data->size() : 0;
        out.writeInt (len);
        for (int i = 0; i < len; ++i)
            out.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround ((*s.data)[(size_t) i] * 32767.0f)));
    }

    // automatable parameters (appended: older saved states simply end before this)
    const auto params = getParameters();
    out.writeInt (params.size());
    for (auto* p : params)
        out.writeFloat (p->getValue());

    // recorded variants (appended in version 1.1: older states end before this block)
    out.writeInt ((int) variantMagic);
    out.writeInt ((int) copy.size());
    auto writeData = [&out] (const SampleData& d)
    {
        const int len = d != nullptr ? (int) d->size() : 0;
        out.writeInt (len);
        for (int i = 0; i < len; ++i)
            out.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround ((*d)[(size_t) i] * 32767.0f)));
    };
    for (const auto& s : copy)
    {
        writeData (s.alt);
        writeData (s.soft);
    }
}

void SupremeDrumsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
    if ((juce::uint32) in.readInt() != stateMagic)
        return;

    const auto proj = in.readString();
    const auto pat = in.readString();
    volume->setValueNotifyingHost (volume->convertTo0to1 (in.readFloat()));

    std::vector<Slot> next;
    const int n = juce::jlimit (0, 64, in.readInt());
    for (int k = 0; k < n && ! in.isExhausted(); ++k)
    {
        Slot s;
        s.name = in.readString();
        s.choke = in.readInt();
        const int len = juce::jmax (0, in.readInt());
        auto v = std::make_shared<std::vector<float>> ((size_t) len + 1, 0.0f);
        for (int i = 0; i < len; ++i)
            (*v)[(size_t) i] = in.readShort() / 32768.0f;
        s.data = v;
        next.push_back (std::move (s));
    }

    if (! in.isExhausted())
    {
        const auto params = getParameters();
        const int np = in.readInt();
        for (int i = 0; i < np && ! in.isExhausted(); ++i)
        {
            const float v = in.readFloat();
            if (i < params.size())
                params[i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
        }
    }

    if (! in.isExhausted() && (juce::uint32) in.readInt() == variantMagic)
    {
        auto readData = [&in]() -> SampleData
        {
            const int len = juce::jlimit (0, 48000 * 30, in.readInt());
            if (len == 0) return {};
            auto v = std::make_shared<std::vector<float>> ((size_t) len, 0.0f);
            for (int i = 0; i < len; ++i)
                (*v)[(size_t) i] = in.readShort() / 32768.0f;
            return v;
        };
        const int nv = in.readInt();
        for (int k = 0; k < nv && ! in.isExhausted(); ++k)
        {
            auto a = readData();
            auto s = readData();
            if (k < (int) next.size()) { next[(size_t) k].alt = a; next[(size_t) k].soft = s; }
        }
    }

    {
        const juce::ScopedLock sl (stateLock);
        projectJson = proj;
        patternJson = pat;
    }
    uiSlots = next;
    for (int i = 0; i < numAutoPads; ++i)
        padNames.set (i, i < (int) next.size() ? next[(size_t) i].name : juce::String());
    refreshParamNames();
    for (const auto& s : slots)
        for (const auto& d : { s.data, s.alt, s.soft })
            if (d != nullptr)
                retired.push_back (d);
    {
        const juce::SpinLock::ScopedLockType lock (audioLock);
        slots.swap (next);
    }
    if (pat.isNotEmpty())
        applyPatternMessage (juce::JSON::parse (pat));
}

juce::AudioProcessorEditor* SupremeDrumsProcessor::createEditor()
{
    return new SupremeDrumsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SupremeDrumsProcessor();
}
