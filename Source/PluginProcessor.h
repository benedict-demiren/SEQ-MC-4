#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DataModel.h"
#include "PlaybackEngine.h"
#include <mutex>
#include <atomic>

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

    bool acceptsMidi() const override { return true; }
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

    // Step record: incoming MIDI notes from audio thread -> UI thread
    // Lock-free ring buffer of note-on pitches (0-127) and velocities
    static constexpr int kMidiRingSize = 64;
    struct MidiNoteIn { int pitch; int velocity; };
    MidiNoteIn midiRing[kMidiRingSize] {};
    std::atomic<int> midiRingWrite { 0 };
    std::atomic<int> midiRingRead  { 0 };

    void pushMidiNote(int pitch, int velocity) {
        int w = midiRingWrite.load(std::memory_order_relaxed);
        int next = (w + 1) % kMidiRingSize;
        if (next != midiRingRead.load(std::memory_order_acquire)) {
            midiRing[w] = { pitch, velocity };
            midiRingWrite.store(next, std::memory_order_release);
        }
    }

    // Called by editor to poll for incoming notes
public:
    bool popMidiNote(int& pitch, int& velocity) {
        int r = midiRingRead.load(std::memory_order_relaxed);
        if (r == midiRingWrite.load(std::memory_order_acquire))
            return false;
        pitch = midiRing[r].pitch;
        velocity = midiRing[r].velocity;
        midiRingRead.store((r + 1) % kMidiRingSize, std::memory_order_release);
        return true;
    }

    // Step record mode toggle
    std::atomic<bool> stepRecordEnabled { false };

private:
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SEQMC4Processor)
};
