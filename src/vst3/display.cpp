#include "display.h"
#include <BinaryData.h>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {

/*
 * gui helper to convert action integer to a compact card label.
 * PARAMS:
 *   ∟ int action     : integer idx for action string
 * RETURNS:
 *   ∟ juce::String   : short visible mapping type label
 */
juce::String assignmentActionLetter(int action) {
    switch (action) {
        case 2: return "P";
        case 3: return "A";
        default: return "D";
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
 * compact horizontal mapping-card strip used by the jive layout.
 * owns a viewport and simple JUCE child cards so mappings can be rebuilt only
 * when the processor mapping revision changes, while values update every timer
 * tick without rebuilding the card list.
 */
class HardwareControlAudioProcessorEditor::MappingStripComponent final : public juce::Component {
public:
    std::function<void(int)> onDelete;

    /*
     * construct the viewport-backed mapping strip.
     * PARAMS: none
     * RETURNS: none
     */
    MappingStripComponent() {
        viewport_.setViewedComponent(&row_, false);
        viewport_.setScrollBarsShown(false, true);
        addAndMakeVisible(viewport_);
    }

    /*
     * replace the current mapping list and rebuild visible cards.
     * PARAMS:
     *   ∟ std::vector<SessionMapping> mappings   : mappings to display
     * RETURNS: none
     */
    void setMappings(std::vector<HardwareControlAudioProcessor::SessionMapping> mappings) {
        mappings_ = std::move(mappings);
        rebuildCards();
    }

    /*
     * refresh live card values from the latest processor snapshots.
     * PARAMS:
     *   ∟ std::array<InputSnapshot, maxInputSlots>& snapshots   : current slot state
     * RETURNS: none
     */
    void setInputSnapshots(
        const std::array<HardwareControlAudioProcessor::InputSnapshot, hardware::maxInputSlots>& snapshots) {
        for (auto& card : cards_) {
            card->setValue(findValueForPin(card->pin(), card->action(), snapshots));
        }
    }

    /*
     * juce component resize callback for the viewport and card row.
     * PARAMS: none
     * RETURNS: none
     */
    void resized() override {
        viewport_.setBounds(getLocalBounds());
        layoutCards();
    }

private:
    /*
     * plain JUCE card showing mapping type, pin, current value, and delete.
     */
    class MappingCard final : public juce::Component {
    public:
        /*
         * construct one visible mapping card.
         * PARAMS:
         *   ∟ int pin                                  : physical hardware pin
         *   ∟ int action                               : firmware assignment action id
         *   ∟ std::function<void(int)> deleteCallback  : delete callback by pin
         * RETURNS: none
         */
        MappingCard(int pin, int action, std::function<void(int)> deleteCallback)
            : pin_(pin), action_(action), onDelete_(std::move(deleteCallback)) {
            typeLabel_.setJustificationType(juce::Justification::centred);
            pinLabel_.setJustificationType(juce::Justification::centred);
            valueLabel_.setJustificationType(juce::Justification::centred);

            typeLabel_.setText(assignmentActionLetter(action_), juce::dontSendNotification);
            pinLabel_.setText(juce::String(pin_), juce::dontSendNotification);
            valueLabel_.setText("--", juce::dontSendNotification);
            deleteButton_.setButtonText("-");
            deleteButton_.onClick = [this] {
                if (onDelete_ != nullptr) {
                    onDelete_(pin_);
                }
            };

            addAndMakeVisible(typeLabel_);
            addAndMakeVisible(pinLabel_);
            addAndMakeVisible(valueLabel_);
            addAndMakeVisible(deleteButton_);
        }

        /*
         * get the physical pin represented by this card.
         * PARAMS: none
         * RETURNS:
         *   ∟ int   : physical hardware pin
         */
        int pin() const {
            return pin_;
        }

        /*
         * get the assignment action represented by this card.
         * PARAMS: none
         * RETURNS:
         *   ∟ int   : firmware assignment action id
         */
        int action() const {
            return action_;
        }

        /*
         * update the visible value label only when the text changes.
         * PARAMS:
         *   ∟ juce::String value   : formatted value text
         * RETURNS: none
         */
        void setValue(const juce::String& value) {
            if (valueLabel_.getText() != value) {
                valueLabel_.setText(value, juce::dontSendNotification);
            }
        }

        /*
         * draw a minimal card border using the default JUCE look.
         * PARAMS:
         *   ∟ juce::Graphics& graphics   : JUCE graphics context
         * RETURNS: none
         */
        void paint(juce::Graphics& graphics) override {
            graphics.setColour(juce::Colours::grey);
            graphics.drawRect(getLocalBounds());
        }

        /*
         * lay out card labels and delete button vertically.
         * PARAMS: none
         * RETURNS: none
         */
        void resized() override {
            auto bounds = getLocalBounds().reduced(6);
            typeLabel_.setBounds(bounds.removeFromTop(20));
            pinLabel_.setBounds(bounds.removeFromTop(20));
            valueLabel_.setBounds(bounds.removeFromTop(22));
            bounds.removeFromTop(6);
            deleteButton_.setBounds(bounds.removeFromTop(24));
        }

    private:
        int pin_ = -1;
        int action_ = 0;
        std::function<void(int)> onDelete_;
        juce::Label typeLabel_;
        juce::Label pinLabel_;
        juce::Label valueLabel_;
        juce::TextButton deleteButton_;
    };

    /*
     * format the latest snapshot value for one mapped pin.
     * PARAMS:
     *   ∟ int pin                                      : physical pin to look up
     *   ∟ int action                                   : mapping action type
     *   ∟ std::array<InputSnapshot, maxInputSlots>& snapshots   : current slot state
     * RETURNS:
     *   ∟ juce::String   : visible value text or "--" when unavailable
     */
    static juce::String findValueForPin(
        int pin,
        int action,
        const std::array<HardwareControlAudioProcessor::InputSnapshot, hardware::maxInputSlots>& snapshots) {
        for (const auto& snapshot : snapshots) {
            if (!snapshot.active || snapshot.pin != pin) {
                continue;
            }

            if (action == 3) {
                return juce::String(juce::roundToInt(snapshot.normalizedValue * 100.0f));
            }

            return snapshot.normalizedValue >= 0.5f ? "1" : "0";
        }

        return "--";
    }

    /*
     * rebuild card components from the current mapping list.
     * PARAMS: none
     * RETURNS: none
     */
    void rebuildCards() {
        cards_.clear();
        row_.removeAllChildren();

        for (const auto& mapping : mappings_) {
            auto card = std::make_unique<MappingCard>(mapping.pin, mapping.action, onDelete);
            row_.addAndMakeVisible(*card);
            cards_.push_back(std::move(card));
        }

        layoutCards();
    }

    /*
     * position cards horizontally inside the viewport row.
     * PARAMS: none
     * RETURNS: none
     */
    void layoutCards() {
        constexpr int cardWidth = 100;
        constexpr int gap = 8;

        const int contentWidth = static_cast<int>(cards_.size()) * (cardWidth + gap);
        const int rowWidth = juce::jmax(viewport_.getWidth(), contentWidth);
        row_.setSize(rowWidth, viewport_.getHeight());

        int x = 0;
        for (auto& card : cards_) {
            card->setBounds(x, 0, cardWidth, juce::jmax(0, row_.getHeight() - 2));
            x += cardWidth + gap;
        }
    }

    juce::Viewport viewport_;
    juce::Component row_;
    std::vector<HardwareControlAudioProcessor::SessionMapping> mappings_;
    std::vector<std::unique_ptr<MappingCard>> cards_;
};

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
    interpreter_.getComponentFactory().set(
        "MappingStrip", [] { return std::make_unique<MappingStripComponent>(); }
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

    // decode the bundled PNG into an image variant for jive's <Image> source
    if (auto* logoItem = jive::findItemWithID(*layout_, "image-logo")) {
        const auto logo = juce::ImageFileFormat::loadFrom(
            BinaryData::ohm_png,
            static_cast<size_t>(BinaryData::ohm_pngSize)
        );

        if (logo.isValid()) {
            logoItem -> state.setProperty(
                "source",
                juce::VariantConverter<juce::Image>::toVar(logo),
                nullptr
            );
        }
    }

    interpreter_.listenTo(*layout_);

    // get component of layout (root) and make visible
    addAndMakeVisible(*layout_ -> getComponent());

    // set window size
    setSize(620, 180);

    // get necessary components from the tree using their identifiers
    deviceEditor_  = findComponent<juce::TextEditor>(*layout_, "device-editor");
    baudBox_       = findComponent<juce::ComboBox>(*layout_, "baud-box");
    connectButton_ = findComponent<juce::TextButton>(*layout_, "connect-button");
    newMapButton_  = findComponent<juce::TextButton>(*layout_, "new-map-button");
    mappingStrip_  = findComponent<MappingStripComponent>(*layout_, "mapping-strip");

    // all components are not null; successful component locating (assertion builds)
    jassert(
        deviceEditor_  != nullptr &&
        baudBox_       != nullptr &&
        connectButton_ != nullptr &&
        newMapButton_  != nullptr &&
        mappingStrip_  != nullptr
    );

    // define placeholder text for TextEditor boxes
    deviceEditor_  -> setTextToShowWhenEmpty("COM3 or /dev/ttyACM0", juce::Colours::grey);
    // set button text directly because jive maps button text to title internally
    newMapButton_  -> setButtonText("+");
    // define button action calls
    connectButton_ -> onClick = [this] { toggleConnection(); };
    newMapButton_  -> onClick = [this] { showNewMappingDialog(); };
    mappingStrip_  -> onDelete = [this](int pin) {
        processor_.sendAssignmentCommand(pin, 0);
    };

    // get serial configuration from the processor backend
    const auto config = processor_.getSerialConfig();

    // define TextEditor values from serial config
    deviceEditor_  -> setText(config.device, juce::dontSendNotification);
    baudBox_       -> setText(juce::String(config.baud), juce::dontSendNotification);

    // update the gui
    renderSessionMappingsIfChanged();
    updateMappingValues();
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
 * juce gui method for showing a new mapping popup.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::showNewMappingDialog() {
    auto* alert = new juce::AlertWindow("New Mapping", {}, juce::AlertWindow::NoIcon, this);
    alert -> addTextEditor("pin", {}, "Pin:", false);
    alert -> addComboBox("type", { "Digital", "Pullup", "Analog" }, "Type:");
    alert -> addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert -> addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    if (auto* pinEditor = alert -> getTextEditor("pin")) {
        pinEditor -> setInputRestrictions(4, "0123456789");
    }

    if (auto* typeBox = alert -> getComboBoxComponent("type")) {
        typeBox -> setSelectedId(1, juce::dontSendNotification);
    }

    juce::Component::SafePointer<HardwareControlAudioProcessorEditor> safeThis(this);
    juce::Component::SafePointer<juce::AlertWindow> safeAlert(alert);
    alert -> enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, safeAlert](int result) {
                if (safeThis == nullptr || safeAlert == nullptr || result != 1) {
                    return;
                }

                const auto pinText = safeAlert -> getTextEditorContents("pin").trim();
                if (pinText.isEmpty()) {
                    return;
                }

                const int pin = pinText.getIntValue();
                const int action = safeAlert -> getComboBoxComponent("type") != nullptr
                    ? safeAlert -> getComboBoxComponent("type") -> getSelectedId()
                    : 1;
                safeThis -> sendMapping(pin, action > 0 ? action : 1);
            }),
        true);
}

/*
 * juce gui method for sending a mapping assignment to the processor.
 * PARAMS:
 *   ∟ int pin      : hardware pin to assign
 *   ∟ int action   : firmware assignment action id
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::sendMapping(int pin, int action) {
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
    newMapButton_ -> setEnabled(connected);
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
    processor_.drainSerialLogLines();

    // update gui
    renderSessionMappingsIfChanged();
    updateMappingValues();
    updateConnectionState();
}

/*
 * juce gui method for updating visible mapping card values.
 * PARAMS: none
 * RETURNS:
 *   ∟ void
 */
void HardwareControlAudioProcessorEditor::updateMappingValues() {
    if (mappingStrip_ != nullptr) {
        mappingStrip_ -> setInputSnapshots(processor_.getInputSnapshots());
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
    if (mappingStrip_ != nullptr) {
        mappingStrip_ -> setMappings(processor_.getSessionMappings());
    }
}
