#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace rack
{
constexpr int kNumSlots = 8;
constexpr int kMaxParams = 12;

enum class Kind { Lin, Log, Choice };

// Description d'un réglage d'un module
struct ParamDef
{
    const char* name;
    float mn, mx, def;      // def = valeur réelle (ou index pour un choix)
    Kind kind;
    const char* unit;
    const char* choices;    // "SSL|Neve|API"
    int decimals;

    int numChoices() const;
    float toReal (float norm) const;
    float toNorm (float real) const;
    juce::String format (float real) const;
};

class Module
{
public:
    virtual ~Module() = default;
    virtual void prepare (double /*sampleRate*/, int /*maxBlock*/) {}
    virtual void reset() {}
    // v = valeurs réelles des réglages
    virtual void process (juce::AudioBuffer<float>&, const float* v) = 0;
    float meter = 0.f;      // réduction de gain en dB (pour l'affichage)
};

enum ModuleType
{
    Empty = 0, Preamp, ConsoleEQ, ProEQ, Compressor, Gate, DeEsser,
    Saturation, Filters, Stereo, Delay, Reverb, Limiter, NumTypes
};

struct ModuleInfo
{
    const char* name;
    const char* tag;
    juce::uint32 colour;
    std::vector<ParamDef> params;
    std::unique_ptr<Module> (*make)();
};

const ModuleInfo& info (int type);

// Courbe de réponse de l'EQ précis (pour l'affichage)
float proEqMagDb (const float* realValues, float freq, double sampleRate);
}
