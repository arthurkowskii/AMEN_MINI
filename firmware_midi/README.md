# AMEN MIDI

## Mise à jour : catalogue de patterns et double ordre de déclenchement

Le clic E3 alterne `HARMONY` / `PATTERN` / `NONE` : en `PATTERN`, les huit pads supérieurs portent huit slots dont l'assignation choisit parmi neuf patterns — `RUN UP`, `RUN DOWN`, `UP DOWN`, `DOWN UP`, `THIRDS UP`, `THIRDS DN`, `ARP UP`, `ARP DOWN`, `REPEAT` — et la rotation E3 réassigne le slot du pad tenu. En `NONE`, les huit pads supérieurs jouent les huit degrés suivants de la gamme, note à note. Le déclenchement marche dans les deux ordres : note inférieure tenue puis pad supérieur, ou pad supérieur tenu puis note inférieure. Tous les patterns bouclent jusqu'au relâchement de la note source ou du pad pattern ; `REPEAT` utilise un gate de 75 %. Un seul run joue à la fois, avec transfert transactionnel. Les assignations vivent en RAM et reviennent aux huit valeurs historiques par défaut au redémarrage. Le clic E1 alterne `OCTAVE`, `TEMPO` et `FREQUENCY`. `TEMPO` règle 20–300 BPM avec des doubles-croches ; `FREQUENCY` règle librement 0,5–50 Hz sur une course logarithmique. Tourner l'une de ces pages active son horloge et préserve la phase du run. E4 est libre. Revenir en `HARMONY` ou `NONE` annule le run ; les rôles des pads tenus, l'ownership partagé et le contexte figé sont préservés. Voir `docs/CONTROLS.md` pour les règles complètes.

`teensy/amen_midi/run_pattern.h` porte le catalogue statique des neuf patterns cycliques, le calcul d'offset de degré signé et la progression du run à horloge fournie par l'appelant. Les tests natifs couvrent les deux ordres de déclenchement, l'arrêt par chacun des deux pads, le gate de `REPEAT`, les horloges BPM/Hz live, le transfert au retrigger, la référence LIFO des notes basses, le LIFO des patterns, l'édition sous pads tenus, les degrés négatifs, les bornes MIDI sur tous les presets, l'atomicité des buffers courts, le dépassement de l'horloge et le saut d'étapes expirées. La validation du geste et de la régularité temporelle sur carte reste à réaliser.

Firmware MIDI pour le PCB AMEN_MINI et Teensy 4.1. Le firmware actif est construit progressivement dans `teensy/amen_midi/`; le moteur d'août sous `src/` a été supprimé du dépôt le 6 septembre 2026.

## État actuel

Les douze pads inférieurs jouent douze degrés consécutifs de la gamme du preset. E1 règle l’octave. E2 alterne au clic entre `ROOT` et `PRESET` : `MAJOR` (ionien), `MINOR` (éolien), `HARM MIN` (mineur harmonique), `CINEMA` (lydien, voicings ouverts), `DARK` (phrygien, clusters), `CHROMATIC` (douze demi-tons, une octave exacte) et `GM KIT` (20 sons General MIDI sur le canal 10). Les six premiers presets partagent quatre palettes de huit recettes construites en degrés de gamme — en `CHROMATIC`, un degré est un demi-ton et la palette reprend les huit noms avec de vrais intervalles (`TRIAD` = C E G, `SEVENTH` = C E G B…) ; les données actives sont dans `musical_presets.h`, `harmony_recipes.h` et `gm_drum_kit.h`, sous `teensy/amen_midi/`.

Les huit pads supérieurs sélectionnent un slot harmonique global momentané en page `HARMONY`, avec priorité au dernier appui et retour LIFO, et un pattern assignable en page `PATTERN`. Chaque degré fige à son enfoncement sa hauteur, son index, sa gamme, son preset et son orthographe : un nouveau preset concerne les nouveaux appuis. Le slot actif transforme chaque degré tenu selon sa palette d’origine. L’ownership global émet un seul NoteOn au premier propriétaire d’une hauteur et le NoteOff au dernier ; les notes communes ne sont pas retriggées. Les actions sont transactionnelles : un buffer trop petit ne modifie ni l’état ni les événements de sortie.

Le cœur musical et l’interface OLED sont des en-têtes C++17 portables, statiques et sans dépendance Arduino. L’accueil affiche l’octave `O0` à `O8`, la fondamentale et le preset ; `*` indique qu’un degré tenu appartient à un autre preset. La dernière note tenue garde son orthographe ; avec harmonie, elle apparaît avec le nom du slot dans le preset sélectionné. Les overlays sont `OCTAVE`, `TEMPO`, `FREQUENCY`, `ROOT`, `PRESET`, `HARMONY`, `PAGE`, `PATTERN` et `SLOT`. `amen_midi.ino` assure le scan, l’USB-MIDI et le backend OLED I²C direct. Voir [les contrôles](docs/CONTROLS.md).

Le voice leading automatique, Shift et E5–E7 restent futurs. Les neuf patterns sont livrés ; l'assignation est en RAM et l'éditeur E3 réassigne le slot tenu. Aucun preset d’artiste n’est proposé. Le [plan de refondation](docs/MUSICAL_REDESIGN_PLAN.md) conserve la recherche et distingue ce socle des ambitions futures ; les autres anciens documents de conception sont historiques.

## Tests natifs

Depuis `firmware_midi/` sous PowerShell :

```powershell
cmake -S . -B "$env:TEMP\opencode\amen-patterns-build" -G "MinGW Makefiles"
cmake --build "$env:TEMP\opencode\amen-patterns-build"
ctest --test-dir "$env:TEMP\opencode\amen-patterns-build" --output-on-failure
```

Le build impose C++17 et traite les avertissements comme des erreurs. `tests/test_main.cpp` couvre les gammes (dont le mode chromatique), les sept presets et leurs recettes (dont le kit General MIDI), les limites MIDI, l’ownership partagé, l’atomicité des buffers insuffisants, le LIFO, les contextes figés entre presets, le rendu OLED exact et le catalogue complet de patterns. Build GCC strict et CTest validés le 6 septembre 2026.

## Teensy 4.1

Le FQBN actif est `teensy:avr:teensy41:usb=serialmidi`. `scripts/build_teensy.sh` compile directement `teensy/amen_midi/` et écrit le HEX dans `teensy_build/`.

La compilation Teensy du mode chromatique a réussi le 6 septembre 2026 (55 844 octets de code, 10 528 de données). `teensy_build/amen_midi.ino.hex` est ignoré par Git. Aucun upload ni écoute matérielle de cette tranche n’a été réalisé ; la version à cinq presets précédente a été auditionnée par Arthur (« super, c’est BEAUCOUP mieux »), ce qui valide la direction musicale sans prétendre à une couverture matérielle exhaustive des patterns.
