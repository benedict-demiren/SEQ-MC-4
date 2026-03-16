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

### State persistence
All sequence data, config, and transport state are serialised to JSON via JUCE's `getStateInformation`/`setStateInformation`. Sessions survive save/close/reopen in any host. Schema is versioned for future compatibility.

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

- **Status bar:** Channel, active layer, play/edit/REC mode, timebase, tempo, cycle state, event count.
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
| Option+1–4 | Select channel 1–4 (laptop-friendly, avoids DAW/system conflicts) |

### Editing

| Key | Action |
|-----|--------|
| I | Insert new event after cursor (ST and GT default to timebase/4) |
| Shift+I | Insert new event before cursor (for adding before first event) |
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
| O | Rotate current measure by N ticks (prompts for ST offset) |

### Patterns

Patterns let you create variations of a sequence and switch between them seamlessly during playback. The engine keeps its tick counter when you switch — no gap, no stutter.

| Key | Action |
|-----|--------|
| P | Commit: duplicate current pattern to a new one and switch editor to it |
| Shift+, (<) | Switch to previous pattern |
| Shift+. (>) | Switch to next pattern |

When multiple patterns exist, the status bar shows `P:N/M` (e.g. `P:2/3` = editing pattern 2 of 3). Pattern switching during playback is legato — the engine continues from the exact tick position, just reading from the new pattern's event list.

### Non-ripple ST edit

Shift the boundary between current event and previous event. Total sequence length unchanged.

| Key | Action |
|-----|--------|
| [ | Push event later (+1 tick to current ST, −1 from previous) |
| ] | Pull event earlier (−1 tick from current ST, +1 to previous) |
| Shift+[ | Same, ±10 ticks |
| Shift+] | Same, ±10 ticks |

### MIDI step recording

| Key | Action |
|-----|--------|
| N | Toggle step record mode (status bar shows REC in red) |

When active, incoming MIDI note-ons from your controller set the pitch and velocity of the current event and advance the cursor automatically. If gate time is 0 (rest), it's promoted to legato (GT = ST) so the note plays. At the end of the event list, new events are auto-inserted. Uses a lock-free ring buffer from the audio thread — no glitches.

### Copy and repeat

| Key | Action |
|-----|--------|
| C | Copy-overwrite (prompts: start measure, end measure, reps, transpose — transpose defaults to 0) |
| Shift+C | Copy-insert / ripple (same prompts) |
| R | Mark repeat start at cursor |
| Shift+R | Set repeat end at cursor (prompts for count) |

### Transport and configuration

| Key | Action |
|-----|--------|
| Space | Play/stop (standalone only; passes through to host in plugin mode) |
| Tab | Toggle cycle (loop) on/off |
| N | Toggle MIDI step record mode |
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
- **PluginProcessor.cpp** — JUCE audio processor. Owns Sequence, Config, PlaybackEngine. Reads host transport. JSON state save/load. MIDI input capture for step recording via lock-free ring buffer. Audio thread uses `try_lock` on mutex.
- **PluginEditor.cpp** — All UI rendering and keyboard handling. Locks mutex for edits. 30fps repaint timer. Polls MIDI ring buffer for step record input.
- **PlaybackEngine.cpp** — Tick-based per-channel sequencer. Fires MIDI note-on/off based on ST/GT boundaries. Handles repeat marks with iteration stack.
- **CMakeLists.txt** — Builds VST3, AU, CLAP, Standalone. IS_MIDI_EFFECT = TRUE.

### Thread model
UI thread locks `sequenceMutex` for all edits. Audio thread calls `try_lock` — if it can't acquire, it skips that buffer (no blocking, no glitches). MIDI step record uses a lock-free SPSC ring buffer (audio thread writes, UI thread reads at 30fps). Undo/redo uses full channel snapshots.

## Current status

**Implemented:**
- 4-channel tick-based sequencer with relative timing
- Full keyboard-driven text UI with live preview
- Per-event velocity with base/accent/per-event layering
- Insert (before/after), delete, divide, join operations
- Copy (overwrite and ripple) with transpose
- Repeat marks with nested playback
- Non-ripple ST editing (micro-timing adjustment)
- Measure markers with navigation
- Measure rotation (circular shift by tick offset)
- Multi-pattern system with seamless legato switching
- Undo/redo (50 levels, snapshots all patterns)
- Host tempo sync
- Cycle (loop) mode
- Configurable default note and base velocity
- JSON state save/load (full session persistence, backwards-compatible)
- MIDI step recording from external controller

**Not yet implemented:**
- MIDI file export/import
- Microscope function (zoom into selection with finer timebase)
- Tables (M8-style automation)
- Real-time MIDI recording (quantised to timebase while transport runs)
- MPE expression output beyond note-on velocity
- Pattern chaining / song mode (ordered playback of multiple patterns)
