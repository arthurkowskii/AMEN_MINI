# AMEN MIDI

Firmware USB-MIDI pour le PCB AMEN_MINI et Teensy 4.1. Le sketch actif est `teensy/amen_midi/`, sans dépendance au lecteur audio SD ni à la PSRAM. Les fichiers éventuels sous `teensy/amen_midi/src/` sont des restes ignorés de l'ancien moteur, pas une source de développement.

## Contrôles actifs

Les encodeurs **E1 à E4 sont sur le côté gauche**, E1 en bas ; **E5 à E7 sont en haut**, de gauche à droite.

| Encodeur | Rotation | Clic |
|---|---|---|
| E1 | Pad pattern tenu : assignation ; sinon paramètre musical ; Shift prioritaire | `OCTAVE` → `TEMPO` → `FREQUENCY` → `ROOT` → `PRESET` |
| E2 | Page `HARMONY` / `PATTERN` / `NOTES` | Banque de patterns en page `PATTERN` |
| E3 | Vélocité (pas de 3) ou modulation CC1 (pas de 5) | Alterne vélocité / modulation |
| E4 | CC23 absolu, 0 à 127 | Multiplicateur ×1/×2/×4/×8 ; Shift + clic : Smart Voicing |
| E5 | CC20 absolu, 0 à 127 | Multiplicateur ×1/×2/×4/×8 |
| E6 | CC21 absolu, 0 à 127 | Multiplicateur ×1/×2/×4/×8 |
| E7 | CC22 absolu, 0 à 127 | Multiplicateur ×1/×2/×4/×8 |

Démarrage : preset `CHROMATIC`, page `NOTES`, octave `O5`, tempo 120 BPM, vélocité 100, modulation 0, Smart Voicing désactivé, E1 sur `OCTAVE`. Les CC20 à CC23 démarrent à 0, sans émission au boot, chacun avec son multiplicateur ×8. Le clic de chaque encodeur MIDI parcourt ×1 → ×2 → ×4 → ×8 → ×1 sans modifier ni émettre sa valeur ; un cran de rotation ajoute ou retire ce nombre, avec saturation à 0 et 127. Les multiplicateurs sont indépendants et affichés sur l’OLED, sans persistance. Pas de persistance des réglages ni des assignations.

Pour le MIDI Learn : activer le Learn du paramètre dans le VST ou l'hôte, puis tourner E4, E5, E6 ou E7. Les clics règlent uniquement les multiplicateurs ; Smart Voicing se bascule avec Shift + clic E4. L'association et sa sauvegarde appartiennent au VST/hôte. Le canal est celui du preset : 1, ou 10 en `GM KIT`. Il n'y a pas de retour MIDI ni de reprise douce : le paramètre peut sauter à la valeur absolue de l'AMEN au premier mouvement. Voir [CONTROLS.md](docs/CONTROLS.md) pour les détails, limites et broches.

## Orientation horizontale (branche `horizontale`)

**Shift + clic E2** bascule vertical / horizontal, uniquement quand tous les pads sont relâchés (Shift peut rester tenu). Une tentative pendant une tenue est ignorée, sans bascule différée. Le démarrage reste vertical et le choix est volatile. Le clic E2 sans Shift conserve la sélection de banque en `PATTERN`.

Tourner le PCB dans le sens horaire, Teensy à droite. Les notes progressent de gauche à droite, puis de bas en haut. En `HARMONY` / `PATTERN`, les trois rangées du bas jouent quinze degrés, la rangée du haut donne accès aux cinq premiers slots de la palette / banque. En `NOTES`, les vingt pads jouent vingt degrés. E1 réassigne toujours le pattern tenu. Aucun changement du scan électrique, de Shift, des CC ou du rendu OLED. La bascule réussie écrit `ORIENTATION HORIZONTAL` ou `ORIENTATION VERTICAL` sur le port série ; la bannière V2.5 annonce cette fonction.

```text
A1  A2  A3  A4  A5     accords / patterns (notes 16 à 20 en NOTES)
N11 N12 N13 N14 N15
N6  N7  N8  N9  N10
N1  N2  N3  N4  N5
```

La compilation et les tests natifs ne valident pas encore le jeu physique horizontal. L’écran reste dans son orientation actuelle.

## Jeu (disposition verticale)

Les vingt pads jouent vingt degrés en `NOTES`. En `HARMONY`, les douze pads inférieurs jouent des degrés et les huit supérieurs sélectionnent des recettes d'accords, avec priorité au dernier appui et retour LIFO. En `PATTERN`, ces huit pads sélectionnent des patterns, avec six banques : ORCH, FUT. ARP, FUT. PATTERN, KAWAII, REPEAT et NOIR. Le déclenchement fonctionne note puis pattern, ou pattern puis note. Un seul run tourne à la fois ; relâcher sa note source ou son pad pattern l'arrête. Les assignations vivent en RAM. Les quinze presets et leurs recettes sont définis dans `musical_presets.h`, `harmony_recipes.h` et `diatonic_scales.h`. `GM KIT` joue vingt percussions sur le canal 10.

Chaque degré fige sa hauteur, sa gamme, son preset et son orthographe à l'appui. Les changements ne déplacent pas les notes déjà tenues. Une hauteur partagée reçoit un seul NoteOn au premier propriétaire et un seul NoteOff au dernier. Le cœur musical applique ses changements de façon transactionnelle : un buffer MIDI trop petit ne publie aucun changement partiel. Les horloges BPM/Hz sont internes, sans synchronisation MIDI externe.

L'OLED indique les notes, la page et les réglages. Les overlays durent 800 ms ; l'accueil conserve un rappel du paramètre E1 sélectionné. Le scan utilise des lignes inactives haute impédance, 3 µs de stabilisation et un debounce symétrique de 5 ms. Le backend OLED est fragmenté pour limiter son impact sur les patterns.

## REAPER sous Windows

Si une reconnexion exige **Reset all MIDI devices**, voir [la procédure REAPER](docs/REAPER_WINDOWS.md) : extension tierce utilisée, installation vérifiée, limites et désinstallation. Cette solution côté hôte ne fait pas partie du firmware.

## Architecture

- `simple_midi_controller.h` : notes, accords, Shift, vélocité, modulation et ownership.
- `front_panel.h` : routing portable des sept encodeurs, navigation E1 et MIDI CC.
- `run_pattern.h` : catalogue de patterns et horloge à temps fourni par l'appelant.
- `oled_ui.h` : framebuffer portable et rendu OLED.
- `amen_midi.ino` : entrées matérielles, USB-MIDI et I²C.

## Tests natifs

Depuis la racine du dépôt, sous Linux :

```sh
cmake -S firmware_midi -B "$TMPDIR/amen-midi-build"
cmake --build "$TMPDIR/amen-midi-build" -j4
ctest --test-dir "$TMPDIR/amen-midi-build" --output-on-failure
```

Sous PowerShell, depuis `firmware_midi/` :

```powershell
cmake -S . -B "$env:TEMP\opencode\amen-patterns-build" -G "MinGW Makefiles"
cmake --build "$env:TEMP\opencode\amen-patterns-build"
ctest --test-dir "$env:TEMP\opencode\amen-patterns-build" --output-on-failure
```

Trois exécutables C++17 avec avertissements traités comme erreurs : les tests musicaux existants et `tests/test_front_panel.cpp` pour le routing, la priorité Shift, les CC, les bornes, les entrées invalides et les rendus OLED exacts ; `tests/test_orientation.cpp` couvre les deux orientations, le mapping physique, les quinze notes, les cinq slots, les changements de page, les buffers insuffisants et la bascule à vide. Une exécution hôte ne valide pas le geste matériel ni le MIDI Learn dans un VST.

## Teensy 4.1

Le FQBN actif est `teensy:avr:teensy41:usb=serialmidi`, core `teensy:avr` 1.62.0. Depuis la racine :

```sh
bash firmware_midi/scripts/build_teensy.sh
```

Le HEX est écrit dans `firmware_midi/teensy_build/` et ignoré par Git. La compilation ne prouve ni le téléversement ni le bon fonctionnement sur l'instrument. Les anciens documents de conception et la checklist de bring-up d'août sont historiques ; le mapping actif est décrit ci-dessus et dans [CONTROLS.md](docs/CONTROLS.md).
