#include "PluginProcessor.h"
#include "PluginEditor.h"

TubeLiftProcessor::TubeLiftProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TubeLift", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TubeLiftProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
                                                  NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));

    NormalisableRange<float> lc (20.0f, 1000.0f, 0.1f);
    lc.setSkewForCentre (150.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lowcut", 1 }, "Low Cut", lc, 20.0f,
                                                  AudioParameterFloatAttributes().withLabel ("Hz")));

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone",
                                                  NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

    NormalisableRange<float> hc (1000.0f, 20000.0f, 1.0f);
    hc.setSkewForCentre (5000.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "highcut", 1 }, "High Cut", hc, 20000.0f,
                                                  AudioParameterFloatAttributes().withLabel ("Hz")));

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                  NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    return l;
}

bool TubeLiftProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void TubeLiftProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (32, samplesPerBlock);
    const int nch = juce::jmax (1, getTotalNumOutputChannels());

    engine.prepare (sampleRate, maxBlock, nch);
    engine.setParams (getRaw ("drive"), getRaw ("lowcut"), getRaw ("tone"), getRaw ("highcut"), true);

    const int lat = engine.latency();
    setLatencySamples (lat);

    mixer.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) nch });
    mixer.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    mixer.setWetLatency ((float) lat);
    mixer.setWetMixProportion (getRaw ("mix") / 100.0f);

    outSm.reset (sampleRate, 0.03);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (getRaw ("output")));
}

void TubeLiftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());

    const int nch = juce::jmin (numIn, numOut, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (nch == 0 || total == 0) return;

    engine.setParams (getRaw ("drive"), getRaw ("lowcut"), getRaw ("tone"), getRaw ("highcut"));
    mixer.setWetMixProportion (getRaw ("mix") / 100.0f);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (getRaw ("output")));

    juce::dsp::AudioBlock<float> all (buffer);
    auto block = all.getSubsetChannelBlock (0, (size_t) nch);

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        auto sub = block.getSubBlock ((size_t) start, (size_t) n);
        mixer.pushDrySamples (sub);
        engine.process (sub);
        mixer.mixWetSamples (sub);
    }

    float pk = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        const float g = outSm.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            d[i] *= g;
            pk = std::max (pk, std::abs (d[i]));
        }
    }
    float prev = outLevel.load();
    while (pk > prev && ! outLevel.compare_exchange_weak (prev, pk)) {}
}

juce::AudioProcessorEditor* TubeLiftProcessor::createEditor() { return new TubeLiftEditor (*this); }

void TubeLiftProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TubeLiftProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            uiScale = (float) t.getProperty ("uiScale", 1.0f);
            apvts.replaceState (t);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TubeLiftProcessor(); }
