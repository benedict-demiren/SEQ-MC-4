#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DataModel.h"
#include "PlaybackEngine.h"
#include <mutex>

class SEQMC4Processor : public juce::AudioProcessor {
public:
    SEQMC4Processor();
    ~SEQMC4Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SEQ-MC-4"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Data model — accessed by editor (UI thread) and processor (audio thread)
    mc4::Sequence sequence;
    mc4::Config config;
    std::mutex sequenceMutex; // Lock for thread-safe access

    // Playback engine
    mc4::PlaybackEngine engine;

    // Transport state for editor display
    std::atomic<bool> isCurrentlyPlaying { false };
    std::atomic<double> hostTempo { 120.0 }; // Last known host tempo

    // Editor triggers play/stop via this flag (standalone mode)
    std::atomic<bool> standalonePlayRequest { false };

private:
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SEQMC4Processor)
};
