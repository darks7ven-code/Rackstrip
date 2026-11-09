#pragma once
#include "Modules.h"
#include <array>

class RackProcessor : public juce::AudioProcessor
{
public:
    RackProcessor();
    ~RackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Rack Strip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Rack ---
    void setSlotType (int slot, int type, bool resetToDefaults);
    void swapSlots (int a, int b);
    void loadPreset (int index);
    static int numPresets();
    static const char* presetName (int index);

    static juce::String typeId (int s)            { return "s" + juce::String (s) + "_type"; }
    static juce::String onId (int s)              { return "s" + juce::String (s) + "_on"; }
    static juce::String paramId (int s, int j)    { return "s" + juce::String (s) + "_p" + juce::String (j); }

    int getSlotType (int slot) const { return (int) typeP[slot]->load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> slotMeter[rack::kNumSlots];
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setNorm (const juce::String& id, float norm);
    float getNorm (const juce::String& id) const;

    std::array<std::array<std::unique_ptr<rack::Module>, rack::NumTypes>, rack::kNumSlots> mods;
    int lastType[rack::kNumSlots] {};

    std::atomic<float>* typeP[rack::kNumSlots];
    std::atomic<float>* onP[rack::kNumSlots];
    std::atomic<float>* pP[rack::kNumSlots][rack::kMaxParams];
    std::atomic<float>* inP = nullptr;
    std::atomic<float>* outP = nullptr;
    std::atomic<float>* bypassP = nullptr;
    float lastIn = 1.f, lastOut = 1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackProcessor)
};
