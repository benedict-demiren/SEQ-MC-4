#include "PluginEditor.h"

//==============================================================================
// Helper: get layer name
static const char* layerName(int layer) {
    static const char* names[] = { "CV1", "ST", "GT", "VEL", "CV2", "MPX" };
    if (layer >= 0 && layer < 6) return names[layer];
    return "?";
}

static const char* fieldName(int field) {
    static const char* names[] = { "Pitch", "StepT", "GateT", "Vel", "CV2", "Acc", "Slide", "MPX" };
    if (field >= 0 && field < 8) return names[field];
    return "?";
}

static const char* inputModeName(mc4::InputMode mode) {
    switch (mode) {
        case mc4::InputMode::InsertMulti:  return "INSERT COUNT: ";
        case mc4::InputMode::DeleteMulti:  return "DELETE COUNT: ";
        case mc4::InputMode::Divide:       return "DIVIDE BY: ";
        case mc4::InputMode::TempoEdit:    return "TEMPO: ";
        case mc4::InputMode::TimebaseEdit: return "TIMEBASE: ";
        case mc4::InputMode::CopyStartMeas: return "COPY START MEAS: ";
        case mc4::InputMode::CopyEndMeas:   return "COPY END MEAS: ";
        case mc4::InputMode::CopyReps:      return "REPETITIONS: ";
        case mc4::InputMode::CopyTranspose: return "TRANSPOSE ST: ";
        case mc4::InputMode::RepeatEnd:     return "REPEAT COUNT: ";
        case mc4::InputMode::DefaultNote:   return "DEFAULT NOTE: ";
        case mc4::InputMode::BaseVelocity:  return "BASE VELOCITY: ";
        case mc4::InputMode::RotateMeasure: return "ROTATE BY ST: ";
        case mc4::InputMode::RotateNotesOnly: return "ROTATE NOTES BY: ";
        default: return "> ";
    }
}

// Map layer to field index
static int layerToField(int layer) {
    // CV1->Pitch, StepTime->StepTime, GateTime->GateTime, Velocity->Velocity, CV2->CV2, MPX->MPX
    switch (layer) {
        case 0: return 0; // CV1 -> Pitch
        case 1: return 1; // StepTime
        case 2: return 2; // GateTime
        case 3: return 3; // Velocity
        case 4: return 4; // CV2
        case 5: return 7; // MPX
        default: return 0;
    }
}

//==============================================================================
SEQMC4Editor::SEQMC4Editor(SEQMC4Processor& p)
    : AudioProcessorEditor(p), proc(p)
{
    setSize(660, 420);
    setWantsKeyboardFocus(true);
    startTimerHz(30); // Repaint at 30fps
}

SEQMC4Editor::~SEQMC4Editor() {
    stopTimer();
}

void SEQMC4Editor::timerCallback() {
    // Poll for incoming MIDI notes (step record)
    if (proc.stepRecordEnabled.load()) {
        int pitch, velocity;
        while (proc.popMidiNote(pitch, velocity)) {
            pushUndo();
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            if (!c.events().empty()) {
                auto& evt = c.events()[c.cursorPos];
                evt.pitch = pitch;
                evt.velocity = velocity;
                // If gate is 0 (rest), set it to step_time (legato) so it actually plays
                if (evt.gate_time == 0)
                    evt.gate_time = evt.step_time;
                // Advance cursor
                if (c.cursorPos < (int)c.events().size() - 1) {
                    c.cursorPos++;
                } else {
                    // At end of list — auto-insert a new event
                    mc4::Event def;
                    def.pitch = -1;
                    def.gate_time = def.step_time; // legato default
                    c.events().push_back(def);
                    c.cursorPos = (int)c.events().size() - 1;
                }
            }
        }
    }
    repaint();
}

//==============================================================================
// PAINT
//==============================================================================
void SEQMC4Editor::paint(juce::Graphics& g)
{
    g.fillAll(bgColor);
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), (float)kFontSize, juce::Font::plain));

    int y = kMargin;
    drawStatusBar(g, y);      y += kLineHeight + 8;

    // Separator
    g.setColour(dimColor);
    g.drawHorizontalLine(y, (float)kMargin, (float)(getWidth() - kMargin));
    y += 6;

    drawCurrentEvent(g, y);   y += kLineHeight * 2 + 4;

    g.setColour(dimColor);
    g.drawHorizontalLine(y, (float)kMargin, (float)(getWidth() - kMargin));
    y += 6;

    drawContextView(g, y);    y += kLineHeight * kContextLines + 4;

    g.setColour(dimColor);
    g.drawHorizontalLine(y, (float)kMargin, (float)(getWidth() - kMargin));
    y += 6;

    drawInputLine(g, y);      y += kLineHeight + 8;

    g.setColour(dimColor);
    g.drawHorizontalLine(y, (float)kMargin, (float)(getWidth() - kMargin));
    y += 6;

    drawShiftMap(g, y);
}

void SEQMC4Editor::drawStatusBar(juce::Graphics& g, int y)
{
    auto& s = seq();
    bool playing = proc.isCurrentlyPlaying.load();

    int x = kMargin;

    // Channel
    g.setColour(headerColor);
    g.drawText("CH:" + juce::String(s.activeChannel + 1), x, y, 50, kLineHeight, juce::Justification::left);
    x += 55;

    // Layer
    g.setColour(accentColor);
    g.drawText(juce::String("[") + layerName(ch().activeLayer) + "]", x, y, 50, kLineHeight, juce::Justification::left);
    x += 55;

    // Mode
    bool stepRec = proc.stepRecordEnabled.load();
    if (stepRec) {
        g.setColour(juce::Colour(0xffff6666)); // Red for record
        g.drawText("REC", x, y, 45, kLineHeight, juce::Justification::left);
    } else {
        g.setColour(playing ? playColor : textColor);
        g.drawText(playing ? "PLAY" : "EDIT", x, y, 45, kLineHeight, juce::Justification::left);
    }
    x += 50;

    // Timebase
    g.setColour(dimColor);
    g.drawText("TB:" + juce::String(s.timebase), x, y, 60, kLineHeight, juce::Justification::left);
    x += 65;

    // Tempo
    g.drawText("T:" + juce::String(s.tempo), x, y, 55, kLineHeight, juce::Justification::left);
    x += 60;

    // Cycle
    g.setColour(s.cycleOn ? accentColor : dimColor);
    g.drawText(juce::String("CYC:") + (s.cycleOn ? "ON" : "OFF"), x, y, 65, kLineHeight, juce::Justification::left);
    x += 70;

    // Pattern number (only show if more than 1 pattern exists)
    auto& thisCh = ch();
    if (thisCh.patterns.size() > 1) {
        g.setColour(juce::Colour(0xff66ccff)); // Light blue for pattern
        g.drawText("P:" + juce::String(thisCh.activePattern + 1) + "/" + juce::String(thisCh.patterns.size()),
                   x, y, 55, kLineHeight, juce::Justification::left);
        x += 58;
    }

    // Event count
    g.setColour(dimColor);
    g.drawText(juce::String(thisCh.events().size()), x, y, 50, kLineHeight, juce::Justification::left);
}

void SEQMC4Editor::drawCurrentEvent(juce::Graphics& g, int y)
{
    auto& c = ch();
    if (c.events().empty()) {
        g.setColour(dimColor);
        g.drawText("(no events)", kMargin, y, 300, kLineHeight, juce::Justification::left);
        return;
    }

    // Make a copy of the event so we can apply live preview
    mc4::Event evt = c.events()[c.cursorPos];

    // Live preview: if there's input in the buffer and we're in Normal mode,
    // temporarily apply the typed value to the active field for display
    bool previewing = false;
    if (inputMode == mc4::InputMode::Normal && inputBuffer.isNotEmpty()) {
        int previewValue = inputBuffer.getIntValue();
        previewing = true;
        switch (c.fieldCursor) {
            case 0: evt.pitch = std::max(0, std::min(previewValue, 127)); break;
            case 1: evt.step_time = std::max(1, std::min(previewValue, 61690)); break;
            case 2: evt.gate_time = std::max(0, std::min(previewValue, 61690)); break;
            case 3: evt.velocity = std::max(1, std::min(previewValue, 127)); break;
            case 4: evt.cv2 = std::max(0, std::min(previewValue, 127)); break;
            case 5: evt.accent = (previewValue != 0); break;
            case 6: evt.slide = (previewValue != 0); break;
            case 7: evt.mpx = (previewValue != 0); break;
        }
    }

    int measureNum, stepInMeasure;
    c.getMeasureInfo(c.cursorPos, measureNum, stepInMeasure);

    // Line 1: M:003 S:07
    g.setColour(headerColor);
    juce::String line1 = "M:" + juce::String(measureNum).paddedLeft('0', 3)
                       + " S:" + juce::String(stepInMeasure).paddedLeft('0', 2);
    g.drawText(line1, kMargin, y, 120, kLineHeight, juce::Justification::left);

    // Line 2: field values with cursor highlight
    int y2 = y + kLineHeight;
    for (int f = 0; f < (int)mc4::Field::NumFields; ++f) {
        juce::String val = fieldValueString(evt, f);
        int fx = fieldColumnX(f);
        int fw = fieldWidth(f);

        if (f == c.fieldCursor) {
            // Highlight the active field — use yellow tint if previewing
            g.setColour(previewing ? inputColor : fieldHiBg);
            g.fillRect(fx - 2, y2, fw + 4, kLineHeight);
            g.setColour(fieldHiColor);
        } else {
            g.setColour(textColor);
        }
        g.drawText(val, fx, y2, fw, kLineHeight, juce::Justification::left);
    }
}

void SEQMC4Editor::drawContextView(juce::Graphics& g, int y)
{
    auto& c = ch();
    int halfView = kContextLines / 2;

    for (int line = 0; line < kContextLines; ++line) {
        int idx = c.cursorPos - halfView + line;
        if (idx >= 0 && idx < (int)c.events().size()) {
            drawContextRow(g, y + line * kLineHeight, idx, idx == c.cursorPos);
        }
    }
}

void SEQMC4Editor::drawContextRow(juce::Graphics& g, int y, int eventIdx, bool isCursor)
{
    auto& c = ch();
    const auto& evt = c.events()[eventIdx];

    if (isCursor) {
        g.setColour(juce::Colour(0xff0a3020));
        g.fillRect(kMargin - 2, y, getWidth() - 2 * kMargin + 4, kLineHeight);
    }

    int x = kMargin;

    // Cursor indicator
    g.setColour(isCursor ? cursorColor : dimColor);
    g.drawText(isCursor ? ">" : " ", x, y, 12, kLineHeight, juce::Justification::left);
    x += 14;

    // Step number (padded)
    g.setColour(isCursor ? cursorColor : dimColor);
    g.drawText(juce::String(eventIdx + 1).paddedLeft('0', 3), x, y, 30, kLineHeight, juce::Justification::left);
    x += 35;

    // Pitch — note name with MIDI number in brackets
    g.setColour(isCursor ? cursorColor : textColor);
    juce::String pitchStr;
    if (evt.pitch < 0) {
        pitchStr = "---";
        g.setColour(isCursor ? cursorColor : dimColor);
    } else {
        pitchStr = juce::String(mc4::midiNoteToName(evt.pitch))
                 + "(" + juce::String(evt.pitch) + ")";
    }
    g.drawText(pitchStr, x, y, 70, kLineHeight, juce::Justification::left);
    x += 75;

    // Step time
    g.setColour(isCursor ? cursorColor : textColor);
    g.drawText(mc4::padLeft(std::to_string(evt.step_time), 5), x, y, 45, kLineHeight, juce::Justification::left);
    x += 50;

    // Gate time
    g.drawText(mc4::padLeft(std::to_string(evt.gate_time), 5), x, y, 45, kLineHeight, juce::Justification::left);
    x += 50;

    // Velocity (per-event or base + accent boost)
    {
        int vel = (evt.velocity >= 0) ? evt.velocity : proc.config.baseVelocity;
        if (evt.accent)
            vel = std::min(127, vel + proc.config.accentBoost);
        g.setColour(isCursor ? cursorColor : (evt.accent ? juce::Colour(0xffff8844) : textColor));
        g.drawText(mc4::padLeft(std::to_string(vel), 3), x, y, 30, kLineHeight, juce::Justification::left);
        x += 35;
    }

    // CV2
    g.setColour(isCursor ? cursorColor : dimColor);
    g.drawText(mc4::padLeft(std::to_string(evt.cv2), 3), x, y, 30, kLineHeight, juce::Justification::left);
    x += 35;

    // Flags: A=accent, S=slide, M=measure_end, X=mpx
    juce::String flags;
    flags += evt.accent ? "A" : "\xC2\xB7";
    flags += evt.slide ? "S" : "\xC2\xB7";
    flags += evt.measure_end ? "M" : "\xC2\xB7";
    flags += evt.mpx ? "X" : "\xC2\xB7";
    g.setColour(isCursor ? cursorColor : dimColor);
    g.drawText(flags, x, y, 50, kLineHeight, juce::Justification::left);
    x += 50;

    // Repeat mark indicators
    {
        auto& c = ch();
        juce::String repInd;
        // Pending repeat start
        if (c.pendingRepeatStart() == eventIdx)
            repInd += "R>";
        // Committed repeat marks
        for (const auto& rm : c.repeatMarks()) {
            if (rm.startEvent == eventIdx) repInd += "|:";
            if (rm.endEvent == eventIdx)   repInd += "x" + juce::String(rm.count) + ":|";
        }
        if (repInd.isNotEmpty()) {
            g.setColour(juce::Colour(0xffcc66ff)); // Purple for repeat markers
            g.drawText(repInd, x, y, 60, kLineHeight, juce::Justification::left);
        }
    }

    // Playback position indicator
    if (proc.isCurrentlyPlaying.load()) {
        int playPos = proc.engine.getPlaybackPosition(seq().activeChannel);
        if (eventIdx == playPos) {
            g.setColour(playColor);
            g.fillRect(getWidth() - kMargin - 6, y + 4, 6, kLineHeight - 8);
        }
    }
}

void SEQMC4Editor::drawInputLine(juce::Graphics& g, int y)
{
    g.setColour(dimColor);
    g.drawText(inputModeName(inputMode), kMargin, y, 120, kLineHeight, juce::Justification::left);

    g.setColour(inputColor);
    juce::String display = inputBuffer + "_";
    g.drawText(display, kMargin + 120, y, 200, kLineHeight, juce::Justification::left);

    // Show what field is being edited
    if (inputMode == mc4::InputMode::Normal) {
        g.setColour(dimColor);
        g.drawText(juce::String("(") + fieldName(ch().fieldCursor) + ")",
                   kMargin + 320, y, 100, kLineHeight, juce::Justification::left);
    }
}

void SEQMC4Editor::drawShiftMap(juce::Graphics& g, int y)
{
    int x = kMargin;
    for (int i = 0; i < 6; ++i) {
        bool active = (ch().activeLayer == i);
        if (active) {
            g.setColour(fieldHiBg);
            g.fillRect(x - 2, y, 42, kLineHeight);
            g.setColour(fieldHiColor);
        } else {
            g.setColour(dimColor);
        }
        g.drawText(layerName(i), x, y, 38, kLineHeight, juce::Justification::centred);
        x += 46;
    }
}

//==============================================================================
// FIELD LAYOUT
//==============================================================================
int SEQMC4Editor::fieldColumnX(int field) const {
    // Pitch, StepTime, GateTime, Velocity, CV2, Accent, Slide, MPX
    static const int offsets[] = { 130, 210, 290, 355, 410, 470, 500, 530 };
    if (field >= 0 && field < 8) return offsets[field];
    return 130;
}

int SEQMC4Editor::fieldWidth(int field) const {
    static const int widths[] = { 70, 70, 55, 45, 55, 25, 25, 25 };
    if (field >= 0 && field < 8) return widths[field];
    return 70;
}

juce::String SEQMC4Editor::fieldValueString(const mc4::Event& e, int field) const {
    switch (field) {
        case 0:
            if (e.pitch < 0) return "---";
            return juce::String(mc4::midiNoteToName(e.pitch)) + " (" + juce::String(e.pitch) + ")";
        case 1: return "ST:" + juce::String(e.step_time);
        case 2: return "GT:" + juce::String(e.gate_time);
        case 3: {
            int vel = (e.velocity >= 0) ? e.velocity : proc.config.baseVelocity;
            if (e.accent) vel = std::min(127, vel + proc.config.accentBoost);
            return juce::String(vel) + (e.velocity < 0 ? "*" : "");
        }
        case 4: return "CV2:" + juce::String(e.cv2);
        case 5: return e.accent ? "A" : "-";
        case 6: return e.slide ? "S" : "-";
        case 7: return e.mpx ? "X" : "-";
        default: return "";
    }
}

//==============================================================================
// KEYBOARD HANDLING
//==============================================================================
bool SEQMC4Editor::keyPressed(const juce::KeyPress& key)
{
    // Grab focus
    if (!hasKeyboardFocus(false))
        grabKeyboardFocus();

    int keyCode = key.getKeyCode();

    // Escape always clears input
    if (keyCode == juce::KeyPress::escapeKey) {
        inputBuffer.clear();
        inputMode = mc4::InputMode::Normal;
        return true;
    }

    // Backspace deletes last digit (but not Cmd+Backspace — that's delete pattern)
    if (keyCode == juce::KeyPress::backspaceKey && !key.getModifiers().isCommandDown()) {
        if (inputBuffer.isNotEmpty())
            inputBuffer = inputBuffer.dropLastCharacters(1);
        return true;
    }

    // Minus key for negative values in rotation and transpose modes
    if ((keyCode == '-' || keyCode == juce::KeyPress::numberPadSubtract) &&
        (inputMode == mc4::InputMode::RotateMeasure ||
         inputMode == mc4::InputMode::RotateNotesOnly ||
         inputMode == mc4::InputMode::CopyTranspose) &&
        inputBuffer.isEmpty()) {
        inputBuffer = "-";
        return true;
    }

    // Digit keys (main keyboard and numpad) — but NOT when Shift is held (those are layer shortcuts)
    if (keyCode >= '0' && keyCode <= '9' && !key.getModifiers().isShiftDown())
        return handleDigitKey(keyCode - '0');
    if (keyCode >= juce::KeyPress::numberPad0 && keyCode <= juce::KeyPress::numberPad9)
        return handleDigitKey(keyCode - juce::KeyPress::numberPad0);

    // Cmd+Enter = commit value AND advance cursor
    // Enter alone = commit value, stay on same event (or advance if no input)
    if (keyCode == juce::KeyPress::returnKey) {
        bool cmdHeld = key.getModifiers().isCommandDown();
        return handleEnterKey(cmdHeld);
    }

    // Undo: Cmd+Z, Redo: Cmd+Shift+Z
    if (key.getModifiers().isCommandDown() && keyCode == 'Z') {
        if (key.getModifiers().isShiftDown())
            performRedo();
        else
            performUndo();
        return true;
    }

    // Cmd+P = new blank pattern
    if (key.getModifiers().isCommandDown() && keyCode == 'P') {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        c.patterns.emplace_back(); // New blank pattern (single rest event)
        c.activePattern = (int)c.patterns.size() - 1;
        c.cursorPos = 0;
        return true;
    }

    // Cmd+Backspace = delete current pattern (switch to previous, playback uninterrupted)
    if (key.getModifiers().isCommandDown() && keyCode == juce::KeyPress::backspaceKey) {
        {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            if (ch().patterns.size() <= 1) return true; // Can't delete last pattern
        }
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        c.patterns.erase(c.patterns.begin() + c.activePattern);
        if (c.activePattern >= (int)c.patterns.size())
            c.activePattern = (int)c.patterns.size() - 1;
        c.clampCursor();
        return true;
    }

    // Channel selection: F1-F4
    if (handleChannelSelection(key)) return true;

    // Layer selection: Shift+1-5
    if (handleLayerSelection(key)) return true;

    // Cmd+arrows: nudge value in current field (M8-style)
    if (handleNudgeKey(key)) return true;

    // Navigation: arrows, page up/down, home/end
    if (handleNavigationKey(key)) return true;

    // Transport
    if (handleTransportKey(key)) return true;

    // Edit commands (only when no modifiers except the ones we check)
    if (handleEditCommand(key)) return true;

    return false;
}

bool SEQMC4Editor::handleDigitKey(int digit)
{
    if (inputBuffer.length() < 6) // Max 6 digits (values up to 61690)
        inputBuffer += juce::String(digit);
    return true;
}

bool SEQMC4Editor::handleEnterKey(bool shouldAdvance)
{
    if (inputBuffer.isEmpty()) {
        // Enter with no input = advance cursor
        advanceCursor();
        return true;
    }

    int value = inputBuffer.getIntValue();
    inputBuffer.clear();

    switch (inputMode) {
        case mc4::InputMode::Normal:
            commitValue(value);
            if (shouldAdvance)
                advanceCursor();
            break;

        case mc4::InputMode::InsertMulti: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            int count = std::max(1, std::min(value, 999));
            c.adjustRepeatMarksForInsert(c.cursorPos, count);
            mc4::Event def;
            for (int i = 0; i < count; ++i)
                c.events().insert(c.events().begin() + c.cursorPos, def);
            inputMode = mc4::InputMode::Normal;
            break;
        }

        case mc4::InputMode::DeleteMulti: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            int count = std::max(1, std::min(value, (int)c.events().size() - c.cursorPos));
            c.adjustRepeatMarksForDelete(c.cursorPos, count);
            c.events().erase(c.events().begin() + c.cursorPos,
                           c.events().begin() + c.cursorPos + count);
            c.clampCursor();
            inputMode = mc4::InputMode::Normal;
            break;
        }

        case mc4::InputMode::Divide: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            if (c.events().empty() || value < 2) {
                inputMode = mc4::InputMode::Normal;
                break;
            }
            int n = std::min(value, 64); // Cap divisor
            const auto& orig = c.events()[c.cursorPos];
            int st = orig.step_time;
            int gt = orig.gate_time;
            int subStep = st / n;
            int remainder = st - subStep * n;
            int soundedRemaining = gt;

            std::vector<mc4::Event> subs;
            for (int i = 0; i < n; ++i) {
                mc4::Event e;
                e.pitch = orig.pitch;
                e.step_time = (i == n - 1) ? subStep + remainder : subStep;
                if (soundedRemaining >= subStep) {
                    e.gate_time = subStep;
                    soundedRemaining -= subStep;
                } else if (soundedRemaining > 0) {
                    e.gate_time = soundedRemaining;
                    soundedRemaining = 0;
                } else {
                    e.gate_time = 0;
                }
                e.cv2 = orig.cv2;
                e.accent = (i == 0) ? orig.accent : false;
                e.slide = false;
                e.mpx = (i == 0) ? orig.mpx : false;
                e.measure_end = (i == n - 1) ? orig.measure_end : false;
                subs.push_back(e);
            }

            // Replace original with sub-events
            c.events().erase(c.events().begin() + c.cursorPos);
            c.events().insert(c.events().begin() + c.cursorPos, subs.begin(), subs.end());
            inputMode = mc4::InputMode::Normal;
            break;
        }

        case mc4::InputMode::TempoEdit: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            seq().tempo = std::max(20, std::min(value, 300));
            inputMode = mc4::InputMode::Normal;
            break;
        }

        case mc4::InputMode::TimebaseEdit: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            seq().timebase = std::max(1, std::min(value, 960));
            inputMode = mc4::InputMode::Normal;
            break;
        }

        // Copy flow: 4 sequential prompts
        case mc4::InputMode::CopyStartMeas: {
            copyState.startMeasure = std::max(1, value);
            inputMode = mc4::InputMode::CopyEndMeas;
            break;
        }

        case mc4::InputMode::CopyEndMeas: {
            copyState.endMeasure = std::max(copyState.startMeasure, value);
            inputMode = mc4::InputMode::CopyReps;
            break;
        }

        case mc4::InputMode::CopyReps: {
            copyState.repetitions = std::max(1, std::min(value, 99));
            inputMode = mc4::InputMode::CopyTranspose;
            inputBuffer = "0"; // Pre-populate with 0 (most common case: no transpose)
            break;
        }

        case mc4::InputMode::CopyTranspose: {
            // Transpose: use raw value directly (supports negative via '-' key)
            copyState.transpose = std::max(-127, std::min(127, value));

            // Execute the copy
            {
                std::lock_guard<std::mutex> lock(proc.sequenceMutex);
                auto& c = ch();
                auto sourceEvents = c.getEventsInMeasureRange(
                    copyState.startMeasure, copyState.endMeasure);

                if (!sourceEvents.empty()) {
                    // Apply transpose
                    if (copyState.transpose != 0) {
                        for (auto& e : sourceEvents)
                            e.pitch = std::max(0, std::min(127, e.pitch + copyState.transpose));
                    }

                    if (copyState.insertMode) {
                        // Copy-insert: ripple-insert at cursor
                        for (int rep = 0; rep < copyState.repetitions; ++rep) {
                            c.events().insert(c.events().begin() + c.cursorPos,
                                            sourceEvents.begin(), sourceEvents.end());
                        }
                    } else {
                        // Copy-overwrite: replace events starting at cursor
                        for (int rep = 0; rep < copyState.repetitions; ++rep) {
                            int destPos = c.cursorPos + rep * (int)sourceEvents.size();
                            for (int i = 0; i < (int)sourceEvents.size(); ++i) {
                                int idx = destPos + i;
                                if (idx < (int)c.events().size()) {
                                    c.events()[idx] = sourceEvents[i];
                                } else {
                                    c.events().push_back(sourceEvents[i]);
                                }
                            }
                        }
                    }
                }
            }
            inputMode = mc4::InputMode::Normal;
            break;
        }

        case mc4::InputMode::DefaultNote: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            if (value == 0)
                proc.config.defaultNote = -1; // 0 = blank/unset
            else
                proc.config.defaultNote = std::max(0, std::min(value, 127));
            inputMode = mc4::InputMode::Normal;
            break;
        }

        case mc4::InputMode::BaseVelocity: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            proc.config.baseVelocity = std::max(1, std::min(value, 127));
            inputMode = mc4::InputMode::Normal;
            break;
        }

        // Repeat end: set repeat count
        case mc4::InputMode::RepeatEnd: {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            int repeatCount = std::max(2, std::min(value, 99));
            if (c.pendingRepeatStart() >= 0 && c.cursorPos >= c.pendingRepeatStart()) {
                mc4::RepeatMark mark;
                mark.startEvent = c.pendingRepeatStart();
                mark.endEvent = c.cursorPos;
                mark.count = repeatCount;
                c.repeatMarks().push_back(mark);
                c.pendingRepeatStart() = -1;
            }
            inputMode = mc4::InputMode::Normal;
            break;
        }

        // Rotate notes only — shares code with RotateMeasure
        case mc4::InputMode::RotateNotesOnly:
        // Rotate measure: circular shift events in current measure by ST ticks
        // Positive values rotate forward (upward), negative rotate backward (downward)
        case mc4::InputMode::RotateMeasure: {
            bool notesOnly = (inputMode == mc4::InputMode::RotateNotesOnly);
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            int measure, step;
            c.getMeasureInfo(c.cursorPos, measure, step);
            int startIdx = c.findMeasureStart(measure);
            int endIdx = c.findMeasureEnd(measure);
            int numEvents = endIdx - startIdx;
            if (numEvents < 2) { inputMode = mc4::InputMode::Normal; break; }

            int rawTicks = value;

            // Calculate total ticks in measure
            int totalTicks = c.measureTotalTicks(startIdx, endIdx);
            if (totalTicks <= 0) { inputMode = mc4::InputMode::Normal; break; }

            // Handle negative rotation: convert to equivalent forward rotation
            int rotateTicks = ((rawTicks % totalTicks) + totalTicks) % totalTicks;
            if (rotateTicks == 0) { inputMode = mc4::InputMode::Normal; break; }

            if (notesOnly) {
                // Notes-only rotation: shift pitches/velocities while preserving
                // the rhythmic structure (step times, gate times, rests, measure_end)
                // Collect only the "note" events (pitch >= 0, gate > 0) in order
                struct NoteData { int pitch; int velocity; bool accent; bool slide; };
                std::vector<NoteData> notes;
                std::vector<int> notePositions; // indices within measure
                for (int i = startIdx; i < endIdx; ++i) {
                    if (c.events()[i].pitch >= 0 && c.events()[i].gate_time > 0) {
                        notes.push_back({c.events()[i].pitch, c.events()[i].velocity,
                                         c.events()[i].accent, c.events()[i].slide});
                        notePositions.push_back(i);
                    }
                }
                if (notes.size() < 2) { inputMode = mc4::InputMode::Normal; break; }

                // Compute rotation in terms of note positions
                // rotateTicks / average-ST gives approximate note offset
                // For simplicity: rotate by 1 note per (totalTicks/numNotes) ticks
                int notesPerMeasure = (int)notes.size();
                int avgTicksPerNote = totalTicks / notesPerMeasure;
                int noteRotation = (avgTicksPerNote > 0) ? (rotateTicks / avgTicksPerNote) : 1;
                if (noteRotation < 1) noteRotation = 1;
                noteRotation = noteRotation % notesPerMeasure;

                // Rotate the note data array
                std::vector<NoteData> rotated(notesPerMeasure);
                for (int i = 0; i < notesPerMeasure; ++i)
                    rotated[i] = notes[(i + noteRotation) % notesPerMeasure];

                // Write back to original positions
                for (int i = 0; i < notesPerMeasure; ++i) {
                    int idx = notePositions[i];
                    c.events()[idx].pitch = rotated[i].pitch;
                    c.events()[idx].velocity = rotated[i].velocity;
                    c.events()[idx].accent = rotated[i].accent;
                    c.events()[idx].slide = rotated[i].slide;
                }
            } else {
                // Full rotation: circular shift all events by tick offset
                // Find the split point: accumulate ticks from the start
                int accumulated = 0;
                int splitIdx = startIdx;
                for (int i = startIdx; i < endIdx; ++i) {
                    accumulated += c.events()[i].step_time;
                    if (accumulated >= rotateTicks) {
                        if (accumulated == rotateTicks) {
                            // Clean split between events
                            splitIdx = i + 1;
                        } else {
                            // Split falls mid-event — need to break the event in two
                            int overshoot = accumulated - rotateTicks;
                            int origST = c.events()[i].step_time;
                            c.events()[i].step_time = origST - overshoot;
                            mc4::Event second = c.events()[i];
                            second.step_time = overshoot;
                            second.pitch = -1;
                            second.gate_time = 0;
                            c.events().insert(c.events().begin() + i + 1, second);
                            endIdx++;
                            splitIdx = i + 1;
                        }
                        break;
                    }
                }

                // Now rotate: move events [startIdx, splitIdx) to after events [splitIdx, endIdx)
                if (splitIdx > startIdx && splitIdx < endIdx) {
                    bool hadMeasureEnd = c.events()[endIdx - 1].measure_end;
                    std::vector<mc4::Event> head(c.events().begin() + startIdx,
                                                  c.events().begin() + splitIdx);
                    std::vector<mc4::Event> tail(c.events().begin() + splitIdx,
                                                  c.events().begin() + endIdx);
                    for (auto& e : head) e.measure_end = false;
                    for (auto& e : tail) e.measure_end = false;
                    int idx = startIdx;
                    for (auto& e : tail) c.events()[idx++] = e;
                    for (auto& e : head) c.events()[idx++] = e;
                    c.events()[endIdx - 1].measure_end = hadMeasureEnd;
                }
            }
            inputMode = mc4::InputMode::Normal;
            break;
        }
    }

    return true;
}

void SEQMC4Editor::commitValue(int value)
{
    pushUndo();
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    if (c.events().empty()) return;
    auto& evt = c.events()[c.cursorPos];

    switch (c.fieldCursor) {
        case 0: evt.pitch = std::max(0, std::min(value, 127)); break;
        case 1: evt.step_time = std::max(1, std::min(value, 61690)); break;
        case 2: evt.gate_time = std::max(0, std::min(value, 61690)); break;
        case 3: evt.velocity = std::max(1, std::min(value, 127)); break;
        case 4: evt.cv2 = std::max(0, std::min(value, 127)); break;
        case 5: evt.accent = (value != 0); break;
        case 6: evt.slide = (value != 0); break;
        case 7: evt.mpx = (value != 0); break;
    }
}

void SEQMC4Editor::advanceCursor()
{
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    if (c.cursorPos < (int)c.events().size() - 1)
        c.cursorPos++;
}

void SEQMC4Editor::pushUndo()
{
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    mc4::ChannelSnapshot snap;
    snap.patterns = c.patterns;
    snap.activePattern = c.activePattern;
    snap.cursorPos = c.cursorPos;
    snap.fieldCursor = c.fieldCursor;
    snap.activeLayer = c.activeLayer;
    undoStack.push_back(snap);
    if ((int)undoStack.size() > kMaxUndoDepth)
        undoStack.erase(undoStack.begin());
    redoStack.clear(); // New edit invalidates redo history
}

void SEQMC4Editor::performUndo()
{
    if (undoStack.empty()) return;
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    mc4::ChannelSnapshot redoSnap;
    redoSnap.patterns = c.patterns;
    redoSnap.activePattern = c.activePattern;
    redoSnap.cursorPos = c.cursorPos;
    redoSnap.fieldCursor = c.fieldCursor;
    redoSnap.activeLayer = c.activeLayer;
    redoStack.push_back(redoSnap);
    auto snap = undoStack.back();
    undoStack.pop_back();
    c.patterns = snap.patterns;
    c.activePattern = snap.activePattern;
    c.cursorPos = snap.cursorPos;
    c.fieldCursor = snap.fieldCursor;
    c.activeLayer = snap.activeLayer;
    c.clampCursor();
}

void SEQMC4Editor::performRedo()
{
    if (redoStack.empty()) return;
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    mc4::ChannelSnapshot undoSnap;
    undoSnap.patterns = c.patterns;
    undoSnap.activePattern = c.activePattern;
    undoSnap.cursorPos = c.cursorPos;
    undoSnap.fieldCursor = c.fieldCursor;
    undoSnap.activeLayer = c.activeLayer;
    undoStack.push_back(undoSnap);
    auto snap = redoStack.back();
    redoStack.pop_back();
    c.patterns = snap.patterns;
    c.activePattern = snap.activePattern;
    c.cursorPos = snap.cursorPos;
    c.fieldCursor = snap.fieldCursor;
    c.activeLayer = snap.activeLayer;
    c.clampCursor();
}

void SEQMC4Editor::insertEvent(bool before)
{
    pushUndo();
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    mc4::Event def;
    def.pitch = -1;  // Always insert as rest; default note only applies via nudge
    int quarter = std::max(1, seq().timebase / 4);  // Quarter of timebase (e.g. 30 at TB=120)
    def.step_time = quarter;
    def.gate_time = quarter;  // Legato by default — ready to play once pitch is set
    if (before) {
        // Insert BEFORE cursor (stays at same index, new event takes current position)
        c.adjustRepeatMarksForInsert(c.cursorPos);
        c.events().insert(c.events().begin() + c.cursorPos, def);
    } else {
        // Insert AFTER cursor and move to new event
        int insertPos = c.cursorPos + 1;
        c.adjustRepeatMarksForInsert(insertPos);
        c.events().insert(c.events().begin() + insertPos, def);
        c.cursorPos = insertPos;
    }
}

void SEQMC4Editor::deleteEvent()
{
    pushUndo();
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    if (c.events().empty()) return;
    c.adjustRepeatMarksForDelete(c.cursorPos);
    c.events().erase(c.events().begin() + c.cursorPos);
    if (c.events().empty()) {
        mc4::Event rest;
        rest.pitch = -1;
        rest.gate_time = std::max(1, seq().timebase / 4);
        c.events().push_back(rest); // Always keep at least one (as a rest)
    }
    c.clampCursor();
}

bool SEQMC4Editor::handleNavigationKey(const juce::KeyPress& key)
{
    int keyCode = key.getKeyCode();
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();

    // Shift+Up/Down = measure navigation (laptop-friendly PageUp/Down alternative)
    if (key.getModifiers().isShiftDown()) {
        if (keyCode == juce::KeyPress::upKey) {
            int measure, step;
            c.getMeasureInfo(c.cursorPos, measure, step);
            if (measure > 1)
                c.cursorPos = c.findMeasureStart(measure - 1);
            else
                c.cursorPos = 0;
            return true;
        }
        if (keyCode == juce::KeyPress::downKey) {
            int measure, step;
            c.getMeasureInfo(c.cursorPos, measure, step);
            c.cursorPos = c.findMeasureStart(measure + 1);
            c.clampCursor();
            return true;
        }
    }

    if (keyCode == juce::KeyPress::upKey) {
        if (c.cursorPos > 0) c.cursorPos--;
        return true;
    }
    if (keyCode == juce::KeyPress::downKey) {
        if (c.cursorPos < (int)c.events().size() - 1) c.cursorPos++;
        return true;
    }
    if (keyCode == juce::KeyPress::leftKey) {
        if (c.fieldCursor > 0) c.fieldCursor--;
        return true;
    }
    if (keyCode == juce::KeyPress::rightKey) {
        if (c.fieldCursor < (int)mc4::Field::NumFields - 1) c.fieldCursor++;
        return true;
    }
    if (keyCode == juce::KeyPress::pageUpKey) {
        int measure, step;
        c.getMeasureInfo(c.cursorPos, measure, step);
        if (measure > 1)
            c.cursorPos = c.findMeasureStart(measure - 1);
        else
            c.cursorPos = 0;
        return true;
    }
    if (keyCode == juce::KeyPress::pageDownKey) {
        int measure, step;
        c.getMeasureInfo(c.cursorPos, measure, step);
        c.cursorPos = c.findMeasureStart(measure + 1);
        c.clampCursor();
        return true;
    }
    if (keyCode == juce::KeyPress::homeKey) {
        c.cursorPos = 0;
        return true;
    }
    if (keyCode == juce::KeyPress::endKey) {
        c.cursorPos = std::max(0, (int)c.events().size() - 1);
        return true;
    }
    return false;
}

bool SEQMC4Editor::handleEditCommand(const juce::KeyPress& key)
{
    int keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();

    // Shift+I = insert before cursor
    if (keyCode == 'I' && shift) {
        insertEvent(true);
        return true;
    }
    // I = insert after cursor
    if (keyCode == 'I' && !shift) {
        insertEvent(false);
        return true;
    }

    // Shift+D = delete multiple
    if (keyCode == 'D' && shift) {
        inputMode = mc4::InputMode::DeleteMulti;
        inputBuffer.clear();
        return true;
    }
    // D = delete single
    if (keyCode == 'D' && !shift) {
        deleteEvent();
        return true;
    }

    // Shift+C = copy-insert (ripple)
    if (keyCode == 'C' && shift) {
        copyState = mc4::CopyState{};
        copyState.insertMode = true;
        // Auto-detect current measure as start measure
        {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            int measure, step;
            ch().getMeasureInfo(ch().cursorPos, measure, step);
            copyState.startMeasure = measure;
        }
        inputMode = mc4::InputMode::CopyEndMeas;
        inputBuffer = juce::String(copyState.startMeasure); // Pre-populate end = start
        return true;
    }
    // C = copy (overwrite)
    if (keyCode == 'C' && !shift) {
        copyState = mc4::CopyState{};
        copyState.insertMode = false;
        // Auto-detect current measure as start measure
        {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            int measure, step;
            ch().getMeasureInfo(ch().cursorPos, measure, step);
            copyState.startMeasure = measure;
        }
        inputMode = mc4::InputMode::CopyEndMeas;
        inputBuffer = juce::String(copyState.startMeasure); // Pre-populate end = start
        return true;
    }

    // Shift+R = repeat end (set count), or clear repeat mark at cursor
    if (keyCode == 'R' && shift) {
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        // Check if there's a committed repeat mark ending at cursor — if so, remove it
        bool removed = false;
        for (int i = (int)c.repeatMarks().size() - 1; i >= 0; --i) {
            if (c.repeatMarks()[i].endEvent == c.cursorPos ||
                c.repeatMarks()[i].startEvent == c.cursorPos) {
                c.repeatMarks().erase(c.repeatMarks().begin() + i);
                removed = true;
            }
        }
        if (!removed) {
            // No mark to remove — enter repeat count mode
            inputMode = mc4::InputMode::RepeatEnd;
            inputBuffer.clear();
        }
        return true;
    }
    // R = toggle repeat start at cursor
    if (keyCode == 'R' && !shift) {
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (c.pendingRepeatStart() == c.cursorPos) {
            // Already a pending start here — cancel it
            c.pendingRepeatStart() = -1;
        } else {
            c.pendingRepeatStart() = c.cursorPos;
        }
        return true;
    }

    // V = divide
    if (keyCode == 'V' && !shift) {
        inputMode = mc4::InputMode::Divide;
        inputBuffer.clear();
        return true;
    }

    // J = join
    if (keyCode == 'J' && !shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (c.cursorPos < (int)c.events().size() - 1) {
            auto& a = c.events()[c.cursorPos];
            const auto& b = c.events()[c.cursorPos + 1];
            int newST = a.step_time + b.step_time;
            int newGT;
            if (a.gate_time >= a.step_time) {
                newGT = a.gate_time + b.gate_time; // Tied
            } else {
                newGT = a.gate_time; // Only A's sounded portion
            }
            a.step_time = newST;
            a.gate_time = newGT;
            a.measure_end = a.measure_end || b.measure_end;
            c.events().erase(c.events().begin() + c.cursorPos + 1);
        }
        return true;
    }

    // A = toggle accent
    if (keyCode == 'A' && !shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (!c.events().empty())
            c.events()[c.cursorPos].accent = !c.events()[c.cursorPos].accent;
        return true;
    }

    // S = toggle slide
    if (keyCode == 'S' && !shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (!c.events().empty())
            c.events()[c.cursorPos].slide = !c.events()[c.cursorPos].slide;
        return true;
    }

    // X = toggle MPX flag
    if (keyCode == 'X' && !shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (!c.events().empty())
            c.events()[c.cursorPos].mpx = !c.events()[c.cursorPos].mpx;
        return true;
    }

    // Shift+X = clear event (reset to rest, preserving step time and measure_end)
    if (keyCode == 'X' && shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (!c.events().empty()) {
            auto& evt = c.events()[c.cursorPos];
            int st = evt.step_time;
            bool me = evt.measure_end;
            evt = mc4::Event{};
            evt.pitch = -1;
            evt.step_time = st;
            evt.gate_time = std::max(1, seq().timebase / 4);
            evt.measure_end = me;
        }
        return true;
    }

    // Shift+M = toggle measure end (clear it if set, set it if unset)
    if (keyCode == 'M' && shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (!c.events().empty())
            c.events()[c.cursorPos].measure_end = !c.events()[c.cursorPos].measure_end;
        return true;
    }
    // M = measure end toggle. If not set: commit input, set measure_end, advance.
    //                          If already set: clear measure_end (no advance).
    if (keyCode == 'M' && !shift) {
        pushUndo();
        {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            if (!c.events().empty()) {
                if (c.events()[c.cursorPos].measure_end) {
                    // Already a measure end — toggle it off, don't advance
                    c.events()[c.cursorPos].measure_end = false;
                    return true;
                }
                // Not a measure end — commit any pending input, set it, advance
                if (inputBuffer.isNotEmpty()) {
                    commitValue(inputBuffer.getIntValue());
                    inputBuffer.clear();
                }
                c.events()[c.cursorPos].measure_end = true;
            }
        }
        advanceCursor();
        return true;
    }

    // [ and ] = non-ripple ST edit (shift event boundary without changing total length)
    // [ = move event earlier (decrease this ST, increase previous ST)
    // ] = move event later (increase this ST, decrease previous ST)
    // Shift+[ / Shift+] = step by 10 instead of 1
    if (keyCode == '[' || keyCode == ']') {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (c.events().empty() || c.cursorPos == 0) return true; // Need a previous event
        auto& curr = c.events()[c.cursorPos];
        auto& prev = c.events()[c.cursorPos - 1];
        int step = shift ? 10 : 1;
        if (keyCode == '[') {
            // Move event later: increase curr ST, decrease prev ST
            int delta = std::min(step, prev.step_time - 1); // Keep prev ST >= 1
            if (delta > 0) {
                curr.step_time += delta;
                prev.step_time -= delta;
            }
        } else {
            // Move event earlier: decrease curr ST, increase prev ST
            int delta = std::min(step, curr.step_time - 1); // Keep curr ST >= 1
            if (delta > 0) {
                curr.step_time -= delta;
                prev.step_time += delta;
            }
        }
        return true;
    }

    // . = rest (gate=0, advance) — but not Shift+. (next pattern)
    if ((keyCode == '.' && !shift) || keyCode == juce::KeyPress::numberPadDecimalPoint) {
        {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            if (!c.events().empty())
                c.events()[c.cursorPos].gate_time = 0;
        }
        advanceCursor();
        return true;
    }

    // T = tie (gate=step_time, advance) — but not Shift+T (tempo)
    if (keyCode == 'T' && !shift) {
        {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            auto& c = ch();
            if (!c.events().empty())
                c.events()[c.cursorPos].gate_time = c.events()[c.cursorPos].step_time;
        }
        advanceCursor();
        return true;
    }

    // O = rotate current measure by ST ticks (full rotation)
    // Shift+O = rotate only notes in current measure (preserve groove/rests)
    if (keyCode == 'O') {
        inputMode = shift ? mc4::InputMode::RotateNotesOnly : mc4::InputMode::RotateMeasure;
        inputBuffer.clear();
        return true;
    }

    // Shift+P = double pattern length (duplicate all events and append)
    if (keyCode == 'P' && shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        auto& evts = c.events();
        if (!evts.empty()) {
            // Copy all events
            std::vector<mc4::Event> copy(evts.begin(), evts.end());
            // Clear measure_end on last event of first half so it flows into the copy
            if (!evts.empty())
                evts.back().measure_end = false;
            // Append
            evts.insert(evts.end(), copy.begin(), copy.end());
        }
        return true;
    }

    // P = commit current pattern: duplicate to a new pattern and switch editor to it
    if (keyCode == 'P' && !shift) {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        // Copy current pattern to a new one
        mc4::Pattern newPat = c.patterns[c.activePattern];
        c.patterns.push_back(newPat);
        // Switch editor (and playback) to the new pattern
        c.activePattern = (int)c.patterns.size() - 1;
        c.cursorPos = 0;
        return true;
    }

    // < (Shift+,) = previous pattern, > (Shift+.) = next pattern
    // Seamless switch: keeps playback tick counter, just swaps which event list is read
    // On macOS, Shift+, produces '<' (0x3C) and Shift+. produces '>' (0x3E)
    if ((keyCode == ',' && shift) || keyCode == '<') {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (c.activePattern > 0) {
            c.activePattern--;
            c.clampCursor();
        }
        return true;
    }
    if ((keyCode == '.' && shift) || keyCode == '>') {
        pushUndo();
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        auto& c = ch();
        if (c.activePattern < (int)c.patterns.size() - 1) {
            c.activePattern++;
            c.clampCursor();
        }
        return true;
    }

    return false;
}

bool SEQMC4Editor::handleLayerSelection(const juce::KeyPress& key)
{
    if (!key.getModifiers().isShiftDown()) return false;

    int keyCode = key.getKeyCode();
    int layer = -1;

    // On macOS, Shift+1 produces '!', Shift+2 -> '@', etc.
    // UK keyboard: Shift+3 = £ (0xA3), US keyboard: Shift+3 = # (0x23)
    // Match both the raw digit and all possible shifted symbols.
    if (keyCode == '1' || keyCode == '!') layer = 0;       // CV1 (pitch)
    else if (keyCode == '2' || keyCode == '@') layer = 1;   // Step Time
    else if (keyCode == '3' || keyCode == '#' || keyCode == 0xA3) layer = 2;   // Gate Time (# US, £ UK)
    else if (keyCode == '4' || keyCode == '$') layer = 3;   // Velocity
    else if (keyCode == '5' || keyCode == '%') layer = 4;   // CV2
    else if (keyCode == '6' || keyCode == '^') layer = 5;   // MPX

    if (layer >= 0) {
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        ch().activeLayer = layer;
        ch().fieldCursor = layerToField(layer);
        return true;
    }
    return false;
}

bool SEQMC4Editor::handleChannelSelection(const juce::KeyPress& key)
{
    int keyCode = key.getKeyCode();

    // F1-F4 for channel selection
    if (keyCode >= juce::KeyPress::F1Key && keyCode <= juce::KeyPress::F4Key) {
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        seq().activeChannel = keyCode - juce::KeyPress::F1Key;
        undoStack.clear(); // Channel switch resets undo context
        redoStack.clear();
        return true;
    }

    return false;
}

bool SEQMC4Editor::handleTransportKey(const juce::KeyPress& key)
{
    int keyCode = key.getKeyCode();
    bool shift = key.getModifiers().isShiftDown();

    // Space = play/stop toggle — ONLY in standalone mode.
    // In plugin mode, let Space pass through to the host for transport control.
    if (keyCode == juce::KeyPress::spaceKey) {
        if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
            bool current = proc.standalonePlayRequest.load();
            proc.standalonePlayRequest.store(!current);
            return true;
        }
        return false; // Let host handle it
    }

    // Tab = toggle cycle
    if (keyCode == juce::KeyPress::tabKey) {
        std::lock_guard<std::mutex> lock(proc.sequenceMutex);
        seq().cycleOn = !seq().cycleOn;
        return true;
    }

    // Shift+T = edit tempo
    if (keyCode == 'T' && shift) {
        inputMode = mc4::InputMode::TempoEdit;
        inputBuffer.clear();
        return true;
    }

    // Shift+B = edit timebase
    if (keyCode == 'B' && shift) {
        inputMode = mc4::InputMode::TimebaseEdit;
        inputBuffer.clear();
        return true;
    }

    // Shift+N = set default note for new events
    if (keyCode == 'N' && shift) {
        inputMode = mc4::InputMode::DefaultNote;
        inputBuffer.clear();
        return true;
    }

    // Shift+V = set base velocity
    if (keyCode == 'V' && shift) {
        inputMode = mc4::InputMode::BaseVelocity;
        inputBuffer.clear();
        return true;
    }

    // N = toggle step record mode (MIDI note input)
    if (keyCode == 'N' && !shift) {
        bool current = proc.stepRecordEnabled.load();
        proc.stepRecordEnabled.store(!current);
        return true;
    }

    // Shift+S or Ctrl+S = sync tempo to host
    if ((keyCode == 'S' && shift) ||
        (keyCode == 'S' && key.getModifiers().isCtrlDown())) {
        double hostBpm = proc.hostTempo.load();
        if (hostBpm > 0) {
            std::lock_guard<std::mutex> lock(proc.sequenceMutex);
            seq().tempo = (int)std::round(hostBpm);
        }
        return true;
    }

    return false;
}

bool SEQMC4Editor::handleNudgeKey(const juce::KeyPress& key)
{
    if (!key.getModifiers().isCommandDown()) return false;

    int keyCode = key.getKeyCode();

    // Cmd+Up/Down without a value field = measure navigation (handled in nav handler)
    // But if we're nudging, we want Cmd+arrows to adjust the current field value.
    // Distinguish: Cmd+Up/Down = nudge by large step, Cmd+Left/Right = nudge by 1

    int delta = 0;
    if (keyCode == juce::KeyPress::rightKey) delta = 1;
    else if (keyCode == juce::KeyPress::leftKey) delta = -1;
    else if (keyCode == juce::KeyPress::upKey) delta = 10;
    else if (keyCode == juce::KeyPress::downKey) delta = -10;
    else return false;

    pushUndo();
    std::lock_guard<std::mutex> lock(proc.sequenceMutex);
    auto& c = ch();
    if (c.events().empty()) return false;
    auto& evt = c.events()[c.cursorPos];

    switch (c.fieldCursor) {
        case 0: // Pitch: left/right = semitone, up/down = octave
            if (delta == 10) delta = 12;
            if (delta == -10) delta = -12;
            if (evt.pitch < 0) evt.pitch = (proc.config.defaultNote >= 0) ? proc.config.defaultNote : 48;
            evt.pitch = std::max(0, std::min(127, evt.pitch + delta));
            break;
        case 1: // Step time
            evt.step_time = std::max(1, std::min(61690, evt.step_time + delta));
            break;
        case 2: // Gate time
            evt.gate_time = std::max(0, std::min(61690, evt.gate_time + delta));
            break;
        case 3: // Velocity
            if (evt.velocity < 0) evt.velocity = proc.config.baseVelocity; // Promote from default
            evt.velocity = std::max(1, std::min(127, evt.velocity + delta));
            break;
        case 4: // CV2
            evt.cv2 = std::max(0, std::min(127, evt.cv2 + delta));
            break;
        case 5: // Accent (toggle on any nudge)
            evt.accent = !evt.accent;
            break;
        case 6: // Slide (toggle on any nudge)
            evt.slide = !evt.slide;
            break;
        case 7: // MPX (toggle on any nudge)
            evt.mpx = !evt.mpx;
            break;
    }
    return true;
}
