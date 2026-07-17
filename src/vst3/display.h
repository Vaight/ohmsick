#pragma once

#include <jive_layouts/jive_layouts.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin.h"

/*
 * juce plugin editor backed by a jive XML layout.
 * owns GUI component pointers, renders serial status, and forwards user
 * assignment actions to the audio processor.
 */
class HardwareControlAudioProcessorEditor final : 
    public juce::AudioProcessorEditor,
    private juce::Timer
{

public:

    /*
     * construct the plugin editor and load the jive layout.
     * PARAMS:
     *   ∟ HardwareControlAudioProcessor& processor   : owning audio processor
     * RETURNS: none
     */
    explicit HardwareControlAudioProcessorEditor(HardwareControlAudioProcessor& processor);

    /*
     * destroy the plugin editor and stop GUI timer updates.
     * PARAMS: none
     * RETURNS: none
     */
    ~HardwareControlAudioProcessorEditor() override;

    /*
     * juce component resize callback.
     * PARAMS: none
     * RETURNS: none
     */
    void resized() override;

private:

    /*
     * connect or disconnect serial based on the current processor state.
     * PARAMS: none
     * RETURNS: none
     */
    void toggleConnection();

    /*
     * send the selected pin/action assignment to the processor.
     * PARAMS: none
     * RETURNS: none
     */
    void sendMapping();

    /*
     * refresh connection controls and status text.
     * PARAMS: none
     * RETURNS: none
     */
    void updateConnectionState();

    /*
     * juce timer callback for periodic GUI refresh.
     * PARAMS: none
     * RETURNS: none
     */
    void timerCallback() override;

    /*
     * count currently active hardware input slots.
     * PARAMS: none
     * RETURNS:
     *   ∟ int   : active hardware input count
     */
    int countActiveInputs() const;

    /*
     * render session mappings only when the processor revision has changed.
     * PARAMS: none
     * RETURNS: none
     */
    void renderSessionMappingsIfChanged();

    /*
     * render the current session mapping list into the read-only mapping view.
     * PARAMS: none
     * RETURNS: none
     */
    void renderSessionMappings();

    /*
     * processor reference and jive layout ownership.
     */
    HardwareControlAudioProcessor& processor_;
    jive::Interpreter interpreter_;
    std::unique_ptr<jive::GuiItem> layout_;

    /*
     * borrowed pointers to jive-created GUI components.
     */
    juce::TextEditor* deviceEditor_ = nullptr;
    juce::ComboBox* baudBox_ = nullptr;
    juce::TextButton* connectButton_ = nullptr;
    juce::Label* statusLabel_ = nullptr;
    juce::TextEditor* pinEditor_ = nullptr;
    juce::ComboBox* typeBox_ = nullptr;
    juce::TextButton* sendButton_ = nullptr;
    juce::TextEditor* mappingsView_ = nullptr;
    juce::TextEditor* logView_ = nullptr;

    /*
     * cached GUI text/revision state used to avoid redundant updates.
     */
    juce::String lastLogLine_;
    juce::String lastMappingsText_;
    int lastMappingsRevision_ = -1;
    
};
