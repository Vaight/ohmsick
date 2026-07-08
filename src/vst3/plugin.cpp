#include "plugin.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>

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
    return false;
}

juce::AudioProcessorEditor* HardwareControlAudioProcessor::createEditor() {
    return nullptr;
}

void HardwareControlAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::MemoryOutputStream stream(destData, true);
    stream.writeString(serialConfig_.device);
    stream.writeInt(serialConfig_.baud);
    stream.writeBool(serialConfig_.enabled);
}

void HardwareControlAudioProcessor::setStateInformation(const void*, int) {
    // Device connection is intentionally controlled by the user config file in v1.
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

void HardwareControlAudioProcessor::startSerialThread() {
    if (!serialConfig_.enabled || serialConfig_.device.isEmpty()) {
        return;
    }

    stopSerialThread_.store(false);
    serialThread_ = std::thread([this] { serialThreadMain(); });
}

void HardwareControlAudioProcessor::stopSerialThread() {
    stopSerialThread_.store(true);

    if (serialThread_.joinable()) {
        serialThread_.join();
    }

    connected_.store(false);
}

void HardwareControlAudioProcessor::serialThreadMain() {
    try {
        hardware::SerialReader reader(serialConfig_.device.toStdString(), serialConfig_.baud);
        connected_.store(true);

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
