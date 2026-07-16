#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "hardware.h"

/*
 * JUCE audio processor that converts hardware readings received over serial
 * into MIDI continuous-controller events.
 *
 * Serial I/O runs on a worker thread. State shared with the audio and GUI
 * threads is synchronized with atomics or mutexes as appropriate.
 */
class HardwareControlAudioProcessor final : public juce::AudioProcessor {
public:
    // MIDI channel and first controller number assigned to hardware input slots.
    static constexpr int midiChannel = 1;
    static constexpr int firstMidiCc = 1;

    // Construct the processor, load serial configuration, and start serial I/O
    // when the loaded configuration is enabled.
    HardwareControlAudioProcessor();

    // Stop and join the serial worker thread before processor destruction.
    ~HardwareControlAudioProcessor() override;

    // juce::AudioProcessor identity and MIDI capability declarations.
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // JUCE program interface. This processor exposes one fixed, unnamed program.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // JUCE playback lifecycle and audio/MIDI processing callbacks.
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // JUCE editor availability and construction.
    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    // Serialize and restore the current serial configuration in plugin state.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // User-configurable serial connection settings.
    struct SerialConfig {
        bool enabled = false;
        juce::String device;
        int baud = 19200;
    };

    // Thread-safe serial configuration and connection interface.
    SerialConfig getSerialConfig() const;
    void setSerialConfig(SerialConfig config);
    void connectSerial(juce::String device, int baud);
    void disconnectSerial();
    bool isSerialConnected() const;

    // Return all queued serial status lines and clear the shared log queue.
    juce::StringArray drainSerialLogLines();

    // Validate and queue a hardware assignment command for serial transmission.
    void sendAssignmentCommand(int pin, int action);

    // Immutable GUI-facing view of one hardware input slot.
    struct InputSnapshot {
        bool active = false;
        hardware::Kind kind = hardware::Kind::Pot;
        float normalizedValue = 0.0f;
        int midiChannel = 1;
        int midiCc = 1;
        int midiValue = 0;
    };

    // Build a point-in-time view of every input slot from atomic processor state.
    std::array<InputSnapshot, hardware::maxInputSlots> getInputSnapshots() const;

    // Assignment accepted during the current processor session.
    struct SessionMapping {
        int pin = 0;
        int action = 0;
    };

    // Return a thread-safe copy of the current session mappings.
    std::vector<SessionMapping> getSessionMappings() const;

private:
    // Load the process-wide serial defaults from the platform config file.
    static SerialConfig loadSerialConfig();

    // Serial worker lifecycle and worker-thread entry point.
    void startSerialThread();
    void stopSerialThread();
    void serialThreadMain(SerialConfig config);

    // Thread-safe queues shared by the GUI and serial worker.
    void pushSerialLogLine(const juce::String& line);
    std::vector<std::string> drainOutgoingSerialLines();

    // Add, replace, or remove a mapping in the current in-memory session.
    void updateSessionMapping(int pin, int action);

    // Mutex-protected connection configuration.
    SerialConfig serialConfig_;
    mutable std::mutex serialConfigMutex_;

    // Serial worker lifecycle state.
    std::atomic<bool> stopSerialThread_ { false };
    std::atomic<bool> connected_ { false };
    std::thread serialThread_;

    // Bounded serial status queue consumed by the GUI.
    std::mutex serialLogMutex_;
    std::deque<juce::String> serialLogLines_;

    // Assignment-command queue consumed by the serial worker.
    std::mutex outgoingSerialMutex_;
    std::deque<std::string> outgoingSerialLines_;

    // GUI-visible mappings accepted during this processor session.
    mutable std::mutex sessionMappingsMutex_;
    std::vector<SessionMapping> sessionMappings_;

    // Per-slot state written by the serial thread and read by the audio thread.
    std::array<std::atomic<float>, hardware::maxInputSlots> targetValues_ {};
    std::array<std::atomic<int>, hardware::maxInputSlots> targetKinds_ {};
    std::array<std::atomic<bool>, hardware::maxInputSlots> targetHasValue_ {};

    // Per-slot MIDI history used to suppress duplicate controller events.
    std::array<std::atomic<int>, hardware::maxInputSlots> lastSentCcValues_ {};
    std::array<std::atomic<bool>, hardware::maxInputSlots> hasSentCcValues_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareControlAudioProcessor)
};
