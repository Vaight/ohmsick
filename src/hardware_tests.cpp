#include "backend.h"

#include <cassert>
#include <cmath>
#include <sstream>
#include <string>

namespace {

void parsesTypicalFrame() {
    const auto frame = hardware::parseFrame("2 14P067 2B1");
    assert(frame);
    assert(frame->expectedCount == 2);
    assert(frame->readings.size() == 2);

    assert(frame->readings[0].slot == 0);
    assert(frame->readings[0].pin == 14);
    assert(frame->readings[0].kind == hardware::Kind::Pot);
    assert(frame->readings[0].normalizedValue > 0.669f);
    assert(frame->readings[0].normalizedValue < 0.671f);

    assert(frame->readings[1].slot == 1);
    assert(frame->readings[1].pin == 2);
    assert(frame->readings[1].kind == hardware::Kind::Button);
    assert(frame->readings[1].normalizedValue == 1.0f);
}

void rejectsStatusAndMalformedLines() {
    assert(!hardware::parseFrame("READY"));
    assert(!hardware::parseFrame(""));
    assert(!hardware::parseFrame("2 nope"));
    assert(!hardware::parseFrame("2 14X067"));
    assert(!hardware::parseFrame("2 14Pabc"));
}

void handlesCrLfAndExtraReadings() {
    const auto crlfFrame = hardware::parseFrame("1 3B0\r\n");
    assert(crlfFrame);
    assert(crlfFrame->readings.size() == 1);
    assert(crlfFrame->readings[0].normalizedValue == 0.0f);

    std::ostringstream line;
    line << "70 ";
    for (int i = 0; i < 70; ++i) {
        line << i << "P050 ";
    }

    const auto largeFrame = hardware::parseFrame(line.str());
    assert(largeFrame);
    assert(largeFrame->readings.size() == hardware::maxInputSlots);
}

void storesInputValuesFromFrames() {
    hardware::DataProcessor processor(1.0f);
    const auto frame = hardware::parseFrame("2 14P067 2B1");
    assert(frame);

    processor.applyFrame(*frame);

    const auto pot = processor.getInputBySlot(0);
    assert(pot.active);
    assert(pot.pin == 14);
    assert(pot.kind == hardware::Kind::Pot);
    assert(std::fabs(pot.normalizedValue - 0.67f) < 0.001f);
    assert(processor.getInputValueAsCC(0) == 85);

    const auto button = processor.getInputByPin(2);
    assert(button);
    assert(button->kind == hardware::Kind::Button);
    assert(button->normalizedValue == 1.0f);

    assert(!processor.getInputByPin(99));
    assert(!processor.hasInputValue(63));
}

void smoothsPotsAndResetsInputs() {
    hardware::DataProcessor processor;
    const auto firstFrame = hardware::parseFrame("1 10P100");
    assert(firstFrame);

    processor.applyFrame(*firstFrame);
    assert(std::fabs(processor.getInputValue(0) - 0.25f) < 0.001f);

    processor.reset();
    assert(!processor.hasInputValue(0));
    assert(processor.getInputValue(0) == 0.0f);
}

}  // namespace

int main() {
    parsesTypicalFrame();
    rejectsStatusAndMalformedLines();
    handlesCrLfAndExtraReadings();
    storesInputValuesFromFrames();
    smoothsPotsAndResetsInputs();
    return 0;
}
