#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "backend.h"

/*
 * JUCE audio processor that converts hardware readings received over serial
 * into MIDI continuous-controller events.
 *
 * Serial I/O runs on a worker thread. State shared with the audio and GUI
 * threads is synchronized with atomics or mutexes as appropriate.
 */
class HardwareControlAudioProcessor final : public juce::AudioProcessor {
public:
    /*
     * MIDI routing constants for generated controller events.
     */
    static constexpr int midiChannel = 1;
    static constexpr int firstMidiCc = 1;

    /*
     * construct the processor, load serial configuration, and start serial I/O
     * when the loaded configuration is enabled.
     * PARAMS: none
     * RETURNS: none
     */
    HardwareControlAudioProcessor();

    /*
     * stop and join the serial worker before processor destruction.
     * PARAMS: none
     * RETURNS: none
     */
    ~HardwareControlAudioProcessor() override;

    /*
     * juce::AudioProcessor identity and MIDI capability declarations.
     */
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    /*
     * juce program interface.
     * this processor exposes one fixed, unnamed program.
     */
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    /*
     * juce playback lifecycle and audio/MIDI processing callbacks.
     */
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /*
     * juce editor availability and construction.
     */
    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    /*
     * serialize and restore the current serial configuration in plugin state.
     */
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /*
     * user-configurable serial connection settings.
     */
    struct SerialConfig {
        bool enabled = false;
        juce::String device;
        int baud = 19200;
    };

    /*
     * get the current serial configuration.
     * PARAMS: none
     * RETURNS:
     *   ∟ SerialConfig   : thread-safe copy of the serial config
     */
    SerialConfig getSerialConfig() const;

    /*
     * replace the current serial configuration.
     * PARAMS:
     *   ∟ SerialConfig config   : new serial config to store
     * RETURNS: none
     */
    void setSerialConfig(SerialConfig config);

    /*
     * connect to a serial device using the provided settings.
     * PARAMS:
     *   ∟ juce::String device   : serial device path or port name
     *   ∟ int baud              : requested baud rate
     * RETURNS: none
     */
    void connectSerial(juce::String device, int baud);

    /*
     * disconnect from the current serial device.
     * PARAMS: none
     * RETURNS: none
     */
    void disconnectSerial();

    /*
     * get the current serial connection status.
     * PARAMS: none
     * RETURNS:
     *   ∟ bool   : true when serial is connected
     */
    bool isSerialConnected() const;

    /*
     * return all queued serial status lines and clear the shared log queue.
     * PARAMS: none
     * RETURNS:
     *   ∟ juce::StringArray   : drained serial log lines
     */
    juce::StringArray drainSerialLogLines();

    /*
     * validate and queue a hardware assignment command for serial transmission.
     * PARAMS:
     *   ∟ int pin      : hardware pin to assign
     *   ∟ int action   : firmware assignment action id
     * RETURNS: none
     */
    void sendAssignmentCommand(int pin, int action);

    /*
     * immutable GUI-facing view of one hardware input slot.
     * pin is tracked separately from the slot so GUI mapping cards can display
     * the latest value for the physical pin they represent.
     */
    struct InputSnapshot {
        bool active = false;
        int pin = -1;
        backend::Kind kind = backend::Kind::Pot;
        float normalizedValue = 0.0f;
        int midiChannel = 1;
        int midiCc = 1;
        int midiValue = 0;
    };

    /*
     * build a point-in-time view of every input slot from atomic processor state.
     * PARAMS: none
     * RETURNS:
     *   ∟ std::array<InputSnapshot, maxInputSlots>   : slot snapshots
     */
    std::array<InputSnapshot, backend::maxInputSlots> getInputSnapshots() const;

    /*
     * assignment accepted during the current processor session.
     */
    struct SessionMapping {
        int pin = 0;
        int action = 0;
    };

    /*
     * return a thread-safe copy of the current session mappings.
     * PARAMS: none
     * RETURNS:
     *   ∟ std::vector<SessionMapping>   : current session mappings
     */
    std::vector<SessionMapping> getSessionMappings() const;

    /*
     * get the session mapping revision counter.
     * PARAMS: none
     * RETURNS:
     *   ∟ int   : mapping revision id for GUI refresh checks
     */
    int getSessionMappingsRevision() const;

private:
    /*
     * load the process-wide serial defaults from the platform config file.
     * PARAMS: none
     * RETURNS:
     *   ∟ SerialConfig   : loaded or default serial config
     */
    static SerialConfig loadSerialConfig();

    /*
     * serial worker lifecycle and worker-thread entry point.
     */
    void startSerialThread();
    void stopSerialThread();
    void serialThreadMain(SerialConfig config);

    /*
     * thread-safe queues shared by the GUI and serial worker.
     */
    void pushSerialLogLine(const juce::String& line);
    std::vector<std::string> drainOutgoingSerialLines();

    /*
     * add, replace, remove, or clear mappings in the current in-memory session.
     */
    void updateSessionMapping(int pin, int action);
    void replaceSessionMappingsFromFrame(const backend::Frame& frame);
    void clearSessionMappings();

    /*
     * mutex-protected connection configuration.
     */
    SerialConfig serialConfig_;
    mutable std::mutex serialConfigMutex_;

    /*
     * serial worker lifecycle state.
     */
    std::atomic<bool> stopSerialThread_ { false };
    std::atomic<bool> connected_ { false };
    std::thread serialThread_;

    /*
     * bounded serial status queue consumed by the GUI.
     */
    std::mutex serialLogMutex_;
    std::deque<juce::String> serialLogLines_;

    /*
     * assignment-command queue consumed by the serial worker.
     */
    std::mutex outgoingSerialMutex_;
    std::deque<std::string> outgoingSerialLines_;

    /*
     * GUI-visible mappings accepted during this processor session.
     */
    mutable std::mutex sessionMappingsMutex_;
    std::vector<SessionMapping> sessionMappings_;
    std::atomic<int> sessionMappingsRevision_ { 0 };

    /*
     * backend-owned input state written by the serial thread and read by the
     * GUI/audio threads.
     */
    backend::DataProcessor dataProcessor_;

    /*
     * per-slot MIDI history used to suppress duplicate controller events.
     */
    std::array<std::atomic<int>, backend::maxInputSlots> lastSentCcValues_ {};
    std::array<std::atomic<bool>, backend::maxInputSlots> hasSentCcValues_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareControlAudioProcessor)
};
