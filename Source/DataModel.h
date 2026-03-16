#pragma once
#include <vector>
#include <string>
#include <cmath>

namespace mc4 {

// Memory layers (what the numeric keypad edits)
enum class Layer { CV1 = 0, StepTime, GateTime, Velocity, CV2, MPX, NumLayers };

// Fields within the current event display
enum class Field { Pitch = 0, StepTime, GateTime, Velocity, CV2, Accent, Slide, MPX, NumFields };

// Editor mode
enum class Mode { Edit, Play };

// Input mode for multi-step commands
enum class InputMode {
    Normal,         // Standard numeric entry
    InsertMulti,    // Waiting for count after Shift+I
    DeleteMulti,    // Waiting for count after Shift+D
    Divide,         // Waiting for divisor after V
    TempoEdit,      // Editing tempo after Shift+T
    TimebaseEdit,   // Editing timebase after Shift+B
    CopyStartMeas,  // Copy: waiting for start measure number
    CopyEndMeas,    // Copy: waiting for end measure number
    CopyReps,       // Copy: waiting for repetitions count
    CopyTranspose,  // Copy: waiting for transpose semitones
    RepeatEnd,      // Repeat end: waiting for repeat count
    DefaultNote,    // Setting default note for new events
    BaseVelocity    // Setting base velocity
};

// State for multi-step copy command
struct CopyState {
    int startMeasure = 1;
    int endMeasure   = 1;
    int repetitions  = 1;
    int transpose    = 0;
    bool insertMode  = false; // true = copy-insert (ripple), false = copy (overwrite)
};

struct Event {
    int pitch      = 48;   // MIDI note 0-127, or -1 for "unset/blank" (default C3)
    int step_time  = 30;   // Ticks until next event (default 16th at TB=120)
    int gate_time  = 0;    // Sounded duration in ticks (default silent/rest)
    int velocity   = -1;   // Per-event velocity 1-127, or -1 = use base velocity
    int cv2        = 64;   // Secondary continuous value 0-127
    bool accent    = false; // Per-step accent flag
    bool slide     = false; // Per-step portamento flag
    bool measure_end = false; // Marks end of a measure
    bool mpx       = false; // Secondary gate flag
};

// Repeat section (playback-only — data stored once, played N times)
struct RepeatMark {
    int startEvent = -1;  // Event index of repeat start (-1 = not set)
    int endEvent   = -1;  // Event index of repeat end (-1 = not set)
    int count      = 2;   // Total plays (2 = play twice)
};

struct Channel {
    std::vector<Event> events;
    int cursorPos   = 0;
    int activeLayer = 0; // Index into Layer enum
    int fieldCursor = 0; // Index into Field enum

    // Repeat marks (multiple allowed per channel)
    std::vector<RepeatMark> repeatMarks;
    int pendingRepeatStart = -1; // Temp: event index marked with R, waiting for Shift+R

    Channel() {
        // Start with a single blank event — build from here
        Event e;
        e.pitch = -1;       // Blank/unset
        e.step_time = 30;   // Default 16th at TB=120
        e.gate_time = 30;   // Default gate = step (legato)
        events.push_back(e);
    }

    void clampCursor() {
        if (events.empty()) {
            cursorPos = 0;
            return;
        }
        if (cursorPos < 0) cursorPos = 0;
        if (cursorPos >= (int)events.size()) cursorPos = (int)events.size() - 1;
    }

    // Find measure number and step-within-measure for a given event index
    void getMeasureInfo(int eventIdx, int& measureNum, int& stepInMeasure) const {
        measureNum = 1;
        stepInMeasure = 1;
        for (int i = 0; i < eventIdx && i < (int)events.size(); ++i) {
            ++stepInMeasure;
            if (events[i].measure_end) {
                ++measureNum;
                stepInMeasure = 1;
            }
        }
    }

    // Find the end event index (exclusive) of a given measure number (1-based)
    int findMeasureEnd(int targetMeasure) const {
        int measure = 1;
        for (int i = 0; i < (int)events.size(); ++i) {
            if (events[i].measure_end) {
                if (measure == targetMeasure)
                    return i + 1; // Exclusive end
                ++measure;
            }
        }
        // If target measure is the last (or only) measure, end is the list end
        if (measure == targetMeasure)
            return (int)events.size();
        return (int)events.size();
    }

    // Extract events from a range of measures [startMeas, endMeas] (1-based, inclusive)
    std::vector<Event> getEventsInMeasureRange(int startMeas, int endMeas) const {
        int startIdx = findMeasureStart(startMeas);
        int endIdx = findMeasureEnd(endMeas);
        if (startIdx >= (int)events.size() || startIdx >= endIdx)
            return {};
        return std::vector<Event>(events.begin() + startIdx, events.begin() + endIdx);
    }

    // Find the first event index of a given measure number (1-based)
    int findMeasureStart(int targetMeasure) const {
        int measure = 1;
        if (targetMeasure <= 1) return 0;
        for (int i = 0; i < (int)events.size(); ++i) {
            if (events[i].measure_end) {
                ++measure;
                if (measure == targetMeasure) {
                    return std::min(i + 1, (int)events.size() - 1);
                }
            }
        }
        return (int)events.size() - 1; // Past last measure
    }
};

struct Sequence {
    Channel channels[4];
    int activeChannel = 0;  // 0-3
    int timebase      = 120; // Ticks per beat
    int tempo         = 120; // BPM (used in standalone; host overrides in plugin)
    bool cycleOn      = true;

    Channel& ch() { return channels[activeChannel]; }
    const Channel& ch() const { return channels[activeChannel]; }
};

// Configuration (settings panel values)
struct Config {
    int cv2OutputMode    = 0;    // 0=Pressure, 1=Slide(CC74), 2=Custom CC
    int mpxOutputCC      = 1;    // CC number for MPX output
    int accentBoost      = 32;   // Velocity boost for accented notes
    int baseVelocity     = 90;   // Base MIDI velocity
    int defaultNote      = -1;   // Default pitch for new events (-1 = blank/unset)
    int portamentoTime   = 50;   // % of step time
    int portamentoCurve  = 0;    // 0=Linear, 1=Exp, 2=Log
    int pitchBendRange   = 48;   // Semitones
    int noteDisplayMode  = 1;    // 0=Number, 1=NoteName, 2=Both
};

// Utility: MIDI note number to note name string
inline std::string midiNoteToName(int note) {
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0) return "---";
    if (note > 127) return "---";
    int octave = (note / 12) - 1;
    return std::string(names[note % 12]) + std::to_string(octave);
}

// Undo/redo snapshot of a single channel
struct ChannelSnapshot {
    std::vector<Event> events;
    int cursorPos = 0;
    int fieldCursor = 0;
    int activeLayer = 0;
};

// Utility: pad/truncate string to fixed width
inline std::string padRight(const std::string& s, int width) {
    if ((int)s.size() >= width) return s.substr(0, width);
    return s + std::string(width - s.size(), ' ');
}

inline std::string padLeft(const std::string& s, int width) {
    if ((int)s.size() >= width) return s.substr(0, width);
    return std::string(width - s.size(), ' ') + s;
}

} // namespace mc4
