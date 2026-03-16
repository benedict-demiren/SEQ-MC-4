#pragma once
#include "DataModel.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <mutex>

namespace mc4 {

// Per-channel playback state
// Tracks iteration count for each active repeat mark
struct RepeatPlayState {
    int markIndex    = -1;  // Which RepeatMark we're inside
    int iteration    = 0;   // Current iteration (0-based)
};

struct ChannelPlayState {
    double tickCounter   = 0.0;  // Fractional tick accumulator
    int    eventIndex    = 0;    // Current event being played
    bool   noteIsOn      = false;
    int    currentNote   = -1;   // MIDI note currently sounding
    double gateTickCount = 0.0;  // Ticks elapsed since note-on (for gate timing)
    bool   finished      = false; // Reached end of sequence (non-cycle mode)

    // Repeat mark playback state (stack for nested repeats)
    std::vector<RepeatPlayState> repeatStack;

    void reset() {
        tickCounter = 0.0;
        eventIndex = 0;
        noteIsOn = false;
        currentNote = -1;
        gateTickCount = 0.0;
        finished = false;
        repeatStack.clear();
    }
};

class PlaybackEngine {
public:
    PlaybackEngine() = default;

    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
    }

    // Called from audio thread. Reads sequence data, writes MIDI output.
    // mutex must be held (or try_locked) by caller.
    void processMidi(juce::MidiBuffer& midiBuffer,
                     int numSamples,
                     const Sequence& seq,
                     const Config& config,
                     bool isPlaying,
                     bool wasPlaying);

    // Reset all playback state (on stop, or song position change)
    void reset();

    // Get current playback position for UI display
    int getPlaybackPosition(int channelIdx) const {
        return playState[channelIdx].eventIndex;
    }

    bool isChannelPlaying(int channelIdx) const {
        return playState[channelIdx].noteIsOn;
    }

private:
    double sampleRate = 44100.0;
    ChannelPlayState playState[4];

    void processChannel(juce::MidiBuffer& midiBuffer,
                        int channelIdx,
                        int numSamples,
                        const Channel& channel,
                        const Sequence& seq,
                        const Config& config);

    void sendNoteOn(juce::MidiBuffer& buf, int sampleOffset, int midiChannel,
                    int note, int velocity);
    void sendNoteOff(juce::MidiBuffer& buf, int sampleOffset, int midiChannel,
                     int note);
    void allNotesOff(juce::MidiBuffer& buf, int sampleOffset);
};

} // namespace mc4
