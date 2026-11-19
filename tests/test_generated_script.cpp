#include "generated_script.hpp"

#include <cstddef>
#include <cstdint>

int main() {
    periph_contract::ScriptedHal<4, 8> hal;
    if (!peripheral_contract_generated::installScript(hal)) return 1;
    if (hal.pinMode(4, periph_contract::PinMode::Output) != periph_contract::Status::Ok) return 2;
    const std::uint8_t command[] = {0x10, 0x02};
    std::uint8_t i2cResponse[1] = {};
    if (hal.i2cTransfer(72, command, sizeof(command), i2cResponse, sizeof(i2cResponse), 25)
        != periph_contract::Status::Ok) return 3;
    if (i2cResponse[0] != 0xAA) return 4;
    std::uint8_t uartResponse[8] = {};
    std::size_t received = 0;
    if (hal.uartRead(1, uartResponse, sizeof(uartResponse), 50, received) != periph_contract::Status::Ok) return 5;
    if (received != 1 || uartResponse[0] != 0x55) return 6;
    std::uint8_t timedOutRead[2] = {};
    if (hal.i2cTransfer(32, nullptr, 0, timedOutRead, sizeof(timedOutRead), 5)
        != periph_contract::Status::Timeout) return 7;
    return hal.assertComplete() ? 0 : 8;
}
