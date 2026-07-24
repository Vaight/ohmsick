// re-documented on 7-24-2026

#pragma once

#include "backend/protocol.h"

#include <array>
#include <atomic>
#include <optional>

namespace backend {

constexpr float defaultInputSmoothingAlpha = 0.25f;

/**
 * @struct InputValue
 * @brief The state of a mapped input.
 *   This struct is a definition for the input state properties of a slot.
 *   This is used across DataProcessor to access slot state properties.
 */
struct InputValue {
    bool active = false;
    int pin = -1;
    Kind kind = Kind::Pot;
    float normalizedValue = 0.0f;
};

struct OutputValue {
    // todo later
};

class DataProcessor {
public:
    explicit DataProcessor(float smoothingAlpha = defaultInputSmoothingAlpha);

    void reset();
    void applyFrame(const Frame& frame);

    InputValue getInputBySlot(int slot) const;
    std::optional<InputValue> getInputByPin(int pin) const;
    std::array<InputValue, maxInputSlots> getInputs() const;

    bool hasInputValue(int slot) const;
    float getInputValue(int slot) const;
    int getInputValueAsCC(int slot) const;

private:
    float smoothingAlpha_ = defaultInputSmoothingAlpha;
    std::array<float, maxInputSlots> smoothedValues_ {};
    std::array<std::atomic<float>, maxInputSlots> values_ {};
    std::array<std::atomic<int>, maxInputSlots> pins_ {};
    std::array<std::atomic<int>, maxInputSlots> kinds_ {};
    std::array<std::atomic<bool>, maxInputSlots> active_ {};
};

int normalizedToCc(float value);

}  // namespace backend
