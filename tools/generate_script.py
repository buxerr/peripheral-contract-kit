#!/usr/bin/env python3
"""Compile bounded JSON peripheral scenarios into a ScriptedHal setup function."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


class ScenarioError(ValueError):
    pass


_OPERATIONS = {
    "pin_mode": {"op", "pin", "mode", "status"},
    "digital_write": {"op", "pin", "value", "status"},
    "digital_read": {"op", "pin", "value", "status"},
    "i2c": {"op", "address", "write", "read", "read_length", "timeout_ms", "status"},
    "spi": {"op", "device", "configuration", "write", "read", "read_length", "status"},
    "uart_write": {"op", "port", "write", "status"},
    "uart_read": {"op", "port", "capacity", "read", "timeout_ms", "status"},
}
_STATUS = {"ok": "Ok", "io_error": "IoError", "timeout": "Timeout"}
_PIN_MODE = {"input": "Input", "input_pullup": "InputPullup", "output": "Output"}
_MAX_OPERATIONS = 64


def _integer(value: Any, name: str, maximum: int) -> int:
    if type(value) is not int or not 0 <= value <= maximum:
        raise ScenarioError(f"{name} must be an integer from 0 to {maximum}")
    return value


def _bytes(value: Any, name: str, max_bytes: int) -> list[int]:
    if not isinstance(value, list) or len(value) > max_bytes:
        raise ScenarioError(f"{name} must be a byte array of at most {max_bytes} values")
    return [_integer(item, name + " byte", 255) for item in value]


def _status(operation: dict[str, Any]) -> str:
    value = operation.get("status", "ok")
    if not isinstance(value, str) or value not in _STATUS:
        raise ScenarioError("status must be ok, io_error, or timeout")
    return "periph_contract::Status::" + _STATUS[value]


def validate_scenario(value: Any, max_bytes: int) -> list[dict[str, Any]]:
    if not isinstance(value, dict) or set(value) != {"operations"}:
        raise ScenarioError("scenario must contain exactly an operations array")
    operations = value["operations"]
    if not isinstance(operations, list) or not 1 <= len(operations) <= _MAX_OPERATIONS:
        raise ScenarioError(f"operations must contain from 1 to {_MAX_OPERATIONS} entries")
    checked = []
    for index, raw in enumerate(operations):
        if not isinstance(raw, dict) or not isinstance(raw.get("op"), str) or raw["op"] not in _OPERATIONS:
            raise ScenarioError(f"operation {index} has an unsupported op")
        kind = raw["op"]
        extra = set(raw) - _OPERATIONS[kind]
        optional = {"status"}
        if kind in {"i2c", "spi"}:
            optional.add("read_length")
        required = _OPERATIONS[kind] - optional
        missing = required - set(raw)
        if extra or missing:
            raise ScenarioError(f"operation {index} fields differ; missing={sorted(missing)}, unknown={sorted(extra)}")
        item = dict(raw)
        item["status_cpp"] = _status(item)
        if kind in {"pin_mode", "digital_write", "digital_read"}:
            item["pin"] = _integer(raw["pin"], f"operation {index} pin", 65535)
        if kind == "pin_mode":
            if raw["mode"] not in _PIN_MODE:
                raise ScenarioError(f"operation {index} mode must be input, input_pullup, or output")
        elif kind in {"digital_write", "digital_read"}:
            if type(raw["value"]) is not bool:
                raise ScenarioError(f"operation {index} value must be boolean")
        elif kind == "i2c":
            item["address"] = _integer(raw["address"], f"operation {index} address", 1023)
            item["write"] = _bytes(raw["write"], f"operation {index} write", max_bytes)
            item["read"] = _bytes(raw["read"], f"operation {index} read", max_bytes)
            item["requested_read_length"] = _integer(raw.get("read_length", len(item["read"])),
                                                       f"operation {index} read_length", max_bytes)
            item["timeout_ms"] = _integer(raw["timeout_ms"], f"operation {index} timeout_ms", 4_294_967_295)
            if item["status_cpp"] == "periph_contract::Status::Ok" and item["requested_read_length"] != len(item["read"]):
                raise ScenarioError(f"operation {index} successful I2C response length must match read_length")
        elif kind == "spi":
            item["device"] = _integer(raw["device"], f"operation {index} device", 65535)
            item["configuration"] = _integer(raw["configuration"], f"operation {index} configuration", 4_294_967_295)
            item["write"] = _bytes(raw["write"], f"operation {index} write", max_bytes)
            item["read"] = _bytes(raw["read"], f"operation {index} read", max_bytes)
            item["requested_read_length"] = _integer(raw.get("read_length", len(item["read"])),
                                                       f"operation {index} read_length", max_bytes)
            if item["status_cpp"] == "periph_contract::Status::Ok" and item["requested_read_length"] != len(item["read"]):
                raise ScenarioError(f"operation {index} successful SPI response length must match read_length")
        elif kind == "uart_write":
            item["port"] = _integer(raw["port"], f"operation {index} port", 65535)
            item["write"] = _bytes(raw["write"], f"operation {index} write", max_bytes)
        elif kind == "uart_read":
            item["port"] = _integer(raw["port"], f"operation {index} port", 65535)
            item["capacity"] = _integer(raw["capacity"], f"operation {index} capacity", max_bytes)
            item["read"] = _bytes(raw["read"], f"operation {index} read", max_bytes)
            item["timeout_ms"] = _integer(raw["timeout_ms"], f"operation {index} timeout_ms", 4_294_967_295)
            if len(item["read"]) > item["capacity"]:
                raise ScenarioError(f"operation {index} response exceeds UART read capacity")
        if item["status_cpp"] != "periph_contract::Status::Ok" and kind in {"i2c", "spi", "uart_read"} and item.get("read"):
            raise ScenarioError(f"operation {index} cannot provide read bytes with a non-ok status")
        checked.append(item)
    return checked


def _array(name: str, values: list[int]) -> list[str]:
    if not values:
        return [f"    const std::uint8_t* {name} = nullptr;", f"    const std::size_t {name}_length = 0U;"]
    body = ", ".join(f"0x{value:02X}U" for value in values)
    return [f"    const std::uint8_t {name}[] = {{{body}}};", f"    const std::size_t {name}_length = sizeof({name});"]


def generate_header(operations: list[dict[str, Any]]) -> str:
    lines = [
        "#pragma once",
        "",
        '#include "periph_contract/scripted_hal.hpp"',
        "",
        "namespace peripheral_contract_generated {",
        "",
        "template <typename Hal>",
        "bool installScript(Hal& hal) {",
    ]
    for index, item in enumerate(operations):
        kind = item["op"]
        if kind in {"i2c", "spi", "uart_write", "uart_read"}:
            for field in ("write", "read"):
                if field in item:
                    if field == "read" and kind in {"i2c", "spi"} and item["status_cpp"] != "periph_contract::Status::Ok":
                        continue
                    lines.extend(_array(f"operation_{index}_{field}", item[field]))
        result = item["status_cpp"]
        if kind == "pin_mode":
            call = f"hal.expectPinMode({item['pin']}U, periph_contract::PinMode::{_PIN_MODE[item['mode']]}, {result})"
        elif kind == "digital_write":
            value = "true" if item["value"] else "false"
            call = f"hal.expectDigitalWrite({item['pin']}U, {value}, {result})"
        elif kind == "digital_read":
            value = "true" if item["value"] else "false"
            call = f"hal.expectDigitalRead({item['pin']}U, {value}, {result})"
        elif kind == "i2c":
            if result == "periph_contract::Status::Ok":
                call = (f"hal.expectI2c({item['address']}U, operation_{index}_write, operation_{index}_write_length, "
                        f"operation_{index}_read, operation_{index}_read_length, {item['timeout_ms']}U, {result})")
            else:
                call = (f"hal.expectI2cFailure({item['address']}U, operation_{index}_write, operation_{index}_write_length, "
                        f"{item['requested_read_length']}U, {item['timeout_ms']}U, {result})")
        elif kind == "spi":
            if result == "periph_contract::Status::Ok":
                call = (f"hal.expectSpi({item['device']}U, {item['configuration']}U, operation_{index}_write, "
                        f"operation_{index}_write_length, operation_{index}_read, operation_{index}_read_length, {result})")
            else:
                call = (f"hal.expectSpiFailure({item['device']}U, {item['configuration']}U, operation_{index}_write, "
                        f"operation_{index}_write_length, {item['requested_read_length']}U, {result})")
        elif kind == "uart_write":
            call = f"hal.expectUartWrite({item['port']}U, operation_{index}_write, operation_{index}_write_length, {result})"
        else:
            call = (f"hal.expectUartRead({item['port']}U, {item['capacity']}U, operation_{index}_read, "
                    f"operation_{index}_read_length, {item['timeout_ms']}U, {result})")
        lines.append(f"    if (!({call})) return false;")
    lines.extend(("    return true;", "}", "", "}  // namespace peripheral_contract_generated", ""))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate a fixed-buffer ScriptedHal setup function from JSON.")
    parser.add_argument("scenario", type=Path, help="input scenario JSON")
    parser.add_argument("--output", type=Path, required=True, help="generated C++ header path")
    parser.add_argument("--max-bytes", type=int, default=64, help="maximum bytes allowed in one operation")
    args = parser.parse_args(argv)
    if not 1 <= args.max_bytes <= 4096:
        parser.error("--max-bytes must be from 1 to 4096")
    try:
        source = args.scenario.read_text(encoding="utf-8")
        parsed = json.loads(source, object_pairs_hook=_unique_object)
        operations = validate_scenario(parsed, args.max_bytes)
        output = generate_header(operations)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8", newline="\n")
        return 0
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ScenarioError) as error:
        parser.error(str(error))
    return 2


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ScenarioError("duplicate JSON key: " + key)
        result[key] = value
    return result


if __name__ == "__main__":
    raise SystemExit(main())
