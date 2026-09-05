import unittest
from copy import deepcopy
import json
from pathlib import Path

from diagnostic_model import DiagnosticRun, PATTERNS, TEST_PROFILE, validate_state


def profile() -> dict:
    contacts = [{"id": f"SW{i + 1}", "label": f"SW{i + 1}"} for i in range(21)]
    contacts.extend({"id": f"ENC{i + 1}_PUSH", "label": f"ENC{i + 1} clic"} for i in range(7))
    return {"id": "fixture", "contacts": contacts,
            "encoders": [{"id": f"ENC{i + 1}"} for i in range(7)],
            "combinations": [[0, 1, 4], [4, 5, 8]]}


def state() -> dict:
    return {"type": "state", "seq": 0, "ms": 0, "contacts": [False] * 28,
            "raw": [False] * 28, "edges": [0] * 28, "changes": [0] * 28,
            "encoders": [0] * 7, "invalid": [0] * 7, "positive": [0] * 7,
            "negative": [0] * 7, "a_edges": [0] * 7, "b_edges": [0] * 7,
            "ab": [3] * 7, "oled": True, "overflow": False}


class Bench:
    def __init__(self, sign: int = 1):
        metadata = {"unit": "TEST", "operator": "test", "pcb_revision": "fixture",
                    "inspection_passed": True, "electrical_passed": True, "profile_confirmed": True,
                    "measurements": "simulated test measurements", "power_notes": "simulation"}
        self.run = DiagnosticRun(profile(), metadata, {"protocol": 1, "profile": "fixture"})
        self.state = state()
        self.sign = sign
        self.tick()

    def tick(self, ms: int = 50) -> None:
        self.state["seq"] = (self.state["seq"] + 1) & 0xFFFFFFFF
        self.state["ms"] = (self.state["ms"] + ms) & 0xFFFFFFFF
        self.run.update(self.state)

    def advance(self, ms: int) -> None:
        for _ in range(ms // 50):
            self.tick()

    def contact(self, index: int, pressed: bool) -> None:
        if self.state["contacts"][index] != pressed:
            self.state["contacts"][index] = self.state["raw"][index] = pressed
            self.state["edges"][index] += 1
            self.state["changes"][index] += 1

    def rotate(self, sign: int) -> None:
        index = self.run.target_encoder
        cycles = TEST_PROFILE["encoder_cycles"]
        self.state["positive" if sign > 0 else "negative"][index] += cycles
        self.state["encoders"][index] += sign * cycles
        self.state["a_edges"][index] += cycles * 2
        self.state["b_edges"][index] += cycles * 2

    def action(self) -> None:
        run = self.run
        if run.phase in ("press", "release"):
            self.contact(run.step["contacts"][run.action_index], run.phase == "press")
        elif run.phase == "rotate":
            self.rotate(self.sign * (1 if run.rotation % 2 == 0 else -1))
        elif run.phase == "visual":
            run.pattern_ready(True)
        self.tick()
        if run.phase == "visual":
            run.confirm_visual(True)

    def until(self, predicate) -> None:
        for _ in range(20000):
            if predicate():
                return
            if self.run.finished:
                raise AssertionError(self.run.instruction)
            self.action()
        raise AssertionError("Test driver timed out")


class DiagnosticTests(unittest.TestCase):
    def test_actual_hardware_profile_supports_mixed_contact_groups(self):
        actual = json.loads((Path(__file__).resolve().parents[1] / "hardware_profile.json").read_text(encoding="utf-8"))
        bench = Bench()
        bench.run = DiagnosticRun(actual, bench.run.metadata, {"protocol": 1, "profile": actual["id"]})
        bench.tick()
        bench.until(lambda: bench.run.finished)
        self.assertFalse(bench.run.failed)
        self.assertEqual(bench.run.progress, (49, 49))

    def test_full_run_requires_all_physical_and_visual_steps(self):
        bench = Bench()
        bench.until(lambda: bench.run.finished)
        report = bench.run.report()
        self.assertEqual(report["verdict"], "controles_reussis_profil_a_qualifier")
        self.assertEqual(bench.run.progress[0], bench.run.progress[1])
        self.assertEqual(sum(result.get("method") == "operator_visual"
                             for result in report["results"].values()), len(PATTERNS))
        self.assertFalse(report["test_profile"]["qualified_on_hardware"])

    def test_inverted_encoder_polarity_is_accepted(self):
        bench = Bench(sign=-1)
        bench.until(lambda: bench.run.finished)
        self.assertFalse(bench.run.failed)

    def test_missing_manual_preflight_never_passes(self):
        bench = Bench()
        bench.run.metadata["electrical_passed"] = False
        bench.until(lambda: bench.run.finished)
        self.assertEqual(bench.run.report()["verdict"], "incomplet")

    def test_no_states_or_partial_run_is_incomplete(self):
        bench = Bench()
        self.assertEqual(bench.run.report()["verdict"], "incomplet")
        bench.run.confirm_visual(True)
        self.assertEqual(bench.run.progress[0], 0)

    def test_raw_rebound_during_settle_is_recorded_and_accepted(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "press_settle")
        bench.state["edges"][0] += 4
        bench.tick()
        bench.until(lambda: bench.run.step["id"] == "SW2")
        self.assertEqual(bench.run.results["SW1"]["status"], "passed")
        self.assertEqual(bench.run.results["SW1"]["final_state"]["edges"][0], 8)

    def test_raw_glitch_during_hold_is_not_hidden_by_debounce(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "hold")
        bench.state["edges"][0] += 2
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_hidden_debounced_double_press_is_detected(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "press")
        bench.state["edges"][0] += 2
        bench.state["changes"][0] += 2
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_wrong_contact_and_coupled_contact_are_detected(self):
        for coupled in (False, True):
            with self.subTest(coupled=coupled):
                bench = Bench()
                bench.until(lambda: bench.run.phase == "press")
                bench.contact(1, True)
                if coupled:
                    bench.contact(0, True)
                bench.tick()
                self.assertTrue(bench.run.failed)

    def test_stuck_release_times_out_without_invented_cause(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "release")
        bench.advance(TEST_PROFILE["action_timeout_ms"] + 100)
        self.assertTrue(bench.run.failed)
        self.assertIn("non observée", bench.run.reason)

    def test_noise_at_rest_is_detected_even_if_net_count_is_zero(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "idle")
        bench.state["a_edges"][3] += 2
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_impossible_encoder_transition_fails(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "rotate")
        bench.state["invalid"][0] += 1
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_mixed_direction_cannot_cancel_out(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "rotate")
        bench.rotate(1)
        bench.rotate(-1)
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_encoder_must_reverse_after_first_rotation(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "rotate" and bench.run.rotation == 1)
        bench.rotate(1)
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_encoder_drift_during_rest_fails(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "encoder_rest")
        bench.state["b_edges"][0] += 2
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_group_detects_ghost_outside_requested_keys(self):
        bench = Bench()
        bench.until(lambda: bench.run.step["id"] == "matrix_1")
        bench.contact(0, True)
        bench.contact(2, True)
        bench.tick()
        self.assertTrue(bench.run.failed)

    def test_visual_confirmation_requires_ack_and_subsequent_state(self):
        bench = Bench()
        bench.until(lambda: bench.run.phase == "visual")
        current = bench.run.index
        bench.run.confirm_visual(True)
        self.assertEqual(bench.run.index, current)
        bench.run.pattern_ready(True)
        bench.run.confirm_visual(True)
        self.assertEqual(bench.run.index, current)
        bench.tick()
        bench.run.confirm_visual(True)
        self.assertEqual(bench.run.index, current + 1)

    def test_oled_send_failure_and_visual_failure_are_preserved(self):
        for send_failure in (True, False):
            with self.subTest(send_failure=send_failure):
                bench = Bench()
                bench.until(lambda: bench.run.phase == "visual")
                bench.run.pattern_ready(not send_failure)
                bench.tick()
                if not send_failure:
                    bench.run.confirm_visual(False)
                self.assertTrue(bench.run.failed)
                bench.run.confirm_visual(True)
                self.assertEqual(bench.run.report()["verdict"], "non_conforme")

    def test_sequence_loss_duplicate_restart_and_overflow_interrupt(self):
        for fault in ("lost", "duplicate", "restart", "overflow", "counter_reset"):
            with self.subTest(fault=fault):
                bench = Bench()
                bench.until(lambda: bench.run.phase == "hold")
                if fault == "lost":
                    bench.state["seq"] += 1
                elif fault == "duplicate":
                    bench.state["seq"] -= 1
                elif fault == "restart":
                    bench.state["ms"] = 0
                elif fault == "overflow":
                    bench.state["overflow"] = True
                else:
                    bench.state["edges"][0] = 0
                bench.tick()
                self.assertTrue(bench.run.interrupted)
                self.assertEqual(bench.run.report()["verdict"], "incomplet")

    def test_clock_wrap_is_not_a_restart(self):
        bench = Bench()
        bench.run.last_state["ms"] = 0xFFFFFFF0
        bench.state["ms"] = 0xFFFFFFF0
        bench.tick()
        self.assertFalse(bench.run.interrupted)

    def test_malformed_arrays_types_missing_fields_cannot_pass(self):
        for key, value in (("contacts", [False] * 27), ("seq", True), ("oled", "true"),
                           ("positive", [-1] * 7), ("ab", [4] * 7), ("raw", [0] * 28)):
            with self.subTest(key=key):
                frame = state()
                frame[key] = value
                with self.assertRaises(ValueError):
                    validate_state(frame)
                bench = Bench()
                bench.run.update(frame)
                self.assertTrue(bench.run.interrupted)
        frame = state()
        del frame["changes"]
        with self.assertRaises(ValueError):
            validate_state(frame)

    def test_report_is_an_independent_snapshot(self):
        bench = Bench()
        report = bench.run.report()
        bench.run.metadata["unit"] = "CHANGED"
        self.assertEqual(report["metadata"]["unit"], "TEST")
        saved = deepcopy(bench.run.report())
        bench.run.interrupt("USB déconnecté")
        self.assertNotEqual(saved["results"], bench.run.report()["results"])


if __name__ == "__main__":
    unittest.main()
