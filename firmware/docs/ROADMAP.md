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
- Format interne : **int16 partout** (WavData.samples = vector<int16_t>), stéréo conservée, WAV 44,1 kHz.
  Les buffers de sortie render() sont des floats dans [-1, 1] (division par 32768).
- Tests PC : `firmware/test_native/` — miniaudio (header-only) dans `test_native/third_party/`.
  Compile + écoute Windows : `g++ -std=c++17 -O2 test_native/rt_player.cpp test_native/screen_preview.cpp src/engine/wav_loader.cpp src/engine/sample_player.cpp src/ui/screen_ui.cpp -I src/engine -I src/ui -I test_native -I test_native/third_party -o amen_rt.exe -lole32 -lwinmm -lgdi32 -luser32` puis `amen_rt.exe test_native/test.wav` (touches 1-5 = vitesse, espace = retrig, m = mode, e = effet, [/] = intensité, -/+ = BPM, q = quitter). Le visualiseur reproduit le framebuffer OLED 128×32 dans une fenêtre 640×160 ; les illustrations sont encore des emplacements réservés.
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

## J3 — Pool de 8 voix [À FAIRE] — P1

- Objectif : 8 SamplePlayer simultanés — le sampler devient polyphonique.
- Fichiers : `src/engine/voice_manager.h` + `voice_manager.cpp` + test dans test_native/.
- Spec : pool de 8 voix, chacune = un SamplePlayer + son état (occupée/libre). Allocation : première voix libre ; si toutes occupées : vol de voix (la plus ancienne). Mixage : somme des render() de toutes les voix dans le même buffer ; clipping : clamp final à [-1, 1] (ou soft clip). trigger(pad) doit pouvoir retrigger la MÊME voix en jouant un second son (retrig + mélange, pas de coupure sèche).
- DoD : 8 déclenchements simultanés s'entendent additionnés sans saturation ni artefact ; un 9e vol de voix proprement.
- Vérification : test PC qui déclenche 8 voix à des positions décalées, mesure que le mix ne dépasse pas [-1,1] ; écoute via rt_player étendu (touches = pads).

## J4 — PSRAM (mémoire Teensy) [À FAIRE] — P1

- Objectif : les échantillons chargés vivent en PSRAM (8 Mo) sur Teensy, pas en RAM interne (1 Mo).
- Fichiers : couche Teensy `src/teensy/` (allocateur) ; le moteur ne change pas.
- Spec : sur Teensy, le vector de WavData doit être alloué via extmem_malloc (PSRAM). 1 s stéréo 16-bit = 176 Ko ; un break 6 s ≈ 1 Mo → ~45 s de stéréo dans 8 Mo. Pattern : custom allocator (std::pmr ou allocator template) injecté au chargement — le code du moteur reste identique PC/Teensy.
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

## J10 — Effets expérimentaux jouables [À FAIRE] — P1

- Objectif septembre : trois familles originales et fiables plutôt que huit effets génériques — trance gate, disperser de phase all-pass et résonateur harmonique accordable. Les 8 pads fx pourront rappeler des variantes/presets ; le mapping final dépendra des tests d'écoute.
- Fichiers : `src/engine/fx/` (un fichier par famille), intégration dans le mix, paramètres exposés à l'UI.
- Spec : trance gate = pattern synchronisé au BPM, profondeur et transitions lissées ; disperser = cascade de filtres all-pass sans traitement FFT, quantité/fréquence/spread/mix ; résonateur = banque de filtres accordée sur une fondamentale et une gamme choisies, decay/couleur/mix. Pad fx maintenu = effet actif et les 4 encodeurs du haut pilotent ses paramètres. Relâchement sans clic via rampes courtes.
- DoD : les trois familles produisent des résultats clairement distincts, expressifs sur un Amen Break, sans dropout ni clic d'activation. L'utilisation CPU maximale est mesurée sur Teensy avant d'ajouter une quatrième famille.
- Vérification : tests numériques des enveloppes/filtres + écoute dans `rt_player` + `AudioProcessorUsageMax()` sur Teensy.
- Après septembre / R&D : spectral gate STFT et frequency stretching conscient de la fondamentale. Ne pas en faire une dépendance de la démo tant que latence, suivi de fondamentale et coût CPU ne sont pas mesurés.

## J11 — Séquenceur minimal [À FAIRE] — P2

- Objectif : 1 pattern × 16 pas, trigger + chance par pas, play/stop, édition via les pads.
- Fichiers : `src/engine/sequencer.h` + `sequencer.cpp`.
- Spec : horloge = BPM global (frames par 16th). Chaque pas : pad/slice à déclencher + chance 0-100 %. Édition : pads = pas, encodeur = chance. Play/stop global. Pas de multi-patterns en V1 (après rentrée).
- DoD : un break joue en boucle avec des triggers aux pas programmés ; la chance saute des pas de façon audible ; le tempo change en live sans désync.
- Vérification : écoute + test de timing (les retrigs tombent dans ±2 ms de la grille).

## J12 — Couche Teensy : drivers + Audio Library [À FAIRE] — P1

- Objectif : faire tourner le moteur sur la vraie machine.
- Fichiers : `firmware.ino` (graphe audio existe déjà depuis le 11/08 : oscillateur → I2S → casque), `src/teensy/` (SD, PSRAM, matrice 21 pads, 7 encodeurs, OLED SSD1306 I2C).
- Spec : brancher le moteur au graphe Audio Library (SGTL5000) : un objet AudioStream qui appelle le render() du moteur à la place de l'oscillateur. SD → wav_load → PSRAM. Matrice : 21 switches (12 chops + 8 fx + shift), lecture sans ghosting. Encodeurs : incrémentaux, jamais de saut de valeur. OLED : contexte des pages.
- DoD : compile arduino-cli (`arduino-cli compile --fqbn teensy:avr:teensy41 firmware`) ; sur matériel : son dans le casque, pads déclenchent les voix, encodeurs changent le pitch.
- Vérification : compile + test réel (matériel attendu fin août).

## J13 — UI pages + USB-MIDI [EN COURS — visualiseur PC] — P1

- Objectif : les 7 encodeurs en pages (pad / globale / browser) + USB-MIDI.
- Fichiers : logique portable `src/ui/`, backend PC `test_native/screen_preview.cpp`, futur backend OLED et MIDI sous `src/teensy/`.
- Spec contrôles : les 4 encodeurs du haut sont contextuels ; les 3 latéraux gardent les rôles Mode / Browser / BPM. Mode cible le dernier chop joué ; chaque chop mémorise son mode (one-shot / loop / granular / slice-sync) et une pression sur Mode applique le mode courant aux 12 chops. Pad fx maintenu = les 4 encodeurs du haut contrôlent cet effet. Shift = couche secondaire (volume, tap tempo, etc.). USB-MIDI : notes sur les pads (canal configurable), CC sur les encodeurs.
- Spec écran : framebuffer monochrome 128×32 portable. Au repos : nom du break, BPM et mode du chop sélectionné. Tout changement de paramètre ouvre un overlay pendant 1 s, avec zone 32×32 réservée au symbole/illustration, nom technique lisible et valeur forte. Direction artistique : anges, ailes, croix et auréoles ; 3 états visuels (calme / tendu / furieux) et micro-animations ponctuelles, à produire après validation fonctionnelle. Les pages utilitaires (browser, erreurs) privilégient la lisibilité.
- DoD : le visualiseur PC suit les contrôles et revient à l'écran Performance 1 s après un overlay ; on navigue dans la SD depuis le browser et on charge un break sans reboot ; le BPM se règle live ; un DAW reçoit les notes.
- Vérification : `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic test_native/screen_ui_test.cpp src/ui/screen_ui.cpp -I src/ui -o screen_ui_test.exe` puis `.\screen_ui_test.exe` + visualiseur PC + test réel sur matériel.

## J14 — Polish, démo, git [À FAIRE] — P1

- Objectif : démo présentable pour la rentrée (début septembre).
- Spec : enchaînement démo 30-60 s : poser un break → auto-chop → 12 chops joués live → fx → séquenceur. Checklist : démarrage USB, son casque sans PC, pads fiables, contrôle live sans dropout, doc de câblage/tests. Si matériel pas arrivé : démo PC (rt_player) + flash dès réception.
- DoD : la checklist passe ; tout est poussé sur dev ; Notion à jour.

## Priorités

- P1 (chemin critique démo) : J3, J4, J5, J7, J10 limité aux 3 familles validées, J12, J13, J14.
- P2 (glisse après rentrée si retard, la démo tient quand même) : J6, J8, J9, J11 et effets R&D de J10.
