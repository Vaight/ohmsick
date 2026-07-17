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
    class MappingStripComponent;

    /*
     * connect or disconnect serial based on the current processor state.
     * PARAMS: none
     * RETURNS: none
     */
    void toggleConnection();

    /*
     * show a simple dialog for adding a new mapping.
     * PARAMS: none
     * RETURNS: none
     */
    void showNewMappingDialog();

    /*
     * send a pin/action assignment to the processor.
     * PARAMS:
     *   ∟ int pin      : hardware pin to assign
     *   ∟ int action   : firmware assignment action id
     * RETURNS: none
     */
    void sendMapping(int pin, int action);

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
     * update mapping card values from the latest input snapshots.
     * PARAMS: none
     * RETURNS: none
     */
    void updateMappingValues();

    /*
     * render session mappings only when the processor revision has changed.
     * PARAMS: none
     * RETURNS: none
     */
    void renderSessionMappingsIfChanged();

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
    juce::TextButton* newMapButton_ = nullptr;
    MappingStripComponent* mappingStrip_ = nullptr;

    /*
     * cached GUI text/revision state used to avoid redundant updates.
     */
    int lastMappingsRevision_ = -1;
    
};
