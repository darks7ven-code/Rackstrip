#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace rack;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout RackProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray names;
    for (int t = 0; t < NumTypes; ++t) names.add (info (t).name);

    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto n = "Slot " + juce::String (s + 1);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { typeId (s), 1 }, n + " Module", names, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { onId (s), 1 }, n + " On", true));
        for (int j = 0; j < kMaxParams; ++j)
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { paramId (s, j), 1 },
                        n + " Param " + juce::String (j + 1), juce::NormalisableRange<float> (0.f, 1.f), 0.5f));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in_gain", 1 }, "Input",
                juce::NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_gain", 1 }, "Output",
                juce::NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

RackProcessor::RackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RACKSTRIP", createLayout())
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        slotMeter[s] = 0.f;
        typeP[s] = apvts.getRawParameterValue (typeId (s));
        onP[s]   = apvts.getRawParameterValue (onId (s));
        for (int j = 0; j < kMaxParams; ++j) pP[s][j] = apvts.getRawParameterValue (paramId (s, j));
        for (int t = 1; t < NumTypes; ++t) mods[(size_t) s][(size_t) t] = info (t).make();
        lastType[s] = 0;
    }
    inP = apvts.getRawParameterValue ("in_gain");
    outP = apvts.getRawParameterValue ("out_gain");
    bypassP = apvts.getRawParameterValue ("bypass");
}

bool RackProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

void RackProcessor::prepareToPlay (double sr, int bs)
{
    for (auto& slot : mods)
        for (auto& m : slot)
            if (m) { m->prepare (sr, bs); m->reset(); }
    for (int s = 0; s < kNumSlots; ++s) lastType[s] = 0;
    lastIn = juce::Decibels::decibelsToGain (inP->load());
    lastOut = juce::Decibels::decibelsToGain (outP->load());
}

void RackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    const float inG = juce::Decibels::decibelsToGain (inP->load());
    buffer.applyGainRamp (0, n, lastIn, inG);
    lastIn = inG;

    float pk = 0.f;
    for (int c = 0; c < buffer.getNumChannels(); ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
    inPeak.store (pk);

    if (bypassP->load() < 0.5f)
    {
        for (int s = 0; s < kNumSlots; ++s)
        {
            const int type = (int) typeP[s]->load();
            if (type <= 0 || type >= NumTypes) { slotMeter[s] = 0.f; lastType[s] = 0; continue; }
            auto& m = mods[(size_t) s][(size_t) type];
            if (type != lastType[s]) { m->reset(); lastType[s] = type; }
            if (onP[s]->load() < 0.5f) { slotMeter[s] = 0.f; continue; }

            const auto& inf = info (type);
            float v[kMaxParams] {};
            for (size_t j = 0; j < inf.params.size(); ++j) v[j] = inf.params[j].toReal (pP[s][j]->load());
            m->process (buffer, v);
            slotMeter[s] = m->meter;
        }
    }
    else
        for (int s = 0; s < kNumSlots; ++s) slotMeter[s] = 0.f;

    const float outG = juce::Decibels::decibelsToGain (outP->load());
    buffer.applyGainRamp (0, n, lastOut, outG);
    lastOut = outG;

    pk = 0.f;
    for (int c = 0; c < buffer.getNumChannels(); ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
    outPeak.store (pk);
}

//==============================================================================
void RackProcessor::setNorm (const juce::String& id, float norm)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, norm));
}
float RackProcessor::getNorm (const juce::String& id) const
{
    if (auto* p = apvts.getParameter (id)) return p->getValue();
    return 0.f;
}

void RackProcessor::setSlotType (int slot, int type, bool resetToDefaults)
{
    type = juce::jlimit (0, NumTypes - 1, type);
    setNorm (typeId (slot), (float) type / (float) (NumTypes - 1));
    if (! resetToDefaults) return;
    const auto& inf = info (type);
    for (int j = 0; j < kMaxParams; ++j)
        setNorm (paramId (slot, j), j < (int) inf.params.size() ? inf.params[(size_t) j].toNorm (inf.params[(size_t) j].def) : 0.5f);
    setNorm (onId (slot), 1.f);
}

void RackProcessor::swapSlots (int a, int b)
{
    if (a < 0 || b < 0 || a >= kNumSlots || b >= kNumSlots || a == b) return;
    auto swapParam = [this, a, b] (const juce::String& ia, const juce::String& ib)
    {
        const float va = getNorm (ia), vb = getNorm (ib);
        setNorm (ia, vb); setNorm (ib, va);
    };
    swapParam (typeId (a), typeId (b));
    swapParam (onId (a), onId (b));
    for (int j = 0; j < kMaxParams; ++j) swapParam (paramId (a, j), paramId (b, j));
}

//==============================================================================
namespace
{
struct PS { int type; std::vector<std::pair<int, float>> vals; };   // (index du réglage, valeur réelle)
struct Preset { const char* name; std::vector<PS> slots; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Empty rack", {} },
        { "Vocal chain", {
            { Filters,    { { 0, 90.f } } },
            { Preamp,     { { 0, 1.f }, { 1, 30.f } } },
            { ConsoleEQ,  { { 0, 1.f }, { 1, 1.5f }, { 2, -2.f }, { 3, 350.f }, { 4, 2.f }, { 5, 3500.f }, { 6, 3.f } } },
            { Compressor, { { 0, 1.f }, { 1, -22.f }, { 2, 3.5f }, { 3, 10.f }, { 4, 250.f }, { 5, 4.f } } },
            { DeEsser,    { { 0, 7000.f }, { 1, -28.f }, { 2, 6.f } } },
            { Saturation, { { 0, 0.f }, { 1, 5.f }, { 3, 40.f } } },
            { Reverb,     { { 0, 35.f }, { 1, 50.f }, { 3, 10.f } } },
            { Limiter,    { { 1, -1.f } } } } },
        { "Drum bus", {
            { Filters,    { { 0, 30.f } } },
            { ConsoleEQ,  { { 0, 2.f }, { 1, 3.f }, { 6, 2.f } } },
            { Compressor, { { 0, 0.f }, { 1, -16.f }, { 2, 4.f }, { 3, 10.f }, { 4, 100.f }, { 5, 4.f } } },
            { Saturation, { { 0, 0.f }, { 1, 8.f }, { 3, 70.f } } },
            { Limiter,    { { 1, -1.f } } } } },
        { "Mix bus", {
            { ConsoleEQ,  { { 0, 0.f }, { 1, 1.f }, { 6, 1.5f } } },
            { Compressor, { { 0, 0.f }, { 1, -14.f }, { 2, 2.f }, { 3, 30.f }, { 4, 200.f }, { 5, 2.f } } },
            { Saturation, { { 0, 0.f }, { 1, 3.f }, { 3, 50.f } } },
            { Stereo,     { { 0, 110.f } } },
            { Limiter,    { { 1, -0.5f } } } } }
    };
    return p;
}
}

int RackProcessor::numPresets() { return (int) presets().size(); }
const char* RackProcessor::presetName (int i) { return presets()[(size_t) juce::jlimit (0, numPresets() - 1, i)].name; }

void RackProcessor::loadPreset (int index)
{
    for (int s = 0; s < kNumSlots; ++s) setSlotType (s, 0, true);
    const auto& pr = presets()[(size_t) juce::jlimit (0, numPresets() - 1, index)];
    for (size_t s = 0; s < pr.slots.size() && (int) s < kNumSlots; ++s)
    {
        const auto& ps = pr.slots[s];
        setSlotType ((int) s, ps.type, true);
        const auto& inf = info (ps.type);
        for (auto& kv : ps.vals)
            if (kv.first < (int) inf.params.size())
                setNorm (paramId ((int) s, kv.first), inf.params[(size_t) kv.first].toNorm (kv.second));
    }
}

//==============================================================================
void RackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void RackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* RackProcessor::createEditor() { return new RackEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RackProcessor(); }
