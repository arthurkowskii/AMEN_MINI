# AMEN MIDI

Firmware MIDI frère pour le PCB AMEN_MINI / Teensy 4.1. Le cœur C++17 est portable, statique et testable sur Linux; il n'inclut ni Arduino ni dépendance externe.

## Build et validation Linux

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/amen_midi_harness
```

Le build impose `-std=c++17 -Wall -Wextra -Wpedantic -Werror`. Le runner unique est `amen_midi_tests`.

## Arborescence

- `src/music`: presets et voicings;
- `src/midi`: événements bornés et ownership des notes;
- `src/performance`: sources, snapshots, HOLD, Gate/Latch, Panic;
- `src/profiles`: destinations CC externes;
- `src/controls`: deltas relatifs;
- `src/algorithms`: ordonnanceur FX sans allocation;
- `src/ui`: modèle texte fixe;
- `src/teensy`: pin map réel et interface d'adaptation sans Arduino;
- `tests`: tests natifs; `apps`: démonstrateur console.

## Statut de validation

Validé sur hôte: compilation stricte, catalogues/voicings, ownership MIDI, interaction, contrôles, FX, HOLD, UI, pin map statique et harness. Non validé sur matériel: scan matriciel électrique, quadrature encodeurs, cadence USB MIDI, écran et latence Teensy. `src/teensy/adapter.hpp` est le contrat d'intégration; le toolchain Teensy n'est pas requis par ce projet. Les presets artistiques sont des prototypes déterministes à auditer musicalement, pas des voicings validés à l'écoute.

## Contrat de drainage MIDI

Les événements sont produits dans un buffer statique. Si ce buffer est saturé au moment d'un `NoteOn`, le moteur conserve une cible de synchronisation bornée, visible via `syncPending()`, sans créer d'owner fantôme. Après avoir envoyé puis vidé le buffer avec `clearEvents()`, l'hôte doit appeler `servicePending(now)` (ou laisser le prochain `tick(now)` le faire) et drainer les événements récupérés. L'adaptateur Teensy fourni applique automatiquement cette séquence. `tick()` et `servicePending()` ne font aucun I/O ni allocation dynamique. Les comparaisons de deadlines restent wrap-safe tant que deux appels de service successifs sont espacés de moins de `2^31` ms; une période sans source ne conserve pas d'ancienne deadline, car le scheduler redémarre lors du passage de zéro à une source active.

`assignFx()` retourne `false` sans modifier le slot lorsque le pad, le type ou le mode est hors plage. Les appels valides retournent `true`.
