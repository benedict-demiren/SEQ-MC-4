#include "PluginProcessor.h"
#include "PluginEditor.h"

SEQMC4Processor::SEQMC4Processor()
    : AudioProcessor(BusesProperties()) // No audio buses — MIDI only
{
}

SEQMC4Processor::~SEQMC4Processor() = default;

void SEQMC4Processor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    engine.prepare(sampleRate);
    engine.reset();
    wasPlaying = false;
}

void SEQMC4Processor::releaseResources()
{
    engine.reset();
}

void SEQMC4Processor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    buffer.clear(); // No audio output

    // Determine play state
    bool playing = false;

    if (auto* playHead = getPlayHead()) {
        if (auto pos = playHead->getPosition()) {
            playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
                hostTempo.store(*bpm);
        }
    }

    // Standalone mode: use the editor's play toggle
    if (wrapperType == wrapperType_Standalone) {
        playing = standalonePlayRequest.load();
    }

    isCurrentlyPlaying.store(playing);

    // Try to lock the sequence for reading
    midiMessages.clear();

    if (sequenceMutex.try_lock()) {
        engine.processMidi(midiMessages, buffer.getNumSamples(),
                           sequence, config, playing, wasPlaying);
        sequenceMutex.unlock();
    }

    wasPlaying = playing;
}

juce::AudioProcessorEditor* SEQMC4Processor::createEditor()
{
    return new SEQMC4Editor(*this);
}

void SEQMC4Processor::getStateInformation(juce::MemoryBlock& /*destData*/)
{
    // Phase 4: JSON serialisation
}

void SEQMC4Processor::setStateInformation(const void* /*data*/, int /*sizeInBytes*/)
{
    // Phase 4: JSON deserialisation
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SEQMC4Processor();
}
