import json
from pathlib import Path
import tempfile
import time
import tkinter as tk
import unittest
from unittest.mock import patch

from amen_diagnostic import DiagnosticApp
from test_diagnostic_model import Bench, profile, state


class FakePort:
    def __init__(self):
        self.sent = []
        self.incoming = bytearray()
        self.closed = False
        self.partial_write = False

    @property
    def in_waiting(self):
        return len(self.incoming)

    def write(self, data):
        self.sent.append(json.loads(data))
        return len(data) - int(self.partial_write)

    def read(self, count):
        result = bytes(self.incoming[:count])
        del self.incoming[:count]
        return result

    def reset_input_buffer(self):
        self.incoming.clear()

    def close(self):
        self.closed = True


class InterfaceTests(unittest.TestCase):
    def setUp(self):
        self.root = tk.Tk()
        self.root.withdraw()
        fixture = profile()
        for i, contact in enumerate(fixture["contacts"]):
            contact.update(x=(i % 7) * 20, y=(i // 7) * 20)
        for i, encoder in enumerate(fixture["encoders"]):
            encoder.update(label=encoder["id"], x=i * 20, y=-20, contact_index=21 + i)
        with patch.object(DiagnosticApp, "refresh_ports"):
            self.app = DiagnosticApp(self.root, fixture)
        self.port = FakePort()
        self.app.port = self.port
        self.errors = patch("amen_diagnostic.messagebox.showerror").start()
        self.addCleanup(patch.stopall)

    def tearDown(self):
        self.app.run = None
        self.app.close()

    def hello(self):
        self.app.send("hello")
        self.app.receive({"type": "hello", "id": self.app.command_id,
                          "protocol": 1, "firmware": "test", "profile": "fixture", "boot": 123})

    def start(self):
        self.hello()
        self.app.start_metadata = Bench().run.metadata
        self.app.send("start")
        self.app.receive({"type": "ack", "id": self.app.command_id, "ok": True})

    def test_virgin_device_silence_is_not_reported_as_bad_hardware(self):
        self.app.send("hello")
        identifier = self.app.command_id
        command, _, fields = self.app.pending[identifier]
        self.app.pending[identifier] = (command, time.monotonic() - 1, fields)
        self.app.tick()
        self.assertTrue(self.port.closed)
        self.assertIn("aucune conclusion", self.app.status.get())
        self.assertIsNone(self.app.run)

    def test_wrong_signature_never_connects(self):
        self.app.send("hello")
        with self.assertRaises(ValueError):
            self.app.receive({"type": "hello", "id": self.app.command_id, "protocol": 1,
                              "firmware": "test", "profile": "other_board", "boot": 123})
        self.assertIsNone(self.app.hello)

    def test_old_state_cannot_start_or_validate_a_new_run(self):
        self.hello()
        self.app.start_metadata = Bench().run.metadata
        self.app.send("start")
        self.app.receive(state())
        self.assertIsNone(self.app.run)
        self.app.receive({"type": "ack", "id": self.app.command_id, "ok": True})
        self.assertEqual(self.app.run.phase, "waiting_state")
        self.app.receive(state())
        self.assertNotEqual(self.app.run.phase, "waiting_state")
        self.assertEqual(self.app.run.progress[0], 0)

    def test_duplicate_ack_is_rejected(self):
        self.hello()
        with self.assertRaises(ValueError):
            self.app.receive({"type": "hello", "id": self.app.command_id, "protocol": 1,
                              "firmware": "test", "profile": "fixture", "boot": 123})

    def test_usb_removal_interrupts_and_retains_report(self):
        self.start()
        self.app.receive(state())
        self.app.disconnect("USB retiré")
        self.assertTrue(self.app.run.interrupted)
        self.assertEqual(self.app.run.report()["verdict"], "incomplet")
        self.assertIsNone(self.app.state)

    def test_partial_serial_write_interrupts(self):
        self.start()
        self.port.partial_write = True
        self.app.send("snapshot")
        self.assertTrue(self.port.closed)
        self.assertTrue(self.app.run.interrupted)

    def test_fragmented_json_does_not_parse_early(self):
        self.start()
        data = (json.dumps(state()) + "\n").encode()
        self.port.incoming.extend(data[:100])
        self.app.tick()
        self.assertEqual(self.app.run.phase, "waiting_state")
        self.port.incoming.extend(data[100:])
        self.app.tick()
        self.assertNotEqual(self.app.run.phase, "waiting_state")

    def test_malformed_frame_disconnects_without_success(self):
        self.start()
        self.port.incoming.extend(b'{"type":"state","contacts":[]}\n')
        self.app.tick()
        self.assertTrue(self.app.run.interrupted)
        self.assertTrue(self.port.closed)

    def test_overflow_keeps_connection_available_for_reset(self):
        self.hello()
        broken = state()
        broken["overflow"] = True
        self.app.receive(broken)
        self.assertIs(self.app.port, self.port)
        self.assertIsNone(self.app.run)
        self.app.start_metadata = Bench().run.metadata
        self.app.send("start")
        self.app.receive({"type": "ack", "id": self.app.command_id, "ok": True})
        self.app.receive(state())
        self.assertFalse(self.app.run.interrupted)

    def test_overflow_during_run_invalidates_measurements_without_hiding_inputs(self):
        self.start()
        self.app.receive(state())
        broken = state()
        broken.update(seq=1, ms=50, overflow=True)
        self.app.receive(broken)
        self.assertTrue(self.app.run.interrupted)
        self.assertIs(self.app.port, self.port)
        self.assertTrue(self.app.state["overflow"])

    def test_free_oled_failure_does_not_disable_contact_observation(self):
        self.hello()
        self.app.free = True
        self.app.free_pattern_name.set("Blanc intégral")
        self.app.select_free_pattern()
        self.assertEqual(self.port.sent[-1]["pattern"], "white")
        self.app.receive({"type": "ack", "id": self.app.command_id, "ok": False})
        frame = state()
        frame["contacts"][0] = frame["raw"][0] = True
        self.app.receive(frame)
        self.assertIsNone(self.app.run)
        self.assertIs(self.app.port, self.port)
        self.assertTrue(self.app.state["contacts"][0])

    def test_ui_accepts_uint32_clock_and_sequence_wrap(self):
        self.hello()
        frame = state()
        frame.update(seq=0xFFFFFFFF, ms=0xFFFFFFF0)
        self.app.receive(frame)
        frame = state()
        frame.update(seq=0, ms=34)
        self.app.receive(frame)
        self.assertEqual(self.app.last_seq, 0)

    def test_visual_ack_cannot_replace_human_confirmation(self):
        self.hello()
        bench = Bench()
        bench.until(lambda: bench.run.phase == "visual")
        self.app.run = bench.run
        self.app.refresh_view()
        self.app.receive({"type": "ack", "id": self.app.command_id, "ok": True})
        before = bench.run.index
        self.app.confirm_visual(True)
        self.assertEqual(bench.run.index, before)
        bench.state["seq"] += 1
        bench.state["ms"] += 50
        self.app.receive(bench.state)
        self.assertEqual(bench.run.index, before)
        self.app.confirm_visual(True)
        self.assertEqual(bench.run.index, before + 1)

    def test_reports_cannot_overwrite_previous_unit_or_erase_failure(self):
        self.start()
        self.app.run.fail("Contact bloqué")
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "unit.json"
            self.assertTrue(self.app.save_report(target))
            contents = target.read_bytes()
            self.assertEqual(json.loads(contents)["verdict"], "non_conforme")
            self.assertFalse(self.app.save_report(target))
            self.assertEqual(target.read_bytes(), contents)
            self.assertIsNotNone(self.app.run)
        self.errors.assert_called_once()

    def test_failed_save_keeps_run_in_memory(self):
        self.start()
        self.app.run.fail("Contact bloqué")
        with tempfile.TemporaryDirectory() as folder:
            self.assertFalse(self.app.save_report(Path(folder) / "absent" / "unit.json"))
        self.assertFalse(self.app.saved)
        self.assertTrue(self.app.run.failed)

    def test_preflight_requires_three_actual_measurements(self):
        self.app.fields["3V3/GND"].set("10 kohm")
        self.app.fields["VIN/GND"].set("20 kohm")
        self.assertEqual(self.app.metadata()["measurements"], "")
        self.app.fields["3V3/VIN"].set("30 kohm")
        self.assertIn("3V3/VIN : 30 kohm", self.app.metadata()["measurements"])


if __name__ == "__main__":
    unittest.main()
