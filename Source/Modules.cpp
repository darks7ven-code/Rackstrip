#include "Modules.h"
#include <cmath>
#include <complex>

namespace rack
{
//==============================================================================
int ParamDef::numChoices() const
{
    if (choices == nullptr) return 0;
    int n = 1;
    for (const char* c = choices; *c; ++c) if (*c == '|') ++n;
    return n;
}

float ParamDef::toReal (float norm) const
{
    norm = juce::jlimit (0.f, 1.f, norm);
    switch (kind)
    {
        case Kind::Choice: return (float) juce::roundToInt (norm * (float) juce::jmax (1, numChoices() - 1));
        case Kind::Log:    return mn * std::pow (mx / mn, norm);
        default:           return mn + (mx - mn) * norm;
    }
}

float ParamDef::toNorm (float real) const
{
    float n = 0.f;
    switch (kind)
    {
        case Kind::Choice: n = real / (float) juce::jmax (1, numChoices() - 1); break;
        case Kind::Log:    n = std::log (juce::jmax (real, mn) / mn) / std::log (mx / mn); break;
        default:           n = (real - mn) / (mx - mn); break;
    }
    return juce::jlimit (0.f, 1.f, n);
}

juce::String ParamDef::format (float real) const
{
    if (kind == Kind::Choice)
    {
        auto arr = juce::StringArray::fromTokens (choices, "|", "");
        return arr[juce::jlimit (0, juce::jmax (0, arr.size() - 1), juce::roundToInt (real))];
    }
    juce::String u (unit ? unit : "");
    if (u == "Hz" && real >= 1000.f)
        return juce::String (real / 1000.f, 2) + " kHz";
    return juce::String (real, decimals) + (u.isEmpty() ? "" : " " + u);
}

//==============================================================================
namespace
{
constexpr float kPi = 3.14159265358979f;

inline float dbToGain (float db) { return std::pow (10.f, db / 20.f); }
inline float gainToDb (float g)  { return 20.f * std::log10 (juce::jmax (g, 1.0e-6f)); }
inline int chans (const juce::AudioBuffer<float>& b) { return juce::jmin (2, b.getNumChannels()); }

// Filtre biquad (RBJ), sans allocation, état séparé par canal
struct Bq
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = { 0, 0 }, z2[2] = { 0, 0 };

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    void set (float nb0, float nb1, float nb2, float a0, float na1, float na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    static float lim (float f, double sr) { return juce::jlimit (10.f, (float) sr * 0.45f, f); }

    void peak (double sr, float f, float q, float gDb)
    {
        f = lim (f, sr);
        const float A = std::pow (10.f, gDb / 40.f), w = 2 * kPi * f / (float) sr;
        const float al = std::sin (w) / (2 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, float f, float q, float gDb)
    {
        f = lim (f, sr);
        const float A = std::pow (10.f, gDb / 40.f), w = 2 * kPi * f / (float) sr;
        const float c = std::cos (w), s = std::sin (w), sq = 2 * std::sqrt (A) * (s / (2 * q));
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double sr, float f, float q, float gDb)
    {
        f = lim (f, sr);
        const float A = std::pow (10.f, gDb / 40.f), w = 2 * kPi * f / (float) sr;
        const float c = std::cos (w), s = std::sin (w), sq = 2 * std::sqrt (A) * (s / (2 * q));
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void hp (double sr, float f, float q)
    {
        f = lim (f, sr);
        const float w = 2 * kPi * f / (float) sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lp (double sr, float f, float q)
    {
        f = lim (f, sr);
        const float w = 2 * kPi * f / (float) sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    float tick (int ch, float x)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    float magDb (float f, double sr) const
    {
        const double w = 2.0 * 3.14159265358979 * f / sr;
        const std::complex<double> e1 (std::cos (-w), std::sin (-w)), e2 (std::cos (-2 * w), std::sin (-2 * w));
        const auto num = (double) b0 + (double) b1 * e1 + (double) b2 * e2;
        const auto den = 1.0 + (double) a1 * e1 + (double) a2 * e2;
        return (float) (20.0 * std::log10 (std::max (std::abs (num / den), 1.0e-9)));
    }
};

// Gain de compensation : garde le même niveau RMS avant/après saturation
template <class F>
float satCompensation (F f, float g)
{
    constexpr float A = 0.25f;
    constexpr int N = 256;
    double sum = 0, sumsq = 0;
    for (int i = 0; i < N; ++i)
    {
        const float y = f (g * A * std::sin (2 * kPi * (float) i / (float) N));
        sum += y; sumsq += (double) y * y;
    }
    const double mean = sum / N, var = sumsq / N - mean * mean;
    const double rms = std::sqrt (std::max (var, 1.0e-12));
    return juce::jlimit (0.05f, 4.f, (float) ((A / std::sqrt (2.0)) / rms));
}

// Saturations du préampli : 0 = SSL (symétrique), 1 = Neve (asymétrique), 2 = API (genou franc)
float shapePre (int model, float x)
{
    switch (model)
    {
        case 1:  return std::tanh (x + 0.22f * x * x);
        case 2:  { const float a = std::abs (x); return x / std::cbrt (1.f + a * a * a); }
        default: return std::tanh (x);
    }
}

// Saturations : 0 = Tape, 1 = Tube, 2 = Transformer, 3 = Diode
float shapeSat (int type, float x)
{
    switch (type)
    {
        case 1:  return std::tanh (x + 0.3f * x * x);
        case 2:  return 0.8f * std::tanh (x) + 0.2f * std::tanh (3.f * x) * 0.6f;
        case 3:  return x >= 0.f ? x / (1.f + x) : x / (1.f - 0.6f * x);
        default: return std::tanh (0.9f * x) / 0.9f * 0.9f;
    }
}

//==============================================================================
class PreampMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; dcR = 1.f - 2 * kPi * 8.f / (float) s; reset(); lastKey = -1; lastTone = -99; }
    void reset() override { lo.reset(); hi.reset(); for (int c = 0; c < 2; ++c) dcx[c] = dcy[c] = 0; }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const int model = (int) v[0];
        const float drive = v[1], tone = v[2];
        const float g = 1.f + drive * 0.08f;

        const float key = (float) model * 1000.f + drive;
        if (key != lastKey) { comp = satCompensation ([model] (float x) { return shapePre (model, x); }, g); lastKey = key; }

        const float tkey = (float) model * 100.f + tone;
        if (tkey != lastTone)
        {
            const float fl = model == 1 ? 150.f : 220.f, fh = model == 1 ? 2500.f : (model == 2 ? 4500.f : 3500.f);
            lo.lowShelf (sr, fl, 0.7f, -tone * 0.5f);
            hi.highShelf (sr, fh, 0.7f, tone * 0.5f);
            lastTone = tkey;
        }

        const float target = dbToGain (v[3]), start = lastOut;
        lastOut = target;
        const int n = b.getNumSamples();
        for (int ch = 0; ch < chans (b); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float y = shapePre (model, d[i] * g) * comp;
                float yd = y - dcx[ch] + dcR * dcy[ch];
                dcx[ch] = y; dcy[ch] = yd;
                yd = hi.tick (ch, lo.tick (ch, yd));
                d[i] = yd * (start + (target - start) * (float) i / (float) n);
            }
        }
    }

private:
    double sr = 44100.0;
    Bq lo, hi;
    float dcx[2] {}, dcy[2] {}, dcR = 0.999f, comp = 1.f, lastKey = -1.f, lastTone = -99.f, lastOut = 1.f;
};

//==============================================================================
class ConsoleEqMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; reset(); for (auto& l : last) l = -9999.f; }
    void reset() override { lf.reset(); lmf.reset(); hmf.reset(); hf.reset(); }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        bool changed = false;
        for (int i = 0; i < 7; ++i) if (v[i] != last[i]) { changed = true; last[i] = v[i]; }
        if (changed)
        {
            const int model = (int) v[0];
            float lfF = 100.f, hfF = 8000.f, shQ = 0.7f, mQ1 = 1.0f, mQ2 = 1.0f;
            if (model == 1) { lfF = 70.f; hfF = 12000.f; shQ = 0.6f; mQ1 = mQ2 = 0.6f; }
            if (model == 2) { lfF = 100.f; hfF = 10000.f; shQ = 0.8f; mQ1 = 1.2f + std::abs (v[2]) * 0.1f; mQ2 = 1.2f + std::abs (v[4]) * 0.1f; }
            lf.lowShelf (sr, lfF, shQ, v[1]);
            lmf.peak (sr, v[3], mQ1, v[2]);
            hmf.peak (sr, v[5], mQ2, v[4]);
            hf.highShelf (sr, hfF, shQ, v[6]);
        }
        const int n = b.getNumSamples();
        for (int ch = 0; ch < chans (b); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = hf.tick (ch, hmf.tick (ch, lmf.tick (ch, lf.tick (ch, d[i]))));
        }
    }

private:
    double sr = 44100.0;
    Bq lf, lmf, hmf, hf;
    float last[7] {};
};

//==============================================================================
void setupProEq (Bq* bands, const float* v, double sr)
{
    bands[0].lowShelf (sr, v[0], v[2], v[1]);
    bands[1].peak (sr, v[3], v[5], v[4]);
    bands[2].peak (sr, v[6], v[8], v[7]);
    bands[3].highShelf (sr, v[9], v[11], v[10]);
}

class ProEqMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; reset(); for (auto& l : last) l = -9999.f; }
    void reset() override { for (auto& q : band) q.reset(); }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        bool changed = false;
        for (int i = 0; i < 12; ++i) if (v[i] != last[i]) { changed = true; last[i] = v[i]; }
        if (changed) setupProEq (band, v, sr);
        const int n = b.getNumSamples();
        for (int ch = 0; ch < chans (b); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                for (auto& q : band) x = q.tick (ch, x);
                d[i] = x;
            }
        }
    }

private:
    double sr = 44100.0;
    Bq band[4];
    float last[12] {};
};

//==============================================================================
class CompressorMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { gr = 0.f; meter = 0.f; }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const int style = (int) v[0];
        const float thr = v[1], ratio = v[2], rel = v[4], makeup = v[5], mix = v[6] / 100.f;
        const float knee = style == 0 ? 4.f : (style == 1 ? 12.f : 1.f);
        float attMs = v[3];
        if (style == 2) attMs *= 0.4f;
        if (style == 1) attMs = juce::jmax (attMs, 8.f);
        const float attC = std::exp (-1.f / (0.001f * attMs * (float) sr));
        const float relMs = style == 1 ? rel * (1.f + gr / 6.f) : rel;
        const float relC = std::exp (-1.f / (0.001f * relMs * (float) sr));
        const float slope = 1.f - 1.f / ratio;

        const int n = b.getNumSamples(), nc = chans (b);
        float* d[2] = { b.getWritePointer (0), nc > 1 ? b.getWritePointer (1) : nullptr };
        float maxGr = 0.f;

        for (int i = 0; i < n; ++i)
        {
            float lvl = std::abs (d[0][i]);
            if (nc > 1) lvl = juce::jmax (lvl, std::abs (d[1][i]));
            const float over = gainToDb (lvl) - thr;
            float target;
            if (2.f * over < -knee) target = 0.f;
            else if (2.f * over > knee) target = over * slope;
            else { const float t = over + knee * 0.5f; target = slope * t * t / (2.f * knee); }

            gr = target > gr ? attC * gr + (1.f - attC) * target : relC * gr + (1.f - relC) * target;
            maxGr = juce::jmax (maxGr, gr);
            const float gain = std::exp (-(gr - makeup) * 0.11512925f);

            for (int c = 0; c < nc; ++c)
            {
                float wet = d[c][i] * gain;
                if (style == 2) wet = std::tanh (wet * 1.1f) / 1.1f;
                d[c][i] = wet * mix + d[c][i] * (1.f - mix);
            }
        }
        meter = maxGr;
    }

private:
    double sr = 44100.0;
    float gr = 0.f;
};

//==============================================================================
class GateMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { env = 0.f; gr = 0.f; open = false; hold = 0; meter = 0.f; }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const float thr = v[0], range = v[1];
        const float attC = std::exp (-1.f / (0.001f * v[2] * (float) sr));
        const int holdN = (int) (0.001f * v[3] * (float) sr);
        const float relC = std::exp (-1.f / (0.001f * v[4] * (float) sr));
        const float detC = std::exp (-1.f / (0.01f * (float) sr));

        const int n = b.getNumSamples(), nc = chans (b);
        float maxGr = 0.f;
        for (int i = 0; i < n; ++i)
        {
            float lvl = 0.f;
            for (int c = 0; c < nc; ++c) lvl = juce::jmax (lvl, std::abs (b.getReadPointer (c)[i]));
            env = juce::jmax (lvl, env * detC);
            const float db = gainToDb (env);

            if (! open && db > thr) { open = true; hold = holdN; }
            else if (open)
            {
                if (db > thr - 3.f) hold = holdN;
                else if (hold > 0) --hold;
                else open = false;
            }
            const float target = open ? 0.f : range;
            gr = target < gr ? attC * gr + (1.f - attC) * target : relC * gr + (1.f - relC) * target;
            maxGr = juce::jmax (maxGr, gr);
            const float gain = std::exp (-gr * 0.11512925f);
            for (int c = 0; c < nc; ++c) b.getWritePointer (c)[i] *= gain;
        }
        meter = maxGr;
    }

private:
    double sr = 44100.0;
    float env = 0.f, gr = 0.f;
    bool open = false;
    int hold = 0;
};

//==============================================================================
class DeEsserMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; reset(); lastF = -1.f; }
    void reset() override { hpf.reset(); env = 0.f; gr = 0.f; meter = 0.f; }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        if (v[0] != lastF) { hpf.hp (sr, v[0], 0.7f); lastF = v[0]; }
        const float thr = v[1], amount = v[2];
        const float attC = std::exp (-1.f / (0.002f * (float) sr)), relC = std::exp (-1.f / (0.04f * (float) sr));
        const float detC = std::exp (-1.f / (0.02f * (float) sr));
        const int n = b.getNumSamples(), nc = chans (b);
        float maxGr = 0.f;
        for (int i = 0; i < n; ++i)
        {
            float band[2] = { 0.f, 0.f }, lvl = 0.f;
            for (int c = 0; c < nc; ++c)
            {
                band[c] = hpf.tick (c, b.getReadPointer (c)[i]);
                lvl = juce::jmax (lvl, std::abs (band[c]));
            }
            env = juce::jmax (lvl, env * detC);
            const float target = juce::jlimit (0.f, amount, (gainToDb (env) - thr) * 0.6f);
            gr = target > gr ? attC * gr + (1.f - attC) * target : relC * gr + (1.f - relC) * target;
            maxGr = juce::jmax (maxGr, gr);
            const float g = std::exp (-gr * 0.11512925f);
            for (int c = 0; c < nc; ++c)
                b.getWritePointer (c)[i] -= band[c] * (1.f - g);
        }
        meter = maxGr;
    }

private:
    double sr = 44100.0;
    Bq hpf;
    float lastF = -1.f, env = 0.f, gr = 0.f;
};

//==============================================================================
class SaturationMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; dcR = 1.f - 2 * kPi * 8.f / (float) s; reset(); lastKey = -1; lastTone = -99; }
    void reset() override { lo.reset(); hi.reset(); for (int c = 0; c < 2; ++c) dcx[c] = dcy[c] = 0; }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const int type = (int) v[0];
        const float driveDb = v[1], tone = v[2], mix = v[3] / 100.f;
        const float g = dbToGain (driveDb);

        const float key = (float) type * 1000.f + driveDb;
        if (key != lastKey) { comp = satCompensation ([type] (float x) { return shapeSat (type, x); }, g); lastKey = key; }
        if (tone != lastTone)
        {
            lo.lowShelf (sr, 200.f, 0.7f, -tone * 0.5f);
            hi.highShelf (sr, 3500.f, 0.7f, tone * 0.5f);
            lastTone = tone;
        }
        const float target = dbToGain (v[4]), start = lastOut;
        lastOut = target;
        const int n = b.getNumSamples();
        for (int ch = 0; ch < chans (b); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float dry = d[i];
                const float y = shapeSat (type, dry * g) * comp;
                float yd = y - dcx[ch] + dcR * dcy[ch];
                dcx[ch] = y; dcy[ch] = yd;
                yd = hi.tick (ch, lo.tick (ch, yd));
                d[i] = (yd * mix + dry * (1.f - mix)) * (start + (target - start) * (float) i / (float) n);
            }
        }
    }

private:
    double sr = 44100.0;
    Bq lo, hi;
    float dcx[2] {}, dcy[2] {}, dcR = 0.999f, comp = 1.f, lastKey = -1.f, lastTone = -99.f, lastOut = 1.f;
};

//==============================================================================
class FiltersMod : public Module
{
public:
    void prepare (double s, int) override { sr = s; reset(); lastHp = lastLp = lastSlope = -1.f; }
    void reset() override { hp1.reset(); hp2.reset(); lp1.reset(); lp2.reset(); }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const float hpF = v[0], lpF = v[1];
        const int slope = (int) v[2];
        if (hpF != lastHp || slope != (int) lastSlope)
        {
            if (slope == 1) { hp1.hp (sr, hpF, 0.5412f); hp2.hp (sr, hpF, 1.3066f); }
            else            { hp1.hp (sr, hpF, 0.7071f); }
            lastHp = hpF;
        }
        if (lpF != lastLp || slope != (int) lastSlope)
        {
            if (slope == 1) { lp1.lp (sr, lpF, 0.5412f); lp2.lp (sr, lpF, 1.3066f); }
            else            { lp1.lp (sr, lpF, 0.7071f); }
            lastLp = lpF;
        }
        lastSlope = (float) slope;

        const bool useHp = hpF > 21.f, useLp = lpF < 19500.f;
        const int n = b.getNumSamples();
        for (int ch = 0; ch < chans (b); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                if (useHp) { x = hp1.tick (ch, x); if (slope == 1) x = hp2.tick (ch, x); }
                if (useLp) { x = lp1.tick (ch, x); if (slope == 1) x = lp2.tick (ch, x); }
                d[i] = x;
            }
        }
    }

private:
    double sr = 44100.0;
    Bq hp1, hp2, lp1, lp2;
    float lastHp = -1.f, lastLp = -1.f, lastSlope = -1.f;
};

//==============================================================================
class StereoMod : public Module
{
public:
    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const float width = v[0] / 100.f, pan = v[1] / 100.f, lvl = dbToGain (v[2]);
        const int n = b.getNumSamples();
        if (chans (b) == 2)
        {
            const float th = (pan + 1.f) * kPi * 0.25f;
            const float tl = 1.41421356f * std::cos (th) * lvl, tr = 1.41421356f * std::sin (th) * lvl;
            auto* L = b.getWritePointer (0);
            auto* R = b.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float k = (float) i / (float) n;
                const float gl = lastL + (tl - lastL) * k, gr = lastR + (tr - lastR) * k;
                const float m = (L[i] + R[i]) * 0.5f, s = (L[i] - R[i]) * 0.5f * width;
                L[i] = (m + s) * gl;
                R[i] = (m - s) * gr;
            }
            lastL = tl; lastR = tr;
        }
        else
        {
            b.applyGainRamp (0, 0, n, lastL, lvl);
            lastL = lvl;
        }
    }

private:
    float lastL = 1.f, lastR = 1.f;
};

//==============================================================================
class DelayMod : public Module
{
public:
    void prepare (double s, int) override
    {
        sr = s; N = (int) (2.0 * s) + 4;
        for (auto& x : buf) x.assign ((size_t) N, 0.f);
        w = 0; dCur = 0.f; st[0] = st[1] = 0.f;
    }
    void reset() override { for (auto& x : buf) std::fill (x.begin(), x.end(), 0.f); st[0] = st[1] = 0.f; }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        if (N == 0) return;
        const float dT = juce::jlimit (4.f, (float) (N - 4), v[0] * 0.001f * (float) sr);
        if (dCur < 1.f) dCur = dT;
        const float fb = v[1] / 100.f, mix = v[3] / 100.f;
        const float a = 1.f - std::exp (-2 * kPi * v[2] / (float) sr);
        const int nc = chans (b), n = b.getNumSamples();
        const bool pp = ((int) v[4] == 1) && nc == 2;
        float* d[2] = { b.getWritePointer (0), nc > 1 ? b.getWritePointer (1) : nullptr };

        for (int i = 0; i < n; ++i)
        {
            dCur += (dT - dCur) * 0.0005f;
            float rp = (float) w - dCur;
            while (rp < 0.f) rp += (float) N;
            const int i0 = (int) rp, i1 = (i0 + 1) % N;
            const float fr = rp - (float) i0;
            float del[2] = { 0.f, 0.f };
            for (int c = 0; c < nc; ++c)
            {
                del[c] = buf[c][(size_t) i0] * (1.f - fr) + buf[c][(size_t) i1] * fr;
                st[c] += a * (del[c] - st[c]);
            }
            const float in0 = d[0][i], in1 = nc > 1 ? d[1][i] : in0;
            if (pp) { buf[0][(size_t) w] = (in0 + in1) * 0.5f + fb * st[1]; buf[1][(size_t) w] = fb * st[0]; }
            else
            {
                buf[0][(size_t) w] = in0 + fb * st[0];
                if (nc > 1) buf[1][(size_t) w] = in1 + fb * st[1];
            }
            for (int c = 0; c < nc; ++c)
                d[c][i] = d[c][i] * (1.f - 0.5f * mix) + del[c] * mix;
            if (++w >= N) w = 0;
        }
    }

private:
    double sr = 44100.0;
    std::vector<float> buf[2];
    int N = 0, w = 0;
    float dCur = 0.f, st[2] { 0.f, 0.f };
};

//==============================================================================
class ReverbMod : public Module
{
public:
    void prepare (double s, int) override { reverb.setSampleRate (s); reverb.reset(); }
    void reset() override { reverb.reset(); }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        juce::Reverb::Parameters p;
        p.roomSize = v[0] / 100.f; p.damping = v[1] / 100.f; p.width = v[2] / 100.f;
        const float mix = v[3] / 100.f;
        p.wetLevel = mix * 0.6f; p.dryLevel = 1.f - mix * 0.5f; p.freezeMode = 0.f;
        reverb.setParameters (p);
        if (chans (b) == 2) reverb.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
        else                reverb.processMono (b.getWritePointer (0), b.getNumSamples());
    }

private:
    juce::Reverb reverb;
};

//==============================================================================
class LimiterMod : public Module
{
public:
    void prepare (double s, int bs) override
    {
        maxB = juce::jmax (64, bs);
        juce::dsp::ProcessSpec sp { s, (juce::uint32) maxB, 2 };
        lim.prepare (sp);
    }
    void reset() override { lim.reset(); }

    void process (juce::AudioBuffer<float>& b, const float* v) override
    {
        const int n = b.getNumSamples();
        const float g = dbToGain (v[0]);
        b.applyGainRamp (0, n, lastG, g);
        lastG = g;
        lim.setThreshold (v[1]);
        lim.setRelease (v[2]);
        juce::dsp::AudioBlock<float> blk (b);
        for (int pos = 0; pos < n; pos += maxB)
        {
            auto sub = blk.getSubBlock ((size_t) pos, (size_t) juce::jmin (maxB, n - pos));
            lim.process (juce::dsp::ProcessContextReplacing<float> (sub));
        }
    }

private:
    juce::dsp::Limiter<float> lim;
    int maxB = 512;
    float lastG = 1.f;
};

template <class T> std::unique_ptr<Module> mk() { return std::make_unique<T>(); }

constexpr Kind L = Kind::Lin, G = Kind::Log, C = Kind::Choice;
} // namespace

//==============================================================================
float proEqMagDb (const float* v, float freq, double sr)
{
    Bq band[4];
    setupProEq (band, v, sr);
    float sum = 0.f;
    for (auto& q : band) sum += q.magDb (freq, sr);
    return sum;
}

const ModuleInfo& info (int type)
{
    static const std::vector<ModuleInfo> table = []
    {
        std::vector<ModuleInfo> t;
        t.push_back ({ "Empty", "", 0xff26282b, {}, nullptr });

        t.push_back ({ "PREAMP", "VINTAGE / MODERN PREAMP", 0xff7a2f2a,
            { { "Model", 0, 2, 1, C, "", "SSL|Neve|API", 0 },
              { "Drive", 0, 100, 25, L, "%", nullptr, 0 },
              { "Tone", -6, 6, 0, L, "dB", nullptr, 1 },
              { "Output", -12, 12, 0, L, "dB", nullptr, 1 } }, mk<PreampMod> });

        t.push_back ({ "CONSOLE EQ", "4-BAND CONSOLE EQ", 0xff2f5f86,
            { { "Model", 0, 2, 0, C, "", "SSL|Neve|API", 0 },
              { "LF", -15, 15, 0, L, "dB", nullptr, 1 },
              { "LMF Gain", -15, 15, 0, L, "dB", nullptr, 1 },
              { "LMF Freq", 150, 2500, 600, G, "Hz", nullptr, 0 },
              { "HMF Gain", -15, 15, 0, L, "dB", nullptr, 1 },
              { "HMF Freq", 600, 8000, 3000, G, "Hz", nullptr, 0 },
              { "HF", -15, 15, 0, L, "dB", nullptr, 1 } }, mk<ConsoleEqMod> });

        t.push_back ({ "PRO EQ", "PRECISE PARAMETRIC EQ", 0xff2b3036,
            { { "LS Freq", 20, 2000, 100, G, "Hz", nullptr, 0 }, { "LS Gain", -18, 18, 0, L, "dB", nullptr, 1 }, { "LS Q", 0.2f, 10, 0.7f, G, "", nullptr, 2 },
              { "B2 Freq", 60, 8000, 400, G, "Hz", nullptr, 0 }, { "B2 Gain", -18, 18, 0, L, "dB", nullptr, 1 }, { "B2 Q", 0.2f, 10, 1.f, G, "", nullptr, 2 },
              { "B3 Freq", 200, 16000, 2500, G, "Hz", nullptr, 0 }, { "B3 Gain", -18, 18, 0, L, "dB", nullptr, 1 }, { "B3 Q", 0.2f, 10, 1.f, G, "", nullptr, 2 },
              { "HS Freq", 1000, 20000, 8000, G, "Hz", nullptr, 0 }, { "HS Gain", -18, 18, 0, L, "dB", nullptr, 1 }, { "HS Q", 0.2f, 10, 0.7f, G, "", nullptr, 2 } }, mk<ProEqMod> });

        t.push_back ({ "COMPRESSOR", "VCA / OPTO / FET COMPRESSOR", 0xff3b4046,
            { { "Style", 0, 2, 0, C, "", "VCA|Opto|FET", 0 },
              { "Thresh", -40, 0, -18, L, "dB", nullptr, 1 },
              { "Ratio", 1.1f, 20, 3, G, ":1", nullptr, 1 },
              { "Attack", 0.1f, 100, 10, G, "ms", nullptr, 1 },
              { "Release", 20, 1500, 200, G, "ms", nullptr, 0 },
              { "Makeup", 0, 24, 0, L, "dB", nullptr, 1 },
              { "Mix", 0, 100, 100, L, "%", nullptr, 0 } }, mk<CompressorMod> });

        t.push_back ({ "GATE", "GATE / EXPANDER", 0xff3d5240,
            { { "Thresh", -80, 0, -45, L, "dB", nullptr, 1 },
              { "Range", 0, 80, 40, L, "dB", nullptr, 0 },
              { "Attack", 0.1f, 50, 1, G, "ms", nullptr, 1 },
              { "Hold", 1, 500, 50, G, "ms", nullptr, 0 },
              { "Release", 5, 1000, 150, G, "ms", nullptr, 0 } }, mk<GateMod> });

        t.push_back ({ "DE-ESSER", "SIBILANCE CONTROL", 0xff5e456e,
            { { "Freq", 2000, 12000, 6500, G, "Hz", nullptr, 0 },
              { "Thresh", -50, 0, -28, L, "dB", nullptr, 1 },
              { "Amount", 0, 15, 6, L, "dB", nullptr, 1 } }, mk<DeEsserMod> });

        t.push_back ({ "SATURATION", "TAPE / TUBE / XFMR / DIODE", 0xff85572a,
            { { "Type", 0, 3, 0, C, "", "Tape|Tube|Xfmr|Diode", 0 },
              { "Drive", 0, 24, 6, L, "dB", nullptr, 1 },
              { "Tone", -6, 6, 0, L, "dB", nullptr, 1 },
              { "Mix", 0, 100, 100, L, "%", nullptr, 0 },
              { "Output", -12, 12, 0, L, "dB", nullptr, 1 } }, mk<SaturationMod> });

        t.push_back ({ "FILTERS", "HIGH-PASS / LOW-PASS", 0xff4a4d50,
            { { "HP Freq", 20, 500, 20, G, "Hz", nullptr, 0 },
              { "LP Freq", 2000, 20000, 20000, G, "Hz", nullptr, 0 },
              { "Slope", 0, 1, 0, C, "", "12 dB|24 dB", 0 } }, mk<FiltersMod> });

        t.push_back ({ "STEREO", "WIDTH / PAN / LEVEL", 0xff3b4a69,
            { { "Width", 0, 200, 100, L, "%", nullptr, 0 },
              { "Pan", -100, 100, 0, L, "", nullptr, 0 },
              { "Level", -60, 12, 0, L, "dB", nullptr, 1 } }, mk<StereoMod> });

        t.push_back ({ "DELAY", "STEREO / PING-PONG DELAY", 0xff2a6465,
            { { "Time", 10, 1000, 250, G, "ms", nullptr, 0 },
              { "Feedback", 0, 95, 35, L, "%", nullptr, 0 },
              { "Tone", 1000, 16000, 6000, G, "Hz", nullptr, 0 },
              { "Mix", 0, 100, 25, L, "%", nullptr, 0 },
              { "Mode", 0, 1, 0, C, "", "Stereo|PingPong", 0 } }, mk<DelayMod> });

        t.push_back ({ "REVERB", "ROOM / HALL REVERB", 0xff3b3b78,
            { { "Size", 0, 100, 50, L, "%", nullptr, 0 },
              { "Damping", 0, 100, 50, L, "%", nullptr, 0 },
              { "Width", 0, 100, 100, L, "%", nullptr, 0 },
              { "Mix", 0, 100, 20, L, "%", nullptr, 0 } }, mk<ReverbMod> });

        t.push_back ({ "LIMITER", "PEAK LIMITER", 0xff7a2030,
            { { "Gain", 0, 18, 0, L, "dB", nullptr, 1 },
              { "Ceiling", -12, 0, -1, L, "dB", nullptr, 1 },
              { "Release", 10, 500, 100, G, "ms", nullptr, 0 } }, mk<LimiterMod> });
        return t;
    }();
    return table[(size_t) juce::jlimit (0, (int) table.size() - 1, type)];
}
} // namespace rack
