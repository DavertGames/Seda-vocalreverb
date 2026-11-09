#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "VocalReverbDSP.h"

// Orden = orden en la interfaz (4 perillas por fila)
enum SedaParam
{
    pMix, pDecay, pPreDelay, pSize,
    pDamping, pBass, pDiffusion, pEarly,
    pModDepth, pModRate, pWidth, pDeEss,
    pLowCut, pHighCut, pDuck, pDuckRelease,
    kNumParams
};

struct SedaParamDef
{
    const char* id;
    const char* name;
    float lo, hi, def;
    float centre;     // < 0 => escala lineal; si no, el valor que queda en el centro del recorrido
    const char* unit;
    int decimals;
};

inline const SedaParamDef kSedaParams[kNumParams] =
{
    { "mix",         "Mix",         0.0f,    1.0f,     0.20f,  -1.0f,  "",    2 },
    { "decay",       "Decay",       0.2f,    15.0f,    1.9f,    2.5f,  " s",  2 },
    { "predelay",    "Pre-Delay",   0.0f,    250.0f,   22.0f,   40.0f, " ms", 0 },
    { "size",        "Size",        0.5f,    2.0f,     0.9f,   -1.0f,  "",    2 },

    { "damping",     "Damping",     0.0f,    1.0f,     0.50f,  -1.0f,  "",    2 },
    { "bass",        "Bass Decay",  0.25f,   1.5f,     0.55f,  -1.0f,  "x",   2 },
    { "diffusion",   "Diffusion",   0.0f,    1.0f,     0.85f,  -1.0f,  "",    2 },
    { "early",       "Early Refl.", 0.0f,    1.0f,     0.35f,  -1.0f,  "",    2 },

    { "moddepth",    "Mod Depth",   0.0f,    1.0f,     0.30f,  -1.0f,  "",    2 },
    { "modrate",     "Mod Rate",    0.1f,    3.0f,     0.60f,   0.7f,  " Hz", 2 },
    { "width",       "Width",       0.0f,    2.0f,     1.00f,  -1.0f,  "",    2 },
    { "deess",       "De-Ess",      0.0f,    1.0f,     0.35f,  -1.0f,  "",    2 },

    { "lowcut",      "Low Cut",     20.0f,   1000.0f,  180.0f, 200.0f, " Hz", 0 },
    { "highcut",     "High Cut",    1000.0f, 20000.0f, 10000.0f, 7000.0f, " Hz", 0 },
    { "duck",        "Duck",        0.0f,    1.0f,     0.40f,  -1.0f,  "",    2 },
    { "duckrelease", "Duck Release", 30.0f,  1000.0f,  250.0f, 250.0f, " ms", 0 },
};

struct SedaPreset
{
    const char* name;
    float v[kNumParams];
};

//  mix  decay  pre  size | damp bass diff early | modD modR width deEss | low  high   duck duckRel
inline const SedaPreset kSedaPresets[] =
{
    { "Silk Plate",     { 0.20f, 1.9f, 22.0f, 0.9f,   0.50f, 0.55f, 0.85f, 0.35f,   0.30f, 0.60f, 1.00f, 0.35f,   180.0f, 10000.0f, 0.40f, 250.0f } },
    { "Warm Hall",      { 0.22f, 3.2f, 38.0f, 1.4f,   0.62f, 0.65f, 0.80f, 0.45f,   0.40f, 0.50f, 1.10f, 0.35f,   160.0f,  8500.0f, 0.45f, 320.0f } },
    { "Intimate Room",  { 0.16f, 0.9f,  8.0f, 0.6f,   0.40f, 0.50f, 0.70f, 0.60f,   0.20f, 0.80f, 0.90f, 0.30f,   200.0f,  9500.0f, 0.25f, 180.0f } },
    { "Ballad Bloom",   { 0.26f, 4.5f, 60.0f, 1.6f,   0.68f, 0.70f, 0.85f, 0.30f,   0.50f, 0.40f, 1.30f, 0.40f,   170.0f,  8000.0f, 0.65f, 450.0f } },
    { "Ethereal",       { 0.30f, 8.0f, 80.0f, 2.0f,   0.72f, 0.85f, 0.90f, 0.15f,   0.65f, 0.35f, 1.50f, 0.45f,   200.0f,  7500.0f, 0.60f, 600.0f } },
};
inline constexpr int kNumSedaPresets = (int) (sizeof (kSedaPresets) / sizeof (kSedaPresets[0]));

class SedaProcessor : public juce::AudioProcessor
{
public:
    SedaProcessor();
    ~SedaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Seda Vocal Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 15.0; }

    int getNumPrograms() override { return kNumSedaPresets; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Medidores: pico lineal desde la ultima lectura (seguro entre hilos).
    // Lo lee el editor desde su timer; cada lectura reinicia el acumulado.
    void readInputPeaks  (float& l, float& r) { l = inPeakL.exchange (0.0f);  r = inPeakR.exchange (0.0f); }
    void readOutputPeaks (float& l, float& r) { l = outPeakL.exchange (0.0f); r = outPeakR.exchange (0.0f); }

private:
    static void raisePeak (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    std::atomic<float> inPeakL { 0.0f }, inPeakR { 0.0f }, outPeakL { 0.0f }, outPeakR { 0.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    vr::VocalReverb reverb;
    std::array<std::atomic<float>*, kNumParams> raw {};
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SedaProcessor)
};
