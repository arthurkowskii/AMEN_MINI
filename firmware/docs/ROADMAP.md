# AMEN_MINI — Roadmap firmware (todo exécutable)

## Goal — la vision de fin

AMEN_MINI est une machine à breaks autonome : on pose un break sur la SD, elle le découpe automatiquement, et on le joue live — 12 pads de chops, 8 pads d'effets (reverse, stutter, tape stop…), modes one-shot / loop / granulaire / slice-sync, le tout calé sur un tempo global réglable avec 7 encodeurs et un séquenceur 16 pas. À la rentrée (début septembre), la machine doit tourner debout sur une Teensy 4.1 : son dans le casque sans PC, zéro latence perceptible, chaque geste = un son — et une démo de 30-60 s qui montre le trajet complet : break → auto-chop → jeu live → effets → séquenceur.

<callout icon="🎯" color="blue_bg">
	COMMENT UTILISER CETTE PAGE : chaque chapitre (J1-J14) est autonome : objectif, fichiers, spec, définition de fait, vérification. Un agent (opencode, Claude, Boris) peut en prendre un et l'exécuter sans autre contexte. Quand un chapitre est terminé : push sur dev, puis mets à jour son statut ici (tag ✅ + hash du commit).
</callout>

## Conventions projet (À LIRE AVANT TOUT)

- Repo : github.com/arthurkowskii/AMEN_MINI — on travaille sur la branche **dev** (jamais main).
  Récupérer : `git fetch origin && git switch dev && git pull`
- Moteur portable : `firmware/src/engine/` — C++17 pur, ZÉRO include Arduino, doit compiler sur PC (g++) ET Teensy (arduino-cli, FQBN `teensy:avr:teensy41`).
- Format interne : **int16 partout** (WavData.samples = vector<int16_t>), stéréo conservée à la fréquence native du WAV ; sortie moteur à 44,1 kHz.
  Les buffers de sortie render() sont des floats dans [-1, 1] (division par 32768).
- Tests PC : `firmware/test_native/` — miniaudio (header-only) dans `test_native/third_party/`.
  Compile + écoute Windows : `g++ -std=c++17 -O2 test_native/rt_player.cpp test_native/sample_catalog_scanner.cpp test_native/screen_preview.cpp src/browser/sample_catalog.cpp src/engine/wav_loader.cpp src/engine/sample_player.cpp src/engine/voice_manager.cpp src/engine/fx/live_repeat.cpp src/ui/screen_ui.cpp -I src/browser -I src/engine -I src/ui -I test_native -I test_native/third_party -o amen_rt.exe -lole32 -lwinmm -lgdi32 -luser32` puis `amen_rt.exe test_native/test.wav`. Les contrôles détaillés et à jour sont dans `docs/CONTROLS.md`.
- Le moteur ne touche JAMAIS au matériel. Tout ce qui est Teensy (SD, PSRAM, GPIO, Audio Library) vit dans la couche Teensy (firmware.ino + src/teensy/).
- Définition de fait globale : compile sans warning (g++ -Wall -Wextra) + vérification numérique/écoute + push sur dev.

## J1 — WavLoader multi-format ✅ FAIT (commit 71a1819)

- Objectif : lire un WAV de la SD et livrer des échantillons int16 en RAM.
- Fichiers : `src/engine/wav_loader.h` (struct WavData { uint32_t sampleRate; uint16_t channels; vector<int16_t> samples; }) + `wav_loader.cpp`.
- Spec : marche de chunks (nom 4 octets + taille LE 4 octets, branches fmt/data/autre→seekg). Conversion → int16 au CHARGEMENT, une seule fois, hors callback : PCM 8-bit (octet−128)<<8, 16-bit direct, 24-bit extension de signe puis >>8, 32-bit >>16, float32 clamp(f×32767). Lecture par paquets (multiple de 12), reserve(), zéro copie au return (move).
- DoD : les 5 formats chargent et produisent des valeurs identiques à la conversion attendue (vérifié échantillon par échantillon vs Python) ; fichier non-WAV → WavData vide.
- Vérification : `g++ -std=c++17 -O2 -I src/engine test_native/main.cpp src/engine/wav_loader.cpp -o /tmp/amen_test && /tmp/amen_test test_native/test.wav` + `python3 test_native/check_formats.py`.

## J2 — SamplePlayer à vitesse variable ✅ FAIT (commit 8ba8638)

- Objectif : jouer un échantillon à la vitesse voulue (magnétophone : pitch et durée bougent ensemble).
- Fichiers : `src/engine/sample_player.h` + `sample_player.cpp`.
- Spec : position de lecture float pos_ ; à chaque frame de sortie : i0=(int)pos_, t=pos_−i0, lerp = samples[i0]×(1−t) + samples[i1]×t (i1 = i0+1, clampé au bord), ÷32768 ; pos_ += speed_ ; arrêt propre si pos_ ≥ fin (playing_=false, sortie de zéros). API : setSample(const WavData&), trigger(), setSpeed(float), bool render(float* outL, float* outR, int numFrames).
- DoD : vérifié numériquement sur test.wav (1 s, 440 Hz stéréo) : speed 0,5 → 220 Hz / 2,0 s ; 1,0 → 440 Hz / 1,0 s ; 2,0 → 880 Hz / 0,5 s. Le player PC complet compile.
- Vérification : analyse par zero-crossing (fréquence) + comptage de frames rendues (durée), pour les 3 vitesses.

## J3 — Pool de 4 voix [EN COURS — moteur natif] — P1

- Objectif : 4 SamplePlayer simultanés — le sampler devient polyphonique tout en conservant de la marge CPU pour les effets.
- Fichiers : `src/engine/voice_manager.h` + `voice_manager.cpp` + test dans test_native/.
- Spec : pool de 4 voix, chacune = un SamplePlayer + son état (occupée/libre) et son `PadId`. Les voix référencent les échantillons partagés sans les copier : la durée du break pèse sur la PSRAM, tandis que le nombre de voix actives pèse sur le CPU. Un nouveau trigger du même pad arrête/remplace immédiatement sa voix active ; des pads différents restent polyphoniques. Allocation : première voix libre ; si quatre pads distincts sont actifs : vol de la voix la plus ancienne. Mixage : somme des render() de toutes les voix dans le même buffer ; clipping final à [-1, 1].
- DoD : 4 pads distincts s'entendent additionnés sans saturation ; retrigger un même pad ne double pas son son ; un 5e pad distinct vole proprement la voix la plus ancienne.
- Vérification : test PC qui déclenche 4 voix à des positions décalées, mesure que le mix ne dépasse pas [-1,1] et vérifie que le 5e trigger vole la bonne voix ; écoute via rt_player étendu (touches = pads).

## J4 — PSRAM (mémoire Teensy) [EN COURS — API buffer externe] — P1

- Objectif : les échantillons chargés vivent en PSRAM (8 Mo) sur Teensy, pas en RAM interne (1 Mo).
- Fichiers : future couche Teensy `src/teensy/` (`WavReader` SD + propriétaire du buffer PSRAM) ; le moteur portable ne dépend pas d'Arduino.
- Spec : le chargeur commence par `wav_probe()`, calcule la taille PCM16 finale, puis `wav_decode()` écrit sans allocation dans un buffer fourni par la plateforme. Sur Teensy, ce buffer est une grande zone allouée une seule fois via `extmem_malloc` ; sur PC, `WavData` reste propriétaire de son vector. 1 s stéréo 16-bit = 176 Ko ; un break 6 s ≈ 1 Mo → ~45 s de stéréo dans 8 Mo. Les voix consomment une `PcmView` non propriétaire et ne dépendent pas du type d'allocation.
- DoD : sur PC rien ne change ; sur Teensy, un sketch de test charge un WAV depuis la SD dans la PSRAM et mesure la mémoire libre (RAM interne quasi intacte, PSRAM consommée).
- Vérification : compile arduino-cli + test réel quand le matériel arrive (sinon : vérification du code + bench RAM sur PC simulé).

## J5 — Slices : découpe + mapping 12 pads [À FAIRE] — P1

- Objectif : découper le break en 12 morceaux jouables depuis les pads.
- Fichiers : `src/engine/slicer.h` + `slicer.cpp` (découpe), extension SamplePlayer (startFrame/endFrame), test_native.
- Spec : une slice = intervalle [start, end] en frames DANS le buffer (pas de copie des échantillons). SamplePlayer gagne setRange(start, end) : trigger() repart de start, l'arrêt se fait à end. 12 slices mappées sur les 12 pads (matrice : pad → slice). Le fichier n'est jamais modifié.
- DoD : chaque pad joue son segment, pitch indépendant par pad, retrig sans clic.
- Vérification : test PC (12 pads → 12 segments distincts, fréquences/durées vérifiées) + écoute.

## J6 — Auto-chop : transients / grille 16th / random [À FAIRE] — P2

- Objectif : générer automatiquement les 12 slices à la pose du break (3 modes au choix).
- Fichiers : `src/engine/auto_chop.h` + `auto_chop.cpp`.
- Spec : (a) transients — énergie par fenêtre (~10 ms) + seuil adaptatif, picks = débuts de slices ; (b) grille — durée totale ÷ 16 → slices régulières ; (c) random — 12 points aléatoires. Sortie = vecteur de slices. La détection doit être robuste sur l'Amen Break canonique (6 s, ~33 coups, 16th).
- DoD : les 3 modes produisent 12 slices exploitables (mode transients : les coups tombent sur les attaques perceptibles).
- Vérification : test sur un break réel + visualisation (impression des positions en secondes) ; affinage des seuils.

## J7 — One-shot + loop + test d'écoute [À FAIRE] — P1

- Objectif : modes one-shot (actuel) et loop ; première session d'écoute complète sur PC.
- Fichiers : SamplePlayer (mode), test_native/rt_player.cpp (choix du mode au clavier).
- Spec : mode loop = à la fin du range, pos_ revient à start (pas d'arrêt, pas de clic audible — si clic : court crossfade de ~5 ms). Mode one-shot = comportement J2/J5 actuel.
- DoD : un break boucle sans artefact pendant 30 s ; one-shot s'arrête net.
- Vérification : écoute + test de durée (la boucle ne retourne jamais false).

## J8 — Granulaire [À FAIRE] — P2

- Objectif : mode de lecture granulaire sur un pad (taille de grain, densité, scan, direction, pitch par grain).
- Fichiers : `src/engine/granular.h` + `granular.cpp` (voix granulaire), intégration mode pad.
- Spec : grains de 10-100 ms lus dans le range du pad, position de scan pilotée (fixe, auto, ou encodeur), densité (grains/s), direction (avant/arrière), pitch par grain (speed_ du grain). Le retrig des grains peut être calé sur le tempo global (BPM → frames).
- DoD : un pad granulaire joue un nuage texturé stable, sans clic ni dépassement CPU (mesure du temps de render sur PC).
- Vérification : écoute + mesure (durée de render < budget du bloc).

## J9 — Slice sync + morph [À FAIRE] — P2

- Objectif : les slices suivent le tempo global ; morph continu entre deux slices adjacentes.
- Fichiers : SamplePlayer/slicer (mode slice-sync + crossfade).
- Spec : slice sync — retrig automatique de la slice à chaque beat (BPM global → frames par beat), la slice se joue en boucle synchro. Morph — paramètre continu 0-100 % : à 50 %, la lecture fusionne slice n et n+1 (crossfade pondéré, héritage Shredder).
- DoD : changer le BPM recalcule les retrigs en live ; le morph glisse d'une slice à l'autre sans saut.
- Vérification : écoute au métronome (BPM 80-140) + vérif des périodes de retrig par analyse.

## J10 — Live FX Repeat V1 [IMPLÉMENTÉ — ÉCOUTE À TESTER] — P1

- Liste verrouillée : `BLANK`, `REPEAT`, `REVERSE`, `TRANCE GATE`. Reverse et Trance Gate restent assignables sans DSP pour l'instant.
- Fichiers : `src/engine/fx/live_repeat.h/.cpp`, test ciblé `test_native/live_repeat_test.cpp`, intégration après le mix global de `VoiceManager` dans `rt_player`.
- Spec Repeat : le maintien du pad capture et boucle l'audio immédiatement antérieur à l'appui. E2 règle le dry/wet (100 % par défaut). E3 sélectionne dynamiquement `1/4`, `1/8`, `1/16`, `1/32` (défaut `1/4`). E7 recalcule la longueur au BPM live. E4 reste exclusivement la vitesse sample.
- Temps réel : `LiveRepeat` n'alloue aucune mémoire et reçoit de l'appelant quatre buffers float (historique/copie gelée stéréo) avec leur capacité. `requiredBufferFrames(sampleRate)` dimensionne le pire cas `1/4` à 20 BPM ; la future couche Teensy pourra fournir ces zones depuis la PSRAM. Activation, relâchement, amount et changement de longueur utilisent des transitions de 128 frames, et chaque couture périodique est lissée sans changer la période BPM. Le segment initial reste intact pendant un maintien prolongé.
- Vérification native : `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic test_native/live_repeat_test.cpp src/engine/fx/live_repeat.cpp -I src/engine -o live_repeat_test.exe` puis exécuter le test. Les tests numériques et le build Windows passent, mais le Repeat n'a pas encore été testé à l'écoute par Arthur. Le lancement de `start_firmware.ps1` vérifie uniquement que le harness démarre, pas la qualité sonore de l'effet.
- Reste matériel : brancher le même processeur après le mix dans le futur `AudioStream`, allouer ses quatre buffers en PSRAM, mesurer `AudioProcessorUsageMax()` et écouter les transitions sur Teensy. Aucune mesure Teensy n'a encore été réalisée.

### Checkpoint 16/08/2026 — Live FX Repeat V1

- Direction Live FX mise à jour : `REPEAT`, `REVERSE`, `TRANCE GATE` ; Disperser et Resonator sont retirés. Reverse et Trance Gate restent sans DSP.
- Repeat implémenté après le mix global : capture de l'audio précédant l'appui, défaut `1/4` et 100 % wet, E2 = dry/wet, E3 = `1/4` / `1/8` / `1/16` / `1/32`, E7 = BPM live.
- Contrôles centralisés dans `docs/CONTROLS.md`. Le processeur reçoit des buffers externes pour permettre leur futur placement en PSRAM et n'alloue rien pendant le traitement.
- Vérifié uniquement par tests automatisés natifs, compilation du harness et démarrage de `start_firmware.ps1`. **Non testé à l'écoute et non testé sur Teensy : validation fonctionnelle en attente.**

## J11 — Séquenceur minimal [À FAIRE] — P2

- Objectif : 1 pattern × 16 pas, trigger + chance par pas, play/stop, édition via les pads.
- Fichiers : `src/engine/sequencer.h` + `sequencer.cpp`.
- Spec : horloge = BPM global (frames par 16th). Chaque pas : pad/slice à déclencher + chance 0-100 %. Édition : pads = pas, encodeur = chance. Play/stop global. Pas de multi-patterns en V1 (après rentrée).
- DoD : un break joue en boucle avec des triggers aux pas programmés ; la chance saute des pas de façon audible ; le tempo change en live sans désync.
- Vérification : écoute + test de timing (les retrigs tombent dans ±2 ms de la grille).

## J12 — Couche Teensy : drivers + Audio Library [À FAIRE] — P1

- Objectif : faire tourner le moteur sur la vraie machine.
- Fichiers à créer : `firmware.ino` et `src/teensy/` (SD, PSRAM, AudioStream, matrice 21 pads, 7 encodeurs, OLED SSD1306 I2C). Aucun de ces fichiers n'existe encore au checkpoint `5cff9c5`.
- Spec : initialiser SGTL5000/I2S et brancher un objet AudioStream qui appelle le render() du moteur. Le backend SD enveloppe `File` dans `WavReader`, le backend mémoire réserve une zone via `extmem_malloc()`, puis la chaîne est SD → `wav_probe()` → contrôle capacité → `wav_decode()` directement en PSRAM → `PcmView`. Matrice : 21 switches (12 chops + 8 fx + shift), lecture sans ghosting. Encodeurs : incrémentaux, jamais de saut de valeur. OLED : contexte des pages et browser.
- DoD : compile arduino-cli (`arduino-cli compile --fqbn teensy:avr:teensy41 firmware`) ; sur matériel : son dans le casque, pads déclenchent les voix, encodeurs changent le pitch.
- Vérification : compile + test réel (matériel attendu fin août).

## J13 — UI pages + USB-MIDI [EN COURS — catalogue/browser + face avant PC] — P1

- Objectif : les 7 encodeurs en pages (pad / globale / browser) + USB-MIDI.
- Fichiers : logique portable `src/ui/` et `src/browser/`, backends PC `test_native/screen_preview.cpp` et `sample_catalog_scanner.cpp`, futur backend OLED/SD/MIDI sous `src/teensy/`.
- Spec contrôles : E1 porte la navigation duale (voice = SD, FX = liste et assignation). E2 règle l'amount dry/wet du Repeat, E3 sa division, E4 reste la vitesse sample, E5 le mode, E6 est réservé et E7 le BPM. Mode cible le dernier chop joué ; chaque chop mémorise son mode. Shift = couche secondaire (volume, tap tempo, etc.). USB-MIDI : notes sur les pads (canal configurable), CC sur les encodeurs. Voir `docs/CONTROLS.md`.
- Spec écran : framebuffer monochrome 128×32 portable. Au repos : nom du break, BPM et mode du chop sélectionné. Tout changement de paramètre ouvre un overlay pendant 1 s, avec zone 32×32 réservée au symbole/illustration, nom technique lisible et valeur forte. Direction artistique : anges, ailes, croix et auréoles ; 3 états visuels (calme / tendu / furieux) et micro-animations ponctuelles, à produire après validation fonctionnelle. Les pages utilitaires (browser, erreurs) privilégient la lisibilité.
- Spec browser : la racine et chaque dossier affichent leurs WAV directs et uniquement les sous-dossiers ayant au moins un WAV descendant. L'arborescence et la casse d'affichage de la carte sont conservées ; comparaisons et extension `.wav` sont insensibles à la casse. La sélection charge un seul WAV à la fois sans dupliquer le PCM entre les pads/voix.
- DoD : le visualiseur PC suit les contrôles et revient à l'écran Performance 1 s après un overlay ; on navigue dans la SD depuis le browser et on charge un break sans reboot ; le BPM se règle live ; un DAW reçoit les notes.
- Vérification : `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic test_native/screen_ui_test.cpp src/ui/screen_ui.cpp -I src/ui -o screen_ui_test.exe` puis `.\screen_ui_test.exe` + visualiseur PC + test réel sur matériel.

### Checkpoint 16/08/2026 — fondation sample browser

- Commit fonctionnel : `5cff9c5` (`feat: add sample browser foundation`) sur `dev`.
- Livré côté portable/PC : `PcmView` non propriétaire, `SamplePlayer` à plage `[startFrame, endFrame)`, pool fixe de 4 voix partageant le même PCM, `wav_probe()` + `wav_decode()` vers un buffer externe sans allocation, catalogue hiérarchique FAT case-insensitive, scanner récursif PC, écran browser 128×32 et chargement d'un WAV sélectionné sans redémarrage.
- Tests ajoutés : `voice_manager_test.cpp`, `wav_loader_reader_test.cpp`, `sample_catalog_test.cpp`, `sample_catalog_scanner_test.cpp` et extension de `screen_ui_test.cpp`. Les tests natifs stricts, les cinq formats WAV, la compilation hôte `-DARDUINO`, le harness Windows et `start_firmware.ps1` passent.
- Limite importante : aucune allocation PSRAM réelle ni lecture SD Teensy n'existe encore. Le PC utilise toujours `WavData::samples` et le scanner `std::filesystem`. Il ne faut pas considérer J4 ou J12 terminés.
- Prochaine étape obligatoire : créer la couche `src/teensy/` et le vrai point d'entrée firmware. Implémenter un `WavReader` autour de `File`, initialiser le lecteur intégré avec `SD.begin(BUILTIN_SDCARD)`, puis fournir au catalogue les chemins WAV récursifs de la carte.
- PSRAM : vérifier `external_psram_size`, appeler `extmem_malloc()` une seule fois au démarrage pour réserver la grande zone PCM16, tester le pointeur et sa capacité, puis appeler `wav_probe()` avant `wav_decode()` directement dans cette zone. Ne jamais allouer, libérer, scanner la SD ou convertir dans le callback audio.
- Remplacement d'un break : arrêter les voix, vérifier format/taille, décoder le nouveau fichier, construire la `PcmView`, puis réaffecter les plages des pads. Un même PCM doit rester partageable par plusieurs pads/voix avec des `startFrame/endFrame` différents, sans copie.
- Intégration matérielle restante : objet `AudioStream` vers SGTL5000/I2S, OLED réel, encodeur Browser, matrice de pads, mapping des 12 slices, gestion d'erreurs SD/PSRAM et mesures `AudioProcessorUsageMax()`/mémoire sur Teensy.
- Risques connus à traiter : le 5e trigger vole actuellement la voix la plus ancienne sans crossfade ; ajouter une transition courte et faire une écoute dédiée avant de marquer J3 terminé. Le chargement PC conserve temporairement ancien et nouveau `WavData`, alors que le backend Teensy à zone PSRAM unique devra choisir une politique explicite en cas d'échec de chargement.
- Lecture multi-fréquence : `VoiceManager` convertit la vitesse utilisateur en pas source (`speed * sampleRate / outputSampleRate`) ; les WAV mono/stéréo restent en PCM16 à leur fréquence native, sans resampling au chargement ni travail supplémentaire dans le callback.
- Vitesse performative : plage 25-400 % par pas de 5 %, affichage en pourcentage et rampe de 128 frames. Un changement vise la voix active du dernier pad joué, conserve sa position de lecture et devient la vitesse initiale de son prochain trigger.
- Retrigger par pad : `VoiceManager::trigger(PadId, ...)` remplace la voix active du même pad au lieu de l'empiler. Les tests couvrent le remplacement par une autre plage, le mix de deux pads distincts et le vol de voix après quatre identités distinctes.

### Checkpoint 16/08/2026 (session 2) — simulation face avant PC

- Commit fonctionnel : `fe80716` (`feat: simulate front-panel pads, fx pads and encoders`) sur `dev`.
- Face avant PC : numpad 1-6 = pads voix (appui = trigger du break, maintien = navigateur SD), numpad 7-9 = pads FX (maintien = activation). F1-F7 sélectionnent l'encodeur, les flèches le tournent et Entrée le clique. E1 navigue/assigne, E2 règle le dry/wet Repeat, E3 sa division, E4 la vitesse sample, E5 le mode, E6 est réservé et E7 règle le BPM. `docs/CONTROLS.md` remplace ce checkpoint comme source détaillée des contrôles.
- Le maintien des pads est réel sous Windows (polling `GetAsyncKeyState` sur VK_NUMPAD1..9, indépendant du NumLock, front détecté toutes les 10 ms) ; l'écran « PAD n » (`ScreenUi::showFxPad`) affiche le nom du FX en grand et un hint dépendant de l'encodeur sélectionné (E1 = « E1 NAV CLIC ASSIGN », sinon « F1 POUR E1 »).
- Le sim PC est une réduction de la machine : 6 pads voix au lieu de 12 chops (J5 pas fait — tous déclenchent le break entier) et 3 pads FX au lieu de 8 ; le pool reste à 4 voix avec vol de la plus ancienne (J3). Le mapping physique E1-E7 sur le panneau reste à confirmer une fois le câblage défini.
- Prochaines étapes : la priorité reste J12/J4 (couche `src/teensy/`, `WavReader` SD, arène PSRAM) comme indiqué au checkpoint précédent. Côté interaction, le chantier logique suivant est J5 (slices — les pads voix jouent des chops distincts). Le Repeat natif est prêt ; Reverse et Trance Gate restent sans DSP.

## J14 — Polish, démo, git [À FAIRE] — P1

- Objectif : démo présentable pour la rentrée (début septembre).
- Spec : enchaînement démo 30-60 s : poser un break → auto-chop → 12 chops joués live → fx → séquenceur. Checklist : démarrage USB, son casque sans PC, pads fiables, contrôle live sans dropout, doc de câblage/tests. Si matériel pas arrivé : démo PC (rt_player) + flash dès réception.
- DoD : la checklist passe ; tout est poussé sur dev ; Notion à jour.

## Priorités

- P1 (chemin critique démo) : J3, J4, J5, J7, J10 Repeat V1, J12, J13, J14.
- P2 (glisse après rentrée si retard, la démo tient quand même) : J6, J8, J9, J11 et effets R&D de J10.
