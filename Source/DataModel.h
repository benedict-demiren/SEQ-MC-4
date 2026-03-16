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
    BaseVelocity,   // Setting base velocity
    RotateMeasure,   // Rotate current measure by ST ticks (full)
    RotateNotesOnly  // Rotate only notes in current measure (preserve groove)
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

// A pattern is a complete event list with repeat marks
struct Pattern {
    std::vector<Event> events;
    std::vector<RepeatMark> repeatMarks;
    int pendingRepeatStart = -1;

    Pattern() {
        // Start with a single blank event
        Event e;
        e.pitch = -1;
        e.step_time = 30;
        e.gate_time = 30;
        events.push_back(e);
    }
};

struct Channel {
    std::vector<Pattern> patterns;
    int activePattern = 0;  // Which pattern the editor is working on
    int cursorPos   = 0;
    int activeLayer = 0; // Index into Layer enum
    int fieldCursor = 0; // Index into Field enum

    Channel() {
        patterns.emplace_back(); // Start with one pattern
    }

    // Convenience: current pattern's events and marks
    std::vector<Event>& events() { return patterns[activePattern].events; }
    const std::vector<Event>& events() const { return patterns[activePattern].events; }
    std::vector<RepeatMark>& repeatMarks() { return patterns[activePattern].repeatMarks; }
    const std::vector<RepeatMark>& repeatMarks() const { return patterns[activePattern].repeatMarks; }
    int& pendingRepeatStart() { return patterns[activePattern].pendingRepeatStart; }
    int pendingRepeatStart() const { return patterns[activePattern].pendingRepeatStart; }

    void clampCursor() {
        auto& evts = events();
        if (evts.empty()) {
            cursorPos = 0;
            return;
        }
        if (cursorPos < 0) cursorPos = 0;
        if (cursorPos >= (int)evts.size()) cursorPos = (int)evts.size() - 1;
    }

    // Find measure number and step-within-measure for a given event index
    void getMeasureInfo(int eventIdx, int& measureNum, int& stepInMeasure) const {
        auto& evts = events();
        measureNum = 1;
        stepInMeasure = 1;
        for (int i = 0; i < eventIdx && i < (int)evts.size(); ++i) {
            ++stepInMeasure;
            if (evts[i].measure_end) {
                ++measureNum;
                stepInMeasure = 1;
            }
        }
    }

    // Find the end event index (exclusive) of a given measure number (1-based)
    int findMeasureEnd(int targetMeasure) const {
        auto& evts = events();
        int measure = 1;
        for (int i = 0; i < (int)evts.size(); ++i) {
            if (evts[i].measure_end) {
                if (measure == targetMeasure)
                    return i + 1; // Exclusive end
                ++measure;
            }
        }
        if (measure == targetMeasure)
            return (int)evts.size();
        return (int)evts.size();
    }

    // Extract events from a range of measures [startMeas, endMeas] (1-based, inclusive)
    std::vector<Event> getEventsInMeasureRange(int startMeas, int endMeas) const {
        int startIdx = findMeasureStart(startMeas);
        int endIdx = findMeasureEnd(endMeas);
        auto& evts = events();
        if (startIdx >= (int)evts.size() || startIdx >= endIdx)
            return {};
        return std::vector<Event>(evts.begin() + startIdx, evts.begin() + endIdx);
    }

    // Find the first event index of a given measure number (1-based)
    int findMeasureStart(int targetMeasure) const {
        auto& evts = events();
        int measure = 1;
        if (targetMeasure <= 1) return 0;
        for (int i = 0; i < (int)evts.size(); ++i) {
            if (evts[i].measure_end) {
                ++measure;
                if (measure == targetMeasure) {
                    return std::min(i + 1, (int)evts.size() - 1);
                }
            }
        }
        return (int)evts.size() - 1;
    }

    // Update repeat mark indices after an insert at position insertIdx (count events inserted)
    void adjustRepeatMarksForInsert(int insertIdx, int count = 1) {
        for (auto& rm : repeatMarks()) {
            if (rm.startEvent >= insertIdx) rm.startEvent += count;
            if (rm.endEvent >= insertIdx) rm.endEvent += count;
        }
        if (pendingRepeatStart() >= insertIdx)
            pendingRepeatStart() += count;
    }

    // Update repeat mark indices after a delete at position deleteIdx (count events removed)
    // Returns false if any marks become invalid and were removed
    void adjustRepeatMarksForDelete(int deleteIdx, int count = 1) {
        int endDel = deleteIdx + count;
        // Remove marks that are fully within the deleted range
        for (int i = (int)repeatMarks().size() - 1; i >= 0; --i) {
            auto& rm = repeatMarks()[i];
            // If start or end falls within deleted range, remove the mark
            if ((rm.startEvent >= deleteIdx && rm.startEvent < endDel) ||
                (rm.endEvent >= deleteIdx && rm.endEvent < endDel)) {
                repeatMarks().erase(repeatMarks().begin() + i);
                continue;
            }
            // Shift indices down
            if (rm.startEvent >= endDel) rm.startEvent -= count;
            if (rm.endEvent >= endDel) rm.endEvent -= count;
        }
        // Clear pending if it was in the deleted range
        if (pendingRepeatStart() >= deleteIdx && pendingRepeatStart() < endDel)
            pendingRepeatStart() = -1;
        else if (pendingRepeatStart() >= endDel)
            pendingRepeatStart() -= count;
    }

    // Total ticks in a measure range
    int measureTotalTicks(int startIdx, int endIdx) const {
        auto& evts = events();
        int total = 0;
        for (int i = startIdx; i < endIdx && i < (int)evts.size(); ++i)
            total += evts[i].step_time;
        return total;
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

// Undo/redo snapshot of a single channel (includes all patterns)
struct ChannelSnapshot {
    std::vector<Pattern> patterns;
    int activePattern = 0;
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
