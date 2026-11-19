# Embedded C++ GPIO, I2C, SPI, and UART Contract Test Kit

Test embedded firmware drivers on a host with a header-only C++11 scripted peripheral adapter. Queue fixed-buffer expectations for GPIO, I2C, SPI, or UART operations, then assert call order, addresses, configuration, timeouts, and payload bytes without accessing hardware.

The adapter uses no heap allocation and does not sleep. A JSON scenario generator can produce the expectation setup function for a host test.

## Example: script an I2C transaction

```cpp
#include <cassert>
#include <cstdint>
#include "periph_contract/scripted_hal.hpp"

int main() {
    periph_contract::ScriptedHal<8, 32> hal;
    const std::uint8_t request[] = {0x10, 0x02};
    const std::uint8_t response[] = {0xAA};
    hal.expectI2c(0x48, request, sizeof(request), response, sizeof(response), 25);

    std::uint8_t received[1] = {};
    auto status = hal.i2cTransfer(0x48, request, sizeof(request), received, sizeof(received), 25);
    assert(status == periph_contract::Status::Ok);
    assert(hal.assertComplete());
}
```

A mismatch records the first failure kind and script index, then returns `Status::IoError`. Tests can also script timeouts and I/O errors; UART reads can return fewer bytes than requested.

## Generate a peripheral test scenario

Requires Python 3 and CMake 3.16 or newer:

```sh
git clone https://github.com/buxerr/peripheral-contract-kit.git
cd peripheral-contract-kit
python tools/generate_script.py examples/sensor_read.json --max-bytes 8 --output build/generated_script.hpp
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The generated header defines `peripheral_contract_generated::installScript(hal)`. The generator accepts up to 64 operations and checks byte and buffer bounds before writing code. It has no third-party dependencies. The generated test can plug into CTest, Unity, Ceedling, or PlatformIO host tests.

## Scope and limitations

This kit checks driver-call contracts and buffer handling, not electrical behavior, interrupts, real timing, DMA, or a specific vendor HAL. Put a thin test seam around your platform API and use the scripted adapter there.

## License

MIT. See [LICENSE](LICENSE).
