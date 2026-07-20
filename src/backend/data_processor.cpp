#include "backend/data_processor.h"

#include <algorithm>
#include <cmath>

namespace {

    /*
     * METHOD clamp01      : clamps a float between 0.0f and 1.0f
     * PARAMS:
     *   ∟ float value     : value to clamp between 0.0f and 1.0f
     * RETURNS:
     *   ∟ float value     : the final clamped value, between 0.0f and 1.0f
     */
    float clamp01(float value) {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    /*
     * METHOD isValidSlot    : checks if a slot is within the range of possible slots
     * PARAMS:
     *   ∟ int slot          : the slot index to test
     * RETURNS:
     *   ∟ bool valid        : t/f if the slot is within max slots
     */
    bool isValidSlot(int slot) {
        return slot >= 0 && slot < backend::maxInputSlots;
    }

}  // namespace

namespace backend {

    /*
     * CONSTRUCTOR DataProcessor            : prepares a processor for normalized hardware input values.
     * PARAMS:
     *   ∟ float smoothingAlpha             : 0.0f-1.0f smoothing amount used for pot readings
     * RETURNS: none
     */
    DataProcessor::DataProcessor(float smoothingAlpha) : smoothingAlpha_(clamp01(smoothingAlpha)) {
        reset();
    }

    /*
     * METHOD DataProcessor::reset          : clears all input slots back to inactive defaults.
     * PARAMS: none
     * RETURNS: none
     */
    void DataProcessor::reset() {
        smoothedValues_.fill(0.0f);

        for (int slot = 0; slot < maxInputSlots; ++slot) {
            const auto index = static_cast<std::size_t>(slot);
            values_[index].store(0.0f);
            pins_[index].store(-1);
            kinds_[index].store(static_cast<int>(Kind::Pot));
            active_[index].store(false);
        }
    }

    /*
     * METHOD DataProcessor::applyFrame     : publishes the latest parsed serial input readings.
     * PARAMS:
     *   ∟ const Frame& frame               : parsed hardware frame containing slot/pin/value readings
     * RETURNS: none
     */
    void DataProcessor::applyFrame(const Frame& frame) {
        for (const auto& reading : frame.readings) {
            if (!isValidSlot(reading.slot)) {
                continue;
            }

            const auto slot = static_cast<std::size_t>(reading.slot);
            const float value = clamp01(reading.normalizedValue);

            if (reading.kind == Kind::Pot) {
                smoothedValues_[slot] += smoothingAlpha_ * (value - smoothedValues_[slot]);
            } else {
                smoothedValues_[slot] = value;
            }

            values_[slot].store(smoothedValues_[slot]);
            pins_[slot].store(reading.pin);
            kinds_[slot].store(static_cast<int>(reading.kind));
            active_[slot].store(true);
        }
    }

    /*
     * METHOD DataProcessor::getInputBySlot : gets the current value for a slot index.
     * PARAMS:
     *   ∟ int slot                         : hardware slot index to read
     * RETURNS:
     *   ∟ InputValue input                 : current slot state, or inactive defaults for invalid slots
     */
    InputValue DataProcessor::getInputBySlot(int slot) const {
        if (!isValidSlot(slot)) {
            return {};
        }

        const auto index = static_cast<std::size_t>(slot);
        InputValue input;
        input.active = active_[index].load();
        input.pin = pins_[index].load();
        input.kind = static_cast<Kind>(kinds_[index].load());
        input.normalizedValue = clamp01(values_[index].load());
        return input;
    }

    /*
     * METHOD DataProcessor::getInputByPin  : finds the current value for a physical pin.
     * PARAMS:
     *   ∟ int pin                          : physical pin number to search for
     * RETURNS:
     *   ∟ optional<InputValue> input       : matching active input, or nullopt if the pin is inactive
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

    /*
     * METHOD DataProcessor::getInputs      : copies all published input slot states.
     * PARAMS: none
     * RETURNS:
     *   ∟ array<InputValue> inputs         : point-in-time snapshot of every input slot
     */
    std::array<InputValue, maxInputSlots> DataProcessor::getInputs() const {
        std::array<InputValue, maxInputSlots> inputs {};
        for (int slot = 0; slot < maxInputSlots; ++slot) {
            inputs[static_cast<std::size_t>(slot)] = getInputBySlot(slot);
        }

        return inputs;
    }

    /*
     * METHOD DataProcessor::hasInputValue  : checks whether a slot has received data.
     * PARAMS:
     *   ∟ int slot                         : hardware slot index to test
     * RETURNS:
     *   ∟ bool active                      : true when the slot currently has a published value
     */
    bool DataProcessor::hasInputValue(int slot) const {
        return getInputBySlot(slot).active;
    }

    /*
     * METHOD DataProcessor::getInputValue  : gets the normalized value for a slot.
     * PARAMS:
     *   ∟ int slot                         : hardware slot index to read
     * RETURNS:
     *   ∟ float value                      : current normalized value from 0.0f to 1.0f
     */
    float DataProcessor::getInputValue(int slot) const {
        return getInputBySlot(slot).normalizedValue;
    }

    /*
     * METHOD DataProcessor::getInputValueAsCC  : gets a slot value mapped to MIDI CC range.
     * PARAMS:
     *   ∟ int slot                             : hardware slot index to read
     * RETURNS:
     *   ∟ int value                            : current slot value mapped from 0 to 127
     */
    int DataProcessor::getInputValueAsCC(int slot) const {
        return normalizedToCc(getInputValue(slot));
    }

    /*
     * METHOD normalizedToCc                : maps a normalized float to the MIDI CC value range.
     * PARAMS:
     *   ∟ float value                      : normalized value to clamp and map
     * RETURNS:
     *   ∟ int value                        : integer value between 0 and 127
     */
    int normalizedToCc(float value) {
        return std::clamp(static_cast<int>(std::lround(clamp01(value) * 127.0f)), 0, 127);
    }

}  // namespace backend
