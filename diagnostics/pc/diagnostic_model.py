from copy import deepcopy
from datetime import datetime, timezone
from uuid import uuid4


APP_VERSION = "1.0.0"
TEST_PROFILE = {
    "id": "assembly-bringup-1",
    "qualified_on_hardware": False,
    "idle_ms": 3000,
    "settle_ms": 150,
    "hold_ms": 1000,
    "release_ms": 600,
    "action_timeout_ms": 30000,
    "contact_repetitions": 2,
    "encoder_cycles": 8,
    "encoder_stop_ms": 1500,
    "encoder_rest_ms": 1500,
    "max_frame_gap_ms": 1000,
}
PATTERNS = ("black", "white", "checker", "inverse", "border", "rows", "columns")
PATTERN_LABELS = {
    "black": "noir intégral : aucun pixel allumé",
    "white": "blanc intégral : aucun pixel noir",
    "checker": "damier : cases de 4 × 4 pixels noirs et blancs",
    "inverse": "damier inversé : tous les pixels ont changé d’état",
    "border": "bordure : quatre côtés complets, intérieur noir",
    "rows": "lignes horizontales : alternance sur toute la largeur",
    "columns": "colonnes verticales : alternance sur toute la hauteur",
}
COUNTERS = ("edges", "changes", "invalid", "positive", "negative", "a_edges", "b_edges")


def diagnostic_steps(profile: dict) -> list[dict]:
    steps = [{"id": "idle", "kind": "idle"}]
    steps.extend({"id": item["id"], "kind": "contact", "contacts": [i]}
                 for i, item in enumerate(profile["contacts"]))
    steps.extend({"id": f"rotation_{item['id']}", "kind": "encoder", "encoder": i}
                 for i, item in enumerate(profile["encoders"]))
    for i, group in enumerate(profile.get("combinations", [])):
        if (not isinstance(group, list) or len(group) < 2 or len(set(group)) != len(group)
                or any(type(x) is not int or not 0 <= x < 28 for x in group)):
            raise ValueError("Combinaison de matrice invalide.")
        steps.append({"id": f"matrix_{i + 1}", "kind": "contact", "contacts": group})
    steps.extend({"id": f"oled_{pattern}", "kind": "oled", "pattern": pattern}
                 for pattern in PATTERNS)
    if len({step["id"] for step in steps}) != len(steps):
        raise ValueError("Identifiants de contrôles dupliqués.")
    return steps


def validate_state(state: dict) -> None:
    if not isinstance(state, dict) or state.get("type") != "state":
        raise ValueError("Trame d’état attendue.")
    for key in ("seq", "ms"):
        if type(state.get(key)) is not int or not 0 <= state[key] <= 0xFFFFFFFF:
            raise ValueError(f"Champ {key} invalide.")
    for key in ("oled", "overflow"):
        if type(state.get(key)) is not bool:
            raise ValueError(f"Champ {key} invalide.")
    lengths = {"contacts": 28, "raw": 28, "edges": 28, "changes": 28,
               "encoders": 7, "invalid": 7, "positive": 7, "negative": 7,
               "a_edges": 7, "b_edges": 7, "ab": 7}
    for key, length in lengths.items():
        values = state.get(key)
        if not isinstance(values, list) or len(values) != length:
            raise ValueError(f"Tableau {key} invalide ({length} valeurs attendues).")
        for value in values:
            if key in ("contacts", "raw"):
                valid = type(value) is bool
            elif key == "encoders":
                valid = type(value) is int and -0x80000000 <= value <= 0x7FFFFFFF
            else:
                valid = type(value) is int and 0 <= value <= (3 if key == "ab" else 0xFFFFFFFF)
            if not valid:
                raise ValueError(f"Valeur invalide dans {key}.")


class DiagnosticRun:
    def __init__(self, profile: dict, metadata: dict, hello: dict, start_index: int = 0):
        if len(profile.get("contacts", [])) != 28 or len(profile.get("encoders", [])) != 7:
            raise ValueError("Le profil doit décrire 28 contacts et 7 encodeurs.")
        if hello.get("protocol") != 1 or hello.get("profile") != profile.get("id"):
            raise ValueError("Version du protocole ou mapping incompatible.")
        self.profile = deepcopy(profile)
        self.metadata = deepcopy(metadata)
        self.hello = deepcopy(hello)
        self.session_id = str(uuid4())
        self.created_at = datetime.now(timezone.utc).isoformat()
        self.ended_at = None
        self.steps = diagnostic_steps(self.profile)
        if type(start_index) is not int or not 0 <= start_index < len(self.steps):
            raise ValueError("Checkpoint de départ invalide.")
        self.results = {step["id"]: {"status": "not_tested"} for step in self.steps}
        self.start_index = start_index
        self.index = start_index
        self.phase = "waiting_state"
        self.finished = False
        self.failed = False
        self.interrupted = False
        self.reason = ""
        self.elapsed = 0
        self.phase_start = 0
        self.last_state = None
        self.baseline = None
        self.step_start = None
        self.trace = []
        self.cycle = 0
        self.action_index = 0
        self.rotation = 0
        self.first_sign = 0
        self.pattern_acknowledged = False
        self.pattern_seen = False
        self.attempts = []

    def retry_current(self) -> None:
        if self.finished and not self.failed:
            raise ValueError("Ce parcours terminé ne peut pas être repris.")
        step_id = self.step["id"]
        self.attempts.append({"step": step_id, "result": deepcopy(self.results[step_id]),
                              "reason": self.reason or "Reprise demandée par l’opérateur",
                              "ended_at": self.ended_at})
        self.results[step_id] = {"status": "not_tested"}
        self.trace.append({"step": step_id, "phase": "retry",
                           "reason": self.reason or "Reprise demandée par l’opérateur"})
        self.ended_at = None
        self.phase = "waiting_state"
        self.finished = False
        self.failed = False
        self.interrupted = False
        self.reason = ""
        self.elapsed = 0
        self.phase_start = 0
        self.last_state = None
        self.baseline = None
        self.step_start = None
        self.cycle = 0
        self.action_index = 0
        self.rotation = 0
        self.first_sign = 0
        self.pattern_acknowledged = False
        self.pattern_seen = False

    @property
    def step(self) -> dict:
        return self.steps[min(self.index, len(self.steps) - 1)]

    @property
    def target_contacts(self) -> set[int]:
        return set() if self.finished else set(self.step.get("contacts", []))

    @property
    def target_encoder(self) -> int | None:
        return None if self.finished else self.step.get("encoder")

    @property
    def current_pattern(self) -> str | None:
        return None if self.finished else self.step.get("pattern")

    @property
    def progress(self) -> tuple[int, int]:
        return sum(result["status"] == "passed" for result in self.results.values()), len(self.steps)

    def _contact_label(self, index: int) -> str:
        return self.profile["contacts"][index].get("label", self.profile["contacts"][index]["id"])

    @property
    def instruction(self) -> str:
        if self.finished:
            if self.failed or self.interrupted:
                return self.reason + " — Recommencer une session après vérification."
            return "Contrôles terminés. Profil de mise au point à qualifier sur le matériel."
        if self.phase == "waiting_state":
            return "En attente de mesures fraîches après remise à zéro du diagnostic…"
        if self.step["kind"] == "idle":
            return "Relâche toutes les commandes, puis ne touche à rien pendant 3 secondes."
        if self.step["kind"] == "oled":
            prefix = "Vérifie visuellement la mire " if self.pattern_acknowledged else "Envoi de la mire "
            return prefix + PATTERN_LABELS[self.current_pattern] + "."
        if self.step["kind"] == "encoder":
            label = self.profile["encoders"][self.target_encoder]["id"]
            if self.phase == "rotate":
                speed = "lentement" if self.rotation < 2 else "plus rapidement"
                direction = "dans un seul sens au choix" if self.rotation == 0 else (
                    "dans le même sens que le premier essai" if self.rotation == 2
                    else "dans le sens opposé à l’essai précédent")
                return f"{label} : tourne {speed}, {direction}, jusqu’au signal d’arrêt."
            return f"{label} : arrête de tourner et lâche l’encodeur. Observation au repos…"
        group = self.step["contacts"]
        label = self._contact_label(group[min(self.action_index, len(group) - 1)])
        cycle = f"Essai {self.cycle + 1}/{self._repetitions()} — "
        if self.phase in ("press", "press_settle"):
            return cycle + f"Appuie sur {label} et garde-le enfoncé. Garde les touches déjà pressées."
        if self.phase == "hold":
            return cycle + "Garde les contacts enfoncés jusqu’à la consigne de relâchement."
        if self.phase in ("release", "release_settle"):
            return cycle + f"Relâche {label} seulement. Garde les autres touches demandées enfoncées."
        return cycle + "Toutes les touches relâchées : ne touche à rien…"

    def _repetitions(self) -> int:
        return TEST_PROFILE["contact_repetitions"] if len(self.step["contacts"]) == 1 else 1

    def _transition(self, phase: str, state: dict) -> None:
        self.phase = phase
        self.phase_start = self.elapsed
        self.baseline = deepcopy(state)
        self.trace.append({"step": self.step["id"], "phase": phase, "elapsed_ms": self.elapsed,
                           "state": deepcopy(state)})

    def _enter_step(self, state: dict) -> None:
        self.step_start = deepcopy(state)
        self.results[self.step["id"]] = {"status": "running", "start_ms": self.elapsed}
        self.cycle = self.action_index = self.rotation = self.first_sign = 0
        self.pattern_acknowledged = self.pattern_seen = False
        phase = {"idle": "neutral", "contact": "press", "encoder": "rotate", "oled": "visual"}
        self._transition(phase[self.step["kind"]], state)

    def _pass_step(self, state: dict, method: str = "measured") -> None:
        result = self.results[self.step["id"]]
        result.update(status="passed", end_ms=self.elapsed, method=method,
                      initial_state=self.step_start, final_state=deepcopy(state))
        self.index += 1
        if self.index == len(self.steps):
            self.finished = True
            self.phase = "complete"
            self.ended_at = datetime.now(timezone.utc).isoformat()
        else:
            self._enter_step(state)

    def fail(self, reason: str) -> None:
        if self.finished:
            return
        self.failed = self.finished = True
        self.reason = reason
        self.results[self.step["id"]].update(status="failed", reason=reason, end_ms=self.elapsed)
        self.phase = "failed"
        self.ended_at = datetime.now(timezone.utc).isoformat()

    def interrupt(self, reason: str) -> None:
        if self.finished:
            return
        self.interrupted = self.finished = True
        self.reason = reason
        self.results[self.step["id"]].update(status="interrupted", reason=reason, end_ms=self.elapsed)
        self.phase = "interrupted"
        self.ended_at = datetime.now(timezone.utc).isoformat()

    def _integrity(self, state: dict) -> bool:
        if state["overflow"]:
            self.interrupt("Acquisition incomplète : dépassement de cadence ou de tampon signalé")
            return False
        if self.last_state is None:
            return True
        previous = self.last_state
        gap = (state["ms"] - previous["ms"]) & 0xFFFFFFFF
        if state["seq"] != (previous["seq"] + 1) & 0xFFFFFFFF or gap > TEST_PROFILE["max_frame_gap_ms"]:
            self.interrupt("Trame perdue, retard excessif ou redémarrage détecté")
            return False
        for key in COUNTERS:
            if any(new < old for new, old in zip(state[key], previous[key])):
                self.interrupt("Compteurs remis à zéro pendant le contrôle")
                return False
        self.elapsed += gap
        return True

    def update(self, state: dict) -> None:
        if self.finished:
            return
        try:
            validate_state(state)
        except ValueError as error:
            self.trace.append({"phase": "invalid_frame", "state": deepcopy(state)})
            self.interrupt(str(error))
            return
        if not self._integrity(state):
            self.trace.append({"phase": "integrity_error", "state": deepcopy(state)})
            return
        first = self.last_state is None
        self.last_state = deepcopy(state)
        if first:
            self._enter_step(state)
        elapsed = self.elapsed - self.phase_start
        if self.step["kind"] != "oled" and elapsed > TEST_PROFILE["action_timeout_ms"]:
            self.fail("Action attendue non observée dans le délai prévu")
            return
        handler = {"idle": self._idle, "contact": self._contact,
                   "encoder": self._encoder, "oled": self._oled}
        handler[self.step["kind"]](state, elapsed)

    def _changed(self, state: dict, key: str, index: int) -> int:
        return state[key][index] - self.baseline[key][index]

    def _guard(self, state: dict, contacts: set[int] | None = None, encoder: int | None = None) -> bool:
        contacts = contacts or set()
        for i in range(28):
            if i not in contacts and (self._changed(state, "edges", i) or self._changed(state, "changes", i)
                                      or state["contacts"][i] != self.baseline["contacts"][i]
                                      or state["raw"][i] != self.baseline["raw"][i]):
                self.fail(f"Activité inattendue sur {self._contact_label(i)}")
                return False
        for i in range(7):
            if i != encoder and any(self._changed(state, key, i)
                                    for key in ("a_edges", "b_edges", "invalid", "positive", "negative")):
                self.fail(f"Activité inattendue sur ENC{i + 1}")
                return False
        return True

    def _idle(self, state: dict, elapsed: int) -> None:
        if self.phase == "neutral":
            if not any(state["contacts"]) and not any(state["raw"]):
                self._transition("idle_settle", state)
        elif self.phase == "idle_settle":
            if any(state["contacts"]) or any(state["raw"]):
                self._transition("neutral", state)
            elif elapsed >= TEST_PROFILE["settle_ms"]:
                self._transition("idle", state)
        elif self._guard(state) and elapsed >= TEST_PROFILE["idle_ms"]:
            self._pass_step(state)

    def _contact(self, state: dict, elapsed: int) -> None:
        group = self.step["contacts"]
        target = group[self.action_index]
        changing = self.phase in ("press", "press_settle", "release", "release_settle")
        if not self._guard(state, {target} if changing else set()):
            return
        if self.phase in ("press", "release"):
            pressed = self.phase == "press"
            changes = self._changed(state, "changes", target)
            if changes > 1 or (changes and state["contacts"][target] != pressed):
                self.fail(f"Activation supplémentaire ou impulsion brève sur {self._contact_label(target)}")
            elif state["contacts"][target] == pressed:
                if changes != 1:
                    self.interrupt("État de contact modifié sans transition comptabilisée")
                else:
                    self._transition(self.phase + "_settle", state)
        elif self.phase in ("press_settle", "release_settle"):
            pressed = self.phase == "press_settle"
            if state["contacts"][target] != pressed or self._changed(state, "changes", target):
                self.fail(f"Contact instable sur {self._contact_label(target)}")
            elif elapsed >= TEST_PROFILE["settle_ms"]:
                if state["raw"][target] != pressed:
                    self.fail(f"Contact brut instable sur {self._contact_label(target)}")
                elif self.action_index + 1 < len(group):
                    self.action_index += 1
                    self._transition("press" if pressed else "release", state)
                else:
                    self.action_index = 0
                    self._transition("hold" if pressed else "release_rest", state)
        elif self.phase == "hold" and elapsed >= TEST_PROFILE["hold_ms"]:
            self._transition("release", state)
        elif self.phase == "release_rest" and elapsed >= TEST_PROFILE["release_ms"]:
            self.cycle += 1
            if self.cycle < self._repetitions():
                self._transition("press", state)
            else:
                self._pass_step(state)

    def _encoder(self, state: dict, elapsed: int) -> None:
        index = self.target_encoder
        if not self._guard(state, encoder=index):
            return
        if self._changed(state, "invalid", index):
            self.fail(f"ENC{index + 1} : transition A/B incohérente ; vérifier contact et cadence de scan")
            return
        if self.phase == "rotate":
            positive = self._changed(state, "positive", index)
            negative = self._changed(state, "negative", index)
            if positive and negative:
                self.fail(f"ENC{index + 1} : cycles dans les deux sens pendant une rotation demandée continue")
                return
            sign = 1 if positive else (-1 if negative else 0)
            expected = self.first_sign * (1 if self.rotation % 2 == 0 else -1)
            if sign and expected and sign != expected:
                self.fail(f"ENC{index + 1} : inversion de rotation attendue non observée")
            elif positive + negative >= TEST_PROFILE["encoder_cycles"]:
                if min(self._changed(state, "a_edges", index), self._changed(state, "b_edges", index)) < 4:
                    self.interrupt("Cycles encodeur sans activité cohérente des deux contacts")
                    return
                if self.rotation == 0:
                    self.first_sign = sign
                self._transition("stop", state)
        elif self.phase == "stop" and elapsed >= TEST_PROFILE["encoder_stop_ms"]:
            self._transition("encoder_rest", state)
        elif self.phase == "encoder_rest":
            if any(self._changed(state, key, index) for key in ("a_edges", "b_edges", "positive", "negative")):
                self.fail(f"ENC{index + 1} : activité au repos ; ne plus toucher l’encodeur")
            elif elapsed >= TEST_PROFILE["encoder_rest_ms"]:
                self.rotation += 1
                if self.rotation == 4:
                    self._pass_step(state)
                else:
                    self._transition("rotate", state)

    def pattern_ready(self, ok: bool) -> None:
        if self.finished or self.step["kind"] != "oled":
            return
        if ok is not True:
            self.fail("OLED : l’envoi de la mire n’a pas été confirmé")
        else:
            self.pattern_acknowledged = True

    def _oled(self, state: dict, elapsed: int) -> None:
        if not self._guard(state):
            return
        if not state["oled"]:
            self.fail("OLED : communication absente ou défaillante")
        elif self.pattern_acknowledged:
            self.pattern_seen = True

    def confirm_visual(self, passed: bool) -> None:
        if (self.finished or self.step["kind"] != "oled" or not self.pattern_acknowledged
                or not self.pattern_seen or self.last_state is None):
            return
        if passed is True:
            self._pass_step(self.last_state, "operator_visual")
        else:
            self.fail("OLED : défaut visuel déclaré par l’opérateur")

    def report(self) -> dict:
        complete = all(value["status"] == "passed" for value in self.results.values())
        if self.failed:
            verdict = "non_conforme"
        elif self.interrupted or not complete:
            verdict = "incomplet"
        elif not TEST_PROFILE["qualified_on_hardware"]:
            verdict = "controles_reussis_profil_a_qualifier"
        else:
            verdict = "conforme_aux_controles_effectues"
        return deepcopy({
            "session_id": self.session_id, "created_at": self.created_at, "ended_at": self.ended_at,
            "application": APP_VERSION, "device": self.hello, "hardware_profile": self.profile,
            "test_profile": TEST_PROFILE, "metadata": self.metadata,
            "verdict": verdict, "scope": "Connexions physiques et inspection visuelle OLED uniquement",
            "qualification": "Seuils de mise au point non encore qualifiés sur un assemblage réel.",
            "results": self.results, "attempts": self.attempts, "reason": self.reason, "trace": self.trace,
            "last_state": self.last_state,
        })
