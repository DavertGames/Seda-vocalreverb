#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout SedaProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& d : kSedaParams)
    {
        juce::NormalisableRange<float> range (d.lo, d.hi, 0.0f);
        if (d.centre > 0.0f)
            range.setSkewForCentre (d.centre);

        const int decimals = d.decimals;

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { d.id, 1 },
            d.name,
            range,
            d.def,
            juce::AudioParameterFloatAttributes()
                .withLabel (juce::String (d.unit).trim())
                .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); })));
    }
    return layout;
}

SedaProcessor::SedaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SEDA_STATE", createLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (kSedaParams[i].id);
}

bool SedaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // salida estéreo; entrada mono (voz) o estéreo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void SedaProcessor::prepareToPlay (double sampleRate, int)
{
    reverb.prepare (sampleRate);
    setLatencySamples (0);   // el reverb no introduce latencia (el pre-delay es un parametro de efecto)
}

void SedaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // entrada mono -> duplicamos al canal derecho
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    raisePeak (inPeakL, buffer.getMagnitude (0, 0, numSamples));
    raisePeak (inPeakR, buffer.getMagnitude (1, 0, numSamples));

    auto v = [this] (SedaParam p) { return raw[(size_t) p]->load(); };

    vr::Params p;
    p.mix           = v (pMix);
    p.decaySec      = v (pDecay);
    p.preDelayMs    = v (pPreDelay);
    p.size          = v (pSize);
    p.damping       = v (pDamping);
    p.bass          = v (pBass);
    p.diffusion     = v (pDiffusion);
    p.early         = v (pEarly);
    p.modDepth      = v (pModDepth);
    p.modRate       = v (pModRate);
    p.width         = v (pWidth);
    p.deEss         = v (pDeEss);
    p.lowCutHz      = v (pLowCut);
    p.highCutHz     = v (pHighCut);
    p.duck          = v (pDuck);
    p.duckReleaseMs = v (pDuckRelease);

    reverb.setParams (p);
    reverb.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    raisePeak (outPeakL, buffer.getMagnitude (0, 0, numSamples));
    raisePeak (outPeakR, buffer.getMagnitude (1, 0, numSamples));
}

void SedaProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumSedaPresets)
        return;

    currentProgram = index;
    for (int i = 0; i < kNumParams; ++i)
        if (auto* prm = apvts.getParameter (kSedaParams[i].id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (kSedaPresets[index].v[i]));
}

const juce::String SedaProcessor::getProgramName (int index)
{
    return (index >= 0 && index < kNumSedaPresets) ? juce::String (kSedaPresets[index].name) : juce::String();
}

void SedaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SedaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SedaProcessor::createEditor()
{
    return new SedaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SedaProcessor();
}
