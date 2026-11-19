# Peripheral Contract Kit

Header-only C++11 scripted peripheral adapter plus a Python standard-library generator for hardware-independent firmware tests. Drivers call small GPIO, I2C, SPI, or UART methods; tests queue bounded expectations and assert exact operation order, addresses, configuration, timeouts, payload bytes, and response bytes. A JSON scenario can generate the expectation setup function used by a host test.

```cpp
periph_contract::ScriptedHal<8, 32> hal;
const std::uint8_t request[] = {0x10, 0x02};
const std::uint8_t response[] = {0xAA};
hal.expectI2c(0x48, request, sizeof(request), response, sizeof(response), 25);

std::uint8_t received[1] = {};
auto status = hal.i2cTransfer(0x48, request, sizeof(request), received, sizeof(received), 25);
assert(status == periph_contract::Status::Ok);
assert(hal.assertComplete());
```

Every script and payload uses compile-time fixed storage. The adapter performs no heap allocation, sleeps, or hardware access. A mismatch records the first failure kind and script index, then returns `Status::IoError`. Scripted timeouts and I/O errors are returned at the configured operation; UART reads can provide a shorter reply than the requested capacity.

Build and run the host tests with CMake 3.16+ and a C++11 compiler:

```sh
python tools/generate_script.py examples/sensor_read.json --max-bytes 8 --output build/generated_script.hpp
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`tools/generate_script.py examples/sensor_read.json --max-bytes 8 --output build/generated_script.hpp` writes a C++ header containing `peripheral_contract_generated::installScript(hal)`. The generator accepts at most 64 operations and checks all byte and buffer bounds before writing code. It has no third-party dependencies.

The kit verifies driver contracts and buffer handling, not electrical behavior, interrupts, timing, DMA, or a specific vendor HAL. Adapt the calls in a thin test seam around your platform API. The generated function plugs into ordinary CTest, Unity, Ceedling, or PlatformIO host tests. MIT licensed.
