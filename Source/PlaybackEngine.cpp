#include "PlaybackEngine.h"

namespace mc4 {

void PlaybackEngine::reset() {
    for (auto& ps : playState)
        ps.reset();
}

void PlaybackEngine::processMidi(juce::MidiBuffer& midiBuffer,
                                  int numSamples,
                                  const Sequence& seq,
                                  const Config& config,
                                  bool isPlaying,
                                  bool wasPlaying)
{
    // Transition: was playing -> stopped
    if (wasPlaying && !isPlaying) {
        allNotesOff(midiBuffer, 0);
        reset();
        return;
    }

    if (!isPlaying)
        return;

    // Transition: was stopped -> playing
    if (!wasPlaying && isPlaying) {
        reset();
    }

    for (int ch = 0; ch < 4; ++ch) {
        const auto& channel = seq.channels[ch];
        const auto& pat = channel.patterns[channel.activePattern];
        processChannel(midiBuffer, ch, numSamples, channel, pat, seq, config);
    }
}

void PlaybackEngine::processChannel(juce::MidiBuffer& midiBuffer,
                                     int channelIdx,
                                     int numSamples,
                                     const Channel& channel,
                                     const Pattern& pattern,
                                     const Sequence& seq,
                                     const Config& config)
{
    auto& ps = playState[channelIdx];

    if (pattern.events.empty() || ps.finished)
        return;

    // MPE member channels 1-4 (JUCE uses 1-based)
    const int midiChannel = channelIdx + 1;

    // Ticks per sample = (BPM * timebase) / (60 * sampleRate)
    const double ticksPerSample = (double(seq.tempo) * double(seq.timebase))
                                  / (60.0 * sampleRate);

    for (int sample = 0; sample < numSamples; ++sample) {
        const auto& event = pattern.events[ps.eventIndex];

        // Check if we need to fire note-off (gate expired)
        if (ps.noteIsOn) {
            if (ps.gateTickCount >= event.gate_time) {
                sendNoteOff(midiBuffer, sample, midiChannel, ps.currentNote);
                ps.noteIsOn = false;
                ps.currentNote = -1;
            }
        }

        // Check if step time elapsed -> advance to next event
        if (ps.tickCounter >= event.step_time) {
            ps.tickCounter -= event.step_time;

            // Turn off current note if still on
            if (ps.noteIsOn) {
                sendNoteOff(midiBuffer, sample, midiChannel, ps.currentNote);
                ps.noteIsOn = false;
                ps.currentNote = -1;
            }

            // Advance to next event
            ps.eventIndex++;

            // Check repeat marks: did we just pass a repeat end?
            for (int mi = 0; mi < (int)pattern.repeatMarks.size(); ++mi) {
                const auto& mark = pattern.repeatMarks[mi];
                if (ps.eventIndex > mark.endEvent && mark.startEvent >= 0 && mark.endEvent >= 0) {
                    // Find this mark in the repeat stack
                    bool found = false;
                    for (auto& rs : ps.repeatStack) {
                        if (rs.markIndex == mi) {
                            rs.iteration++;
                            if (rs.iteration < mark.count) {
                                // Loop back to start
                                ps.eventIndex = mark.startEvent;
                            }
                            // If iterations exhausted, continue past
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // First time hitting this repeat end — start iteration tracking
                        RepeatPlayState rs;
                        rs.markIndex = mi;
                        rs.iteration = 1; // Already played once
                        if (rs.iteration < mark.count) {
                            ps.eventIndex = mark.startEvent;
                        }
                        ps.repeatStack.push_back(rs);
                    }
                }
            }

            if (ps.eventIndex >= (int)pattern.events.size()) {
                if (seq.cycleOn) {
                    ps.eventIndex = 0;
                    ps.repeatStack.clear(); // Reset repeat state on cycle
                } else {
                    ps.finished = true;
                    return;
                }
            }

            // Fire note-on for new event (if gate_time > 0 and pitch is valid)
            const auto& nextEvent = pattern.events[ps.eventIndex];
            if (nextEvent.gate_time > 0 && nextEvent.pitch >= 0) {
                int velocity = (nextEvent.velocity >= 0) ? nextEvent.velocity : config.baseVelocity;
                if (nextEvent.accent)
                    velocity = std::min(127, velocity + config.accentBoost);

                sendNoteOn(midiBuffer, sample, midiChannel, nextEvent.pitch, velocity);
                ps.noteIsOn = true;
                ps.currentNote = nextEvent.pitch;
                ps.gateTickCount = 0.0;
            } else {
                ps.gateTickCount = 0.0;
            }
        }

        // First event note-on (at the very start of playback)
        if (ps.tickCounter < ticksPerSample && !ps.noteIsOn && ps.eventIndex == 0
            && ps.gateTickCount == 0.0 && event.gate_time > 0 && event.pitch >= 0) {
            int velocity = (event.velocity >= 0) ? event.velocity : config.baseVelocity;
            if (event.accent)
                velocity = std::min(127, velocity + config.accentBoost);

            sendNoteOn(midiBuffer, sample, midiChannel, event.pitch, velocity);
            ps.noteIsOn = true;
            ps.currentNote = event.pitch;
        }

        ps.tickCounter += ticksPerSample;
        if (ps.noteIsOn)
            ps.gateTickCount += ticksPerSample;
    }
}

void PlaybackEngine::sendNoteOn(juce::MidiBuffer& buf, int sampleOffset,
                                 int midiChannel, int note, int velocity)
{
    buf.addEvent(juce::MidiMessage::noteOn(midiChannel, note, (juce::uint8)velocity),
                 sampleOffset);
}

void PlaybackEngine::sendNoteOff(juce::MidiBuffer& buf, int sampleOffset,
                                  int midiChannel, int note)
{
    buf.addEvent(juce::MidiMessage::noteOff(midiChannel, note, (juce::uint8)0),
                 sampleOffset);
}

void PlaybackEngine::allNotesOff(juce::MidiBuffer& buf, int sampleOffset)
{
    for (int ch = 1; ch <= 4; ++ch) {
        auto& ps = playState[ch - 1];
        if (ps.noteIsOn && ps.currentNote >= 0) {
            sendNoteOff(buf, sampleOffset, ch, ps.currentNote);
            ps.noteIsOn = false;
            ps.currentNote = -1;
        }
    }
}

} // namespace mc4
