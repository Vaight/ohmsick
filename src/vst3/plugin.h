#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "hardware.h"

class HardwareControlAudioProcessor final : public juce::AudioProcessor {
public:
    HardwareControlAudioProcessor();
    ~HardwareControlAudioProcessor() override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    struct SerialConfig {
        bool enabled = false;
        juce::String device;
        int baud = 19200;
    };

    SerialConfig getSerialConfig() const;
    void setSerialConfig(SerialConfig config);
    void connectSerial(juce::String device, int baud);
    void disconnectSerial();
    bool isSerialConnected() const;
    juce::StringArray drainSerialLogLines();

    struct InputSnapshot {
        bool active = false;
        hardware::Kind kind = hardware::Kind::Pot;
        float normalizedValue = 0.0f;
        int midiChannel = 1;
        int midiCc = 1;
        int midiValue = 0;
    };

    std::array<InputSnapshot, hardware::maxInputSlots> getInputSnapshots() const;

private:
    static SerialConfig loadSerialConfig();

    void startSerialThread();
    void stopSerialThread();
    void serialThreadMain(SerialConfig config);
    void pushSerialLogLine(const juce::String& line);

    SerialConfig serialConfig_;
    mutable std::mutex serialConfigMutex_;
    std::atomic<bool> stopSerialThread_ { false };
    std::atomic<bool> connected_ { false };
    std::thread serialThread_;
    std::mutex serialLogMutex_;
    std::deque<juce::String> serialLogLines_;

    std::array<std::atomic<float>, hardware::maxInputSlots> targetValues_ {};
    std::array<std::atomic<int>, hardware::maxInputSlots> targetKinds_ {};
    std::array<std::atomic<bool>, hardware::maxInputSlots> targetHasValue_ {};
    std::array<std::atomic<int>, hardware::maxInputSlots> lastSentCcValues_ {};
    std::array<std::atomic<bool>, hardware::maxInputSlots> hasSentCcValues_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareControlAudioProcessor)
};
