# AMEN_MINI — Avancée du 17 août 2026

> **Pour Hermes :** exécuter ce plan seulement après validation d'Arthur, avec une implémentation par tâche, revue de conformité puis revue qualité.

**Goal:** livrer aujourd'hui un socle Teensy compilable et fermer trois manques P1 déjà spécifiés : crossfade de vol de voix, vrai mode LOOP latch et comportements OLED/browser.

**Architecture:** conserver le moteur audio portable en C++17 sous `firmware/src/engine/`, le matériel sous `firmware/src/teensy/` et l'UI portable sous `firmware/src/ui/`. Aucun ajout matériel non testable, aucune allocation dans le callback audio, aucune décision sur ONE SHOT gate/E6. Chaque lot aura son test ciblé et son commit conventionnel sur `dev`.

**Tech Stack:** C++17, Teensy 4.1 / Teensyduino 1.62, Arduino CLI 1.5.1, tests natifs g++, harness PC miniaudio.

---

## État vérifié avant plan

- Branche active : `dev`, alignée avec `origin/dev` après `git pull --ff-only origin dev` (`Already up to date`).
- Dernier commit : `99d6719` — intégration des décisions post-review dans les chapitres J.
- Commits de la soirée lus : vitesse/retrigger par pad, simulation face avant, Repeat synchronisé + triplets, bootstrap Teensy PSRAM/SD, décisions UI et crossfade.
- Baseline native actuelle :
  - `voice_manager_test` : PASS
  - `live_repeat_test` : PASS
  - `screen_ui_test` : PASS
- Baseline Teensy actuelle : FAIL. La première erreur réelle est `firmware.ino:3:10: fatal error: extmem.h: No such file or directory`. Dans Teensyduino 1.62, `extmem_malloc` est déclaré par le core (`wiring.h` via `Arduino.h`), pas par un header `extmem.h`.
- Le worktree contient déjà des fichiers non suivis appartenant à Arthur (`firmware/README.md`, `firmware/docs/CONCEPT.md`, `firmware/out.wav`, `firmware/scripts/notion_concept.py`, `firmware/src/engine/README.md`, `hardware/AMEN_MINI.d356`, `hardware/AMEN_MINI.net`). Ils seront laissés intacts et exclus de tous les commits.

---

### Tâche 1 — Rendre le bootstrap Teensy réellement compilable

**Objective:** transformer le bootstrap J4/J12 livré hier en artefact que `arduino-cli` compile vraiment pour Teensy 4.1.

**Files:**
- Modify: `firmware/firmware.ino`
- Modify: `firmware/src/teensy/psram_arena.cpp`
- Modify: `firmware/src/teensy/sample_loader.h`
- Modify only if the compiler exposes another incompatibility: `firmware/src/teensy/*.h/.cpp`
- Test/build: `/tmp/amen-build/` uniquement, aucun artefact généré dans le repo

**Steps:**
1. Ajouter un test de compilation qui reproduit l'échec actuel avec Teensyduino 1.62.
2. Supprimer la dépendance inexistante à `extmem.h` et utiliser les déclarations fournies par le core Teensy.
3. Corriger l'include moteur erroné de `sample_loader.h` (`../engine/pcm_view.h`).
4. Relancer la compilation et traiter chaque erreur suivante sans contourner les APIs Teensy.
5. Vérifier que le binaire cible reste un diagnostic SD → probe WAV → décodage PSRAM, sans introduire de travail dans `loop()` ni dans un callback.
6. Lancer les tests natifs pour prouver que la couche portable n'a pas régressé.

**Verification:**
- `arduino-cli compile --fqbn teensy:avr:teensy41 --build-path /tmp/amen-build firmware`
- Attendu : exit 0, mémoire programme/RAM affichée, aucun `fatal error`.
- Recompiler et exécuter les tests `voice_manager`, `wav_loader_reader` et formats WAV natifs.

**Commit:** `fix(teensy): make PSRAM SD bootstrap compile`

---

### Tâche 2 — Crossfade de vol de voix sur 64 frames

**Objective:** terminer la partie manquante de J3 : le cinquième pad vole bien la voix la plus ancienne sans coupure abrupte.

**Files:**
- Modify: `firmware/src/engine/voice_manager.h`
- Modify: `firmware/src/engine/voice_manager.cpp`
- Modify if a primitive de gain interne est préférable: `firmware/src/engine/sample_player.h`
- Modify if a primitive de gain interne est préférable: `firmware/src/engine/sample_player.cpp`
- Test: `firmware/test_native/voice_manager_test.cpp`

**Approach:**
- Garder quatre voix principales.
- Lors d'un vol, conserver temporairement l'état de la voix sortante dans un état de retraite fixe, sans allocation dynamique.
- Pendant exactement 64 frames, appliquer une rampe linéaire descendante à l'ancienne voix et une rampe montante à la nouvelle.
- Le retrigger du même pad conserve son comportement actuel et ne consomme pas une voix supplémentaire permanente.
- `stopAll()` doit aussi annuler proprement toute retraite en cours.

**Tests à écrire avant implémentation:**
1. La cinquième identité vole toujours l'âge le plus ancien.
2. La première frame après vol ne contient pas une discontinuité pleine échelle.
3. La contribution ancienne décroît et la nouvelle croît sur 64 frames.
4. Après 64 frames, seule la nouvelle voix contribue.
5. Deux vols rapprochés restent déterministes et sans allocation.
6. Les valeurs finales restent clampées dans `[-1, 1]`.

**Verification:**
- Compilation stricte `-Wall -Wextra -Wpedantic` du test moteur.
- Exécution de toute la suite moteur native.
- Build du harness PC ; mise à jour de `firmware/amen_rt.exe` si les sources du harness changent.

**Commit:** `feat(engine): crossfade stolen voices over 64 frames`

---

### Tâche 3 — Dissocier mode de lecture et comportement du pad

**Objective:** rendre indépendants (1) ce que fait la tête de lecture à la fin du range et (2) ce que fait le relâchement du pad.

**Files:**
- Modify: `firmware/src/engine/sample_player.h`
- Modify: `firmware/src/engine/sample_player.cpp`
- Modify: `firmware/src/engine/voice_manager.h`
- Modify: `firmware/src/engine/voice_manager.cpp`
- Modify: `firmware/test_native/voice_manager_test.cpp`
- Modify: `firmware/test_native/rt_player.cpp`
- Modify: `firmware/src/ui/screen_ui.h/.cpp` si l'état doit être affiché
- Modify: `firmware/docs/CONTROLS.md`
- Modify if harness source/dependencies change: `start_firmware.ps1`
- Rebuild tracked deliverable: `firmware/amen_rt.exe`

**Modèle retenu à faire valider:**
- Axe 1 — `PlaybackMode` :
  - `ONE SHOT` : la tête s'arrête à la fin du range.
  - `LOOP` : la tête revient au début du range.
- Axe 2 — `TriggerBehavior` :
  - `GATE` : le relâchement coupe la voix.
  - `LATCH` : le relâchement ne coupe pas la voix.
- Combinaisons obtenues :
  - ONE SHOT + GATE : joue tant que le pad reste tenu, avec fin naturelle si le sample finit avant.
  - ONE SHOT + LATCH : un appui joue jusqu'à la fin, même après relâchement.
  - LOOP + GATE : boucle uniquement tant que le pad est tenu.
  - LOOP + LATCH : continue de boucler après relâchement ; un deuxième appui arrête le pad.

**Proposition de contrôle PC/façade:**
- Tourner E5 : choisit `ONE SHOT` / `LOOP` (puis futurs modes).
- Cliquer E5 : bascule `GATE` / `LATCH` pour le pad tenu.
- E6 reste libre : aucun conflit avec le futur LFO.
- Chaque pad mémorise séparément son `PlaybackMode` et son `TriggerBehavior`.

**Scope précis:**
- Implémenter réellement ONE SHOT et LOOP avec les deux comportements ci-dessus.
- Ajouter `VoiceManager::releasePad()` et `VoiceManager::stopPad()` pour ne jamais utiliser `stopAll()` lors d'un relâchement individuel.
- Ajouter une couture courte et déterministe au retour de LOOP si le test montre un saut de bord ; aucune allocation dans `render()`.
- `GRANULAR` et `SLICE SYNC` restent des libellés non implémentés, sans comportement fictif.

**Tests à écrire avant implémentation:**
1. ONE SHOT + GATE s'arrête au relâchement.
2. ONE SHOT + LATCH ignore le relâchement et s'arrête à la fin.
3. LOOP + GATE revient plusieurs fois au début puis s'arrête au relâchement.
4. LOOP + LATCH ignore le relâchement et le deuxième appui l'arrête.
5. Arrêter ou relâcher un pad n'arrête pas les autres voix.
6. Le mode et le comportement sont mémorisés indépendamment par pad dans le harness.
7. Changement de vitesse et conversion de sample rate restent corrects en LOOP.
8. La couture ne dépasse pas `[-1,1]` et ne produit pas de trou inattendu.

**Verification:**
- Suite `voice_manager_test` stricte.
- Build et smoke test du harness sur les quatre combinaisons.
- `start_firmware.ps1` devra rester aligné avec les sources et contrôles.
- Validation auditive laissée explicitement « en attente Arthur » ; ne pas prétendre qu'une écoute humaine a été faite.

**Commit:** `feat(engine): separate playback mode from trigger behavior`

---

### Tâche 4 — Finaliser les interactions OLED P1/P2 déjà décidées

**Objective:** implémenter l'overlay renouvelé à chaque mouvement d'encodeur et le scroll horizontal des noms WAV longs.

**Files:**
- Modify: `firmware/src/ui/screen_ui.h`
- Modify: `firmware/src/ui/screen_ui.cpp`
- Modify: `firmware/test_native/screen_ui_test.cpp`
- Modify: `firmware/test_native/rt_player.cpp`
- Modify: `firmware/test_native/screen_preview.cpp` seulement si nécessaire
- Modify: `firmware/docs/CONTROLS.md`
- Rebuild tracked deliverable: `firmware/amen_rt.exe`

**Scope précis:**
- Chaque mouvement d'encodeur rappelle `showParameter(..., nowMs)` et repousse l'expiration à `nowMs + 1000 ms`.
- L'overlay expire exactement 1 s après la dernière interaction, pas après la première.
- Le browser mémorise l'instant où la sélection change.
- Après 500 ms immobile, seul le nom sélectionné défile horizontalement à environ 30 px/s.
- Avant 500 ms, afficher la forme tronquée avec `..`.
- Remettre le scroll à zéro à chaque changement de sélection/dossier ; ne jamais faire défiler les autres lignes.

**Tests à écrire avant implémentation:**
1. Une seconde interaction prolonge l'overlay d'une seconde supplémentaire.
2. L'overlay revient à Performance exactement après le dernier délai.
3. Le nom long ne bouge pas avant 500 ms.
4. Le nom sélectionné bouge après 500 ms avec un offset déterministe.
5. Changer la sélection remet l'offset à zéro.
6. Un nom court ne défile pas.
7. Browser et FX pad continuent de prendre priorité sur l'overlay.

**Verification:**
- `screen_ui_test` strict : PASS.
- Build du harness et smoke test.
- Vérifier visuellement via le framebuffer/screen preview que le texte reste dans les 128×32.

**Commit:** `feat(ui): persist overlays and scroll browser names`

---

### Tâche 5 — Régression complète, documentation et livraison

**Objective:** livrer une branche `dev` propre, reproductible et honnêtement documentée.

**Files:**
- Modify: `firmware/docs/ROADMAP.md`
- Modify if controls changed: `firmware/docs/CONTROLS.md`
- Modify if verification commands changed: `AGENTS.md`
- Keep updated if harness changed: `firmware/amen_rt.exe`

**Steps:**
1. Exécuter tous les tests natifs disponibles : formats WAV, reader, catalogue, scanner, voix, Repeat et UI.
2. Compiler le harness complet avec ses options Windows documentées.
3. Compiler le firmware Teensy 4.1 via Arduino CLI.
4. Inspecter `git diff`, secrets, fichiers générés et TODO/debug accidentels.
5. Vérifier que les sept fichiers non suivis préexistants d'Arthur n'ont pas été modifiés ni ajoutés.
6. Mettre à jour les statuts J3/J7/J12/J13 avec les preuves réellement obtenues. Les validations d'écoute et de matériel resteront « à faire ».
7. Pousser les commits atomiques sur `origin/dev` uniquement si toutes les vérifications automatisables passent.

**Final verification commands:**
- `git status --short --branch`
- `git diff <base>..HEAD --check`
- tous les exécutables de test natifs dans `/tmp`
- `arduino-cli compile --fqbn teensy:avr:teensy41 --build-path /tmp/amen-build firmware`
- compilation harness documentée dans `AGENTS.md`

**Commit:** `docs: record 17 August firmware checkpoint`

---

## Ce que je ne code pas aujourd'hui

- **REVERSE DSP** : la roadmap dit à la fois « FX global après le mix » et « lecture inversée du sample ». Implémenter maintenant obligerait à choisir silencieusement entre reverse du mix rétrospectif et reverse de chaque voix. Mauvaise décision à prendre pendant la randonnée d'Arthur.
- **E6** : je le laisse libre. La sélection GATE/LATCH passe par le clic E5 dans la proposition actuelle, afin de ne pas bloquer le futur LFO.
- **J5 slices** : encore marqué suspendu malgré son rôle dans la démo ; pas de changement de scope sans validation.
- **AudioStream/SGTL5000 réel** : le premier objectif est de rendre le bootstrap actuel compilable. L'intégration audio matérielle sera le lot suivant, idéalement avec Teensy/Audio Shield disponibles pour vérifier autre chose qu'une compilation.

## Risques et garde-fous

- Le crossfade de vol implique brièvement une ancienne et une nouvelle source ; mesurer/borner ce coût et n'allouer aucune mémoire à chaud.
- La couture LOOP peut altérer l'attaque si elle est trop longue ; rester courte, testée numériquement, et laisser l'écoute finale à Arthur.
- `firmware/amen_rt.exe` est volontairement suivi : le reconstruire après tout changement du harness et vérifier qu'il accompagne les sources.
- Aucun push si la compilation Teensy ou une régression native échoue ; dans ce cas, conserver des commits locaux clairement séparés et rapporter le blocage exact.
