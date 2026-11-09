#pragma once
#include "PluginProcessor.h"
#include <array>

//==============================================================================
class RackLnF : public juce::LookAndFeel_V4
{
public:
    RackLnF();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getLabelFont (juce::Label&) override;
};

//==============================================================================
class ModulePanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 148, kHeight = 600;

    ModulePanel (RackProcessor&, int slot);
    ~ModulePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (int slot, int dir)> onMove;

private:
    void timerCallback() override;
    void refresh();
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>);
    juce::Rectangle<int> displayRect() const { return { 8, 64, getWidth() - 16, 60 }; }

    struct Cell
    {
        juce::Slider slider;
        juce::ComboBox combo;
        juce::Label label;
        std::unique_ptr<juce::SliderParameterAttachment> sAtt;
        std::unique_ptr<juce::ParameterAttachment> cAtt;
        bool isChoice = false;
    };

    RackProcessor& proc;
    const int slot;
    int type = -1;
    bool updating = false;

    juce::ComboBox typeBox;
    juce::ToggleButton powerBtn;
    juce::TextButton leftBtn { "<" }, rightBtn { ">" };
    std::array<Cell, rack::kMaxParams> cells;
    std::unique_ptr<juce::ParameterAttachment> typeAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> onAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulePanel)
};

//==============================================================================
class RackEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RackEditor (RackProcessor&);
    ~RackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (RackEditor& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        RackEditor& owner;
    };
    void paintContent (juce::Graphics&);
    void timerCallback() override;

    static constexpr int kW = 30 * 2 + ModulePanel::kWidth * rack::kNumSlots, kH = 54 + ModulePanel::kHeight + 10;

    RackProcessor& proc;
    RackLnF lnf;
    Content content { *this };
    std::array<std::unique_ptr<ModulePanel>, rack::kNumSlots> panels;

    juce::Slider inKnob, outKnob;
    juce::Label inLabel, outLabel;
    juce::ToggleButton bypassBtn { "Bypass" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::SliderParameterAttachment> inAtt, outAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAtt;
    float dispIn = 0.f, dispOut = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackEditor)
};
