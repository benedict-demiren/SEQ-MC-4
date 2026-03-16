#include "PluginProcessor.h"
#include "PluginEditor.h"

SEQMC4Processor::SEQMC4Processor()
    : AudioProcessor(BusesProperties()) // No audio buses — MIDI only
{
}

SEQMC4Processor::~SEQMC4Processor() = default;

void SEQMC4Processor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    engine.prepare(sampleRate);
    engine.reset();
    wasPlaying = false;
}

void SEQMC4Processor::releaseResources()
{
    engine.reset();
}

void SEQMC4Processor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    buffer.clear(); // No audio output

    // Determine play state
    bool playing = false;

    if (auto* playHead = getPlayHead()) {
        if (auto pos = playHead->getPosition()) {
            playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
                hostTempo.store(*bpm);
        }
    }

    // Standalone mode: use the editor's play toggle
    if (wrapperType == wrapperType_Standalone) {
        playing = standalonePlayRequest.load();
    }

    isCurrentlyPlaying.store(playing);

    // Capture incoming MIDI note-ons for step recording
    if (stepRecordEnabled.load(std::memory_order_relaxed)) {
        for (const auto metadata : midiMessages) {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                pushMidiNote(msg.getNoteNumber(), msg.getVelocity());
        }
    }

    // Try to lock the sequence for reading
    midiMessages.clear();

    if (sequenceMutex.try_lock()) {
        engine.processMidi(midiMessages, buffer.getNumSamples(),
                           sequence, config, playing, wasPlaying);
        sequenceMutex.unlock();
    }

    wasPlaying = playing;
}

juce::AudioProcessorEditor* SEQMC4Processor::createEditor()
{
    return new SEQMC4Editor(*this);
}

// ============================================================================
// JSON Serialisation
// ============================================================================

static juce::var eventToVar(const mc4::Event& e)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("p", e.pitch);
    obj->setProperty("st", e.step_time);
    obj->setProperty("gt", e.gate_time);
    obj->setProperty("vel", e.velocity);
    obj->setProperty("cv2", e.cv2);
    obj->setProperty("acc", e.accent);
    obj->setProperty("sld", e.slide);
    obj->setProperty("mend", e.measure_end);
    obj->setProperty("mpx", e.mpx);
    return juce::var(obj);
}

static mc4::Event varToEvent(const juce::var& v)
{
    mc4::Event e;
    if (auto* obj = v.getDynamicObject()) {
        e.pitch      = (int)obj->getProperty("p");
        e.step_time  = (int)obj->getProperty("st");
        e.gate_time  = (int)obj->getProperty("gt");
        e.velocity   = (int)obj->getProperty("vel");
        e.cv2        = (int)obj->getProperty("cv2");
        e.accent     = (bool)obj->getProperty("acc");
        e.slide      = (bool)obj->getProperty("sld");
        e.measure_end = (bool)obj->getProperty("mend");
        e.mpx        = (bool)obj->getProperty("mpx");
    }
    return e;
}

static juce::var repeatMarkToVar(const mc4::RepeatMark& rm)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("start", rm.startEvent);
    obj->setProperty("end", rm.endEvent);
    obj->setProperty("count", rm.count);
    return juce::var(obj);
}

static mc4::RepeatMark varToRepeatMark(const juce::var& v)
{
    mc4::RepeatMark rm;
    if (auto* obj = v.getDynamicObject()) {
        rm.startEvent = (int)obj->getProperty("start");
        rm.endEvent   = (int)obj->getProperty("end");
        rm.count      = (int)obj->getProperty("count");
    }
    return rm;
}

static juce::var patternToVar(const mc4::Pattern& pat)
{
    auto* obj = new juce::DynamicObject();

    juce::Array<juce::var> evts;
    for (const auto& e : pat.events)
        evts.add(eventToVar(e));
    obj->setProperty("events", evts);

    juce::Array<juce::var> rmarks;
    for (const auto& rm : pat.repeatMarks)
        rmarks.add(repeatMarkToVar(rm));
    obj->setProperty("repeats", rmarks);

    return juce::var(obj);
}

static mc4::Pattern varToPattern(const juce::var& v)
{
    mc4::Pattern pat;
    pat.events.clear(); // Remove default event
    if (auto* obj = v.getDynamicObject()) {
        if (auto* evts = obj->getProperty("events").getArray()) {
            for (const auto& ev : *evts)
                pat.events.push_back(varToEvent(ev));
        }
        if (pat.events.empty()) {
            mc4::Event e;
            e.pitch = -1;
            e.gate_time = 30;
            pat.events.push_back(e);
        }
        if (auto* rmarks = obj->getProperty("repeats").getArray()) {
            for (const auto& rm : *rmarks)
                pat.repeatMarks.push_back(varToRepeatMark(rm));
        }
    }
    return pat;
}

static juce::var channelToVar(const mc4::Channel& ch)
{
    auto* obj = new juce::DynamicObject();

    juce::Array<juce::var> pats;
    for (const auto& p : ch.patterns)
        pats.add(patternToVar(p));
    obj->setProperty("patterns", pats);
    obj->setProperty("activePat", ch.activePattern);

    obj->setProperty("cursor", ch.cursorPos);
    obj->setProperty("layer", ch.activeLayer);
    obj->setProperty("field", ch.fieldCursor);

    return juce::var(obj);
}

static void varToChannel(const juce::var& v, mc4::Channel& ch)
{
    if (auto* obj = v.getDynamicObject()) {
        // Load patterns (new format)
        if (auto* pats = obj->getProperty("patterns").getArray()) {
            ch.patterns.clear();
            for (const auto& p : *pats)
                ch.patterns.push_back(varToPattern(p));
            if (ch.patterns.empty())
                ch.patterns.emplace_back();
        }
        // Backwards compat: old format had "events" and "repeats" at channel level
        else if (auto* evts = obj->getProperty("events").getArray()) {
            ch.patterns.clear();
            mc4::Pattern pat;
            pat.events.clear();
            for (const auto& ev : *evts)
                pat.events.push_back(varToEvent(ev));
            if (pat.events.empty()) {
                mc4::Event e;
                e.pitch = -1;
                e.gate_time = 30;
                pat.events.push_back(e);
            }
            if (auto* rmarks = obj->getProperty("repeats").getArray()) {
                for (const auto& rm : *rmarks)
                    pat.repeatMarks.push_back(varToRepeatMark(rm));
            }
            ch.patterns.push_back(pat);
        }

        ch.activePattern = std::max(0, std::min((int)ch.patterns.size() - 1,
                                                 (int)obj->getProperty("activePat")));
        ch.cursorPos   = (int)obj->getProperty("cursor");
        ch.activeLayer = (int)obj->getProperty("layer");
        ch.fieldCursor = (int)obj->getProperty("field");

        ch.clampCursor();
    }
}

static juce::var configToVar(const mc4::Config& cfg)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("cv2Mode", cfg.cv2OutputMode);
    obj->setProperty("mpxCC", cfg.mpxOutputCC);
    obj->setProperty("accentBoost", cfg.accentBoost);
    obj->setProperty("baseVel", cfg.baseVelocity);
    obj->setProperty("defaultNote", cfg.defaultNote);
    obj->setProperty("portTime", cfg.portamentoTime);
    obj->setProperty("portCurve", cfg.portamentoCurve);
    obj->setProperty("pbRange", cfg.pitchBendRange);
    obj->setProperty("noteDisplay", cfg.noteDisplayMode);
    return juce::var(obj);
}

static void varToConfig(const juce::var& v, mc4::Config& cfg)
{
    if (auto* obj = v.getDynamicObject()) {
        cfg.cv2OutputMode   = (int)obj->getProperty("cv2Mode");
        cfg.mpxOutputCC     = (int)obj->getProperty("mpxCC");
        cfg.accentBoost     = (int)obj->getProperty("accentBoost");
        cfg.baseVelocity    = (int)obj->getProperty("baseVel");
        cfg.defaultNote     = (int)obj->getProperty("defaultNote");
        cfg.portamentoTime  = (int)obj->getProperty("portTime");
        cfg.portamentoCurve = (int)obj->getProperty("portCurve");
        cfg.pitchBendRange  = (int)obj->getProperty("pbRange");
        cfg.noteDisplayMode = (int)obj->getProperty("noteDisplay");
    }
}

void SEQMC4Processor::getStateInformation(juce::MemoryBlock& destData)
{
    std::lock_guard<std::mutex> lock(sequenceMutex);

    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1); // Schema version for future compatibility

    // Sequence
    auto* seqObj = new juce::DynamicObject();
    juce::Array<juce::var> chans;
    for (int i = 0; i < 4; ++i)
        chans.add(channelToVar(sequence.channels[i]));
    seqObj->setProperty("channels", chans);
    seqObj->setProperty("activeCh", sequence.activeChannel);
    seqObj->setProperty("timebase", sequence.timebase);
    seqObj->setProperty("tempo", sequence.tempo);
    seqObj->setProperty("cycle", sequence.cycleOn);
    root->setProperty("sequence", juce::var(seqObj));

    // Config
    root->setProperty("config", configToVar(config));

    juce::String json = juce::JSON::toString(juce::var(root));
    destData.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void SEQMC4Processor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::String json(juce::CharPointer_UTF8(static_cast<const char*>(data)),
                      (size_t)sizeInBytes);
    auto root = juce::JSON::parse(json);

    if (root.isVoid()) return; // Invalid JSON

    std::lock_guard<std::mutex> lock(sequenceMutex);

    // Check version
    int version = (int)root.getProperty("version", 0);
    if (version < 1) return; // Unknown format

    // Sequence
    auto seqVar = root.getProperty("sequence", {});
    if (auto* seqObj = seqVar.getDynamicObject()) {
        if (auto* chans = seqObj->getProperty("channels").getArray()) {
            for (int i = 0; i < std::min(4, chans->size()); ++i)
                varToChannel((*chans)[i], sequence.channels[i]);
        }
        sequence.activeChannel = std::max(0, std::min(3, (int)seqObj->getProperty("activeCh")));
        sequence.timebase      = std::max(1, (int)seqObj->getProperty("timebase"));
        sequence.tempo         = std::max(20, std::min(300, (int)seqObj->getProperty("tempo")));
        sequence.cycleOn       = (bool)seqObj->getProperty("cycle");
    }

    // Config
    auto cfgVar = root.getProperty("config", {});
    if (!cfgVar.isVoid())
        varToConfig(cfgVar, config);

    // Reset playback engine so it doesn't try to play from stale positions
    engine.reset();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SEQMC4Processor();
}
