// re-documented on 7-24-2026

#include "backend/data_processor.h"

#include <algorithm>
#include <cmath>

namespace {

    /**
     * @brief Clamps a float between 0.0f and 1.0f.
     * 
     * @param value  The value to clamp.
     * @return float The clamped value.
     */
    float clamp01(float value) {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    /**
     * @brief Checker for if a slot is valid.
     *   This method simply checks if a slot is within the max slot boundary.
     * 
     * @param slot   The slot to check.
     * @return true  The slot is valid.
     * @return false The slot is invalid.
     */
    bool isValidSlot(int slot) {
        return slot >= 0 && slot < backend::maxInputSlots;
    }

    /**
     * @brief Inverts a normalized value's mapping.
     *   This method inverts a normalized float from the 0.0f - 1.0f range to the
     *   1.0f - 0.0f range. Used by the inversion modifiers.
     *
     * @param value  The float value to invert.
     * @return float The inverted float.
     */
    float normalizedInvert(float value) {
        return 1.0f - clamp01(value);
    }

}  // namespace

namespace backend {

    /**
     * @brief DataProcessor Constructor.
     *
     *   This is the constructor definition for a DataProcessor object.
     *   Here we set all initial values and run an initial reset.
     *
     * @param smoothingAlpha     The amoount of smoothing on analog inputs.
     * @param interpolationDelta The amount moved per frame by digital interpolation.
     */
    DataProcessor::DataProcessor(float smoothingAlpha, float interpolationDelta)
        : smoothingAlpha_(clamp01(smoothingAlpha)),
          interpolationDelta_(clamp01(interpolationDelta)) {
        reset();
    }

    /**
     * @brief Resets the property value arrays of the processor.
     *   This method overwrites the existing data with zeros or otherwise empty handled values.
     *   (Member of the DataProcessor class)
     */
    void DataProcessor::reset() {
        smoothedValues_.fill(0.0f);
        interpolatedValues_.fill(0.0f);
        previousDigitalValues_.fill(false);
        toggledValues_.fill(false);
        appliedModifiers_.fill(Modifier::None);
        for (int slot = 0; slot < maxInputSlots; ++slot) {
            const auto index = static_cast<std::size_t>(slot);
            values_[index].store(0.0f);
            pins_[index].store(-1);
            kinds_[index].store(static_cast<int>(Kind::Pot));
            modifiers_[index].store(static_cast<int>(Modifier::None));
            active_[index].store(false);
        }
    }

    /**
     * @brief Processes and publishes the latest serial readings to the property arrays.
     *   This method is the backbone of the DataProcessor. It takes in a frame reference to read from
     *   and it applies the frame readings to the corresponding property array for the slot indexes.
     *   This method now handles modifier application!
     *   (Member of the DataProcessor class)
     *
     * @param frame The serial frame reference
     */
    void DataProcessor::applyFrame(const Frame& frame) {
        // for each reading frame.
        for (const auto& reading : frame.readings) {

            // if the slot is valid, continue processing.
            if (!isValidSlot(reading.slot)) continue;

            // get the slot and value constants from the current reading frame.
            const auto slot          = static_cast<std::size_t>(reading.slot);
            const float value        = clamp01(reading.normalizedValue);
            const auto modifier      = static_cast<Modifier>(modifiers_[slot].load());
            const bool isDigital     = value >= 0.5f;
            const float digitalValue = isDigital ? 1.0f : 0.0f;

            if (appliedModifiers_[slot] != modifier) {
                smoothedValues_[slot]        = values_[slot].load();
                interpolatedValues_[slot].   = values_[slot].load();
                previousDigitalValues_[slot] = false;
                toggledValues_[slot]         = false;
                appliedModifiers_[slot]      = modifier;
            }

            float modifiedValue = value;

            // apply modifiers
            switch (modifier) {
                case Modifier::DigitalInvert:
                    modifiedValue = normalizedInvert(digitalValue);
                    break;
                case Modifier::DigitalToggle:
                    if (isDigital && !previousDigitalValues_[slot]) {
                        toggledValues_[slot] = !toggledValues_[slot];
                    }
                    modifiedValue = toggledValues_[slot] ? 1.0f : 0.0f;
                    break;
                case Modifier::DigitalLerp:
                    if (interpolatedValues_[slot] < digitalValue) {
                        interpolatedValues_[slot] =
                            std::min(digitalValue, interpolatedValues_[slot] + interpolationDelta_);
                    } else if (interpolatedValues_[slot] > digitalValue) {
                        interpolatedValues_[slot] =
                            std::max(digitalValue, interpolatedValues_[slot] - interpolationDelta_);
                    }
                    modifiedValue = interpolatedValues_[slot];
                    break;
                case Modifier::AnalogInvert:
                    modifiedValue = normalizedInvert(value);
                    break;
                case Modifier::AnalogSmooth:
                    smoothedValues_[slot] +=
                        smoothingAlpha_ * (value - smoothedValues_[slot]);
                    modifiedValue = smoothedValues_[slot];
                    break;
                case Modifier::None:
                default:
                    // unknown modifier, do nothing.
                    break;
            }
            previousDigitalValues_[slot] = isDigital;

            // apply the properties.
            values_[slot].store(clamp01(modifiedValue));
            pins_[slot].store(reading.pin);
            kinds_[slot].store(static_cast<int>(reading.kind));
            active_[slot].store(true);

        }
    }

    /**
     * @brief Assigns the modifier used when processing future frames for a slot.
     *
     * @param slot     The mapped slot to configure.
     * @param modifier The modifier to apply.
     * @return true    The slot was valid and its modifier was assigned.
     * @return false   The slot was invalid.
     */
    bool DataProcessor::setInputModifier(int slot, Modifier modifier) {
        if (!isValidSlot(slot)) return false;

        modifiers_[static_cast<std::size_t>(slot)].store(static_cast<int>(modifier));
        return true;
    }

    /**
     * @brief Getter for an InputValue object from a mapped slot.
     *   This method builds a new InputValue object based on the properties of the provided
     *   slot index. The slot index is checked for each property array and is assigned to
     *   its corresponding property within the InputValue object.
     *   (Member of the DataProcessor class)
     *
     * @param slot        The mapped slot (index) to read properties from.
     * @return InputValue The current slot state.
     */
    InputValue DataProcessor::getInputBySlot(int slot) const {
        if (!isValidSlot(slot)) return {};

        const auto index = static_cast<std::size_t>(slot);
        InputValue input;
        input.active          = active_[index].load();
        input.pin             = pins_[index].load();
        input.kind            = static_cast<Kind>(kinds_[index].load());
        input.modifier        = static_cast<Modifier>(modifiers_[index].load());
        input.normalizedValue = clamp01(values_[index].load());
        return input;
    }

    /**
     * @brief Getter for an InputValue object based on an assigned input pin
     *   This method searches all assigned InputValue objects for an active
     *   object with the requested 'pin' property.
     *   (Member of the DataProcessor class)
     *
     * @param pin                        The physical pin number to search for.
     * @return std::optional<InputValue> The matching InputValue object, can also be nullopt (not found).
     */
    std::optional<InputValue> DataProcessor::getInputByPin(int pin) const {
        for (int slot = 0; slot < maxInputSlots; ++slot) {
            const auto input = getInputBySlot(slot);
            if (input.active && input.pin == pin) {
                return input;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Getter for an array of all mapped input slots.
     *   This method returns an std array of all mapped input slots.
     *   (Member of the DataProcessor class)
     *
     * @return std::array<InputValue, maxInputSlots> The array
     */
    std::array<InputValue, maxInputSlots> DataProcessor::getInputs() const {
        std::array<InputValue, maxInputSlots> inputs {};
        for (int slot = 0; slot < maxInputSlots; ++slot) {
            inputs[static_cast<std::size_t>(slot)] = getInputBySlot(slot);
        }
        return inputs;
    }
    
    /**
     * @brief Checker for a slot to see if it has recieved data.
     *   This method executes the getInputBySlot method, which returns an InputValue object.
     *   The InputValue object has a property 'active' which is returned here.
     *   (Member of the DataProcessor class)
     *
     * @param slot   The mapped slot to get the input value from.
     * @return true  The slot recieved data.
     * @return false The slot is idle / hasn't recieved data.
     */
    bool DataProcessor::hasInputValue(int slot) const {
        return getInputBySlot(slot).active;
    }

    /**
     * @brief Getter for the normalized input value of a mapped slot.
     *   This method executes the getInputBySlot method, which returns an InputValue object.
     *   The InputValue object has a property 'normalizedValue' which is returned here.
     *   (Member of the DataProcessor class)
     *
     * @param slot   The mapped slot to get the input value from.
     * @return float The normalized result of the recieved input value.
     */
    float DataProcessor::getInputValue(int slot) const {
        return getInputBySlot(slot).normalizedValue;
    }
    
    /**
     * @brief Getter for a mapped slot's value that's been converted to the MIDI CC range.
     *   This method executes the getInputValue method for a slot and uses the normalizedToCc
     *   method to convert the normalized value float into the integer range for MIDI CC.
     *   (Member of the DataProcessor class)
     *
     * @param slot The hardware slot to read from.
     * @return int The converted MIDI CC integer from the slot value.
     */
    int DataProcessor::getInputValueAsCC(int slot) const {
        return normalizedToCc(getInputValue(slot));
    }

    /**
     * @brief Convert normalized float into a MIDI CC integer.
     *   This method clamps and casts a provided 0.0f - 1.0f float and converts it to
     *   a range 0 - 127 integer value by static casting & rounding the float. MIDI CC
     *   is exclusively operated within this integer range.
     *
     * @param value The float value to remap to the MIDI CC integer range.
     * @return int  The resulting MIDI CC integer.
     */
    int normalizedToCc(float value) {
        return std::clamp(static_cast<int>(std::lround(clamp01(value) * 127.0f)), 0, 127);
    }

    /**
    * @brief Convert normalized float into integer.
    *   This method clamps and casts a provided 0.0f - 1.0f float and converts it to
    *   either a 0 or 1 integer value by static casting & rounding the float.
    *
    * @param value The float value to convert.
    * @return int  The resulting integer, 0 or 1.
    */
    int normalizedToInt(float value) {
        return std::clamp(static_cast<int>(std::lround(clamp01(value))), 0, 1);
    }

}  // namespace backend
