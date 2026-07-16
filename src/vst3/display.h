#pragma once

#include <jive_layouts/jive_layouts.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin.h"

// class definition
class HardwareControlAudioProcessorEditor final : 
    public juce::AudioProcessorEditor,
    private juce::Timer
{

public:

    explicit HardwareControlAudioProcessorEditor(HardwareControlAudioProcessor& processor);
    ~HardwareControlAudioProcessorEditor() override;

    void resized() override;

private:

    void toggleConnection();
    void sendMapping();
    void updateConnectionState();
    void timerCallback() override;
    int countActiveInputs() const;
    void renderSessionMappings();

    HardwareControlAudioProcessor& processor_;
    jive::Interpreter interpreter_;
    std::unique_ptr<jive::GuiItem> layout_;
    juce::TextEditor* deviceEditor_ = nullptr;
    juce::ComboBox* baudBox_ = nullptr;
    juce::TextButton* connectButton_ = nullptr;
    juce::Label* statusLabel_ = nullptr;
    juce::TextEditor* pinEditor_ = nullptr;
    juce::ComboBox* typeBox_ = nullptr;
    juce::TextButton* sendButton_ = nullptr;
    juce::TextEditor* mappingsView_ = nullptr;
    juce::TextEditor* logView_ = nullptr;
    juce::String lastLogLine_;
    juce::String lastMappingsText_;
    
};
