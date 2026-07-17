#include "display.h"
#include <BinaryData.h>
#include <sstream>

namespace {

/*
 * gui helper to convert action integer to named string (dropdown)
 * PARAMS:
 *   ∟ int action     : integer idx for action string
 * RETURNS:
 *   ∟ juce::String   : named string for action idx
 */
juce::String assignmentActionName(int action) {
    switch (action) {
        case 0: return "Remove";
        case 1: return "Digital";
        case 2: return "Pullup";
        case 3: return "Analog";
        default: return "Unknown";
    }
}

/*
 * jive gui helper to find gui items within a tree of given id.
 * PARAMS:
 *   ∟ jive::GuiItem &root          : reference to the root item of the tree
 *   ∟ const juce::Identifier &id   : reference to the requested item id
 * RETURNS:
 *   ∟ Component*                   : template type for a variety of juce components
 *   ∟ nullptr                      : returns when component isnt found
 */
template <typename Component>
Component *findComponent(jive::GuiItem &root, const juce::Identifier &id) {
    if (auto *item = jive::findItemWithID(root, id)) {
        return dynamic_cast<Component *>(item -> getComponent().get());
    }
    return nullptr;
}

} // end namespace

/*
 * juce gui constructor method.
 * calls initializers: AudioProcessorEditor(processor) & processor_(processor) 
 * PARAMS:
 *   ∟ HardwareControlAudioProcessor &processor   : plugin audio processor reference
 * RETURNS: none
 */
HardwareControlAudioProcessorEditor::HardwareControlAudioProcessorEditor(
    HardwareControlAudioProcessor &processor) : AudioProcessorEditor(processor), processor_(processor) {

    // registers a juce::TextEditor factory to the jive xml element type <TextEditor>
    interpreter_.getComponentFactory().set(
        "TextEditor", [] { return std::make_unique<juce::TextEditor>(); }
    );

    // use jive to interpret the gui layout xml
    layout_ = interpreter_.interpret(
        BinaryData::layout_xml,
        BinaryData::layout_xmlSize
    );

    // layout is not null; successful interpretation (assertion builds)
    jassert(layout_ != nullptr);

    // return if layout failed to interpret
    if (layout_ == nullptr) return;

    // enable live interpreting of changes to the layout items
    interpreter_.listenTo(*layout_);

    // get component of layout (root) and make visible
    addAndMakeVisible(*layout_ -> getComponent());

    // set window size
    setSize(620, 520);

    // get necessary components from the tree using their identifiers
    deviceEditor_  = findComponent<juce::TextEditor>(*layout_, "device-editor");
    baudBox_       = findComponent<juce::ComboBox>(*layout_, "baud-box");
    connectButton_ = findComponent<juce::TextButton>(*layout_, "connect-button");
    statusLabel_   = findComponent<juce::Label>(*layout_, "status-label");
    pinEditor_     = findComponent<juce::TextEditor>(*layout_, "pin-editor");
    typeBox_       = findComponent<juce::ComboBox>(*layout_, "type-box");
    sendButton_    = findComponent<juce::TextButton>(*layout_, "send-button");
    mappingsView_  = findComponent<juce::TextEditor>(*layout_, "mappings-view");
    logView_       = findComponent<juce::TextEditor>(*layout_, "log-view");

    // all components are not null; successful component locating (assertion builds)
    jassert(
        deviceEditor_  != nullptr &&
        baudBox_       != nullptr &&
        connectButton_ != nullptr &&
        statusLabel_   != nullptr &&
        pinEditor_     != nullptr &&
        typeBox_       != nullptr &&
        sendButton_    != nullptr &&
        mappingsView_  != nullptr &&
        logView_       != nullptr
    );

    // define placeholder text for TextEditor boxes
    deviceEditor_  -> setTextToShowWhenEmpty("COM3 or /dev/ttyACM0", juce::Colours::grey);
    pinEditor_     -> setTextToShowWhenEmpty("0", juce::Colours::grey);
    // define TextEditor input to only integers
    pinEditor_     -> setInputRestrictions(4, "0123456789");
    // define button action calls
    connectButton_ -> onClick = [this] { toggleConnection(); };
    sendButton_    -> onClick = [this] { sendMapping(); };
    // define item alignment
    statusLabel_   -> setJustificationType(juce::Justification::centredLeft);

    // define TextEditor to be multiline, not editable, has scrollbars, no caret, and has font
    mappingsView_  -> setMultiLine(true);
    mappingsView_  -> setReadOnly(true);
    mappingsView_  -> setScrollbarsShown(true);
    mappingsView_  -> setCaretVisible(false);
    mappingsView_  -> setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));

    // define TextEditor to be single line, not editable, no scrollbars, no caret, and has font
    logView_       -> setMultiLine(false);
    logView_       -> setReadOnly(true);
    logView_       -> setScrollbarsShown(false);
    logView_       -> setCaretVisible(false);
    logView_       -> setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));

    // get serial configuration from the processor backend
    const auto config = processor_.getSerialConfig();

    // define TextEditor values from serial config
    deviceEditor_  -> setText(config.device, juce::dontSendNotification);
    baudBox_       -> setText(juce::String(config.baud), juce::dontSendNotification);

    // update the gui
    renderSessionMappingsIfChanged();
    updateConnectionState();
    
    // start a 20hz interval timer
    startTimerHz(20);
}

/*
 * juce gui destructor method.
 * ends update timer so gui stops updating
 * PARAMS: none
 * RETURNS: none
 */
HardwareControlAudioProcessorEditor::~HardwareControlAudioProcessorEditor() {
    stopTimer();
}

/*
 * juce gui window resizing method.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::resized() {
    // if layout root exists
    if (layout_ != nullptr) {
        // get root component and adjust bounds accordingly
        layout_ -> getComponent() -> setBounds(getLocalBounds());
    }
}

/*
 * juce gui method for toggling connections.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::toggleConnection() {
    // if processor is connected to a serial device, disconnect it
    if (processor_.isSerialConnected()) processor_.disconnectSerial();
    // otherwise, attempt a connection
    else {
        // get the device definition from the device TextEditor box
        const auto device = deviceEditor_ -> getText().trim();
        // if the string is not empty / provided, attempt a serial connection using the defined baud rate
        if (device.isNotEmpty()) processor_.connectSerial(device, baudBox_ -> getText().getIntValue());
    }

    // update the gui
    updateConnectionState();
}

/*
 * juce gui method for sending mappings over serial.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::sendMapping() {
    // get the pin from the TextEditor box
    const int pin = pinEditor_  -> getText().getIntValue();
    // get the action from the dropdown
    const int action = typeBox_ -> getSelectedId() - 1;

    // if the action is invalid, exit quietly
    if (action < 0 || action > 3) return;

    // tells the processor to queue an assignment command
    processor_.sendAssignmentCommand(pin, action);
    // update the gui
    renderSessionMappingsIfChanged();
}

/*
 * juce gui method for updating the connection state.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::updateConnectionState() {
    // get serial connection status
    const bool connected = processor_.isSerialConnected();
    // based on the status, reset the button text accordingly
    connectButton_ -> setButtonText(connected ? "Disconnect" : "Connect");

    // get the serial configuration
    const auto config = processor_.getSerialConfig();
    // get the number of active inputs
    const int activeCount = countActiveInputs();
    // enable the send assignment button if the connection is live
    sendButton_ -> setEnabled(connected);
    
    // update the status label text
    statusLabel_ -> setText(
        (connected ? "Connected" : "Not connected") +
            juce::String(" | Mode: MIDI CC instrument | Ch ") +
            juce::String(HardwareControlAudioProcessor::midiChannel) +
            " | CC " +
            juce::String(HardwareControlAudioProcessor::firstMidiCc) + "-" +
            juce::String(HardwareControlAudioProcessor::firstMidiCc +
                         hardware::maxInputSlots - 1) +
            " | Active " + juce::String(activeCount) + "/" +
            juce::String(hardware::maxInputSlots) +
            (config.device.isNotEmpty() ? " | " + config.device : ""),
        juce::dontSendNotification);
}

/*
 * juce gui timer callback method
 * this is called on the requested timer rate of 20 hz.
 * note: may not run at 20 hz exactly all the time.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::timerCallback() {
    // string value of the most recent log line
    juce::String latestLogLine;
    
    // get the processor's log lines, clear them, then iterate over them
    for (const auto &line : processor_.drainSerialLogLines()) {
        // set the latest log line, ends up being the most recent one
        latestLogLine = line;
    }

    // if there is a new log line to display
    if (latestLogLine.isNotEmpty() && latestLogLine != lastLogLine_) {
        // fixed lag by only having a single log line for now
        // might replace this later when the log can be made more performant
        logView_ -> setText(latestLogLine, false);
        logView_ -> moveCaretToEnd();
        lastLogLine_ = latestLogLine;
    }

    // update gui
    renderSessionMappingsIfChanged();
    updateConnectionState();
}

/*
 * juce gui method for counting the number of active inputs from a device.
 * PARAMS: none
 * RETURNS:
 *   ∟ int
 */
int HardwareControlAudioProcessorEditor::countActiveInputs() const {
    // initialize the counter
    int count = 0;

    // for each active input from the processor increment the counter
    for (const auto &input : processor_.getInputSnapshots()) {
        if (input.active) {
            ++count;
        }
    }

    // return final count
    return count;
}

/*
 * juce gui method for rendering the session mappings.
 * TODO:
 *   rework the mappings to be defined in vst3 config and update automagically based
 *   on user gui input / routing
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::renderSessionMappings() {
    // get the mappings from the processor
    const auto mappings = processor_.getSessionMappings();
    // output a temporary string for viewing mappings, will replace l8r
    std::ostringstream out;
    out << "Current session mappings\n";
    if (mappings.empty()) {
        out << "(none)\n";
    } else {
        for (const auto &mapping : mappings) {
            out << "Pin " << mapping.pin << " ~> "
                << assignmentActionName(mapping.action).toStdString() << '\n';
        }
    }

    // convert the out string stream to a juce string
    const juce::String nextText(out.str());
    if (nextText != lastMappingsText_) {
        mappingsView_ -> setText(nextText, false);
        lastMappingsText_ = nextText;
    }
}

/*
 * juce gui method for rendering mappings only after a processor revision change.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::renderSessionMappingsIfChanged() {
    const int revision = processor_.getSessionMappingsRevision();
    if (revision == lastMappingsRevision_) {
        return;
    }

    lastMappingsRevision_ = revision;
    renderSessionMappings();
}
