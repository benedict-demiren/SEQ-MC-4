# SEQ-MC-4 — Project Conventions

## Project Overview
MPE step sequencer plugin inspired by the Roland MC-4. MIDI generator only — no audio DSP.
Text-mode calculator interface. Four monophonic channels outputting MPE MIDI.

## Build
```bash
bash scripts/build.sh
```
If CMakeLists.txt changes, delete `build/` and re-run.

## Architecture
- `Source/DataModel.h` — Event, Channel, Sequence structs. Core data.
- `Source/PlaybackEngine.h/cpp` — Tick-based sequencer, MIDI event generation.
- `Source/PluginProcessor.h/cpp` — JUCE AudioProcessor wrapper. Owns data model + engine.
- `Source/PluginEditor.h/cpp` — Text-mode UI rendering + keyboard input handling.

## Key Conventions
- All time values in ticks (timebase = ticks per beat, default 120)
- Event list is relative-time, not absolute grid positions
- UI is fully keyboard-driven, no mouse interaction needed
- Thread safety: mutex with try_lock on audio thread
- MPE channels: sequencer ch 1-4 → MIDI ch 1-4 (zone lower, manager ch 0)

## Dependencies
- JUCE (git submodule at libs/JUCE)
- clap-juce-extensions (git submodule at libs/clap-juce-extensions)
- No Faust, no external DSP libraries
