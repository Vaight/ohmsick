#include "plugin.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace {

constexpr float potSmoothingAlpha = 0.25f;
constexpr float potNotifyThreshold = 0.005f;

juce::String inputParameterId(int slot) {
    return "input" + juce::String(slot + 1).paddedLeft('0', 2);
}

juce::String inputParameterName(int slot) {
    return "Input " + juce::String(slot + 1).paddedLeft('0', 2);
}

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

}  // namespace

class HardwareControlAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer {
public:
    explicit HardwareControlAudioProcessorEditor(HardwareControlAudioProcessor& processor)
        : AudioProcessorEditor(processor), processor_(processor) {
        setSize(420, 260);

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

        dataView_.setMultiLine(true);
        dataView_.setReadOnly(true);
        dataView_.setScrollbarsShown(true);
        dataView_.setCaretVisible(false);
        addAndMakeVisible(dataView_);

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
        connectButton_.setBounds(bounds.removeFromTop(28).removeFromLeft(100));

        bounds.removeFromTop(8);
        dataView_.setBounds(bounds);
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
            dataView_.setText(logLines_.joinIntoString("\n"), false);
            dataView_.moveCaretToEnd();
            lastRenderedLineCount_ = logLines_.size();
        }

        updateConnectionState();
    }

    static constexpr int maxVisibleLines = 120;

    HardwareControlAudioProcessor& processor_;
    juce::Label deviceLabel_;
    juce::TextEditor deviceEditor_;
    juce::Label baudLabel_;
    juce::ComboBox baudBox_;
    juce::TextButton connectButton_;
    juce::TextEditor dataView_;
    juce::StringArray logLines_;
    int lastRenderedLineCount_ = -1;
};

HardwareControlAudioProcessor::HardwareControlAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      serialConfig_(loadSerialConfig()) {
    inputParameters_.reserve(hardware::maxInputSlots);

    for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
        targetValues_[slot].store(0.0f);
        targetKinds_[slot].store(static_cast<int>(hardware::Kind::Pot));
        lastNotifiedValues_[slot] = 0.0f;

        auto* parameter = new juce::AudioParameterFloat(
            juce::ParameterID(inputParameterId(slot), 1),
            inputParameterName(slot),
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.0f);

        addParameter(parameter);
        inputParameters_.push_back(parameter);
    }

    startTimerHz(30);
    startSerialThread();
}

HardwareControlAudioProcessor::~HardwareControlAudioProcessor() {
    stopTimer();
    stopSerialThread();
}

const juce::String HardwareControlAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool HardwareControlAudioProcessor::acceptsMidi() const {
    return false;
}

bool HardwareControlAudioProcessor::producesMidi() const {
    return false;
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
    const auto& mainInput = layouts.getMainInputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo()) {
        return false;
    }

    return mainInput == mainOutput;
}

void HardwareControlAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel) {
        buffer.clear(channel, 0, buffer.getNumSamples());
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
                }

                for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
                    targetValues_[slot].store(nextValues[slot]);
                    targetKinds_[slot].store(nextKinds[slot]);
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

void HardwareControlAudioProcessor::timerCallback() {
    for (int slot = 0; slot < hardware::maxInputSlots; ++slot) {
        const float value = clamp01(targetValues_[slot].load());
        const auto kind = static_cast<hardware::Kind>(targetKinds_[slot].load());
        const float previous = lastNotifiedValues_[slot];

        const bool shouldNotify = kind == hardware::Kind::Button
            ? ((value >= 0.5f) != (previous >= 0.5f))
            : std::abs(value - previous) >= potNotifyThreshold;

        if (shouldNotify) {
            inputParameters_[slot]->setValueNotifyingHost(value);
            lastNotifiedValues_[slot] = value;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HardwareControlAudioProcessor();
}
