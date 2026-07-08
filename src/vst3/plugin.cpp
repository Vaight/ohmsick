#include "plugin.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

constexpr float potSmoothingAlpha = 0.25f;
constexpr int midiChannel = 1;
constexpr int firstMidiCc = 1;

juce::File configFilePath() {
    if (const char* appData = std::getenv("APPDATA")) {
        if (*appData != '\0') {
            return juce::File(juce::String(appData)).getChildFile("vst3arduinothing/config.json");
        }
    }

    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdgConfig != '\0') {
            return juce::File(juce::String(xdgConfig)).getChildFile("vst3arduinothing/config.json");
        }
    }

    if (const char* home = std::getenv("HOME")) {
        if (*home != '\0') {
            return juce::File(juce::String(home)).getChildFile(".config/vst3arduinothing/config.json");
        }
    }

    return {};
}

float clamp01(float value) {
    return juce::jlimit(0.0f, 1.0f, value);
}

int normalizedToMidiValue(float value) {
    return juce::jlimit(0, 127, juce::roundToInt(clamp01(value) * 127.0f));
}

juce::String kindName(hardware::Kind kind) {
    return kind == hardware::Kind::Button ? "Button" : "Pot";
}

}  // namespace

class HardwareControlAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer {
public:
    explicit HardwareControlAudioProcessorEditor(HardwareControlAudioProcessor& processor)
        : AudioProcessorEditor(processor), processor_(processor) {
        setSize(620, 520);

        deviceLabel_.setText("Device", juce::dontSendNotification);
        deviceLabel_.attachToComponent(&deviceEditor_, true);
        addAndMakeVisible(deviceLabel_);

        deviceEditor_.setTextToShowWhenEmpty("COM3 or /dev/ttyACM0", juce::Colours::grey);
        addAndMakeVisible(deviceEditor_);

        baudLabel_.setText("Baud", juce::dontSendNotification);
        baudLabel_.attachToComponent(&baudBox_, true);
        addAndMakeVisible(baudLabel_);

        for (int baud : { 9600, 19200, 38400, 57600, 115200 }) {
            baudBox_.addItem(juce::String(baud), baud);
        }
        addAndMakeVisible(baudBox_);

        connectButton_.onClick = [this] { toggleConnection(); };
        addAndMakeVisible(connectButton_);

        statusLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(statusLabel_);

        inputView_.setMultiLine(true);
        inputView_.setReadOnly(true);
        inputView_.setScrollbarsShown(true);
        inputView_.setCaretVisible(false);
        inputView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        addAndMakeVisible(inputView_);

        logView_.setMultiLine(true);
        logView_.setReadOnly(true);
        logView_.setScrollbarsShown(true);
        logView_.setCaretVisible(false);
        logView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
        addAndMakeVisible(logView_);

        const auto config = processor_.getSerialConfig();
        deviceEditor_.setText(config.device, juce::dontSendNotification);
        baudBox_.setText(juce::String(config.baud), juce::dontSendNotification);
        updateConnectionState();
        startTimerHz(20);
    }

    ~HardwareControlAudioProcessorEditor() override {
        stopTimer();
    }

    void paint(juce::Graphics& graphics) override {
        graphics.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);

        auto top = bounds.removeFromTop(28);
        top.removeFromLeft(56);
        deviceEditor_.setBounds(top.removeFromLeft(170));

        top.removeFromLeft(48);
        baudBox_.setBounds(top.removeFromLeft(90));

        bounds.removeFromTop(8);
        auto statusRow = bounds.removeFromTop(28);
        connectButton_.setBounds(statusRow.removeFromLeft(100));
        statusRow.removeFromLeft(10);
        statusLabel_.setBounds(statusRow);

        bounds.removeFromTop(8);
        inputView_.setBounds(bounds.removeFromTop(330));

        bounds.removeFromTop(8);
        logView_.setBounds(bounds);
    }

private:
    void toggleConnection() {
        if (processor_.isSerialConnected()) {
            processor_.disconnectSerial();
        } else {
            const auto device = deviceEditor_.getText().trim();
            if (device.isNotEmpty()) {
                processor_.connectSerial(device, baudBox_.getText().getIntValue());
            }
        }

        updateConnectionState();
    }

    void updateConnectionState() {
        const bool connected = processor_.isSerialConnected();
        connectButton_.setButtonText(connected ? "Disconnect" : "Connect");

        const auto config = processor_.getSerialConfig();
        const int activeCount = countActiveInputs();
        statusLabel_.setText(
            (connected ? "Connected" : "Not connected")
                + juce::String(" | Mode: MIDI CC instrument | Ch ")
                + juce::String(midiChannel)
                + " | CC " + juce::String(firstMidiCc) + "-"
                + juce::String(firstMidiCc + hardware::maxInputSlots - 1)
                + " | Active " + juce::String(activeCount) + "/"
                + juce::String(hardware::maxInputSlots)
                + (config.device.isNotEmpty() ? " | " + config.device : ""),
            juce::dontSendNotification);
    }

    void timerCallback() override {
        bool logChanged = false;
        for (const auto& line : processor_.drainSerialLogLines()) {
            logLines_.add(line);
            logChanged = true;
        }

        while (logLines_.size() > maxVisibleLines) {
            logLines_.remove(0);
            logChanged = true;
        }

        if (logChanged || logLines_.size() != lastRenderedLineCount_) {
            logView_.setText(logLines_.joinIntoString("\n"), false);
            logView_.moveCaretToEnd();
            lastRenderedLineCount_ = logLines_.size();
        }

        renderInputTable();
        updateConnectionState();
    }

    int countActiveInputs() const {
        int count = 0;
        for (const auto& input : processor_.getInputSnapshots()) {
            if (input.active) {
                ++count;
            }
        }

        return count;
    }

    void renderInputTable() {
        const auto inputs = processor_.getInputSnapshots();
        std::ostringstream out;
        out << "Input Active Type   Ch  CC  MIDI  Value\n";
        out << "----- ------ ------ --- --- ----- -------\n";

        for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
            const auto& input = inputs[static_cast<size_t>(slot)];
            out << std::setw(5) << (slot + 1) << ' '
                << std::setw(6) << (input.active ? "yes" : "no") << ' '
                << std::setw(6) << kindName(input.kind).toStdString() << ' '
                << std::setw(3) << input.midiChannel << ' '
                << std::setw(3) << input.midiCc << ' '
                << std::setw(5) << input.midiValue << ' '
                << std::fixed << std::setprecision(3)
                << std::setw(7) << input.normalizedValue << '\n';
        }

        const juce::String nextText(out.str());
        if (nextText != lastInputTableText_) {
            inputView_.setText(nextText, false);
            lastInputTableText_ = nextText;
        }
    }

    static constexpr int maxVisibleLines = 120;

    HardwareControlAudioProcessor& processor_;
    juce::Label deviceLabel_;
    juce::TextEditor deviceEditor_;
    juce::Label baudLabel_;
    juce::ComboBox baudBox_;
    juce::TextButton connectButton_;
    juce::Label statusLabel_;
    juce::TextEditor inputView_;
    juce::TextEditor logView_;
    juce::StringArray logLines_;
    juce::String lastInputTableText_;
    int lastRenderedLineCount_ = -1;
};

HardwareControlAudioProcessor::HardwareControlAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      serialConfig_(loadSerialConfig()) {
    for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
        targetValues_[slot].store(0.0f);
        targetKinds_[slot].store(static_cast<int>(hardware::Kind::Pot));
        targetHasValue_[slot].store(false);
        lastSentCcValues_[slot].store(-1);
        hasSentCcValues_[slot].store(false);
    }

    startSerialThread();
}

HardwareControlAudioProcessor::~HardwareControlAudioProcessor() {
    stopSerialThread();
}

const juce::String HardwareControlAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool HardwareControlAudioProcessor::acceptsMidi() const {
    return true;
}

bool HardwareControlAudioProcessor::producesMidi() const {
    return true;
}

bool HardwareControlAudioProcessor::isMidiEffect() const {
    return false;
}

double HardwareControlAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int HardwareControlAudioProcessor::getNumPrograms() {
    return 1;
}

int HardwareControlAudioProcessor::getCurrentProgram() {
    return 0;
}

void HardwareControlAudioProcessor::setCurrentProgram(int) {
}

const juce::String HardwareControlAudioProcessor::getProgramName(int) {
    return {};
}

void HardwareControlAudioProcessor::changeProgramName(int, const juce::String&) {
}

void HardwareControlAudioProcessor::prepareToPlay(double, int) {
}

void HardwareControlAudioProcessor::releaseResources() {
}

bool HardwareControlAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    return layouts.getMainInputChannelSet().isDisabled()
        && (mainOutput == juce::AudioChannelSet::mono()
            || mainOutput == juce::AudioChannelSet::stereo());
}

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

bool HardwareControlAudioProcessor::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* HardwareControlAudioProcessor::createEditor() {
    return new HardwareControlAudioProcessorEditor(*this);
}

void HardwareControlAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto config = getSerialConfig();
    juce::MemoryOutputStream stream(destData, true);
    stream.writeString(config.device);
    stream.writeInt(config.baud);
    stream.writeBool(config.enabled);
}

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

HardwareControlAudioProcessor::SerialConfig HardwareControlAudioProcessor::getSerialConfig() const {
    const std::lock_guard<std::mutex> lock(serialConfigMutex_);
    return serialConfig_;
}

void HardwareControlAudioProcessor::setSerialConfig(SerialConfig config) {
    const std::lock_guard<std::mutex> lock(serialConfigMutex_);
    serialConfig_ = std::move(config);
}

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

        pushSerialLogLine("Connecting to " + config.device + " at " + juce::String(config.baud));
        startSerialThread();
    }
}

void HardwareControlAudioProcessor::disconnectSerial() {
    stopSerialThread();

    auto config = getSerialConfig();
    if (config.enabled || config.device.isNotEmpty()) {
        config.enabled = false;
        setSerialConfig(config);
        pushSerialLogLine("Disconnected");
    }
}

bool HardwareControlAudioProcessor::isSerialConnected() const {
    return connected_.load();
}

juce::StringArray HardwareControlAudioProcessor::drainSerialLogLines() {
    juce::StringArray lines;
    const std::lock_guard<std::mutex> lock(serialLogMutex_);

    for (const auto& line : serialLogLines_) {
        lines.add(line);
    }

    serialLogLines_.clear();
    return lines;
}

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

void HardwareControlAudioProcessor::startSerialThread() {
    const auto config = getSerialConfig();
    if (!config.enabled || config.device.isEmpty()) {
        return;
    }

    stopSerialThread_.store(false);
    serialThread_ = std::thread([this, config] { serialThreadMain(config); });
}

void HardwareControlAudioProcessor::stopSerialThread() {
    stopSerialThread_.store(true);

    if (serialThread_.joinable()) {
        serialThread_.join();
    }

    if (connected_.exchange(false)) {
        pushSerialLogLine("Serial port closed");
    }
}

void HardwareControlAudioProcessor::serialThreadMain(SerialConfig config) {
    try {
        hardware::SerialReader reader(config.device.toStdString(), config.baud);
        connected_.store(true);
        pushSerialLogLine("Connected to " + config.device);

        std::string serialBuffer;
        std::array<float, hardware::maxInputSlots> smoothedValues {};

        while (!stopSerialThread_.load()) {
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

void HardwareControlAudioProcessor::pushSerialLogLine(const juce::String& line) {
    const std::lock_guard<std::mutex> lock(serialLogMutex_);
    serialLogLines_.push_back(line);

    while (serialLogLines_.size() > 200) {
        serialLogLines_.pop_front();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HardwareControlAudioProcessor();
}
