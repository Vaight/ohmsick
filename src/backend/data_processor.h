// re-documented on 7-24-2026

#pragma once

#include "backend/protocol.h"

#include <array>
#include <atomic>
#include <optional>

namespace backend {

constexpr float defaultInputSmoothingAlpha = 0.25f;
constexpr float defaultInputInterpolationDelta = 0.05f;

/**
 * @enum Modifier
 * @brief The optional transformation applied to a mapped input.
 */
enum class Modifier {
    None,           // Raw data assignment, no modifier.
    DigitalInvert,  // digital input inversion      :  0 -> 1 maps to 1 -> 0.
    DigitalToggle,  // digital toggle signal        :  0 -> 1 -> 0 maps to 0 -> 1 then 0 -> 1 -> 0 maps to 1 -> 0.
    DigitalLerp,    // digital linear interpolation :  0 -> 1 maps to 0.0f ~> 1.0f with a lerp algorithm.
    AnalogInvert,   // analog input inversion       :  0.0f -> 1.0f maps to 1.0f -> 0.0f.
    AnalogSmooth    // analog input smoothing       :  0.0f -> 1.0f maps to 0.0f ~> 1.0f with a smoothing alpha.
};

/**
 * @struct InputValue
 * @brief The state of a mapped input.
 *   This struct is a definition for the input state properties of a slot.
 *   This is used across DataProcessor to access slot state properties.
 */
struct InputValue {

    bool active           = false;
    int pin               = -1;
    Kind kind             = Kind::Pot;
    Modifier modifier     = Modifier::None;
    float normalizedValue = 0.0f;

};

struct OutputValue {
    // todo later
};

class DataProcessor {
public:
    explicit DataProcessor(
        float smoothingAlpha = defaultInputSmoothingAlpha,
        float interpolationDelta = defaultInputInterpolationDelta);

    void reset();
    void applyFrame(const Frame& frame);

    /**
     * @brief Assigns the modifier used when processing future frames for a slot.
     *
     * @param slot     The mapped slot to configure.
     * @param modifier The modifier to apply.
     * @return true    The slot was valid and its modifier was assigned.
     * @return false   The slot was invalid.
     */
    bool setInputModifier(int slot, Modifier modifier);

    InputValue getInputBySlot(int slot) const;
    std::optional<InputValue> getInputByPin(int pin) const;
    std::array<InputValue, maxInputSlots> getInputs() const;

    bool hasInputValue(int slot) const;
    float getInputValue(int slot) const;
    int getInputValueAsCC(int slot) const;

private:
    float smoothingAlpha_     = defaultInputSmoothingAlpha;
    float interpolationDelta_ = defaultInputInterpolationDelta;
    std::array<float, maxInputSlots>              smoothedValues_     {};
    std::array<float, maxInputSlots>              interpolatedValues_ {};
    std::array<bool, maxInputSlots>               previousDigitalValues_ {};
    std::array<bool, maxInputSlots>               toggledValues_      {};
    std::array<Modifier, maxInputSlots>           appliedModifiers_   {};
    std::array<std::atomic<float>, maxInputSlots> values_             {};
    std::array<std::atomic<int>, maxInputSlots>   pins_               {};
    std::array<std::atomic<int>, maxInputSlots>   kinds_              {};
    std::array<std::atomic<int>, maxInputSlots>   modifiers_          {};
    std::array<std::atomic<bool>, maxInputSlots>  active_             {};
};

int normalizedToCc(float value);

}  // namespace backend
