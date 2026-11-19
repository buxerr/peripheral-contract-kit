import json
from pathlib import Path
import tempfile
import unittest

from tools.generate_script import ScenarioError, generate_header, main, validate_scenario


class ScenarioGeneratorTests(unittest.TestCase):
    def test_generates_script_setup_for_bus_and_uart(self):
        operations = validate_scenario({"operations": [
            {"op": "i2c", "address": 72, "write": [16, 2], "read": [170], "timeout_ms": 25},
            {"op": "uart_read", "port": 1, "capacity": 8, "read": [85], "timeout_ms": 50},
        ]}, 8)
        source = generate_header(operations)
        self.assertIn("hal.expectI2c(72U", source)
        self.assertIn("0x10U, 0x02U", source)
        self.assertIn("hal.expectUartRead(1U, 8U", source)
        self.assertIn("namespace peripheral_contract_generated", source)

    def test_empty_payloads_use_null_pointer_and_non_ok_results_have_no_reply(self):
        operations = validate_scenario({"operations": [
            {"op": "uart_write", "port": 0, "write": []},
            {"op": "i2c", "address": 1, "write": [], "read": [], "timeout_ms": 2, "status": "timeout"},
        ]}, 4)
        source = generate_header(operations)
        self.assertIn("operation_0_write = nullptr", source)
        self.assertIn("periph_contract::Status::Timeout", source)

    def test_rejects_unknown_fields_invalid_bytes_and_too_small_uart_capacity(self):
        with self.assertRaisesRegex(ScenarioError, "unknown"):
            validate_scenario({"operations": [{"op": "digital_write", "pin": 1, "value": True, "oops": 1}]}, 8)
        with self.assertRaisesRegex(ScenarioError, "byte"):
            validate_scenario({"operations": [{"op": "uart_write", "port": 0, "write": [256]}]}, 8)
        with self.assertRaisesRegex(ScenarioError, "capacity"):
            validate_scenario({"operations": [{"op": "uart_read", "port": 0, "capacity": 1, "read": [1, 2], "timeout_ms": 1}]}, 8)

    def test_rejects_oversized_scripts_and_non_ok_read_data(self):
        with self.assertRaisesRegex(ScenarioError, "at most"):
            validate_scenario({"operations": [{"op": "uart_write", "port": 0, "write": [0, 1]}]}, 1)
        with self.assertRaisesRegex(ScenarioError, "non-ok"):
            validate_scenario({"operations": [{
                "op": "spi", "device": 1, "configuration": 0, "write": [], "read": [1], "status": "io_error",
            }]}, 4)
        failed_read = validate_scenario({"operations": [{
            "op": "i2c", "address": 32, "write": [], "read": [], "read_length": 2,
            "timeout_ms": 5, "status": "timeout",
        }]}, 4)
        self.assertIn("expectI2cFailure(32U", generate_header(failed_read))
        self.assertIn("2U, 5U, periph_contract::Status::Timeout", generate_header(failed_read))
        with self.assertRaisesRegex(ScenarioError, "64"):
            validate_scenario({"operations": [{"op": "digital_write", "pin": 1, "value": True}] * 65}, 4)

    def test_cli_writes_generated_header(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenario = root / "scenario.json"
            output = root / "generated.hpp"
            scenario.write_text(json.dumps({"operations": [{"op": "pin_mode", "pin": 2, "mode": "input"}]}))
            self.assertEqual(main([str(scenario), "--output", str(output)]), 0)
            self.assertIn("expectPinMode(2U", output.read_text())


if __name__ == "__main__":
    unittest.main()
