#include "periph_contract/scripted_hal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::size_t allocationCount = 0;

void* operator new(std::size_t size) {
    ++allocationCount;
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    ++allocationCount;
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

typedef periph_contract::ScriptedHal<8, 16> TestHal;

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    const std::size_t allocationsBefore = allocationCount;

    TestHal hal;
    const std::uint8_t command[] = {0x10, 0x02};
    const std::uint8_t reply[] = {0xAA, 0x55};
    CHECK(hal.expectPinMode(4, periph_contract::PinMode::Output));
    CHECK(hal.expectDigitalWrite(4, true));
    CHECK(hal.expectDigitalRead(7, false));
    CHECK(hal.expectI2c(0x48, command, sizeof(command), reply, sizeof(reply), 30));
    CHECK(hal.expectSpi(2, 3, command, sizeof(command), reply, sizeof(reply)));
    CHECK(hal.expectUartWrite(1, command, sizeof(command)));
    CHECK(hal.expectUartRead(1, 8, reply, sizeof(reply), 50));

    CHECK(hal.pinMode(4, periph_contract::PinMode::Output) == periph_contract::Status::Ok);
    CHECK(hal.digitalWrite(4, true) == periph_contract::Status::Ok);
    bool pinValue = true;
    CHECK(hal.digitalRead(7, pinValue) == periph_contract::Status::Ok);
    CHECK(!pinValue);
    std::uint8_t i2cReply[2] = {};
    CHECK(hal.i2cTransfer(0x48, command, sizeof(command), i2cReply, sizeof(i2cReply), 30)
          == periph_contract::Status::Ok);
    CHECK(i2cReply[0] == 0xAA && i2cReply[1] == 0x55);
    std::uint8_t spiReply[2] = {};
    CHECK(hal.spiTransfer(2, 3, command, sizeof(command), spiReply, sizeof(spiReply))
          == periph_contract::Status::Ok);
    CHECK(spiReply[0] == 0xAA && spiReply[1] == 0x55);
    CHECK(hal.uartWrite(1, command, sizeof(command)) == periph_contract::Status::Ok);
    std::uint8_t uartReply[8] = {};
    std::size_t received = 0;
    CHECK(hal.uartRead(1, uartReply, sizeof(uartReply), 50, received) == periph_contract::Status::Ok);
    CHECK(received == 2 && uartReply[0] == 0xAA && uartReply[1] == 0x55);
    CHECK(hal.assertComplete());
    CHECK(allocationCount == allocationsBefore);

    TestHal mismatch;
    CHECK(mismatch.expectI2c(0x48, command, sizeof(command), reply, sizeof(reply)));
    CHECK(mismatch.i2cTransfer(0x49, command, sizeof(command), i2cReply, sizeof(i2cReply))
          == periph_contract::Status::IoError);
    CHECK(mismatch.failure() == periph_contract::Failure::WrongEndpoint);
    CHECK(mismatch.failureIndex() == 0);

    TestHal partial;
    CHECK(partial.expectUartRead(3, 8, reply, sizeof(reply), 10));
    CHECK(partial.expectI2c(0x20, nullptr, 0, nullptr, 0, 5, periph_contract::Status::Timeout));
    received = 0;
    CHECK(partial.uartRead(3, uartReply, sizeof(uartReply), 10, received) == periph_contract::Status::Ok);
    CHECK(received == 2);
    CHECK(partial.i2cTransfer(0x20, nullptr, 0, nullptr, 0, 5) == periph_contract::Status::Timeout);
    CHECK(partial.assertComplete());

    TestHal readTimeout;
    CHECK(readTimeout.expectI2cFailure(0x20, nullptr, 0, 2, 5, periph_contract::Status::Timeout));
    std::uint8_t timedOutRead[2] = {};
    CHECK(readTimeout.i2cTransfer(0x20, nullptr, 0, timedOutRead, sizeof(timedOutRead), 5)
          == periph_contract::Status::Timeout);
    CHECK(readTimeout.assertComplete());

    TestHal incomplete;
    CHECK(incomplete.expectDigitalWrite(1, false));
    CHECK(!incomplete.assertComplete());
    CHECK(incomplete.failure() == periph_contract::Failure::ExpectedOperationsRemaining);

    TestHal overflow;
    std::uint8_t tooLong[17] = {};
    CHECK(!overflow.expectUartWrite(0, tooLong, sizeof(tooLong)));
    CHECK(overflow.failure() == periph_contract::Failure::InvalidExpectation);

    std::puts("peripheral contract tests passed without heap allocations");
    return 0;
}
