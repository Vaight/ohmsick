#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin.h"

class HardwareControlAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer {
public:
    explicit HardwareControlAudioProcessorEditor(HardwareControlAudioProcessor& processor);
    ~HardwareControlAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void toggleConnection();
    void sendMapping();
    void updateConnectionState();
    void timerCallback() override;
    int countActiveInputs() const;
    void renderSessionMappings();

    HardwareControlAudioProcessor& processor_;
    juce::Label deviceLabel_;
    juce::TextEditor deviceEditor_;
    juce::Label baudLabel_;
    juce::ComboBox baudBox_;
    juce::TextButton connectButton_;
    juce::Label statusLabel_;
    juce::Label pinLabel_;
    juce::TextEditor pinEditor_;
    juce::Label typeLabel_;
    juce::ComboBox typeBox_;
    juce::TextButton sendButton_;
    juce::TextEditor mappingsView_;
    juce::TextEditor logView_;
    juce::String lastLogLine_;
    juce::String lastMappingsText_;
};
