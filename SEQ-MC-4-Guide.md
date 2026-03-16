# SEQ-MC-4: System Overview

A keyboard-driven MPE step sequencer plugin inspired by the Roland MC-4 MicroComposer (1981). Built in JUCE C++. Outputs VST3, AU, CLAP, and Standalone formats.

## What it is

A text-mode sequencer with no mouse interaction. The UI is rendered entirely via `paint()` — like a hardware LCD. You type note data numerically, field by field, navigating with the keyboard. It outputs MIDI on channels 1–4.

This is not a piano roll or grid sequencer. Events are stored as an ordered list with relative timing: each event has a step time (ticks until the next event) and a gate time (how long the note sounds). There is no fixed grid. You build patterns event by event.

## Core concepts

### Relative timing
Every event has two time values:
- **Step Time (ST):** ticks until the next event starts. This is how long this event "occupies."
- **Gate Time (GT):** ticks the note actually sounds. If GT < ST, there's silence between notes. If GT = ST, notes are legato. If GT = 0, the event is a rest (silent placeholder).

The **timebase** sets ticks per beat. At TB=120, a quarter note = 120 ticks, a 16th = 30.

### Channels and layers
4 independent channels, each with its own event list, cursor position, and repeat marks. Output on MIDI channels 1–4.

6 **layers** control which parameter you're typing into: CV1 (pitch), Step Time, Gate Time, Velocity, CV2, MPX. Select with Shift+1 through Shift+6.

### Velocity model
Three-tier system:
1. **Base velocity** (default 90) — applies to all events without a per-event override. Set with Shift+V.
2. **Per-event velocity** — set by editing the Velocity field. Overrides base. Events using base velocity show `*` in the detail view.
3. **Accent boost** (default +32) — added when the accent flag is set. An accented event at base 90 plays at 122.

### Initial state
Each channel starts with a single blank event (pitch unset, GT = ST = 30). You build from there — insert events with `I`, set pitches, adjust timing.

## Display layout

```
CH:1  [CV1]  EDIT  TB:120  T:120  CYC:ON  1 events
─────────────────────────────────────────────────────
M:001 S:01
  --- (-)   ST:30   GT:30   90*   CV2:64   -  -  -
─────────────────────────────────────────────────────
>001  ---      30    30   90   64  ····
─────────────────────────────────────────────────────
> _                                        (Pitch)
─────────────────────────────────────────────────────
CV1  ST  GT  VEL  CV2  MPX
```

- **Status bar:** Channel, active layer, play/edit mode, timebase, tempo, cycle state, event count.
- **Detail view:** Current event with all fields. Active field highlighted; typed values preview in yellow.
- **Context view:** Scrolling list centred on cursor. Columns: index, pitch, ST, GT, velocity, CV2, flags.
- **Input line:** Current typing buffer and active field name.
- **Layer bar:** Which parameter is selected for entry.

## Full keyboard reference

### Navigation

| Key | Action |
|-----|--------|
| Up / Down | Move cursor between events |
| Left / Right | Move between fields |
| Shift+Up / Shift+Down | Jump to previous / next measure start |
| Page Up / Page Down | Jump to previous / next measure start |
| Home / End | Jump to first / last event |

### Numeric entry

| Key | Action |
|-----|--------|
| 0–9 | Type value into current field (live preview in yellow) |
| Enter | Commit typed value, stay on current event |
| Cmd+Enter | Commit typed value and advance to next event |
| Backspace | Delete last typed digit |
| Escape | Clear input / cancel current mode |

### Value nudge (M8-style)

| Key | Action |
|-----|--------|
| Cmd+Left / Right | Nudge field value ±1 |
| Cmd+Up / Down | Nudge ±10 (±octave for pitch) |

### Layer selection (Shift + number)

| Key | Layer |
|-----|-------|
| Shift+1 | CV1 (Pitch) |
| Shift+2 | Step Time |
| Shift+3 | Gate Time |
| Shift+4 | Velocity |
| Shift+5 | CV2 |
| Shift+6 | MPX |

### Channel selection

| Key | Action |
|-----|--------|
| F1–F4 | Select channel 1–4 |
| Ctrl+1–4 | Select channel 1–4 (laptop-friendly) |

### Editing

| Key | Action |
|-----|--------|
| I | Insert new event after cursor (blank pitch, default GT) |
| Shift+I | Insert multiple (prompts for count) |
| D | Delete event at cursor |
| Shift+D | Delete multiple (prompts for count) |
| V | Divide event into N equal parts |
| J | Join current event with next (merge timing) |
| A | Toggle accent flag |
| S | Toggle slide flag |
| M | Toggle measure end (sets + advances if unset; clears if already set) |
| Shift+M | Toggle measure end (always toggles, never advances) |
| T | Tie: set GT = ST, then advance |
| . (period) | Rest: set GT = 0, then advance |

### Non-ripple ST edit

Shift the boundary between current event and previous event. Total sequence length unchanged.

| Key | Action |
|-----|--------|
| [ | Push event later (+1 tick to current ST, −1 from previous) |
| ] | Pull event earlier (−1 tick from current ST, +1 to previous) |
| Shift+[ | Same, ±10 ticks |
| Shift+] | Same, ±10 ticks |

### Copy and repeat

| Key | Action |
|-----|--------|
| C | Copy-overwrite (prompts: start measure, end measure, reps, transpose) |
| Shift+C | Copy-insert / ripple (same prompts) |
| R | Mark repeat start at cursor |
| Shift+R | Set repeat end at cursor (prompts for count) |

### Transport and configuration

| Key | Action |
|-----|--------|
| Space | Play/stop (standalone only; passes through to host in plugin mode) |
| Tab | Toggle cycle (loop) on/off |
| Shift+T | Set tempo (prompts for BPM) |
| Shift+B | Set timebase (prompts for ticks per beat) |
| Shift+S / Ctrl+S | Sync tempo to host BPM |
| Shift+N | Set default note for new events (0 = blank, or MIDI number) |
| Shift+V | Set base velocity |

### Undo / redo

| Key | Action |
|-----|--------|
| Cmd+Z | Undo (50 levels) |
| Cmd+Shift+Z | Redo |

## Timing cheat sheet (TB=120)

| Duration | ST |
|----------|-----|
| Whole | 480 |
| Half | 240 |
| Quarter | 120 |
| 8th | 60 |
| 16th | 30 |
| 32nd | 15 |
| Dotted quarter | 180 |
| Triplet quarter | 80 |

## Architecture

- **DataModel.h** — All data structures: Event, Channel, Sequence, Config, enums.
- **PluginProcessor.cpp** — JUCE audio processor. Owns Sequence, Config, PlaybackEngine. Reads host transport. Audio thread uses `try_lock` on mutex.
- **PluginEditor.cpp** — All UI rendering and keyboard handling. Locks mutex for edits. 30fps repaint timer.
- **PlaybackEngine.cpp** — Tick-based per-channel sequencer. Fires MIDI note-on/off based on ST/GT boundaries. Handles repeat marks with iteration stack.
- **CMakeLists.txt** — Builds VST3, AU, CLAP, Standalone. IS_MIDI_EFFECT = TRUE.

### Thread model
UI thread locks `sequenceMutex` for all edits. Audio thread calls `try_lock` — if it can't acquire, it skips that buffer (no blocking, no glitches). Undo/redo uses full channel snapshots.

## Current status

**Implemented:**
- 4-channel tick-based sequencer with relative timing
- Full keyboard-driven text UI with live preview
- Per-event velocity with base/accent/per-event layering
- Insert, delete, divide, join operations
- Copy (overwrite and ripple) with transpose
- Repeat marks with nested playback
- Non-ripple ST editing (micro-timing adjustment)
- Measure markers with navigation
- Undo/redo (50 levels)
- Host tempo sync
- Cycle (loop) mode
- Configurable default note and base velocity

**Not yet implemented:**
- State save/load (JSON serialisation)
- Microscope function (zoom into selection with finer timebase)
- Tables (M8-style automation)
- MIDI input (record from keyboard)
- MPE expression output beyond note-on velocity
- Pattern chaining across channels
