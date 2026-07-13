#include "display.h"

#include <sstream>

namespace {

juce::String assignmentActionName(int action) {
    switch (action) {
        case 0: return "Remove";
        case 1: return "Digital";
        case 2: return "Pullup";
        case 3: return "Analog";
        default: return "Unknown";
    }
}

}  // namespace

HardwareControlAudioProcessorEditor::HardwareControlAudioProcessorEditor(HardwareControlAudioProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor) {
    setSize(620, 520);

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

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    pinLabel_.setText("Pin", juce::dontSendNotification);
    pinLabel_.attachToComponent(&pinEditor_, true);
    addAndMakeVisible(pinLabel_);

    pinEditor_.setInputRestrictions(4, "0123456789");
    pinEditor_.setTextToShowWhenEmpty("0", juce::Colours::grey);
    addAndMakeVisible(pinEditor_);

    typeLabel_.setText("Type", juce::dontSendNotification);
    typeLabel_.attachToComponent(&typeBox_, true);
    addAndMakeVisible(typeLabel_);

    typeBox_.addItem("Remove", 1);
    typeBox_.addItem("Digital", 2);
    typeBox_.addItem("Pullup", 3);
    typeBox_.addItem("Analog", 4);
    typeBox_.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(typeBox_);

    sendButton_.setButtonText("Send");
    sendButton_.onClick = [this] { sendMapping(); };
    addAndMakeVisible(sendButton_);

    mappingsView_.setMultiLine(true);
    mappingsView_.setReadOnly(true);
    mappingsView_.setScrollbarsShown(true);
    mappingsView_.setCaretVisible(false);
    mappingsView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    addAndMakeVisible(mappingsView_);

    logView_.setMultiLine(false);
    logView_.setReadOnly(true);
    logView_.setScrollbarsShown(false);
    logView_.setCaretVisible(false);
    logView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    addAndMakeVisible(logView_);

    const auto config = processor_.getSerialConfig();
    deviceEditor_.setText(config.device, juce::dontSendNotification);
    baudBox_.setText(juce::String(config.baud), juce::dontSendNotification);
    updateConnectionState();
    startTimerHz(20);
}

HardwareControlAudioProcessorEditor::~HardwareControlAudioProcessorEditor() {
    stopTimer();
}

void HardwareControlAudioProcessorEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void HardwareControlAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds().reduced(16);

    auto top = bounds.removeFromTop(28);
    top.removeFromLeft(56);
    deviceEditor_.setBounds(top.removeFromLeft(170));

    top.removeFromLeft(48);
    baudBox_.setBounds(top.removeFromLeft(90));

    bounds.removeFromTop(8);
    auto statusRow = bounds.removeFromTop(28);
    connectButton_.setBounds(statusRow.removeFromLeft(100));
    statusRow.removeFromLeft(10);
    statusLabel_.setBounds(statusRow);

    bounds.removeFromTop(8);
    auto mappingRow = bounds.removeFromTop(28);
    mappingRow.removeFromLeft(32);
    pinEditor_.setBounds(mappingRow.removeFromLeft(70));
    mappingRow.removeFromLeft(48);
    typeBox_.setBounds(mappingRow.removeFromLeft(120));
    mappingRow.removeFromLeft(8);
    sendButton_.setBounds(mappingRow.removeFromLeft(80));

    bounds.removeFromTop(8);
    mappingsView_.setBounds(bounds.removeFromTop(110));

    bounds.removeFromTop(8);
    logView_.setBounds(bounds);
}

void HardwareControlAudioProcessorEditor::toggleConnection() {
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

void HardwareControlAudioProcessorEditor::sendMapping() {
    const int pin = pinEditor_.getText().getIntValue();
    const int action = typeBox_.getSelectedId() - 1;
    if (action < 0 || action > 3) {
        return;
    }

    processor_.sendAssignmentCommand(pin, action);
    renderSessionMappings();
}

void HardwareControlAudioProcessorEditor::updateConnectionState() {
    const bool connected = processor_.isSerialConnected();
    connectButton_.setButtonText(connected ? "Disconnect" : "Connect");

    const auto config = processor_.getSerialConfig();
    const int activeCount = countActiveInputs();
    sendButton_.setEnabled(connected);
    statusLabel_.setText(
        (connected ? "Connected" : "Not connected")
            + juce::String(" | Mode: MIDI CC instrument | Ch ")
            + juce::String(HardwareControlAudioProcessor::midiChannel)
            + " | CC " + juce::String(HardwareControlAudioProcessor::firstMidiCc) + "-"
            + juce::String(HardwareControlAudioProcessor::firstMidiCc + hardware::maxInputSlots - 1)
            + " | Active " + juce::String(activeCount) + "/"
            + juce::String(hardware::maxInputSlots)
            + (config.device.isNotEmpty() ? " | " + config.device : ""),
        juce::dontSendNotification);
}

void HardwareControlAudioProcessorEditor::timerCallback() {
    juce::String latestLogLine;
    for (const auto& line : processor_.drainSerialLogLines()) {
        latestLogLine = line;
    }

    if (latestLogLine.isNotEmpty() && latestLogLine != lastLogLine_) {
        logView_.setText(latestLogLine, false);
        logView_.moveCaretToEnd();
        lastLogLine_ = latestLogLine;
    }

    renderSessionMappings();
    updateConnectionState();
}

int HardwareControlAudioProcessorEditor::countActiveInputs() const {
    int count = 0;
    for (const auto& input : processor_.getInputSnapshots()) {
        if (input.active) {
            ++count;
        }
    }

    return count;
}

void HardwareControlAudioProcessorEditor::renderSessionMappings() {
    const auto mappings = processor_.getSessionMappings();
    std::ostringstream out;
    out << "Current session mappings\n";
    if (mappings.empty()) {
        out << "(none)\n";
    } else {
        for (const auto& mapping : mappings) {
            out << "Pin " << mapping.pin << " ~> "
                << assignmentActionName(mapping.action).toStdString() << '\n';
        }
    }

    const juce::String nextText(out.str());
    if (nextText != lastMappingsText_) {
        mappingsView_.setText(nextText, false);
        lastMappingsText_ = nextText;
    }
}
