#include "PluginEditor.h"

using namespace juce;
using namespace rack;

//==============================================================================
RackLnF::RackLnF()
{
    setColour (ComboBox::backgroundColourId, Colour (0xff15171a));
    setColour (ComboBox::textColourId, Colours::white);
    setColour (ComboBox::outlineColourId, Colour (0xff555a60));
    setColour (ComboBox::arrowColourId, Colours::white);
    setColour (PopupMenu::backgroundColourId, Colour (0xff1c1f23));
    setColour (PopupMenu::textColourId, Colours::white);
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff3a6ea5));
    setColour (Label::textColourId, Colours::white);
    setColour (Slider::textBoxTextColourId, Colour (0xffe8e8e8));
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (TextButton::buttonColourId, Colour (0xff15171a));
    setColour (TextButton::textColourOffId, Colours::white);
}

Font RackLnF::getLabelFont (Label& l) { return Font (FontOptions (l.getFont().getHeight())); }

void RackLnF::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const float r = jmin (w, h) * 0.5f - 3.f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const bool en = s.isEnabled();

    // arc de valeur
    Path track;
    track.addCentredArc (cx, cy, r, r, 0.f, a0, a1, true);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.strokePath (track, PathStrokeType (2.5f));
    Path val;
    val.addCentredArc (cx, cy, r, r, 0.f, a0, ang, true);
    g.setColour (Colour (0xffffb23e).withAlpha (en ? 1.f : 0.4f));
    g.strokePath (val, PathStrokeType (2.5f));

    // corps du bouton
    const float kr = r - 5.f;
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (cx - kr + 1.f, cy - kr + 2.f, kr * 2, kr * 2);
    g.setGradientFill (ColourGradient (Colour (0xff6d7177), cx, cy - kr, Colour (0xff23262a), cx, cy + kr, false));
    g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    g.setColour (Colour (0xff0d0e10));
    g.drawEllipse (cx - kr, cy - kr, kr * 2, kr * 2, 1.f);

    // repère
    Path p;
    p.addRoundedRectangle (-1.2f, -kr + 2.f, 2.4f, kr * 0.5f, 1.f);
    g.setColour (Colours::white);
    g.fillPath (p, AffineTransform::rotation (ang).translated (cx, cy));
}

void RackLnF::drawToggleButton (Graphics& g, ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float d = jmin (r.getWidth(), r.getHeight()) - 6.f;
    auto c = r.getCentre();
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (c.x - d / 2 - 2, c.y - d / 2 - 2, d + 4, d + 4);
    g.setColour (on ? Colour (0xff4cff7a) : Colour (0xff233026));
    g.fillEllipse (c.x - d / 2, c.y - d / 2, d, d);
    if (on)
    {
        g.setColour (Colour (0xff4cff7a).withAlpha (0.25f));
        g.fillEllipse (c.x - d, c.y - d, d * 2, d * 2);
    }
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (Colours::white);
        g.setFont (Font (FontOptions (11.f)));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 8).toNearestInt(), Justification::centredLeft);
    }
}

//==============================================================================
ModulePanel::ModulePanel (RackProcessor& p, int s) : proc (p), slot (s)
{
    // choix du module
    for (int t = 0; t < NumTypes; ++t) typeBox.addItem (t == 0 ? "- Empty -" : String (info (t).name), t + 1);
    addAndMakeVisible (typeBox);
    typeBox.onChange = [this]
    {
        if (updating) return;
        proc.setSlotType (slot, typeBox.getSelectedId() - 1, true);
    };

    addAndMakeVisible (powerBtn);
    onAtt = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter (RackProcessor::onId (slot)), powerBtn);

    addAndMakeVisible (leftBtn);
    addAndMakeVisible (rightBtn);
    leftBtn.onClick  = [this] { if (onMove) onMove (slot, -1); };
    rightBtn.onClick = [this] { if (onMove) onMove (slot, +1); };

    for (int j = 0; j < kMaxParams; ++j)
    {
        auto& c = cells[(size_t) j];
        auto* prm = proc.apvts.getParameter (RackProcessor::paramId (slot, j));

        c.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        c.slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        c.slider.setTextBoxStyle (Slider::TextBoxBelow, false, kWidth / 2 - 8, 14);
        c.slider.setVelocityBasedMode (false);
        c.sAtt = std::make_unique<SliderParameterAttachment> (*prm, c.slider);
        addChildComponent (c.slider);

        c.label.setJustificationType (Justification::centred);
        c.label.setFont (Font (FontOptions (10.f, Font::bold)));
        c.label.setColour (Label::textColourId, Colours::white.withAlpha (0.85f));
        addChildComponent (c.label);

        addChildComponent (c.combo);
        c.cAtt = std::make_unique<ParameterAttachment> (*prm, [this, j] (float v)
        {
            if (type <= 0 || j >= (int) info (type).params.size()) return;
            const auto& pd = info (type).params[(size_t) j];
            if (pd.kind != Kind::Choice) return;
            updating = true;
            cells[(size_t) j].combo.setSelectedItemIndex (pd.numChoices() > 1 ? roundToInt (pd.toReal (v)) : 0, dontSendNotification);
            updating = false;
        });
        c.combo.onChange = [this, j]
        {
            if (updating || type <= 0 || j >= (int) info (type).params.size()) return;
            const auto& pd = info (type).params[(size_t) j];
            cells[(size_t) j].cAtt->setValueAsCompleteGesture (pd.toNorm ((float) cells[(size_t) j].combo.getSelectedItemIndex()));
        };
    }

    typeAtt = std::make_unique<ParameterAttachment> (*proc.apvts.getParameter (RackProcessor::typeId (slot)),
                                                     [this] (float) { refresh(); });
    refresh();
    startTimerHz (24);
}

ModulePanel::~ModulePanel() { stopTimer(); }

void ModulePanel::refresh()
{
    const int newType = proc.getSlotType (slot);
    updating = true;
    typeBox.setSelectedId (newType + 1, dontSendNotification);
    updating = false;
    type = newType;

    const auto& inf = info (type);
    const int n = (int) inf.params.size();

    for (int j = 0; j < kMaxParams; ++j)
    {
        auto& c = cells[(size_t) j];
        const bool used = type > 0 && j < n;
        c.slider.setVisible (false);
        c.combo.setVisible (false);
        c.label.setVisible (used);
        if (! used) continue;

        const auto& pd = inf.params[(size_t) j];
        c.label.setText (pd.name, dontSendNotification);
        c.isChoice = (pd.kind == Kind::Choice);

        if (c.isChoice)
        {
            updating = true;
            c.combo.clear (dontSendNotification);
            auto items = StringArray::fromTokens (pd.choices, "|", "");
            for (int i = 0; i < items.size(); ++i) c.combo.addItem (items[i], i + 1);
            c.combo.setSelectedItemIndex (roundToInt (pd.toReal (proc.apvts.getParameter (RackProcessor::paramId (slot, j))->getValue())), dontSendNotification);
            updating = false;
            c.combo.setVisible (true);
        }
        else
        {
            c.slider.textFromValueFunction = [pd] (double norm) { return pd.format (pd.toReal ((float) norm)); };
            c.slider.valueFromTextFunction = [pd] (const String& t) { return (double) pd.toNorm (t.getFloatValue()); };
            c.slider.setDoubleClickReturnValue (true, (double) pd.toNorm (pd.def));
            c.slider.updateText();
            c.slider.setVisible (true);
        }
    }
    resized();
    repaint();
}

void ModulePanel::resized()
{
    leftBtn.setBounds (4, 6, 20, 24);
    typeBox.setBounds (26, 6, kWidth - 52, 24);
    rightBtn.setBounds (kWidth - 24, 6, 20, 24);
    powerBtn.setBounds (getWidth() - 34, 34, 26, 26);

    const int n = type > 0 ? (int) info (type).params.size() : 0;
    const int cols = n > 8 ? 3 : 2, cw = getWidth() / cols, ch = 76, gy = 132;
    for (int j = 0; j < kMaxParams; ++j)
    {
        auto& c = cells[(size_t) j];
        const int x = (j % cols) * cw, y = gy + (j / cols) * ch;
        c.label.setBounds (x, y, cw, 14);
        c.slider.setBounds (x + 2, y + 14, cw - 4, 62);
        c.combo.setBounds (x + 4, y + 30, cw - 8, 24);
        c.slider.setTextBoxStyle (Slider::TextBoxBelow, false, cw - 4, 14);
    }
}

void ModulePanel::timerCallback()
{
    if (type == ProEQ || type == Compressor || type == Gate || type == DeEsser)
        repaint (displayRect());
}

void ModulePanel::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.f, 0.f);
    const auto& inf = info (jmax (0, type));
    const Colour c (inf.colour);

    g.setGradientFill (ColourGradient (c.brighter (0.18f), 0, 0, c.darker (0.3f), 0, (float) getHeight(), false));
    g.fillRect (b);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawRect (b, 1.5f);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawLine (b.getX() + 1, 0, b.getX() + 1, (float) getHeight());

    // vis
    for (float sx : { 12.f, (float) getWidth() - 12.f })
        for (float sy : { 12.f + 0.f, (float) getHeight() - 12.f })
            if (sy > 40.f)
            {
                g.setColour (Colour (0xff8a8d91)); g.fillEllipse (sx - 5, sy - 5, 10, 10);
                g.setColour (Colour (0xff2a2c2f)); g.drawLine (sx - 3, sy - 1, sx + 3, sy + 1.5f, 1.5f);
            }

    if (type <= 0)
    {
        g.setColour (Colours::white.withAlpha (0.35f));
        g.setFont (Font (FontOptions (13.f)));
        g.drawFittedText ("Empty slot\n\nChoose a module\nwith the menu above", 10, 150, getWidth() - 20, 90, Justification::centred, 5);
        return;
    }

    g.setColour (Colours::white);
    g.setFont (Font (FontOptions (15.f, Font::bold)));
    g.drawText (inf.name, 8, 36, getWidth() - 44, 22, Justification::centredLeft);

    paintDisplay (g, displayRect().toFloat());

    // numéro de slot
    g.setColour (Colours::white.withAlpha (0.4f));
    g.setFont (Font (FontOptions (10.f)));
    g.drawText (String (slot + 1), 0, getHeight() - 22, getWidth(), 14, Justification::centred);
}

void ModulePanel::paintDisplay (Graphics& g, Rectangle<float> r)
{
    g.setColour (Colour (0xff0b0d0f));
    g.fillRoundedRectangle (r, 4.f);
    g.setColour (Colour (0xff000000));
    g.drawRoundedRectangle (r, 4.f, 1.f);

    const auto& inf = info (type);
    if (type == ProEQ)
    {
        float v[kMaxParams];
        for (int j = 0; j < 12; ++j)
            v[j] = inf.params[(size_t) j].toReal (proc.apvts.getParameter (RackProcessor::paramId (slot, j))->getValue());
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;

        auto area = r.reduced (3.f);
        g.setColour (Colours::white.withAlpha (0.08f));
        for (float f : { 100.f, 1000.f, 10000.f })
        {
            const float x = area.getX() + area.getWidth() * (std::log10 (f / 20.f) / 3.f);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        Path path;
        for (int i = 0; i <= 80; ++i)
        {
            const float f = 20.f * std::pow (1000.f, (float) i / 80.f);
            const float db = jlimit (-20.f, 20.f, proEqMagDb (v, f, sr));
            const float x = area.getX() + area.getWidth() * (float) i / 80.f;
            const float y = area.getCentreY() - db / 20.f * area.getHeight() * 0.5f;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (Colour (0xff4ad6ff));
        g.strokePath (path, PathStrokeType (1.6f));
    }
    else if (type == Compressor || type == Gate || type == DeEsser)
    {
        const float gr = proc.slotMeter[slot].load();
        const float range = type == Gate ? 40.f : (type == DeEsser ? 15.f : 20.f);
        auto bar = r.reduced (8.f, 0.f).withTrimmedTop (r.getHeight() * 0.55f).withTrimmedBottom (8.f);
        g.setColour (Colour (0xff1c2024));
        g.fillRoundedRectangle (bar, 2.f);
        const float w = bar.getWidth() * jlimit (0.f, 1.f, gr / range);
        g.setColour (Colour (0xffffb23e));
        g.fillRoundedRectangle (bar.withX (bar.getRight() - w).withWidth (w), 2.f);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.setFont (Font (FontOptions (11.f, Font::bold)));
        g.drawText ("GAIN REDUCTION  " + String (gr, 1) + " dB", r.withHeight (r.getHeight() * 0.55f), Justification::centred);
    }
    else
    {
        g.setColour (Colour (0xff6fe8a8));
        g.setFont (Font (FontOptions (10.f, Font::bold)));
        g.drawFittedText (inf.tag, r.reduced (4.f).toNearestInt(), Justification::centred, 3);
    }
}

//==============================================================================
RackEditor::RackEditor (RackProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    for (int s = 0; s < kNumSlots; ++s)
    {
        panels[(size_t) s] = std::make_unique<ModulePanel> (proc, s);
        panels[(size_t) s]->setBounds (30 + s * ModulePanel::kWidth, 54, ModulePanel::kWidth, ModulePanel::kHeight);
        panels[(size_t) s]->onMove = [this] (int slot, int dir) { proc.swapSlots (slot, slot + dir); };
        content.addAndMakeVisible (*panels[(size_t) s]);
    }

    auto setupKnob = [this] (Slider& k, Label& l, const String& name, const String& id, std::unique_ptr<SliderParameterAttachment>& att, int x)
    {
        k.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        k.setTextBoxStyle (Slider::TextBoxRight, false, 60, 16);
        att = std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter (id), k);
        k.textFromValueFunction = [] (double v) { return String (v, 1) + " dB"; };
        k.updateText();
        k.setBounds (x + 38, 8, 120, 38);
        l.setText (name, dontSendNotification);
        l.setFont (Font (FontOptions (11.f, Font::bold)));
        l.setBounds (x, 8, 40, 38);
        content.addAndMakeVisible (k);
        content.addAndMakeVisible (l);
    };
    setupKnob (inKnob, inLabel, "INPUT", "in_gain", inAtt, 330);
    setupKnob (outKnob, outLabel, "OUTPUT", "out_gain", outAtt, 530);

    presetBox.setTextWhenNothingSelected ("Chains...");
    for (int i = 0; i < RackProcessor::numPresets(); ++i) presetBox.addItem (RackProcessor::presetName (i), i + 1);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0) { proc.loadPreset (id - 1); presetBox.setSelectedId (0, dontSendNotification); }
    };
    presetBox.setBounds (190, 14, 130, 26);
    content.addAndMakeVisible (presetBox);

    bypassBtn.setBounds (kW - 120, 12, 100, 30);
    bypassAtt = std::make_unique<ButtonParameterAttachment> (*proc.apvts.getParameter ("bypass"), bypassBtn);
    content.addAndMakeVisible (bypassBtn);

    content.setBounds (0, 0, kW, kH);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setResizeLimits ((int) (kW * 0.5), (int) (kH * 0.5), (int) (kW * 1.3), (int) (kH * 1.3));
    setSize ((int) (kW * 0.85), (int) (kH * 0.85));
    startTimerHz (24);
}

RackEditor::~RackEditor() { stopTimer(); setLookAndFeel (nullptr); }

void RackEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    content.setTransform (AffineTransform::scale (s));
    content.setBounds (0, 0, kW, kH);
}

void RackEditor::paint (Graphics& g) { g.fillAll (Colour (0xff101113)); }

void RackEditor::timerCallback()
{
    dispIn  = jmax (proc.inPeak.load(),  dispIn  * 0.88f);
    dispOut = jmax (proc.outPeak.load(), dispOut * 0.88f);
    content.repaint (0, 0, kW, 54);
}

void RackEditor::paintContent (Graphics& g)
{
    g.fillAll (Colour (0xff101113));

    // barre du haut
    g.setGradientFill (ColourGradient (Colour (0xff25282c), 0, 0, Colour (0xff17191c), 0, 54, false));
    g.fillRect (0, 0, kW, 54);
    g.setColour (Colours::white);
    g.setFont (Font (FontOptions (20.f, Font::bold)));
    g.drawText ("RACK STRIP", 16, 6, 170, 28, Justification::centredLeft);
    g.setColour (Colour (0xffffb23e));
    g.setFont (Font (FontOptions (9.f, Font::bold)));
    g.drawText ("MODULAR CHANNEL CHAIN", 16, 31, 170, 14, Justification::centredLeft);

    // vumètres
    auto meter = [&g] (float v, int x, int y)
    {
        const float db = Decibels::gainToDecibels (v, -60.f);
        const float frac = jlimit (0.f, 1.f, (db + 60.f) / 60.f);
        g.setColour (Colour (0xff0b0d0f));
        g.fillRect (x, y, 100, 6);
        g.setColour (db > -1.f ? Colour (0xffff4040) : (db > -12.f ? Colour (0xffffb23e) : Colour (0xff4cff7a)));
        g.fillRect (x, y, (int) (100 * frac), 6);
    };
    meter (dispIn, 700, 16);
    meter (dispOut, 700, 32);
    g.setColour (Colours::white.withAlpha (0.6f));
    g.setFont (Font (FontOptions (9.f, Font::bold)));
    g.drawText ("IN", 675, 13, 22, 10, Justification::centredRight);
    g.drawText ("OUT", 668, 29, 29, 10, Justification::centredRight);

    // rails du rack
    for (int side = 0; side < 2; ++side)
    {
        const int x = side == 0 ? 0 : kW - 30;
        g.setGradientFill (ColourGradient (Colour (0xff3a3d41), (float) x, 0, Colour (0xff222427), (float) (x + 30), 0, false));
        g.fillRect (x, 54, 30, ModulePanel::kHeight + 10);
        g.setColour (Colour (0xff08090a));
        for (int y = 74; y < kH - 10; y += 60)
            g.fillRoundedRectangle ((float) x + 9.f, (float) y, 12.f, 20.f, 4.f);
    }
}
