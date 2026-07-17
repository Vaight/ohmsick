#include "plugin.h"
#include "display.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {

constexpr float potSmoothingAlpha = 0.25f;
constexpr int removeAssignmentAction = 0;
constexpr int digitalAssignmentAction = 1;
constexpr int pullupAssignmentAction = 2;
constexpr int analogAssignmentAction = 3;

/*
 * a method that returns the plugin config file.
 * PARAMS: none
 * RETURNS:
 *   ∟ juce::File     : the found config file
 */
juce::File configFilePath() {
    
    // using APPDAPA (windows)
    if (const char* appData = std::getenv("APPDATA")) {
        if (*appData != '\0') {
            return juce::File(juce::String(appData)).getChildFile("ohmsick/config.json");
        }
    }

    // using xdg config (linux/unix)
    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdgConfig != '\0') {
            return juce::File(juce::String(xdgConfig)).getChildFile("ohmsick/config.json");
        }
    }

    // using home directory (linux/unix)
    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') {
            return juce::File(juce::String(home)).getChildFile(".config/ohmsick/config.json");
        }
    }

    return {};
}

/*
 * helper method for executing a jlimit clamp between floats 0.0 and 1.0.
 * PARAMS:
 *   ∟ float   : the provided float to apply ther jlimit to
 * RETURNS:
 *   ∟ float   : the resulting jlimited float
 */
float clamp01(float value) {
    return juce::jlimit(0.0f, 1.0f, value);
}

/*
 * helper method for executing a jlimit clamp between integers 0 and 127.
 * PARAMS:
 *   ∟ float   : the provided float to apply ther jlimit to
 * RETURNS:
 *   ∟ int   : the resulting jlimited integer
 */
int normalizedToMidiValue(float value) {
    return juce::jlimit(0, 127, juce::roundToInt(clamp01(value) * 127.0f));
}

/*
 * helper method to convert a parsed hardware input kind into a firmware
 * assignment action id.
 * PARAMS:
 *   ∟ hardware::Kind kind     : parsed hardware input kind from serial data
 *   ∟ int existingAction      : current action for the same pin, if any
 * RETURNS:
 *   ∟ int                     : assignment action id for GUI/session mapping
 */
int assignmentActionForKind(hardware::Kind kind, int existingAction) {
    if (kind == hardware::Kind::Button && existingAction == pullupAssignmentAction) {
        return pullupAssignmentAction;
    }

    return kind == hardware::Kind::Pot
        ? analogAssignmentAction
        : digitalAssignmentAction;
}

/*
 * helper method to compare two session mapping lists without requiring C++20
 * aggregate equality support.
 * PARAMS:
 *   ∟ const std::vector<SessionMapping>& lhs   : first mapping list
 *   ∟ const std::vector<SessionMapping>& rhs   : second mapping list
 * RETURNS:
 *   ∟ bool                                     : true when both lists match
 */
bool sessionMappingsEqual(
    const std::vector<HardwareControlAudioProcessor::SessionMapping>& lhs,
    const std::vector<HardwareControlAudioProcessor::SessionMapping>& rhs) {
    return lhs.size() == rhs.size()
        && std::equal(
            lhs.begin(),
            lhs.end(),
            rhs.begin(),
            [](const auto& a, const auto& b) {
                return a.pin == b.pin && a.action == b.action;
            });
}

}  // end namespace

/*
 * juce plugin constructor, sets up basic properties
 * PARAMS: none
 * RETURNS: none
 */
HardwareControlAudioProcessor::HardwareControlAudioProcessor() :
    // call the audio processor to ensure audio channel output is set to stereo
    AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    // call the serial output congifuration
    serialConfig_(loadSerialConfig())
{
    // iterate over the maximum hardware input slots
    for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
        // 
        targetValues_[slot].store(0.0f);
        targetKinds_[slot].store(static_cast<int>(hardware::Kind::Pot));
        targetHasValue_[slot].store(false);
        lastSentCcValues_[slot].store(-1);
        hasSentCcValues_[slot].store(false);
    }

    startSerialThread();
}

/*
 * juce plugin destructor method.
 * stops the serial worker before processor teardown.
 * PARAMS: none
 * RETURNS: none
 */
HardwareControlAudioProcessor::~HardwareControlAudioProcessor() {
    stopSerialThread();
}

/*
 * juce plugin method for returning the display name.
 * PARAMS: none
 * RETURNS:
 *   ∟ const juce::String   : plugin name defined by the juce target
 */
const juce::String HardwareControlAudioProcessor::getName() const {
    return JucePlugin_Name;
}

/*
 * juce plugin method for declaring MIDI input support.
 * PARAMS: none
 * RETURNS:
 *   ∟ bool   : true when MIDI input is accepted
 */
bool HardwareControlAudioProcessor::acceptsMidi() const {
    return true;
}

/*
 * juce plugin method for declaring MIDI output support.
 * PARAMS: none
 * RETURNS:
 *   ∟ bool   : true when MIDI output is produced
 */
bool HardwareControlAudioProcessor::producesMidi() const {
    return true;
}

/*
 * juce plugin method for declaring MIDI-effect plugin type.
 * PARAMS: none
 * RETURNS:
 *   ∟ bool   : false because this plugin is an instrument-style processor
 */
bool HardwareControlAudioProcessor::isMidiEffect() const {
    return false;
}

/*
 * juce plugin method for reporting audio tail length.
 * PARAMS: none
 * RETURNS:
 *   ∟ double   : tail length in seconds
 */
double HardwareControlAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

/*
 * juce plugin method for reporting program count.
 * PARAMS: none
 * RETURNS:
 *   ∟ int   : number of supported programs
 */
int HardwareControlAudioProcessor::getNumPrograms() {
    return 1;
}

/*
 * juce plugin method for reporting the selected program.
 * PARAMS: none
 * RETURNS:
 *   ∟ int   : selected program index
 */
int HardwareControlAudioProcessor::getCurrentProgram() {
    return 0;
}

/*
 * juce plugin method for setting the selected program.
 * PARAMS:
 *   ∟ int   : requested program index, unused because there is one program
 * RETURNS: none
 */
void HardwareControlAudioProcessor::setCurrentProgram(int) {
}

/*
 * juce plugin method for returning a program name.
 * PARAMS:
 *   ∟ int   : requested program index, unused because programs are unnamed
 * RETURNS:
 *   ∟ const juce::String   : empty program name
 */
const juce::String HardwareControlAudioProcessor::getProgramName(int) {
    return {};
}

/*
 * juce plugin method for changing a program name.
 * PARAMS:
 *   ∟ int                  : requested program index, unused
 *   ∟ const juce::String&  : requested program name, unused
 * RETURNS: none
 */
void HardwareControlAudioProcessor::changeProgramName(int, const juce::String&) {
}

/*
 * juce plugin method called before audio playback starts.
 * PARAMS:
 *   ∟ double   : sample rate, unused
 *   ∟ int      : block size, unused
 * RETURNS: none
 */
void HardwareControlAudioProcessor::prepareToPlay(double, int) {
}

/*
 * juce plugin method called when audio playback resources are released.
 * PARAMS: none
 * RETURNS: none
 */
void HardwareControlAudioProcessor::releaseResources() {
}

/*
 * juce plugin method for validating supported audio bus layouts.
 * PARAMS:
 *   ∟ const BusesLayout& layouts   : requested host bus layout
 * RETURNS:
 *   ∟ bool                         : true when the layout is supported
 */
bool HardwareControlAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    return layouts.getMainInputChannelSet().isDisabled()
        && (mainOutput == juce::AudioChannelSet::mono()
            || mainOutput == juce::AudioChannelSet::stereo());
}

/*
 * juce plugin audio callback.
 * converts current hardware input slot values into MIDI CC events.
 * PARAMS:
 *   ∟ juce::AudioBuffer<float>& buffer   : audio buffer to clear
 *   ∟ juce::MidiBuffer& midiMessages     : MIDI output buffer to populate
 * RETURNS: none
 */
void HardwareControlAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
        if (!targetHasValue_[slot].load()) {
            continue;
        }

        const auto kind = static_cast<hardware::Kind>(targetKinds_[slot].load());
        const float value = clamp01(targetValues_[slot].load());
        const int midiValue = kind == hardware::Kind::Button
            ? (value >= 0.5f ? 127 : 0)
            : normalizedToMidiValue(value);

        if (!hasSentCcValues_[slot].load() || midiValue != lastSentCcValues_[slot].load()) {
            midiMessages.addEvent(
                juce::MidiMessage::controllerEvent(midiChannel, firstMidiCc + slot, midiValue),
                0);
            lastSentCcValues_[slot].store(midiValue);
            hasSentCcValues_[slot].store(true);
        }
    }
}

/*
 * juce plugin method for declaring editor availability.
 * PARAMS: none
 * RETURNS:
 *   ∟ bool   : true because this plugin provides a custom editor
 */
bool HardwareControlAudioProcessor::hasEditor() const {
    return true;
}

/*
 * juce plugin method for creating the custom editor.
 * PARAMS: none
 * RETURNS:
 *   ∟ juce::AudioProcessorEditor*   : newly allocated plugin editor
 */
juce::AudioProcessorEditor* HardwareControlAudioProcessor::createEditor() {
    return new HardwareControlAudioProcessorEditor(*this);
}

/*
 * juce plugin method for serializing plugin state.
 * PARAMS:
 *   ∟ juce::MemoryBlock& destData   : destination memory block
 * RETURNS: none
 */
void HardwareControlAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto config = getSerialConfig();
    juce::MemoryOutputStream stream(destData, true);
    stream.writeString(config.device);
    stream.writeInt(config.baud);
    stream.writeBool(config.enabled);
}

/*
 * juce plugin method for restoring plugin state.
 * PARAMS:
 *   ∟ const void* data   : serialized state bytes
 *   ∟ int sizeInBytes    : number of serialized bytes
 * RETURNS: none
 */
void HardwareControlAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0) {
        return;
    }

    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);

    SerialConfig config;
    config.device = stream.readString();
    config.baud = stream.readInt();
    config.enabled = stream.readBool();

    setSerialConfig(config);
    if (config.enabled) {
        connectSerial(config.device, config.baud);
    }
}

/*
 * load serial configuration defaults from the platform config file.
 * PARAMS: none
 * RETURNS:
 *   ∟ SerialConfig   : loaded or default serial config
 */
HardwareControlAudioProcessor::SerialConfig HardwareControlAudioProcessor::loadSerialConfig() {
    SerialConfig config;

    const juce::File file = configFilePath();
    if (!file.existsAsFile()) {
        return config;
    }

    const juce::var parsed = juce::JSON::parse(file);
    if (!parsed.isObject()) {
        return config;
    }

    auto* object = parsed.getDynamicObject();
    if (object == nullptr) {
        return config;
    }

    config.enabled = static_cast<bool>(object->getProperty("enabled"));
    config.device = object->getProperty("device").toString();

    const juce::var baud = object->getProperty("baud");
    if (baud.isInt() || baud.isInt64() || baud.isDouble()) {
        config.baud = static_cast<int>(baud);
    }

    if (config.device.isEmpty()) {
        config.enabled = false;
    }

    return config;
}

/*
 * get the current serial configuration.
 * PARAMS: none
 * RETURNS:
 *   ∟ SerialConfig   : thread-safe copy of the current serial config
 */
HardwareControlAudioProcessor::SerialConfig HardwareControlAudioProcessor::getSerialConfig() const {
    const std::lock_guard<std::mutex> lock(serialConfigMutex_);
    return serialConfig_;
}

/*
 * replace the current serial configuration.
 * PARAMS:
 *   ∟ SerialConfig config   : new serial config to store
 * RETURNS: none
 */
void HardwareControlAudioProcessor::setSerialConfig(SerialConfig config) {
    const std::lock_guard<std::mutex> lock(serialConfigMutex_);
    serialConfig_ = std::move(config);
}

/*
 * connect to a serial device using the provided port and baud rate.
 * PARAMS:
 *   ∟ juce::String device   : serial device path or port name
 *   ∟ int baud              : requested baud rate
 * RETURNS: none
 */
void HardwareControlAudioProcessor::connectSerial(juce::String device, int baud) {
    disconnectSerial();

    SerialConfig config;
    config.enabled = device.isNotEmpty();
    config.device = std::move(device);
    config.baud = baud > 0 ? baud : 19200;
    setSerialConfig(config);

    if (config.enabled) {
        for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
            targetHasValue_[slot].store(false);
            lastSentCcValues_[slot].store(-1);
            hasSentCcValues_[slot].store(false);
        }
        clearSessionMappings();

        pushSerialLogLine("Connecting to " + config.device + " at " + juce::String(config.baud));
        startSerialThread();
    }
}

/*
 * disconnect from the current serial device.
 * PARAMS: none
 * RETURNS: none
 */
void HardwareControlAudioProcessor::disconnectSerial() {
    stopSerialThread();

    auto config = getSerialConfig();
    if (config.enabled || config.device.isNotEmpty()) {
        config.enabled = false;
        setSerialConfig(config);
        clearSessionMappings();
        pushSerialLogLine("Disconnected");
    }
}

/*
 * get the current serial connection status.
 * PARAMS: none
 * RETURNS:
 *   ∟ bool   : true when the serial worker is connected
 */
bool HardwareControlAudioProcessor::isSerialConnected() const {
    return connected_.load();
}

/*
 * drain queued serial log/status lines for GUI display.
 * PARAMS: none
 * RETURNS:
 *   ∟ juce::StringArray   : queued log lines, cleared from shared storage
 */
juce::StringArray HardwareControlAudioProcessor::drainSerialLogLines() {
    juce::StringArray lines;
    const std::lock_guard<std::mutex> lock(serialLogMutex_);

    for (const auto& line : serialLogLines_) {
        lines.add(line);
    }

    serialLogLines_.clear();
    return lines;
}

/*
 * validate and enqueue an assignment command for the serial device.
 * PARAMS:
 *   ∟ int pin      : hardware pin to assign
 *   ∟ int action   : firmware assignment action id
 * RETURNS: none
 */
void HardwareControlAudioProcessor::sendAssignmentCommand(int pin, int action) {
    if (pin < 0 || action < 0 || action > 3) {
        pushSerialLogLine("Invalid assignment command");
        return;
    }

    if (!isSerialConnected()) {
        pushSerialLogLine("Not connected; assignment was not sent");
        return;
    }

    const std::string command = "a " + std::to_string(pin) + " " + std::to_string(action);
    {
        const std::lock_guard<std::mutex> lock(outgoingSerialMutex_);
        outgoingSerialLines_.push_back(command);
    }

    updateSessionMapping(pin, action);
    pushSerialLogLine("Queued: " + juce::String(command));
}

/*
 * get a point-in-time copy of all hardware input slot values.
 * PARAMS: none
 * RETURNS:
 *   ∟ std::array<InputSnapshot, maxInputSlots>   : GUI/audio-safe slot state
 */
std::array<HardwareControlAudioProcessor::InputSnapshot, hardware::maxInputSlots>
HardwareControlAudioProcessor::getInputSnapshots() const {
    std::array<InputSnapshot, hardware::maxInputSlots> snapshots {};

    for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
        auto& snapshot = snapshots[static_cast<size_t>(slot)];
        snapshot.active = targetHasValue_[slot].load();
        snapshot.kind = static_cast<hardware::Kind>(targetKinds_[slot].load());
        snapshot.normalizedValue = clamp01(targetValues_[slot].load());
        snapshot.midiChannel = midiChannel;
        snapshot.midiCc = firstMidiCc + slot;
        snapshot.midiValue = snapshot.kind == hardware::Kind::Button
            ? (snapshot.normalizedValue >= 0.5f ? 127 : 0)
            : normalizedToMidiValue(snapshot.normalizedValue);
    }

    return snapshots;
}

/*
 * get the current GUI-visible session mappings.
 * PARAMS: none
 * RETURNS:
 *   ∟ std::vector<SessionMapping>   : thread-safe copy of mappings
 */
std::vector<HardwareControlAudioProcessor::SessionMapping>
HardwareControlAudioProcessor::getSessionMappings() const {
    const std::lock_guard<std::mutex> lock(sessionMappingsMutex_);
    return sessionMappings_;
}

/*
 * get the revision counter for the current session mappings.
 * PARAMS: none
 * RETURNS:
 *   ∟ int   : monotonically increasing revision for mapping changes
 */
int HardwareControlAudioProcessor::getSessionMappingsRevision() const {
    return sessionMappingsRevision_.load();
}

/*
 * start the serial worker thread when the serial config is enabled.
 * PARAMS: none
 * RETURNS: none
 */
void HardwareControlAudioProcessor::startSerialThread() {
    const auto config = getSerialConfig();
    if (!config.enabled || config.device.isEmpty()) {
        return;
    }

    stopSerialThread_.store(false);
    serialThread_ = std::thread([this, config] { serialThreadMain(config); });
}

/*
 * stop and join the serial worker thread.
 * PARAMS: none
 * RETURNS: none
 */
void HardwareControlAudioProcessor::stopSerialThread() {
    stopSerialThread_.store(true);

    if (serialThread_.joinable()) {
        serialThread_.join();
    }

    if (connected_.exchange(false)) {
        pushSerialLogLine("Serial port closed");
    }
}

/*
 * serial worker thread main loop.
 * reads incoming frames, sends queued assignment commands, and updates slot state.
 * PARAMS:
 *   ∟ SerialConfig config   : serial config captured for this worker run
 * RETURNS: none
 */
void HardwareControlAudioProcessor::serialThreadMain(SerialConfig config) {
    try {
        hardware::SerialReader reader(config.device.toStdString(), config.baud);
        connected_.store(true);
        pushSerialLogLine("Connected to " + config.device);

        std::string serialBuffer;
        std::array<float, hardware::maxInputSlots> smoothedValues {};
        bool loadedSessionMappingsFromSerial = false;

        while (!stopSerialThread_.load()) {
            for (const auto& command : drainOutgoingSerialLines()) {
                reader.sendLine(command);
                pushSerialLogLine("Sent: " + juce::String(command));
            }

            serialBuffer += reader.readAvailable();

            std::size_t newline = std::string::npos;
            while ((newline = serialBuffer.find('\n')) != std::string::npos) {
                std::string line = serialBuffer.substr(0, newline);
                serialBuffer.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                const auto parsedLine = hardware::parseLine(line);
                pushSerialLogLine(juce::String(parsedLine.text));

                const auto frame = hardware::parseFrame(line);
                if (!frame) {
                    continue;
                }

                if (!loadedSessionMappingsFromSerial
                    && frame->expectedCount == static_cast<int>(frame->readings.size())) {
                    replaceSessionMappingsFromFrame(*frame);
                    loadedSessionMappingsFromSerial = true;
                }

                std::array<float, hardware::maxInputSlots> nextValues {};
                std::array<int, hardware::maxInputSlots> nextKinds {};
                std::array<bool, hardware::maxInputSlots> touchedSlots {};
                nextKinds.fill(static_cast<int>(hardware::Kind::Pot));

                for (const auto& reading : frame->readings) {
                    if (reading.slot < 0 || reading.slot >= hardware::maxInputSlots) {
                        continue;
                    }

                    const auto kind = reading.kind;
                    const float value = clamp01(reading.normalizedValue);
                    if (kind == hardware::Kind::Pot) {
                        smoothedValues[reading.slot] += potSmoothingAlpha * (value - smoothedValues[reading.slot]);
                        nextValues[reading.slot] = smoothedValues[reading.slot];
                    } else {
                        smoothedValues[reading.slot] = value;
                        nextValues[reading.slot] = value;
                    }

                    nextKinds[reading.slot] = static_cast<int>(kind);
                    touchedSlots[reading.slot] = true;
                }

                for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
                    if (!touchedSlots[slot]) {
                        continue;
                    }

                    targetValues_[slot].store(nextValues[slot]);
                    targetKinds_[slot].store(nextKinds[slot]);
                    targetHasValue_[slot].store(true);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (...) {
        connected_.store(false);
        auto currentConfig = getSerialConfig();
        if (currentConfig.device == config.device) {
            currentConfig.enabled = false;
            setSerialConfig(currentConfig);
        }
        pushSerialLogLine("Serial connection failed");
    }
}

/*
 * append a line to the bounded serial log queue.
 * PARAMS:
 *   ∟ const juce::String& line   : log/status line to queue
 * RETURNS: none
 */
void HardwareControlAudioProcessor::pushSerialLogLine(const juce::String& line) {
    const std::lock_guard<std::mutex> lock(serialLogMutex_);
    serialLogLines_.push_back(line);

    while (serialLogLines_.size() > 200) {
        serialLogLines_.pop_front();
    }
}

/*
 * drain outgoing serial commands queued by the GUI thread.
 * PARAMS: none
 * RETURNS:
 *   ∟ std::vector<std::string>   : pending command lines
 */
std::vector<std::string> HardwareControlAudioProcessor::drainOutgoingSerialLines() {
    std::vector<std::string> lines;
    const std::lock_guard<std::mutex> lock(outgoingSerialMutex_);

    while (!outgoingSerialLines_.empty()) {
        lines.push_back(std::move(outgoingSerialLines_.front()));
        outgoingSerialLines_.pop_front();
    }

    return lines;
}

/*
 * update one GUI-visible session mapping after a local assignment command.
 * PARAMS:
 *   ∟ int pin      : hardware pin to update
 *   ∟ int action   : assignment action id
 * RETURNS: none
 */
void HardwareControlAudioProcessor::updateSessionMapping(int pin, int action) {
    const std::lock_guard<std::mutex> lock(sessionMappingsMutex_);
    const auto existing = std::find_if(
        sessionMappings_.begin(),
        sessionMappings_.end(),
        [pin](const SessionMapping& mapping) { return mapping.pin == pin; });

    if (action == removeAssignmentAction) {
        if (existing != sessionMappings_.end()) {
            sessionMappings_.erase(existing);
            sessionMappingsRevision_.fetch_add(1);
        }
        return;
    }

    if (existing != sessionMappings_.end()) {
        if (existing->action == action) {
            return;
        }

        existing->action = action;
        sessionMappingsRevision_.fetch_add(1);
        return;
    }

    sessionMappings_.push_back({ pin, action });
    sessionMappingsRevision_.fetch_add(1);
}

/*
 * replace GUI-visible session mappings from the first complete serial frame.
 * PARAMS:
 *   ∟ const hardware::Frame& frame   : parsed serial data frame
 * RETURNS: none
 */
void HardwareControlAudioProcessor::replaceSessionMappingsFromFrame(const hardware::Frame& frame) {
    const std::lock_guard<std::mutex> lock(sessionMappingsMutex_);
    std::vector<SessionMapping> nextMappings;
    nextMappings.reserve(frame.readings.size());

    for (const auto& reading : frame.readings) {
        if (reading.pin < 0 || reading.slot < 0 || reading.slot >= hardware::maxInputSlots) {
            continue;
        }

        const auto currentMapping = std::find_if(
            sessionMappings_.begin(),
            sessionMappings_.end(),
            [&reading](const SessionMapping& mapping) { return mapping.pin == reading.pin; });
        const int existingAction = currentMapping != sessionMappings_.end()
            ? currentMapping->action
            : removeAssignmentAction;
        const int action = assignmentActionForKind(reading.kind, existingAction);
        const auto existing = std::find_if(
            nextMappings.begin(),
            nextMappings.end(),
            [&reading](const SessionMapping& mapping) { return mapping.pin == reading.pin; });

        if (existing != nextMappings.end()) {
            existing->action = action;
            continue;
        }

        nextMappings.push_back({ reading.pin, action });
    }

    if (!sessionMappingsEqual(sessionMappings_, nextMappings)) {
        sessionMappings_ = std::move(nextMappings);
        sessionMappingsRevision_.fetch_add(1);
    }
}

/*
 * clear GUI-visible session mappings and bump the revision if needed.
 * PARAMS: none
 * RETURNS: none
 */
void HardwareControlAudioProcessor::clearSessionMappings() {
    const std::lock_guard<std::mutex> lock(sessionMappingsMutex_);
    if (sessionMappings_.empty()) {
        return;
    }

    sessionMappings_.clear();
    sessionMappingsRevision_.fetch_add(1);
}

/*
 * juce vst3 factory entry point.
 * PARAMS: none
 * RETURNS:
 *   ∟ juce::AudioProcessor*   : newly allocated processor instance
 */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HardwareControlAudioProcessor();
}
