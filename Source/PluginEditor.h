#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "DataModel.h"

class SEQMC4Editor : public juce::AudioProcessorEditor,
                     public juce::Timer {
public:
    explicit SEQMC4Editor(SEQMC4Processor&);
    ~SEQMC4Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override {}
    bool keyPressed(const juce::KeyPress& key) override;

    void timerCallback() override;

private:
    SEQMC4Processor& proc;

    // Input buffer for numeric entry
    juce::String inputBuffer;
    mc4::InputMode inputMode = mc4::InputMode::Normal;
    mc4::CopyState copyState; // Multi-step copy command state

    // Undo/redo stacks (per-channel snapshots)
    static constexpr int kMaxUndoDepth = 50;
    std::vector<mc4::ChannelSnapshot> undoStack;
    std::vector<mc4::ChannelSnapshot> redoStack;

    // UI layout constants
    static constexpr int kLineHeight = 20;
    static constexpr int kFontSize = 15;
    static constexpr int kMargin = 12;
    static constexpr int kContextLines = 9; // Visible lines in context view

    // Colors
    juce::Colour bgColor       { 0xff1a1a1a };
    juce::Colour textColor     { 0xffe0e0e0 };
    juce::Colour dimColor      { 0xff707070 };
    juce::Colour accentColor   { 0xff00cc88 }; // Green accent
    juce::Colour cursorColor   { 0xff33ff99 }; // Bright green for cursor line
    juce::Colour fieldHiColor  { 0xff1a1a1a }; // Text on highlighted field
    juce::Colour fieldHiBg     { 0xff00cc88 }; // Background for highlighted field
    juce::Colour headerColor   { 0xffffaa33 }; // Amber for headers/labels
    juce::Colour inputColor    { 0xffffff66 }; // Yellow for input text
    juce::Colour playColor     { 0xffff4444 }; // Red for play indicator

    // Drawing helpers
    void drawStatusBar(juce::Graphics& g, int y);
    void drawCurrentEvent(juce::Graphics& g, int y);
    void drawContextView(juce::Graphics& g, int y);
    void drawInputLine(juce::Graphics& g, int y);
    void drawShiftMap(juce::Graphics& g, int y);
    void drawContextRow(juce::Graphics& g, int y, int eventIdx, bool isCursor);

    // Keyboard handlers
    bool handleDigitKey(int digit);
    bool handleEnterKey(bool shouldAdvance = false);
    bool handleNavigationKey(const juce::KeyPress& key);
    bool handleEditCommand(const juce::KeyPress& key);
    bool handleLayerSelection(const juce::KeyPress& key);
    bool handleChannelSelection(const juce::KeyPress& key);
    bool handleTransportKey(const juce::KeyPress& key);
    bool handleNudgeKey(const juce::KeyPress& key);

    // Edit operations
    void commitValue(int value);
    void insertEvent(bool before = false);
    void deleteEvent();
    void advanceCursor();
    void pushUndo();       // Save current state to undo stack
    void performUndo();
    void performRedo();

    // Helpers
    mc4::Sequence& seq() { return proc.sequence; }
    mc4::Channel& ch() { return seq().ch(); }
    juce::String fieldValueString(const mc4::Event& e, int field) const;
    int fieldColumnX(int field) const;
    int fieldWidth(int field) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SEQMC4Editor)
};
