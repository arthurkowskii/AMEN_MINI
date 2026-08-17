# AMEN MIDI — Contrôleur harmonique portable — Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Construire un second firmware pour le PCB AMEN_MINI qui transforme les 12 pads du bas en surface mélodique/harmonique et les 8 pads du haut en slots d’algorithmes MIDI assignables, afin de composer loin d’un clavier 88 touches et de piloter aussi bien des synthétiseurs comme Serum/Falcon que des banques orchestrales.

**Architecture:** Le firmware MIDI est un produit frère du sampler, dans `firmware_midi/`, afin de conserver le même PCB, la même grammaire de contrôle et les mêmes conventions de test sans mélanger moteur audio et moteur MIDI. Le cœur C++17 reste portable et déterministe : état musical → notes/accords → algorithme temporel → événements MIDI. La couche Teensy ne fait que lire les contrôles, afficher l’OLED et émettre les événements par USB-MIDI.

**Tech Stack:** C++17 portable, Teensy 4.1 / Teensyduino via `arduino-cli`, USB-MIDI natif Teensy, OLED SSD1306 128×32, matrice 5×5, 7 encodeurs poussoirs, tests natifs `g++` avec horloge simulée.

---

## 1. Vision produit

### Identité

AMEN MIDI n’est pas un clavier miniature. C’est une **surface de composition MIDI harmonique portable**, indépendante de l’instrument piloté :

```text
CHOISIR tonalité/gamme
→ JOUER une ligne ou des accords sur une géographie stable
→ TENIR un pad FX pour appliquer un geste musical
→ MODELER dynamique, voicing et tempo
→ ENREGISTRER le MIDI dans le DAW
```

Le geste reconnaissable de la démo doit être :

```text
même grille → lead Serum → accords Falcon → run de bois → brass staccato en arpège
```

### Ce que l’on emprunte aux références modernes

- **Nopia — adapter :** conserver une géographie stable dans toutes les tonalités, fondée sur les degrés, plutôt que déplacer mentalement les notes à chaque changement de gamme. Nopia revendique précisément une disposition identique dans toutes les tonalités, un cadre chromatique fondé sur les degrés, un module ARP et un geste STRUM.[3]
- **HiChord — adapter :** rendre les degrés diatoniques immédiatement jouables, puis proposer inversion, voice leading et modes de jeu comme transformations. Son manuel présente sept boutons d’accords diatoniques, les inversions et un voice leading qui minimise le déplacement des voix.[2]
- **AKT-0.1 — emprunter le geste, pas l’interface :** son écran permet des modes direct/bimanuel/strum et jusqu’à quatre effets simultanés.[4] AMEN MIDI reprend l’idée d’un geste musical transformateur, mais l’adapte à huit pads physiques assignables et à l’OLED 128×32.

### Borrow / adapt / reject / defer

| Décision | Élément | Traduction AMEN MIDI |
|---|---|---|
| Borrow | Layout stable par degrés | Les 12 pads gardent toujours la même position fonctionnelle, quelle que soit la tonalité. |
| Adapt | Forme harmonique globale | E5 choisit librement `1/NOTE`, `SUS2`, `TRI`, `SUS4`, `6`, `7`, `9`, `11`, `13` ; les 12 pads gardent leurs degrés et appliquent immédiatement la forme choisie. |
| Adapt | Voicings intelligents | Chaque preset de gamme embarque des inversions préparées musicalement par degré et par forme d’accord. Le résultat est prévisible et ne demande aucune édition d’inversion pendant le jeu. |
| Adapt | Geste tenu | Comme AMEN : tenir un pad FX l’active et le désigne comme cible des encodeurs. |
| Reject | Moteur audio interne | Ce deuxième instrument est uniquement USB-MIDI. Aucun SGTL5000, SD audio ou DSP n’est initialisé. |
| Reject | Simulation de vélocité tactile | Les MX ne mesurent pas la force. La vélocité vient d’une valeur, d’une courbe ou d’un algorithme explicite. |
| Reject V1 | MPE/poly-aftertouch | Le hardware ne fournit ni vélocité ni pression ; cela complexifierait le protocole sans nouveau geste physique. |
| Defer | Séquenceur complet et multi-pistes | Après validation du jeu direct, des runs et de l’enregistrement MIDI propre. |
| Defer | Templates détaillés par librairie orchestrale | Commencer par MIDI générique, puis ajouter des profils CC/key-switch configurables. |

---

## 2. Périmètre fonctionnel

### P0 — première machine réellement jouable

1. USB-MIDI reconnu par Windows et un DAW.
2. 12 pads fondés sur une tonalité et une gamme ; leur sortie dépend de la forme harmonique librement sélectionnée avec E5.
3. Sélecteur global de forme : `1/NOTE`, `SUS2`, `TRI`, `SUS4`, `6`, `7`, `9`, `11`, `13` — liste exacte à verrouiller avec Arthur avant le code.
4. Voicings et inversions intelligentes déjà préparés dans chaque preset de gamme, par degré et par forme.
5. Note On/Off impeccables, même si l’état musical change pendant qu’une touche est tenue.
6. Panic matériel accessible à tout moment.
7. 8 pads FX assignables via un browser embarqué.
8. Algorithmes P0 : `BLANK`, `STRUM`, `ARP`, `RUN UP`, `RUN DOWN`, `TRANCE GATE`, `NOTE REPEAT`, `RANDOM` et `VELOCITY`.
9. Paramètres contextuels des FX : quantité, division, étendue, comportement gate/latch, chance/dynamique.
10. OLED lisible : tonalité/gamme, forme harmonique, accord réel, FX actif, paramètre et BPM.
11. Harness PC permettant de tester toute la surface sans le PCB.

### P1 — expression multi-instrument

1. `OSTINATO`, `RUN UP-DOWN`, patterns de vélocité (crescendo/decrescendo/accent), humanize borné.
2. Macro `DYNAMICS` configurable : CC1, CC11 ou paire CC1+CC11.
3. Profils de jeu sauvegardables : canal MIDI, vélocité, CC/macros, plage, key-switch éventuel et comportement de dynamique.
4. Éditeur avancé de presets de voicing et voice leading dynamique optionnel ; le P0 utilise les tables préparées.
5. Presets persistants des huit assignations et des paramètres.
6. MIDI Clock interne ou suivi d’une clock externe si le besoin est confirmé après le P0.

### P2 — après validation musicale

- Séquenceur/patterns, scènes multi-canaux, splits/layers, progressions mémorisées, harmonisation à plusieurs instruments, générateur de contrechants.

---

## 3. Grammaire de contrôle proposée

### Les 12 pads musicaux

Disposition physique inchangée :

```text
09  10  11  12
05  06  07  08
01  02  03  04
```

- E5 sélectionne librement la forme harmonique globale. `1/NOTE` fait jouer une note seule ; les autres valeurs construisent la forme choisie depuis le degré du pad : `SUS2`, `TRI`, `SUS4`, `6`, `7`, `9`, `11`, `13`.
- Les douze pads restent douze degrés successifs de la gamme courante, sur environ une octave et demie pour une gamme heptatonique. Ils ne changent jamais de géographie quand E5 change de forme.
- Chaque preset de gamme contient une table de voicing préparée. Pour chaque degré et chaque forme, elle donne l’ordre des notes et leurs décalages d’octave. Une progression obtient donc immédiatement des inversions cohérentes, sans calcul opportuniste ni encodeur d’inversion pendant la performance.
- Un appui capture un **snapshot de l’état musical**. Son relâchement envoie les Note Off correspondant exactement aux notes émises à l’appui, même si la tonalité, le voicing ou le mode a changé entre-temps.
- Le mode chromatique reste disponible comme type de gamme/layout, mais le cœur de l’instrument est diatonique.

### Les 8 pads FX

- Tenir un pad FX active l’algorithme qui lui est assigné et le rend cible des encodeurs.
- Tenir le pad + tourner `E1` parcourt le browser ; clic `E1` assigne l’algorithme, comme le browser FX du sampler.
- Chaque slot mémorise indépendamment : algorithme, paramètres, `GATE/LATCH`, chance et courbe de vélocité.
- `GATE` : l’algorithme vit tant que le pad FX est tenu ; le relâchement annule proprement les événements futurs et ferme ses notes actives.
- `LATCH` : premier appui démarre, second appui arrête proprement.
- Plusieurs FX simultanés sont permis seulement après définition d’une règle de composition sûre. P0 commence avec **un transformateur temporel principal actif à la fois**, les macros non temporelles (velocity/humanize) pouvant se cumuler.

### Mapping recommandé des encodeurs

#### Aucun pad FX tenu — page globale

| Encodeur | Rotation | Clic |
|---|---|---|
| E1 NAV | Naviguer pages/presets | Valider |
| E2 KEY | Tonique C…B | Alterner orthographe dièses/bémols pour l’affichage |
| E3 SCALE | Major, minor naturelle, harmonique, mélodique, modes retenus | Retour Major/Minor rapide |
| E4 RANGE | Octave de base | Retour à l’octave par défaut |
| E5 CHORD | Sélection libre `1/NOTE → SUS2 → TRI → SUS4 → 6 → 7 → 9 → 11 → 13` | Fonction secondaire à décider ; aucune inversion manuelle P0 |
| E6 DYNAMICS | Macro CC1/CC11 ou vélocité selon profil | Parcourir le profil de dynamique |
| E7 BPM | 20–300 BPM | Tap tempo ou lecture/arrêt clock selon décision P1 |

#### Pad FX tenu — page contextuelle

| Encodeur | Fonction contextuelle |
|---|---|
| E1 | Browser + assignation |
| E2 | Amount/depth |
| E3 | Division/rate (`1/4`, `1/8`, `1/8T`, `1/16`, `1/16T`, `1/32`) |
| E4 | Range/octaves/direction selon l’algorithme |
| E5 | Rotation : variante ; clic : `GATE/LATCH` |
| E6 | Chance, vélocité, humanize ou densité |
| E7 | BPM global, toujours accessible |

### Shift et sécurité

- `SHIFT + clic E7` = **PANIC immédiat** : Note Off sur le registre interne + CC123 All Notes Off sur les canaux utilisés.
- Le panic ne doit jamais dépendre d’un menu ou d’un FX actif.
- Les combinaisons Shift supplémentaires ne sont ajoutées qu’après validation physique du P0.

---

## 4. Modèle des algorithmes MIDI

Les FX ne traitent pas de l’audio. Ils transforment une intention musicale en une liste ordonnée d’événements MIDI horodatés.

```text
Pad musical
→ NotePlan (notes ou accord)
→ algorithme MIDI
→ ScheduledMidiEvent[]
→ MidiScheduler
→ USB MIDI
```

### Contrat commun

Chaque algorithme reçoit :

- les notes sources ;
- la vélocité de base ;
- le canal ;
- le BPM et la division ;
- un identifiant de source (`musicalPad + fxSlot + generation`) ;
- une seed explicite pour les comportements randomisés.

Chaque algorithme produit des `MidiEvent` déterministes. Aucun `delay()`, sommeil, allocation ou envoi USB n’existe dans le cœur musical.

### Comportements P0

- `STRUM` : ordonne les notes d’un accord, applique direction et espacement ; aucune note supplémentaire.
- `ARP` : cycle sur les notes tenues, patterns Up/Down/UpDown/AsPlayed, division BPM.
- `RUN UP` / `RUN DOWN` : génère les degrés de gamme entre une borne de départ et une borne d’arrivée ; utile pour cordes, bois et cuivres.
- `TRANCE GATE` : alterne Note On/Off ou vélocité sur une grille, sans produire de Note Off orphelin.
- `NOTE REPEAT` : répète note ou accord à la division choisie.
- `RANDOM` : réordonne ou choisit des notes dans une plage bornée ; seed testable, jamais hors gamme sauf option chromatique explicite.
- `VELOCITY` : applique Fixed, Accent, Crescendo, Decrescendo ou Pattern ; bornage MIDI 1–127.

### Règles anti-notes bloquées

1. Toute note active appartient à une source logique.
2. Une source garde la liste exacte de ses notes réellement envoyées.
3. Annuler une source supprime ses événements futurs et ferme ses notes présentes.
4. Si plusieurs sources possèdent la même note/canal, utiliser un compteur de références ou une politique d’ownership explicite pour ne pas couper la note de l’autre source.
5. Un changement d’état musical n’altère jamais la liste de relâchement d’un pad déjà tenu.
6. Panic vide scheduler, ownership et état de pads avant d’envoyer All Notes Off.

---

## 5. Organisation des fichiers

Ne pas modifier l’actuel `firmware/` du sampler pour construire cette variante. Créer un produit frère :

```text
firmware_midi/
├── firmware_midi.ino
├── docs/
│   ├── CONCEPT.md
│   ├── CONTROLS.md
│   ├── MIDI_CONTRACT.md
│   └── ORCHESTRAL_PROFILES.md
├── src/
│   ├── music/
│   │   ├── musical_state.h
│   │   ├── scale_map.h/.cpp
│   │   ├── chord_builder.h/.cpp
│   │   ├── harmony_preset.h/.cpp
│   │   └── voice_leading.h/.cpp        # option P1, pas chemin P0
│   ├── midi/
│   │   ├── midi_event.h
│   │   ├── midi_out.h
│   │   ├── midi_scheduler.h/.cpp
│   │   └── note_registry.h/.cpp
│   ├── algorithms/
│   │   ├── midi_algorithm.h
│   │   ├── algorithm_registry.h/.cpp
│   │   ├── strum.h/.cpp
│   │   ├── arpeggiator.h/.cpp
│   │   ├── scale_run.h/.cpp
│   │   ├── rhythmic_gate.h/.cpp
│   │   └── velocity_pattern.h/.cpp
│   ├── control/
│   │   ├── front_panel_controller.h/.cpp
│   │   ├── pad_assignments.h/.cpp
│   │   └── control_event.h
│   ├── ui/
│   │   └── midi_screen_ui.h/.cpp
│   └── teensy/
│       ├── teensy_midi_out.h/.cpp
│       ├── matrix_scanner.h/.cpp
│       ├── encoder_bank.h/.cpp
│       └── oled_backend.h/.cpp
├── test_native/
│   ├── music_engine_test.cpp
│   ├── chord_builder_test.cpp
│   ├── midi_scheduler_test.cpp
│   ├── note_registry_test.cpp
│   ├── algorithms_test.cpp
│   ├── front_panel_test.cpp
│   ├── midi_screen_ui_test.cpp
│   └── midi_harness.cpp
└── scripts/
    └── test_native.py
```

### Réutilisation avec le sampler

- Ne pas créer immédiatement un dossier `shared/` : les drivers matériels du sampler ne sont pas encore finalisés et son worktree comporte un développement actif parallèle.
- Réutiliser les **contrats**, la grammaire et les tests, pas copier aveuglément le moteur audio.
- Quand les deux variantes possèdent des drivers matrice/encodeurs/OLED stables et identiques, faire un refactor séparé vers `firmware_shared/`, couvert par les suites des deux firmwares.
- Le `README.md` racine et `hardware/COMPONENT_HANDOFF.md` contiennent encore des traces de l’ancien AKOR/Pico ; ne pas les prendre comme source hardware. La cible validée ici est le PCB AMEN_MINI + Teensy 4.1 décrit dans `firmware/docs/CONCEPT.md`.

---

## 6. Plan d’implémentation

### Task 1: Verrouiller la spec AMEN MIDI

**Objective:** Transformer ce plan en contrat produit relisible avant tout code.

**Files:**
- Create: `firmware_midi/docs/CONCEPT.md`
- Create: `firmware_midi/docs/CONTROLS.md`
- Create: `firmware_midi/docs/MIDI_CONTRACT.md`

**Steps:**
1. Écrire la vision, les P0/P1/P2 et les non-objectifs.
2. Documenter les deux pages d’encodeurs et les gestes Gate/Latch/Panic.
3. Documenter les règles de snapshot, ownership, Note Off et changement d’état.
4. Faire valider par Arthur la liste P0 d’algorithmes et le mapping E2–E7.
5. Commit: `docs(midi): define orchestral controller interaction contract`.

**Gate:** aucun code tant que le mapping physique et les algorithmes P0 ne sont pas approuvés.

### Task 2: Créer le squelette portable et le test runner

**Objective:** Obtenir une cible native vide mais compilable, indépendante d’Arduino.

**Files:**
- Create: `firmware_midi/scripts/test_native.py`
- Create: `firmware_midi/test_native/smoke_test.cpp`
- Create: `firmware_midi/src/midi/midi_event.h`

**Steps:**
1. Écrire un smoke test rouge qui inclut `midi_event.h` absent.
2. Exécuter `python3 firmware_midi/scripts/test_native.py`; attendu : FAIL fichier absent.
3. Créer les types minimaux `MidiEventType` et `MidiEvent`.
4. Relancer ; attendu : PASS avec `-std=c++17 -Wall -Wextra -Wpedantic`.
5. Commit: `test(midi): add strict native firmware test runner`.

### Task 3: Modéliser l’état musical et les 12 pads

**Objective:** Produire les douze notes MIDI d’une gamme sans hardware.

**Files:**
- Create: `firmware_midi/src/music/musical_state.h`
- Create: `firmware_midi/src/music/scale_map.h/.cpp`
- Create: `firmware_midi/test_native/music_engine_test.cpp`

**Tests requis:**
- C Major, A natural minor, D Dorian et chromatique.
- Les pads 01–12 sont strictement croissants.
- Chaque note reste dans 0–127.
- Changer l’octave translate de 12 demi-tons.
- Les noms affichés et numéros MIDI correspondent.

**Commit:** `feat(music): map stable degree pads across keys and scales`.

### Task 4: Construire les formes d’accord et les voicings préparés

**Objective:** Faire des mêmes pads une surface allant librement de la note seule aux accords étendus, avec des inversions intelligentes définies par le preset de gamme.

**Files:**
- Create: `firmware_midi/src/music/chord_builder.h/.cpp`
- Create: `firmware_midi/src/music/harmony_preset.h/.cpp`
- Create: `firmware_midi/test_native/chord_builder_test.cpp`

**Tests requis:**
- `1/NOTE` produit exactement une note par pad.
- E5 peut sélectionner librement chaque forme définie dans la liste P0, sans changer de gamme ni de géographie.
- C Major triads : C, Dm, Em, F, G, Am, Bdim.
- Seventh : Cmaj7, Dm7, Em7, Fmaj7, G7, Am7, Bm7b5.
- Les tables du preset donnent, pour chaque degré et chaque forme, des décalages d’octave déterministes.
- Les voicings préparés conservent les classes de hauteur de l’accord, respectent la plage MIDI et produisent les inversions attendues.
- Une même progression rejouée donne toujours exactement les mêmes notes ; aucun choix caché ne dépend de l’historique de jeu en P0.
- Plage et polyphonie restent bornées.

**Commit:** `feat(music): add selectable chord shapes and preset voicings`.

### Task 5: Garantir Note On/Off et Panic

**Objective:** Rendre impossible une note bloquée dans les scénarios testés.

**Files:**
- Create: `firmware_midi/src/midi/note_registry.h/.cpp`
- Create: `firmware_midi/test_native/note_registry_test.cpp`

**Tests requis:**
- Relâcher un pad après changement de gamme coupe les anciennes notes, pas les nouvelles notes théoriques.
- Deux sources partageant la même note ne se coupent pas mutuellement.
- Annulation d’une source ferme seulement ses notes.
- Panic vide toutes les sources et génère les messages de sécurité attendus.

**Commit:** `feat(midi): track note ownership and deterministic panic`.

### Task 6: Ajouter l’horloge et le scheduler sans `delay()`

**Objective:** Planifier arpèges, runs et gates avec une horloge testable.

**Files:**
- Create: `firmware_midi/src/midi/midi_scheduler.h/.cpp`
- Create: `firmware_midi/test_native/midi_scheduler_test.cpp`

**Tests requis:**
- Divisions droites et triplets exactes à plusieurs BPM.
- Ordre stable pour événements au même timestamp : Note Off avant Note On de la même note.
- Annulation supprime les événements futurs d’une source.
- Wrap de temps et longues sessions sans dérive cumulative non bornée.
- Aucun malloc dans `tick()` après initialisation.

**Commit:** `feat(midi): schedule tempo-synced events without blocking`.

### Task 7: Implémenter STRUM en tranche verticale

**Objective:** Prouver le contrat complet algorithme → scheduler → registry.

**Files:**
- Create: `firmware_midi/src/algorithms/midi_algorithm.h`
- Create: `firmware_midi/src/algorithms/strum.h/.cpp`
- Create/Modify: `firmware_midi/test_native/algorithms_test.cpp`

**Tests requis:** Up/Down/Outside-In, espacement, vélocité, annulation Gate, arrêt Latch, aucun Note Off orphelin.

**Commit:** `feat(midi-fx): add safe assignable strum algorithm`.

### Task 8: Ajouter ARP, RUN UP/DOWN et NOTE REPEAT

**Objective:** Livrer les gestes essentiels de composition orchestrale.

**Files:**
- Create: `firmware_midi/src/algorithms/arpeggiator.h/.cpp`
- Create: `firmware_midi/src/algorithms/scale_run.h/.cpp`
- Extend: `firmware_midi/test_native/algorithms_test.cpp`

**Tests requis:**
- Arp Up/Down/UpDown/AsPlayed.
- Run limité à la gamme, aux bornes et à la range MIDI.
- Une note source produit un run cohérent ; un accord source définit correctement son point de départ.
- Gate/relâchement et changement de BPM ne laissent aucune note active.

**Commit:** `feat(midi-fx): add orchestral arpeggio and scale runs`.

### Task 9: Ajouter TRANCE GATE, RANDOM et VELOCITY

**Objective:** Compléter le browser P0 avec des transformations rythmiques et expressives.

**Files:**
- Create: `firmware_midi/src/algorithms/rhythmic_gate.h/.cpp`
- Create: `firmware_midi/src/algorithms/velocity_pattern.h/.cpp`
- Extend: `firmware_midi/test_native/algorithms_test.cpp`

**Tests requis:**
- Random reproductible avec seed.
- Vélocité toujours entre 1 et 127.
- Crescendo/decrescendo monotones dans les cas simples.
- Trance gate respecte duty cycle et division.
- Panic durant chaque algorithme laisse le registre vide.

**Commit:** `feat(midi-fx): add rhythmic random and velocity transforms`.

### Task 10: Browser d’algorithmes et assignations par pad

**Objective:** Reproduire la philosophie AMEN : tenir le pad, naviguer, cliquer pour assigner.

**Files:**
- Create: `firmware_midi/src/algorithms/algorithm_registry.h/.cpp`
- Create: `firmware_midi/src/control/pad_assignments.h/.cpp`
- Create: `firmware_midi/test_native/front_panel_test.cpp`

**Tests requis:**
- Huit slots indépendants.
- `BLANK` désassigne.
- Assigner n’active pas accidentellement l’algorithme.
- Chaque slot garde ses paramètres et Gate/Latch.
- Le dernier pad FX tenu devient cible ; le relâchement retombe sur le précédent encore tenu.

**Commit:** `feat(control): add eight contextual MIDI algorithm slots`.

### Task 11: Contrôleur de façade et harness PC

**Objective:** Tester le produit complet au clavier avant le matériel.

**Files:**
- Create: `firmware_midi/src/control/front_panel_controller.h/.cpp`
- Create: `firmware_midi/test_native/midi_harness.cpp`
- Create: `firmware_midi/start_midi_firmware.ps1`

**Mapping harness:**
- Numpad 1–9 puis touches dédiées pour les pads 10–12.
- F1–F7 sélectionnent E1–E7 ; flèches tournent ; Entrée clique.
- Huit touches dédiées représentent les FX.
- Shift physique simulé séparément.
- Console : événements MIDI horodatés + notes actives + état musical.

**Vérification:** scénario automatisé et séance d’écoute via un synthé logiciel/DAW avec MIDI virtuel si disponible.

**Commit:** `feat(sim): add complete AMEN MIDI front-panel harness`.

### Task 12: UI OLED portable

**Objective:** Afficher le contexte musical sans menu profond.

**Files:**
- Create: `firmware_midi/src/ui/midi_screen_ui.h/.cpp`
- Create: `firmware_midi/test_native/midi_screen_ui_test.cpp`

**Écrans P0:**
- Home : `Fm`, forme E5 (`1`, `SUS2`, `7`, etc.), octave, BPM, dynamique.
- Accord tenu : nom réel + nom/indice du voicing préparé.
- FX tenu : slot, nom, Gate/Latch et hint encodeur.
- Browser : trois lignes, sélection, scroll horizontal.
- Paramètre : overlay persistant pendant interaction puis timeout.
- Erreur/Panic : message prioritaire.

**Commit:** `feat(ui): add 128x32 orchestral MIDI screens`.

### Task 13: Backend USB-MIDI Teensy

**Objective:** Remplacer la sortie console par l’USB-MIDI réel sans toucher au cœur.

**Files:**
- Create: `firmware_midi/src/teensy/teensy_midi_out.h/.cpp`
- Create: `firmware_midi/firmware_midi.ino`
- Create: `firmware_midi/src/teensy/arduino_stubs/Arduino.h` si nécessaire pour les tests hôte.

**Steps:**
1. Interroger `arduino-cli board details --fqbn teensy:avr:teensy41 --full` et documenter l’option USB MIDI exacte au lieu de la deviner.
2. Écrire un test de backend avec sortie fake.
3. Implémenter Note On, Note Off, CC et All Notes Off.
4. Compiler avec `arduino-cli`.
5. Vérifier l’énumération comme périphérique MIDI, le nom du device et la réception dans un moniteur MIDI.

**Commit:** `feat(teensy): emit AMEN MIDI events over native USB`.

### Task 14: Drivers matrice, encodeurs et OLED du PCB réel

**Objective:** Faire correspondre les événements physiques au contrôleur de façade portable.

**Files:**
- Create: `firmware_midi/src/teensy/matrix_scanner.h/.cpp`
- Create: `firmware_midi/src/teensy/encoder_bank.h/.cpp`
- Create: `firmware_midi/src/teensy/oled_backend.h/.cpp`
- Modify: `firmware_midi/firmware_midi.ino`

**Verification hardware:**
1. Audit netlist du véritable `hardware/AMEN_MINI.kicad_sch/.kicad_pcb` pour obtenir le pin map ; ne pas utiliser les restes AKOR/Pico des vieux README.
2. Diagnostic série brut : 21 touches indépendantes, 7 rotations, 7 clics.
3. Debounce et anti-ghosting, test d’accords multi-pads.
4. 100 tours rapides par encodeur sans saut/rebond.
5. OLED stable pendant trafic MIDI intensif.
6. Aucun codec/audio board initialisé dans cette variante.

**Commit:** `feat(teensy): connect AMEN MINI controls to MIDI engine`.

### Task 15: Profils d’instruments, macros et dynamique

**Objective:** Adapter la même logique musicale aux synthétiseurs, instruments multitimbrals et banques orchestrales sans contaminer le moteur musical avec des règles propres à un plugin.

**Files:**
- Create: `firmware_midi/src/music/performance_profile.h/.cpp`
- Create: `firmware_midi/docs/INSTRUMENT_PROFILES.md`
- Extend tests.

**Profil minimal:** canal, vélocité, plage, quatre macros CC configurables, dynamique (`CC1`, `CC11`, paire ou off) et key-switch optionnel.

**Gate:** tester au minimum Serum, Falcon et une banque orchestrale réelle. Les profils stockent les mappings choisis par Arthur ; ne pas coder de CC, macro ou key-switch comme norme universelle.

**Commit:** `feat(midi): add configurable instrument performance profiles`.

### Task 16: Persistence, endurance et démo

**Objective:** Livrer un instrument, pas un prototype qui oublie son état.

**Files:**
- Create: `firmware_midi/src/teensy/settings_store.h/.cpp`
- Create: `firmware_midi/docs/DEMO_CHECKLIST.md`
- Update: `firmware_midi/docs/CONTROLS.md`

**Tests:**
- Version + checksum + valeurs par défaut si stockage invalide.
- Sauvegarde hors boucle temps réel et sans usure à chaque cran.
- Reboot restitue les huit slots.
- Stress 30 minutes : accords, changement de gamme, latch, panic, déconnexion/reconnexion USB ; zéro note bloquée.
- Démo 30–60 s enregistrée dans un DAW : mélodie → accord voice-led → strum → run → trance gate → panic contrôlé.

**Commit:** `feat(midi): persist assignments and harden live performance`.

---

## 7. Commandes de vérification prévues

```bash
# Tous les tests portables stricts
python3 firmware_midi/scripts/test_native.py

# Harness PC (commande exacte à générer dans le script)
./firmware_midi/build/midi_harness

# Découvrir les options Teensy, notamment USB
arduino-cli board details --fqbn teensy:avr:teensy41 --full

# Compiler le firmware réel
arduino-cli compile --fqbn teensy:avr:teensy41 firmware_midi
```

L’option USB MIDI exacte doit être ajoutée à la commande après inspection de `board details` et vérifiée par une vraie énumération USB.

---

## 8. Critères d’acceptation

### Musical

- La disposition reste mentalement stable en changeant de tonalité.
- Les notes et accords restent dans la gamme choisie, sauf mode chromatique explicite.
- Les accords affichés correspondent aux notes émises.
- Les voicings préparés du preset produisent les inversions prévues et des enchaînements musicalement cohérents, vérifiés à l’écoute.
- Les runs sont crédibles sur les banques orchestrales et restent musicalement utiles sur un synthétiseur.

### MIDI

- Chaque Note On possède un Note Off correspondant.
- Changer de gamme, octave, chord mode ou FX pendant une note tenue ne crée aucune note bloquée.
- Panic fonctionne dans tous les écrans et états.
- L’USB reconnecte proprement après débranchement.
- Velocity et CC restent toujours dans 0–127 ; les Note On musicales utilisent velocity 1–127.

### Interaction

- Jouer ne nécessite aucun menu.
- Assigner un FX reproduit le geste AMEN : tenir → E1 browse → clic assign.
- Un paramètre édité est toujours identifiable sur l’OLED.
- Gate/Latch est cohérent avec le sampler AMEN.
- Les fonctions les plus importantes restent accessibles sans Shift.

### Qualité

- Cœur portable sans include Arduino.
- Tests stricts sans warning dans le code propriétaire.
- Aucun `delay()`, I/O, allocation ou scan de stockage dans le scheduler/tick.
- Tests random déterministes grâce à une seed.
- Les deux variantes firmware coexistent sans conditionnelles massives.

---

## 9. Risques et décisions ouvertes

1. **Portée trop large :** piloter synthés et orchestre peut devenir un DAW entier. Le P0 reste un générateur harmonique et performatif ; sound design profond, orchestration multi-instrument et séquenceur complet sont différés.
2. **Expression limitée par les MX :** aucune vélocité physique. La réponse honnête est un moteur de courbes/accents et une macro CC, pas une fausse mesure de frappe.
3. **Composition de plusieurs FX :** appliquer simultanément arp + run + gate peut exploser le nombre d’événements et rendre le résultat ambigu. P0 autorise un transformateur temporel principal ; l’empilement n’arrive qu’avec une pipeline formalisée.
4. **Cibles MIDI incompatibles :** macros Serum, paramètres Falcon, CC orchestraux et key-switches ne partagent pas les mêmes conventions. Les profils sont configurables par instrument, jamais codés comme norme universelle.
5. **Documentation racine obsolète :** plusieurs fichiers hérités décrivent encore AKOR/Pico alors que cette variante cible le PCB AMEN_MINI/Teensy. Toute implémentation hardware commence par l’audit des fichiers KiCad réels.
6. **Développement parallèle en cours :** le worktree `dev` contient des modifications du firmware sampler par une autre session. L’implémentation MIDI doit rester dans `firmware_midi/` et éviter toute édition de `firmware/` tant que ce travail n’est pas intégré.

### Décisions à faire valider avant Task 2

- La liste exacte des gammes P0 : recommandation `Major`, `Natural Minor`, `Harmonic Minor`, `Melodic Minor`, `Dorian`, `Phrygian`, `Mixolydian`, `Chromatic`.
- La liste exacte et l’ordre de rotation E5 ; base proposée : `1/NOTE`, `SUS2`, `TRI`, `SUS4`, `6`, `7`, `9`, `11`, `13`.
- Le format des tables de voicing par preset de gamme et le premier jeu de presets à préparer musicalement.
- Le comportement P0 d’E6 : macro CC1/CC11 prioritaire ou vélocité prioritaire.
- La liste P0 définitive du browser et l’autorisation ou non de cumuler plusieurs algorithmes temporels.

## Sources

[2] https://manual.hichord.shop — HiChord manual
[3] https://nopia.io — Nopia official site
[4] https://www.musicradar.com/news/akuto-studio-chord-machine-akt-01 — MusicRadar: AKT-0.1
