# AMEN MIDI

Firmware MIDI pour le PCB AMEN_MINI et Teensy 4.1. Le firmware actif est construit progressivement dans `teensy/amen_midi/`; les anciens modules sous `src/` ne participent plus au build CMake ni au sketch.

## État actuel

Les douze pads inférieurs jouent douze degrés consécutifs d’un mode diatonique. E1 règle l’octave. E2 sélectionne la fondamentale ou, après un clic, l’un des sept modes diatoniques. Les huit pads supérieurs, Shift et les autres encodeurs restent réservés aux prochaines étapes.

Le cœur musical minimal et l’interface OLED sont des en-têtes C++17 portables, statiques et sans dépendance Arduino. L’accueil conserve l’octave `O0` à `O8` et le nom complet de la gamme en caractères doubles, puis affiche en grand la dernière note encore tenue avec son orthographe diatonique. Des écrans temporaires explicites accompagnent les réglages d’octave, de fondamentale et de mode. `amen_midi.ino` contient uniquement l’intégration Teensy : scan, USB-MIDI et backend OLED I²C direct.

## Tests natifs

Depuis `firmware_midi/` sous PowerShell :

```powershell
cmake -S . -B "$env:TEMP\amen-midi-build" -G "MinGW Makefiles"
cmake --build "$env:TEMP\amen-midi-build"
ctest --test-dir "$env:TEMP\amen-midi-build" --output-on-failure
```

Le build impose C++17 et traite les avertissements comme des erreurs. `tests/test_main.cpp` valide les gammes, les limites MIDI, les NoteOff après changement d’état et les transitions de l’OLED.

## Teensy 4.1

Le FQBN actif est `teensy:avr:teensy41:usb=serialmidi`. `scripts/build_teensy.sh` compile directement `teensy/amen_midi/` et écrit le HEX dans `teensy_build/`.
