# AMEN MIDI

Firmware MIDI pour le PCB AMEN_MINI et Teensy 4.1. Le firmware actif est construit progressivement dans `teensy/amen_midi/`; les anciens modules sous `src/` ne participent plus au build CMake ni au sketch.

## État actuel

Les douze pads inférieurs jouent douze degrés consécutifs de la gamme du preset. E1 règle l’octave. E2 alterne au clic entre `ROOT` et `PRESET` : `MAJOR` (ionien), `MINOR` (éolien), `HARM MIN` (mineur harmonique), `CINEMA` (lydien, voicings ouverts) et `DARK` (phrygien, clusters). Ces cinq presets partagent trois palettes de huit recettes construites en degrés de gamme ; les données actives sont dans `musical_presets.h` et `harmony_recipes.h`, sous `teensy/amen_midi/`.

Les huit pads supérieurs sélectionnent un slot harmonique global momentané, avec priorité au dernier appui et retour LIFO. Chaque degré fige à son enfoncement sa hauteur, son index, sa gamme, son preset et son orthographe : un nouveau preset concerne les nouveaux appuis. Le slot actif transforme chaque degré tenu selon sa palette d’origine. L’ownership global émet un seul NoteOn au premier propriétaire d’une hauteur et le NoteOff au dernier ; les notes communes ne sont pas retriggées. Les actions sont transactionnelles : un buffer trop petit ne modifie ni l’état ni les événements de sortie.

Le cœur musical et l’interface OLED sont des en-têtes C++17 portables, statiques et sans dépendance Arduino. L’accueil affiche l’octave `O0` à `O8`, la fondamentale et le preset ; `*` indique qu’un degré tenu appartient à un autre preset. La dernière note tenue garde son orthographe ; avec harmonie, elle apparaît avec le nom du slot dans le preset sélectionné. Les overlays sont `OCTAVE`, `ROOT`, `PRESET` et `HARMONY`. `amen_midi.ino` assure le scan, l’USB-MIDI et le backend OLED I²C direct. Voir [les contrôles](docs/CONTROLS.md).

Le voice leading automatique, les patterns, Shift et E3–E7 ne sont pas implémentés. Aucun preset d’artiste n’est proposé. Le [plan de refondation](docs/MUSICAL_REDESIGN_PLAN.md) conserve la recherche et distingue ce socle des ambitions futures ; les autres anciens documents de conception sont historiques.

## Tests natifs

Depuis `firmware_midi/` sous PowerShell :

```powershell
cmake -S . -B "$env:TEMP\opencode\amen-presets-build" -G "MinGW Makefiles"
cmake --build "$env:TEMP\opencode\amen-presets-build"
ctest --test-dir "$env:TEMP\opencode\amen-presets-build" --output-on-failure
```

Le build impose C++17 et traite les avertissements comme des erreurs. `tests/test_main.cpp` couvre les gammes, les cinq presets et leurs recettes, les limites MIDI, l’ownership partagé, l’atomicité des buffers insuffisants, le LIFO, les contextes figés entre presets et le rendu OLED exact. Build GCC strict et CTest validés le 6 septembre 2026.

## Teensy 4.1

Le FQBN actif est `teensy:avr:teensy41:usb=serialmidi`. `scripts/build_teensy.sh` compile directement `teensy/amen_midi/` et écrit le HEX dans `teensy_build/`.

Le 6 septembre 2026, le build des presets a réussi (50 788 octets de code, 9 504 de données), puis l’upload a terminé avec le code 0, sans passage manuel en bootloader. `teensy_build/amen_midi.ino.hex` est ignoré par Git. Arthur a auditionné cette version sur le matériel : « super, c’est BEAUCOUP mieux ». Ce retour valide la direction musicale, pas une couverture exhaustive de tous les cas matériels.
