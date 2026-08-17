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

## J3 — Pool de 4 voix [MOTEUR NATIF LIVRÉ — écoute matérielle en attente] — P1

- Objectif : 4 SamplePlayer simultanés — le sampler devient polyphonique tout en conservant de la marge CPU pour les effets.
- Fichiers : `src/engine/voice_manager.h` + `voice_manager.cpp` + test dans test_native/.
- Spec : pool de 4 voix, chacune = un SamplePlayer + son état (occupée/libre) et son `PadId`. Les voix référencent les échantillons partagés sans les copier : la durée du break pèse sur la PSRAM, tandis que le nombre de voix actives pèse sur le CPU. Un nouveau trigger du même pad arrête/remplace immédiatement sa voix active ; des pads différents restent polyphoniques. Allocation : première voix libre ; si quatre pads distincts sont actifs : vol de la voix la plus ancienne avec **crossfade de 64 frames (~1.5 ms)** — fade out linéaire sur la voix volée, inaudible mais anti-clic. Mixage : somme des render() de toutes les voix dans le même buffer ; clipping final à [-1, 1].
- DoD : 4 pads distincts s'entendent additionnés sans saturation ; retrigger un même pad ne double pas son son ; un 5e pad distinct vole la voix la plus ancienne sans clic audible.
- Vérification : test PC qui déclenche 4 voix à des positions décalées, mesure que le mix ne dépasse pas [-1,1] et vérifie que le 5e trigger vole la bonne voix ; écoute via rt_player étendu (touches = pads).

## J4 — PSRAM (mémoire Teensy) [EN COURS — arène robuste 7 MiB livrée] — P1

- Objectif : les échantillons chargés vivent en PSRAM (8 Mo) sur Teensy, pas en RAM interne (1 Mo).
- Fichiers : `src/teensy/psram_arena.h/.cpp` (bootstrap commit `48d9f1c`), `src/teensy/teensy_wav_reader.h/.cpp`, `src/teensy/sample_loader.h/.cpp` ; le moteur portable ne dépend pas d'Arduino.
- Spec : le chargeur commence par `wav_probe()`, calcule la taille PCM16 finale, puis `wav_decode()` écrit sans allocation dans un buffer fourni par la plateforme. Sur Teensy, ce buffer est une grande zone allouée une seule fois via `extmem_malloc` ; sur PC, `WavData` reste propriétaire de son vector. 1 s stéréo 16-bit = 176 Ko ; un break 6 s ≈ 1 Mo → ~45 s de stéréo dans 8 Mo. Les voix consomment une `PcmView` non propriétaire et ne dépendent pas du type d'allocation.
- DoD : sur PC rien ne change ; sur Teensy, un sketch de test charge un WAV depuis la SD dans la PSRAM et mesure la mémoire libre (RAM interne quasi intacte, PSRAM consommée).
- Vérification : compile arduino-cli + test réel quand le matériel arrive (sinon : vérification du code + bench RAM sur PC simulé).

## J5 — Slices : découpe + mapping 12 pads [SUSPENDU] -  P1

- Objectif : découper le break en 12 morceaux jouables depuis les pads.
- Fichiers : `src/engine/slicer.h` + `slicer.cpp` (découpe), extension SamplePlayer (startFrame/endFrame), test_native.
- Spec : une slice = intervalle [start, end] en frames DANS le buffer (pas de copie des échantillons). SamplePlayer gagne setRange(start, end) : trigger() repart de start, l'arrêt se fait à end. 12 slices mappées sur les 12 pads (matrice : pad → slice). Le fichier n'est jamais modifié.
- DoD : chaque pad joue son segment, pitch indépendant par pad, retrig sans clic.
- Vérification : test PC (12 pads → 12 segments distincts, fréquences/durées vérifiées) + écoute.

## J6 — Auto-chop : transients / grille 16th / random [SUSPENDU — à valider, pas une dépendance démo] — P2

- Objectif : générer automatiquement les 12 slices à la pose du break (3 modes au choix).
- Fichiers : `src/engine/auto_chop.h` + `auto_chop.cpp`.
- Spec : (a) transients — énergie par fenêtre (~10 ms) + seuil adaptatif, picks = débuts de slices ; (b) grille — durée totale ÷ 16 → slices régulières ; (c) random — 12 points aléatoires. Sortie = vecteur de slices. La détection doit être robuste sur l'Amen Break canonique (6 s, ~33 coups, 16th).
- DoD : les 3 modes produisent 12 slices exploitables (mode transients : les coups tombent sur les attaques perceptibles).
- Vérification : test sur un break réel + visualisation (impression des positions en secondes) ; affinage des seuils.

## J7 — One-shot + loop + test d'écoute [MOTEUR/HARNESS LIVRÉS — écoute matérielle en attente] — P1

- Objectif : modes one-shot et loop, avec comportement de latch intégré au mode — le mode définit si le pad reste actif doigt levé.
- Fichiers : SamplePlayer (mode), test_native/rt_player.cpp (choix du mode au clavier).
- Spec :
  - **ONE SHOT** : lecture complète du range. Deux variantes à trancher — AUTO (l'appui déclenche la lecture complète) vs GATE (lecture uniquement tant que le pad est tenu). Le mécanisme de sélection est à définir (E6 `TRIG MODE` ou clic long E5).
  - **LOOP** : latch — l'appui lance la boucle, elle continue doigt levé. Un deuxième appui sur le même pad stoppe. À la fin du range, pos_ revient à start sans clic (crossfade de couture ~5 ms si nécessaire).
  - **GRANULAR** (futur J8) et **SLICE SYNC** (futur J9) : latch également, 2e appui stoppe.
- DoD : un break boucle sans artefact pendant 30 s, 2e appui stoppe net ; one-shot s'arrête à la fin du range ou au relâchement (gate).
- Vérification : écoute + test de durée (la boucle ne retourne jamais false).

## J8 — Granulaire [V0 LIVRÉ — CLOUD par pad ; scan/pitch par grain à venir] — P2

- Objectif : mode de lecture granulaire sur un pad (taille de grain, densité, scan, direction, pitch par grain).
- Fichiers : `src/engine/granular.h` + `granular.cpp` (nuage granulaire), intégration mode pad `CLOUD` (rotation E5), test `granular_test.cpp`.
- V0 livré (plan spectral 7.3) : la plage assignée devient un nuage — grains de 30 à 150 ms, un toutes les ~22 ms, 8 grains simultanés maximum, positions/longueurs déterministes par pad, enveloppes Hann sans clic, PCM emprunté (jamais copié), arrêt en fondu ~10 ms. Mesure native : ~13 ms CPU par seconde d'audio (1,3 %).
- Reste à venir : scan piloté (fixe/auto/encodeur), densité réglable, direction, pitch par grain, retrig synchro BPM.
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

- Liste assignable (8 slots, mapping 8 pads FX) : `BLANK`, `REPEAT`, `REVERSE`, `TRANCE GATE`, `FILTER`, `DELAY`, `BITCRUSH`, `CHAOS`. DSP livrés à ce jour : `REPEAT`, `TRANCE GATE` (spectral gate 8 bandes) et `FREEZE` (gel spectral, ajouté au workflow MUTATE) ; `REVERSE` reste sans DSP, `FILTER`/`DELAY`/`BITCRUSH`/`CHAOS` sont P2 (post-démo).
- Fichiers : `src/engine/fx/live_repeat.h/.cpp`, test ciblé `test_native/live_repeat_test.cpp`, intégration après le mix global de `VoiceManager` dans `rt_player`.
- Spec Repeat : le maintien du pad capture et boucle l'audio immédiatement antérieur à l'appui. E2 règle le dry/wet (100 % par défaut). E3 sélectionne dynamiquement `1/4`, `1/8`, `1/8T`, `1/16`, `1/16T`, `1/32` (défaut `1/4`). Les noms affichés sont abrégés pour l'OLED 128×32 : `1/4`, `1/8`, `1/8T`, `1/16`, `1/16T`, `1/32`. E7 recalcule la longueur au BPM live. E4 reste exclusivement la vitesse sample.
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

## J12 — Couche Teensy : drivers + Audio Library [EN COURS — bootstrap livré] — P1

- Objectif : faire tourner le moteur sur la vraie machine.
- Fichiers créés (bootstrap commit `48d9f1c`) : `firmware.ino` (setup diagnostic), `src/teensy/psram_arena.h/.cpp` (extmem_malloc 8 Mo), `src/teensy/teensy_wav_reader.h/.cpp` (wrapper SD File → WavReader), `src/teensy/sample_loader.h/.cpp` (SD → PSRAM avec validation avant arrêt des voix).
- Reste à créer : intégration AudioStream/SGTL5000 dans le callback render(), driver OLED SSD1306 I2C, matrice 21 pads (12 chops + 8 fx + shift) avec anti-ghosting, 7 encodeurs incrémentaux, USB-MIDI.
- Spec : initialiser SGTL5000/I2S et brancher un objet AudioStream qui appelle le render() du moteur. Le backend SD enveloppe `File` dans `WavReader`, le backend mémoire réserve une zone via `extmem_malloc()`, puis la chaîne est SD → `wav_probe()` → contrôle capacité → `wav_decode()` directement en PSRAM → `PcmView`. Matrice : 21 switches (12 chops + 8 fx + shift), lecture sans ghosting. Encodeurs : incrémentaux, jamais de saut de valeur. OLED : contexte des pages et browser.
- DoD : compile arduino-cli (`arduino-cli compile --fqbn teensy:avr:teensy41 firmware`) ; sur matériel : son dans le casque, pads déclenchent les voix, encodeurs changent le pitch.
- Vérification : compile + test réel (matériel attendu fin août).

## J13 — UI pages + USB-MIDI [EN COURS — logique OLED/browser livrée, MIDI restant] — P1

- Objectif : les 7 encodeurs en pages (pad / globale / browser) + USB-MIDI.
- Fichiers : logique portable `src/ui/` et `src/browser/`, backends PC `test_native/screen_preview.cpp` et `sample_catalog_scanner.cpp`, futur backend OLED/SD/MIDI sous `src/teensy/`.
- Spec contrôles : E1 porte la navigation duale (voice = SD, FX = liste et assignation). E2 règle l'amount dry/wet du Repeat, E3 sa division, E4 la vitesse du pad voix tenu, E5 son mode, E6 est réservé et E7 le BPM. Chaque pad mémorise sa propre vitesse et son mode. Shift = couche secondaire (volume, tap tempo, etc.). USB-MIDI : notes sur les pads (canal configurable), CC sur les encodeurs. Voir `docs/CONTROLS.md`.
- Spec écran : framebuffer monochrome 128×32 portable. Au repos : nom du break, BPM et mode du chop sélectionné. Tout changement de paramètre ouvre un overlay qui **persiste tant que l'encodeur sélectionné tourne** (disparaît 1 s après la dernière interaction). Zone 32×32 réservée au symbole/illustration religieuse, nom technique abrégé et valeur forte. Direction artistique : anges, ailes, croix et auréoles ; 3 états visuels (calme / tendu / furieux) et micro-animations ponctuelles, à produire après validation fonctionnelle. Les pages utilitaires (browser, erreurs) privilégient la lisibilité.
- Spec browser : la racine et chaque dossier affichent leurs WAV directs et uniquement les sous-dossiers ayant au moins un WAV descendant. L'arborescence et la casse d'affichage de la carte sont conservées ; comparaisons et extension `.wav` sont insensibles à la casse. La sélection charge un seul WAV à la fois sans dupliquer le PCM entre les pads/voix.
  - Navigation : liste verticale de 3 lignes (10 px par ligne). E1 tourne = déplace la flèche de sélection.
  - Fichiers longs : après 0.5 s d'arrêt sur une entrée, le nom défile horizontalement à ~30 px/s.
  - Préfixes : `>` pour dossier, `-` pour fichier WAV. Noms tronqués avec `..` si pas en scroll.
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
- Risques connus restant à traiter : le chargement PC conserve temporairement ancien et nouveau `WavData`, alors que le backend Teensy à zone PSRAM unique devra choisir une politique explicite en cas d'échec de chargement. Le vol de la 5e voix dispose désormais de son crossfade linéaire exact de 64 frames, vérifié numériquement ; l'écoute dédiée sur matériel reste à faire.
- Lecture multi-fréquence : `VoiceManager` convertit la vitesse utilisateur en pas source (`speed * sampleRate / outputSampleRate`) ; les WAV mono/stéréo restent en PCM16 à leur fréquence native, sans resampling au chargement ni travail supplémentaire dans le callback.
- Vitesse performative : plage 25-400 % par pas de 5 %, affichage en pourcentage et rampe de 128 frames. Un changement vise la voix active du dernier pad joué, conserve sa position de lecture et devient la vitesse initiale de son prochain trigger.
- Retrigger par pad : `VoiceManager::trigger(PadId, ...)` remplace la voix active du même pad au lieu de l'empiler. Les tests couvrent le remplacement par une autre plage, le mix de deux pads distincts et le vol de voix après quatre identités distinctes.

### Checkpoint 16/08/2026 (session 2) — simulation face avant PC

- Commit fonctionnel : `fe80716` (`feat: simulate front-panel pads, fx pads and encoders`) sur `dev`.
- Face avant PC : numpad 1-6 = pads voix (appui = trigger du break, maintien = navigateur SD), numpad 7-9 = pads FX (maintien = activation). F1-F7 sélectionnent l'encodeur, les flèches le tournent et Entrée le clique. E1 navigue/assigne, E2 règle le dry/wet Repeat, E3 sa division, E4 la vitesse sample, E5 le mode, E6 est réservé et E7 règle le BPM. `docs/CONTROLS.md` remplace ce checkpoint comme source détaillée des contrôles.
- Le maintien des pads est réel sous Windows (polling `GetAsyncKeyState` sur VK_NUMPAD1..9, indépendant du NumLock, front détecté toutes les 10 ms) ; l'écran « PAD n » (`ScreenUi::showFxPad`) affiche le nom du FX en grand et un hint dépendant de l'encodeur sélectionné (E1 = « E1 NAV CLIC ASSIGN », sinon « F1 POUR E1 »).
- Le sim PC est une réduction de la machine : 6 pads voix au lieu de 12 chops (J5 pas fait — tous déclenchent le break entier) et 3 pads FX au lieu de 8 ; le pool reste à 4 voix avec vol de la plus ancienne (J3). Le mapping physique E1-E7 sur le panneau reste à confirmer une fois le câblage défini.
- Prochaines étapes : la priorité reste J12/J4 (couche `src/teensy/`, `WavReader` SD, arène PSRAM) comme indiqué au checkpoint précédent. Côté interaction, le chantier logique suivant est J5 (slices — les pads voix jouent des chops distincts). Le Repeat natif est prêt ; Reverse et Trance Gate restent sans DSP.

### Checkpoint 16/08/2026 (session 3) — cible paramètres « pad maintenu »

- Commit fonctionnel : `ab28049` (`feat: target held pad for speed and mode edit`) sur `dev`.
- E4/E5 ne ciblent plus le « dernier pad joué » mais le pad voix **tenu** : chaque pad mémorise sa propre vitesse (25-400 %) et son propre mode, appliqués en direct à la voix active (rampe 128 frames, sans retrigger) et réutilisés au prochain trigger. Clic E4 = 100 % du pad tenu, clic E5 = ONE SHOT du pad tenu. Sans pad tenu : hints `E4/E5 TENIR PAD`, aucun effet.
- Grammaire « tenir le pad = cibler » : E1 ouvre le navigateur SD du pad tenu (l'appui seul ne change plus l'écran) ; le relâchement retombe sur le dernier pad encore tenu (règle « le dernier appuyé gagne »).
- Implémentation : `UiSimulation` gagne un tableau `PadSettings` par pad + `heldVoicePad` (ordre d'appui mémorisé pour le retarget) ; le callback audio reçoit une paire d'atomiques (pad + vitesse, écriture vitesse puis pad) et appelle `VoiceManager::setPadSpeed`. Le moteur (`voice_manager.cpp`, `sample_player.cpp`) et `ScreenUi` sont inchangés.
- Espace retrigger : dernier pad joué avec SA propre vitesse.
- Vérifié : compile g++ exit 0 (warnings miniaudio vendored uniquement), smoke test PC, `start_firmware.ps1` (« deja compile » + lancement avec les nouveaux contrôles). **Non encore validé à l'écoute par Arthur.**

## J14 — Polish, démo, git [À FAIRE] — P1

- Objectif : démo présentable pour la rentrée (début septembre).
- Spec : enchaînement démo 30-60 s : poser un break → découpe en chops (J5 manuelle ; l'auto-chop J6 est suspendu) → 12 chops joués live → fx → séquenceur. Checklist : démarrage USB, son casque sans PC, pads fiables, contrôle live sans dropout, doc de câblage/tests. Si matériel pas arrivé : démo PC (rt_player) + flash dès réception.
- DoD : la checklist passe ; tout est poussé sur dev ; Notion à jour.

## Décisions nouvelles features — 16/08/2026

Suite au rapport d'investigation samplers (SP-404 / Elektron / MPC) et aux avis d'Arthur :

### ✅ ADOPTÉ — Triplets sur E3 (P1 démo) — IMPLÉMENTÉ

- La division Repeat passe de `1/4 · 1/8 · 1/16 · 1/32` à `+ 1/12 · 1/24` (roll triplet, geste signature du breakbeat).
- Implémenté : 2 valeurs d'enum `EighthTriplet`/`SixteenthTriplet` (scale ×2/3), noms et cycles E3/Linux passés à 6, buffer inchangé (pire cas = `1/4` à 20 BPM), crossfades live existants. Test moteur : 1/12 = 33 frames et 1/24 = 17 frames @ BPM 60 (fixture 100 Hz), périodes exactes.
- À valider à l'écoute par Arthur.

### ✅ ADOPTÉ — Skip Back V1 (P2, après démo)

- Buffer rétrospectif du mix de **15 s** en PSRAM (15 s stéréo 16-bit 44,1 kHz = 2,6 Mo ; break 6 s ≈ 1 Mo ; marge PSRAM ≈ 4,4 Mo restants).
- Capture = **Shift + pad voix** → assigne les 15 dernières secondes au pad. Fusionne l'ancienne idée « resample-to-commit » : même mécanisme de capture rétrospective (jouer d'abord, décider après).
- Écriture annulaire par blocs de 512 frames dans le callback, aucune allocation. Mesurer `AudioProcessorUsageMax()` sur Teensy avant d'étendre à 20-30 s.
- **Pas de provenance/takes numérotés en V1** (suspendu) : capture simple, éventuelle méta « capturé à HH:MM:SS » plus tard.

### ✅ ADOPTÉ — Checkpoint temporaire (P2, après démo)

- Snapshot de l'état (pattern 16 pas + chance, BPM, vitesses/modes des pads, assigns FX) ≈ quelques Ko, copié hors callback → zéro impact temps réel.
- Restore instantané : les gros risques live deviennent gratuits. À définir : gestes save/restore (candidat E6) et sémantique du restore (couper ou laisser finir les voix).

### 🔒 SUSPENDU (à valider plus tard, pas une priorité)

- **Auto-chop (J6 transients/grille/random)** : Arthur n'est pas sûr de le vouloir → J6 reste en recherche, **la démo ne doit pas en dépendre** (le flux démo peut passer par la découpe manuelle J5).
- **Loop bed** (break entier boucle en fond + chops par-dessus) : intéressant, à valider plus tard.
- **Fill comme geste** (pas marqués Fill joués quand on tient un pad FX) : à valider avec le séquenceur J11.
- **Provenance/takes** des captures (voir Skip Back).
- **Tap chop** (découpe manuelle au tapping) : écarté — pas la philosophie de la machine.

### ✅ ADOPTÉ — CHAOS en slot FX (P2)

- **Option A retenue** : `CHAOS` devient le 8e slot FX assignable (liste : BLANK/REPEAT/REVERSE/TRANCE GATE/FILTER/DELAY/BITCRUSH/CHAOS) → aucun nouveau bouton, grammaire existante.
- DSP à définir : transformation globale du mix en un geste (drops/breakdowns).

### ✅ ADOPTÉ — FX roadmap étendue (P2, post-démo)

- 8 slots FX pour les 8 pads physiques. Priorités :
  - P1 : **REVERSE** (lecture inversée du sample, le plus simple à implémenter après Repeat).
  - P2 : **TRANCE GATE** (gate rythmique synchronisée BPM), **FILTER** (state-variable LP/BP/HP, cutoff + résonance), **DELAY** (ping-pong stéréo, feedback, synchro BPM), **BITCRUSH** (réduction résolution + sample rate), **CHAOS** (destruction globale du mix).

### ✅ ADOPTÉ — E6 proposition LFO (P2, post-démo)

- E6 (actuellement réservé) proposé comme LFO assignable : forme (sinus, carré, dents de scie, aléatoire), vitesse (sync BPM ou libre), destination (pitch ou filter cutoff).
- À valider avant implémentation.

### ✅ ADOPTÉ — OLED overlay persistant + noms abrégés (P1)

- L'overlay paramètre reste affiché tant que l'encodeur sélectionné tourne (disparaît 1 s après la dernière interaction, pas 1 s fixe).
- Les noms techniques longs utilisent des abréviations : `1/8T` (Eighth Triplet), `1/16T` (Sixteenth Triplet). Gain de place immédiat sur le 128×32.

## Priorités

- P1 (chemin critique démo) : J3 + **crossfade vol de voix 64 frames**, J4, J5, J7 + **mode=latch**, J10 Repeat V1 + **Triplets E3** + **noms abrégés (1/8T, 1/16T)**, **REVERSE DSP**, J12 + **AudioStream/SGTL5000 callback**, J13 + **overlay persistant + scroll horizontal SD**, J14.
- P2 (glisse après rentrée si retard, la démo tient quand même) : J8, J9, J11, **Skip Back 15 s**, **Checkpoint**, TRANCE GATE, FILTER, DELAY, BITCRUSH, CHAOS, E6/LFO, recherches suspendues (J6, loop bed, fill).

### Checkpoint 16/08/2026 (session 4) — Triplets Repeat

- Commit : `feat: triplet repeat divisions` sur `dev` (hash à reporter après push).
- `LiveRepeat` : enum `RepeatDivision` + `EighthTriplet` (1/12) et `SixteenthTriplet` (1/24), `divisionScale()` = 0,5×2/3 et 0,25×2/3. `requiredBufferFrames` inchangé (le pire cas reste `1/4` à 20 BPM) → aucune réserve PSRAM supplémentaire.
- Harness : `kDivisionNames` à 6 entrées, `repeat_division()` à 6 cases, cycles E3 (Windows) et touche `e` (Linux) modulo 6, overlay `0..5`.
- Test moteur : `testTripletDivisions` — 1/12 → 33 frames et 1/24 → 17 frames @ BPM 60 (fixture 100 Hz), périodes stabilisées exactes. Tous les tests Live Repeat passent.
- Vérifié : test natif strict (-Wall -Wextra -Wpedantic), compile harness, smoke run, `start_firmware.ps1`. **Non encore validé à l'écoute par Arthur.**

### Checkpoint 17/08/2026 — Revue post-analyse + bootstrap Teensy

- Commit `48d9f1c` (`feat: post-review plan, teensy layer bootstrap, gitignore cleanup`) poussé sur `dev`.
- **Revue d'architecture complète** : layout PCB, paradigme interaction, OLED, firmware, adéquation objectif sound design expérimental. Plan détaillé dans `docs/PLAN_REVIEW_AOUT_2026.md`.
- **Décisions d'interaction** : le mode de lecture définit le latch (ONE SHOT = full auto ou gate, LOOP/GRANULAR/SLICE SYNC = latch, 2e appui stoppe). Crossfade vol de voix fixé à 64 frames (~1.5 ms). Navigateur SD : liste verticale 3 lignes + défilement horizontal après 0.5 s. Overlay OLED persistant tant qu'encodeur tourne. Noms techniques abrégés (1/8T, 1/16T).
- **Direction artistique OLED confirmée** : tile religieuse 32×32 avec 3 états visuels (calme/tendu/furieux), sprites après validation fonctionnelle.
- **FX roadmap étendue** : 8 slots (BLANK, REPEAT ✅, REVERSE, TRANCE GATE, FILTER, DELAY, BITCRUSH, CHAOS). E6 proposé comme LFO assignable.
- **Bootstrap Teensy (J4/J12 commencé)** : `firmware.ino` setup() diagnostic, `PsramArena` (extmem_malloc 8 Mo), `TeensyWavReader` (wrapper SD File → WavReader), `SampleLoader` (SD → PSRAM avec validation avant arrêt des voix). Prochaine étape : intégrer AudioStream/SGTL5000 et callback render().
- `.gitignore` : exclut `*.o` et `test_native/samples`.
- **Checkpoint Notion** : non synchronisé (API Notion indisponible — header `Notion-Version` manquant côté serveur).

### Checkpoint 17/08/2026 (session finale) — jalon firmware vérifié

- **PSRAM J4/J12** : `PsramArena` réserve par défaut **7 MiB** sur les 8 MiB installés afin de laisser 1 MiB de marge à l'allocateur et aux futurs buffers fixes. Elle rejette et libère un fallback en RAM interne ainsi que toute plage qui dépasserait la PSRAM physique. Vérifié par `psram_arena_test` et par la compilation Teensy 4.1 ; aucune allocation n'a été mesurée sur la carte réelle.
- **Voix J3** : le vol de la voix la plus ancienne utilise un crossfade **linéaire de 64 frames exactes**. Les queues sont bornées, le retrigger/stop d'un pad supprime ses queues obsolètes sans couper les autres pads, et les tests numériques couvrent l'enveloppe frame par frame.
- **Modes J7** : chaque pad mémorise deux axes indépendants, lecture `ONE SHOT` / `LOOP` et déclenchement `GATE` / `LATCH`. E5 tourne le mode de lecture et son clic bascule Gate/Latch ; **E6 reste réservé**. Les quatre combinaisons et leur retrigger/stop sont couvertes nativement et simulées dans le harness PC, sans prétendre à une écoute ou un essai physique.
- **OLED/browser J13** : chaque interaction renouvelle l'overlay pour **1 s depuis la dernière interaction**. Après **500 ms** d'immobilité, le WAV sélectionné défile à **30 px/s** ; le texte est découpé dans son viewport et ne déborde ni sur le marqueur ni hors du framebuffer **128×32**. La sélection et le dossier sont les seuls événements qui remettent cette chronologie à zéro.
- **Suite native stricte** : compilations C++17 `-O2 -Wall -Wextra -Wpedantic -Werror` et exécutions réussies pour `voice_manager_test`, `live_repeat_test`, `screen_ui_test`, `psram_arena_test`, `sample_catalog_test`, `sample_catalog_scanner_test`, `wav_loader_reader_test` et `test_native/main.cpp`. `python3 test_native/check_formats.py` valide PCM 8/16/24/32 bits et float32, tous identiques à la conversion int16 attendue.
- **Sanitizers** : les sept suites portables ci-dessus repassent avec `-fsanitize=address,undefined -fno-omit-frame-pointer`, sans diagnostic ASan/UBSan.
- **Harness Linux** : compilation complète vers `/tmp/amen_rt_linux` avec `-Wall -Wextra -Wpedantic`, puis smoke borné à 5 s avec `test_native/test.wav` et sortie propre (`0`). ALSA/JACK signalent l'absence de périphérique audio sur cet hôte, sans blocage ; deux warnings `-Wunused-variable` restent propres à la branche Linux du simulateur (`kFxNames`, `kEncoderNames`).
- **Deliverable Windows** : reconstruction en place par `x86_64-w64-mingw32-g++ -std=c++17 -O2 ... -lole32 -lwinmm -lgdi32 -luser32`, mode restauré à `0644`. `file`/`objdump` confirment un PE32+ x86-64 et `strings` retrouve `ONE SHOT`, `LOOP`, `GATE`, `LATCH`, `E1 NAV`, `E5 MODE`, `E6 LFO`, `1/12` et `1/24`. Le rebuild a changé les octets (`c2d70b60…` → `8409ffed…`) à taille identique (1 116 905 octets).
- **Teensy 4.1 compile-only** : `arduino-cli compile --fqbn teensy:avr:teensy41 --output-dir /tmp/amen_mini_teensy41 firmware` réussit (FLASH code 79 576 octets ; RAM1 variables 11 072 octets ; RAM2 variables 12 416 octets). L'hôte prévient que `/etc/udev/rules.d/00-teensy.rules` manque. **Aucun upload, test audio, test PSRAM physique ni validation sur appareil n'a été réalisé.**

### Checkpoint 17/08/2026 — workflow spectral LOAD → MUTATE → COMMIT

- **Nouvelle priorité workflow** : le plan spectral (`~/.hermes/plans/2026-08-17_085030-amen-mini-spectral-workflow.md`) rend la boucle `LOAD → MUTATE → COMMIT` centrale. Assignation automatique du break en 12 plages, mutation par gestes (CLOUD granulaire, TRANCE GATE, FREEZE), puis COMMIT rétrospectif qui transforme les 15 dernières secondes du mix en nouvelle matière assignable.
- **Assignation 12 pads** : `PadAssignmentPlan` (12 plages, invariants garantis : constructeur privé, factories `std::optional`, pas d'aliasing), `TransientDetector` déterministe (fenêtres 5 ms, hop 2,5 ms, séparation 20 ms, plancher −48 dBFS, cap 256 candidats, fallback déterministe, validé sur un break CC0 authentique — ancrage 11/11), `AssignmentSession` atomique (validation AVANT tout effet de bord ; stopAll → échange → publication ; l'échec laisse l'état et le plan précédents intacts).
- **Appui long E1** : machine à états `BrowserInteraction` (600 ms, horloge injectée, une seule ouverture par pression, jamais sur dossier, relâchement sans confirmation) + menu OLED 128×32 `ALL PADS` / `TRANSIENT` / `CANCEL` (nom tronqué puis défilant, marqueur sur TRANSIENT). Entrée confirme, Retour annule sans réinitialiser le défilement.
- **MUTATE — effets spectraux livrés** : `SpectralGate` 8 bandes (échelle LP Linkwitz-Riley télescopique à somme exactement plate, motif 16 pas synchronisé BPM, lissage one-pole 5 ms sans clic, ~1 ms CPU/s d'audio) ; `SpectralFreeze` (FFT 512, capture du spectre d'amplitude, boucle 512-périodique à phases déterministes, rampes wet 10 ms, passthrough bit-exact hors gel, ~5 ms CPU/s) ; `CLOUD` granulaire par pad (mode E5, 8 grains max, PCM emprunté, enveloppes Hann, ~13 ms CPU/s).
- **COMMIT / Skip Back** : anneau rétrospectif statique de 15 s (buffers fournis par l'appelant, PSRAM-ready, zéro allocation dans le callback) ; geste matériel `Shift + pad voix`, touche `v` sur le harness PC ; publication atomique via `AssignmentSession` (l'ancienne matière survit à tout échec). Exercé sur le harness Linux réel : 95 256 frames capturées → 12 plages publiées.
- **Liste FX assignable** : `BLANK`, `REPEAT`, `REVERSE` (sans DSP), `TRANCE GATE`, `FREEZE`. Un seul effet global actif à la fois en V0.
- **Réduction du harness PC** : 6 pads voix jouables (numpad 1-6), 3 pads FX (7-9) ; les pads 7-12 reçoivent leurs plages du plan mais ne sont pas jouables sur PC (en attente du matériel).
- **Limites honnêtes** : tout est vérifié par tests natifs stricts, sanitizers, builds Windows/Linux et revues de conformité/qualité ; **aucune écoute sur Teensy ni mesure AudioProcessorUsageMax() n'a été réalisée** (matériel en attente, règle udev absente). `Reverse`, `Resonator` et `Smear` restent derrière la porte de validation matérielle.
