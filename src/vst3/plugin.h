#pragma once

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "hardware.h"

class HardwareControlAudioProcessor final : public juce::AudioProcessor,
                                            private juce::Timer {
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

private:
    struct SerialConfig {
        bool enabled = false;
        juce::String device;
        int baud = 19200;
    };

    static SerialConfig loadSerialConfig();

    void startSerialThread();
    void stopSerialThread();
    void serialThreadMain();
    void timerCallback() override;

    SerialConfig serialConfig_;
    std::atomic<bool> stopSerialThread_ { false };
    std::atomic<bool> connected_ { false };
    std::thread serialThread_;

    std::array<std::atomic<float>, hardware::maxInputSlots> targetValues_ {};
    std::array<std::atomic<int>, hardware::maxInputSlots> targetKinds_ {};
    std::array<float, hardware::maxInputSlots> lastNotifiedValues_ {};
    std::vector<juce::AudioParameterFloat*> inputParameters_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareControlAudioProcessor)
};
