#ifndef PERIPH_CONTRACT_SCRIPTED_HAL_HPP
#define PERIPH_CONTRACT_SCRIPTED_HAL_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace periph_contract {

enum class Status { Ok, IoError, Timeout };
enum class PinMode : std::uint8_t { Input = 0, InputPullup = 1, Output = 2 };
enum class Failure {
    None,
    ScriptFull,
    InvalidBuffer,
    InvalidExpectation,
    UnexpectedCall,
    WrongOperation,
    WrongEndpoint,
    WrongConfiguration,
    WrongTimeout,
    WrongWriteLength,
    WrongWriteData,
    WrongReadLength,
    ResponseBufferTooSmall,
    ExpectedOperationsRemaining,
};

enum class OperationKind : std::uint8_t {
    PinMode,
    DigitalWrite,
    DigitalRead,
    I2cTransfer,
    SpiTransfer,
    UartWrite,
    UartRead,
};

template <std::size_t MaxOperations, std::size_t MaxBytes>
class ScriptedHal {
    static_assert(MaxOperations > 0, "MaxOperations must be positive");
    static_assert(MaxBytes > 0, "MaxBytes must be positive");

    struct Operation {
        OperationKind kind;
        std::uint16_t endpoint;
        std::uint32_t configuration;
        std::uint32_t timeoutMs;
        std::size_t writeLength;
        std::size_t requestedReadLength;
        std::size_t responseLength;
        Status result;
        std::uint8_t writeData[MaxBytes];
        std::uint8_t responseData[MaxBytes];

        Operation()
            : kind(OperationKind::PinMode), endpoint(0), configuration(0), timeoutMs(0),
              writeLength(0), requestedReadLength(0), responseLength(0), result(Status::Ok),
              writeData(), responseData() {}
    };

public:
    ScriptedHal() : operations_(), operationCount_(0), cursor_(0), failure_(Failure::None), failureIndex_(0) {}

    void reset() {
        operationCount_ = 0;
        cursor_ = 0;
        failure_ = Failure::None;
        failureIndex_ = 0;
    }

    bool expectPinMode(std::uint16_t pin, PinMode mode, Status result = Status::Ok) {
        return push(OperationKind::PinMode, pin, static_cast<std::uint32_t>(mode), 0,
                    nullptr, 0, 0, nullptr, 0, result);
    }

    bool expectDigitalWrite(std::uint16_t pin, bool value, Status result = Status::Ok) {
        const std::uint8_t byte = value ? 1 : 0;
        return push(OperationKind::DigitalWrite, pin, 0, 0, &byte, 1, 0, nullptr, 0, result);
    }

    bool expectDigitalRead(std::uint16_t pin, bool value, Status result = Status::Ok) {
        const std::uint8_t byte = value ? 1 : 0;
        return push(OperationKind::DigitalRead, pin, 0, 0, nullptr, 0, 1, &byte, 1, result);
    }

    bool expectI2c(std::uint16_t address, const std::uint8_t* writeData, std::size_t writeLength,
                   const std::uint8_t* responseData, std::size_t responseLength,
                   std::uint32_t timeoutMs = 0, Status result = Status::Ok) {
        return push(OperationKind::I2cTransfer, address, 0, timeoutMs, writeData, writeLength,
                    responseLength, responseData, result == Status::Ok ? responseLength : 0, result);
    }

    bool expectI2cFailure(std::uint16_t address, const std::uint8_t* writeData, std::size_t writeLength,
                          std::size_t requestedReadLength, std::uint32_t timeoutMs, Status result) {
        if (result == Status::Ok) return failBool(Failure::InvalidExpectation);
        return push(OperationKind::I2cTransfer, address, 0, timeoutMs, writeData, writeLength,
                    requestedReadLength, nullptr, 0, result);
    }

    bool expectSpi(std::uint16_t device, std::uint32_t configuration,
                   const std::uint8_t* writeData, std::size_t writeLength,
                   const std::uint8_t* responseData, std::size_t responseLength,
                   Status result = Status::Ok) {
        return push(OperationKind::SpiTransfer, device, configuration, 0, writeData, writeLength,
                    responseLength, responseData, result == Status::Ok ? responseLength : 0, result);
    }

    bool expectSpiFailure(std::uint16_t device, std::uint32_t configuration,
                          const std::uint8_t* writeData, std::size_t writeLength,
                          std::size_t requestedReadLength, Status result) {
        if (result == Status::Ok) return failBool(Failure::InvalidExpectation);
        return push(OperationKind::SpiTransfer, device, configuration, 0, writeData, writeLength,
                    requestedReadLength, nullptr, 0, result);
    }

    bool expectUartWrite(std::uint16_t port, const std::uint8_t* writeData, std::size_t writeLength,
                         Status result = Status::Ok) {
        return push(OperationKind::UartWrite, port, 0, 0, writeData, writeLength, 0, nullptr, 0, result);
    }

    bool expectUartRead(std::uint16_t port, std::size_t requestedLength,
                        const std::uint8_t* responseData, std::size_t responseLength,
                        std::uint32_t timeoutMs, Status result = Status::Ok) {
        return push(OperationKind::UartRead, port, 0, timeoutMs, nullptr, 0, requestedLength,
                    responseData, result == Status::Ok ? responseLength : 0, result);
    }

    Status pinMode(std::uint16_t pin, PinMode mode) {
        return run(OperationKind::PinMode, pin, static_cast<std::uint32_t>(mode), 0,
                   nullptr, 0, 0, nullptr, 0, nullptr);
    }

    Status digitalWrite(std::uint16_t pin, bool value) {
        const std::uint8_t byte = value ? 1 : 0;
        return run(OperationKind::DigitalWrite, pin, 0, 0, &byte, 1, 0, nullptr, 0, nullptr);
    }

    Status digitalRead(std::uint16_t pin, bool& value) {
        std::uint8_t byte = 0;
        std::size_t received = 0;
        const Status result = run(OperationKind::DigitalRead, pin, 0, 0, nullptr, 0, 1, &byte, 1, &received);
        if (result == Status::Ok && received == 1) {
            if (byte > 1) return fail(Failure::InvalidExpectation);
            value = byte != 0;
        }
        return result;
    }

    Status i2cTransfer(std::uint16_t address, const std::uint8_t* writeData, std::size_t writeLength,
                       std::uint8_t* responseData, std::size_t responseLength, std::uint32_t timeoutMs = 0) {
        return run(OperationKind::I2cTransfer, address, 0, timeoutMs, writeData, writeLength,
                   responseLength, responseData, responseLength, nullptr);
    }

    Status spiTransfer(std::uint16_t device, std::uint32_t configuration,
                       const std::uint8_t* writeData, std::size_t writeLength,
                       std::uint8_t* responseData, std::size_t responseLength) {
        return run(OperationKind::SpiTransfer, device, configuration, 0, writeData, writeLength,
                   responseLength, responseData, responseLength, nullptr);
    }

    Status uartWrite(std::uint16_t port, const std::uint8_t* writeData, std::size_t writeLength) {
        return run(OperationKind::UartWrite, port, 0, 0, writeData, writeLength, 0, nullptr, 0, nullptr);
    }

    Status uartRead(std::uint16_t port, std::uint8_t* responseData, std::size_t capacity,
                    std::uint32_t timeoutMs, std::size_t& received) {
        received = 0;
        return run(OperationKind::UartRead, port, 0, timeoutMs, nullptr, 0, capacity,
                   responseData, capacity, &received);
    }

    bool assertComplete() {
        if (failure_ != Failure::None) return false;
        if (cursor_ != operationCount_) return failBool(Failure::ExpectedOperationsRemaining);
        return true;
    }

    Failure failure() const { return failure_; }
    std::size_t failureIndex() const { return failureIndex_; }
    std::size_t expectedCount() const { return operationCount_; }
    std::size_t consumedCount() const { return cursor_; }

private:
    Operation operations_[MaxOperations];
    std::size_t operationCount_;
    std::size_t cursor_;
    Failure failure_;
    std::size_t failureIndex_;

    bool validBuffer(const std::uint8_t* data, std::size_t length) const {
        return length <= MaxBytes && (length == 0 || data != nullptr);
    }

    bool push(OperationKind kind, std::uint16_t endpoint, std::uint32_t configuration,
              std::uint32_t timeoutMs, const std::uint8_t* writeData, std::size_t writeLength,
              std::size_t requestedReadLength, const std::uint8_t* responseData,
              std::size_t responseLength, Status result) {
        if (failure_ != Failure::None) return false;
        if (operationCount_ >= MaxOperations) return failBool(Failure::ScriptFull);
        if (!validBuffer(writeData, writeLength) || !validBuffer(responseData, responseLength)
            || requestedReadLength > MaxBytes || responseLength > requestedReadLength
            || (result != Status::Ok && responseLength != 0)) {
            return failBool(Failure::InvalidExpectation);
        }
        Operation& operation = operations_[operationCount_++];
        operation.kind = kind;
        operation.endpoint = endpoint;
        operation.configuration = configuration;
        operation.timeoutMs = timeoutMs;
        operation.writeLength = writeLength;
        operation.requestedReadLength = requestedReadLength;
        operation.responseLength = responseLength;
        operation.result = result;
        if (writeLength != 0) std::memcpy(operation.writeData, writeData, writeLength);
        if (responseLength != 0) std::memcpy(operation.responseData, responseData, responseLength);
        return true;
    }

    Status run(OperationKind kind, std::uint16_t endpoint, std::uint32_t configuration,
               std::uint32_t timeoutMs, const std::uint8_t* writeData, std::size_t writeLength,
               std::size_t requestedReadLength, std::uint8_t* responseData,
               std::size_t responseCapacity, std::size_t* received) {
        if (received != nullptr) *received = 0;
        if (failure_ != Failure::None) return Status::IoError;
        if (!validBuffer(writeData, writeLength) || responseCapacity > MaxBytes
            || requestedReadLength > MaxBytes || (responseCapacity != 0 && responseData == nullptr)) {
            return fail(Failure::InvalidBuffer);
        }
        if (cursor_ >= operationCount_) return fail(Failure::UnexpectedCall);
        const Operation& expected = operations_[cursor_];
        if (kind != expected.kind) return fail(Failure::WrongOperation);
        if (endpoint != expected.endpoint) return fail(Failure::WrongEndpoint);
        if (configuration != expected.configuration) return fail(Failure::WrongConfiguration);
        if (timeoutMs != expected.timeoutMs) return fail(Failure::WrongTimeout);
        if (writeLength != expected.writeLength) return fail(Failure::WrongWriteLength);
        if (requestedReadLength != expected.requestedReadLength) return fail(Failure::WrongReadLength);
        if (writeLength != 0 && std::memcmp(writeData, expected.writeData, writeLength) != 0) {
            return fail(Failure::WrongWriteData);
        }
        if (expected.responseLength > responseCapacity) return fail(Failure::ResponseBufferTooSmall);
        ++cursor_;
        if (expected.result != Status::Ok) return expected.result;
        if (expected.responseLength != 0) {
            std::memcpy(responseData, expected.responseData, expected.responseLength);
        }
        if (received != nullptr) *received = expected.responseLength;
        return Status::Ok;
    }

    Status fail(Failure reason) {
        if (failure_ == Failure::None) {
            failure_ = reason;
            failureIndex_ = cursor_;
        }
        return Status::IoError;
    }

    bool failBool(Failure reason) {
        fail(reason);
        return false;
    }
};

}  // namespace periph_contract

#endif
