# Contrôles

## Firmware MIDI actif

- SW1–SW12 jouent douze degrés consécutifs de la gamme sélectionnée.
- SW13–SW20 sélectionnent chacun un slot harmonique global tant qu’ils sont tenus : les degrés déjà enfoncés et ceux joués ensuite deviennent des accords. Un autre pad remplace temporairement le slot ; relâcher le pad actif restaure le précédent encore tenu (empilement LIFO). Chaque degré utilise la recette de ce slot dans sa palette d’origine.
- SW21 reste réservé à Shift.
- E1 transpose les douze degrés sur neuf octaves affichées `O0` à `O8`.
- E2 contrôle la fondamentale au démarrage. Un clic alterne entre `ROOT` et `PRESET`; tourner sélectionne la fondamentale chromatique ou l’un des six presets, avec bouclage en fin de liste.
- Le clic E3 fait défiler les pages persistantes `HARMONY` → `PATTERN` → `NONE` → `HARMONY`. Le démarrage se fait en `HARMONY`.
- En page `PATTERN`, la rotation E3 réassigne le slot pattern du pad supérieur tenu le plus récent ; en `HARMONY` elle est sans effet.
- E4 règle l'espacement du prochain run : 80 ms au démarrage, de 30 à 200 ms par pas de 5 ms.

`MAJOR` utilise l’ionien, `MINOR` l’éolien et `HARM MIN` le mineur harmonique. Ils partagent, de SW13 à SW20 : `TRIAD`, `SEVENTH`, `NINTH`, `ADD9`, `SUS2`, `SUS4`, `SIXTH`, `SIX9`.

`CINEMA` utilise le lydien avec `OPEN TRIAD`, `OPEN7`, `OPEN9`, `FIFTH`, `SUS9`, `QUARTAL`, `QUINTAL`, `OPEN69`. `DARK` utilise le phrygien avec `TRIAD`, `SEVENTH`, `NINTH`, `ADD2`, `CLUSTER`, `SUS2`, `QUARTAL`, `FIFTH`. `CHROMATIC` utilise les douze demi-tons : SW1 à SW12 couvrent exactement une octave, un demi-ton par pad. Sa palette reprend les huit noms de la palette partagée avec des intervalles en demi-tons — `TRIAD` = 0,4,7 (C E G depuis Do), `SEVENTH` = 0,4,7,11, `NINTH` = 0,4,7,11,14, `ADD9` = 0,4,7,14, `SUS2` = 0,2,7, `SUS4` = 0,5,7, `SIXTH` = 0,4,7,9, `SIX9` = 0,4,7,9,14. Les recettes suivent les degrés de la gamme : `TRIAD` en Do majeur donne C–E–G sur le premier degré, D–F–A sur le deuxième ; en CHROMATIC, un degré est un demi-ton et les mêmes noms donnent les intervalles ci-dessus depuis chaque pad. Les noms quartal/quintal désignent ici des empilements de degrés, pas des intervalles chromatiques fixes. Les données exactes sont dans `../teensy/amen_midi/musical_presets.h` et `harmony_recipes.h`.

Chaque degré fige sa hauteur, son index, sa gamme, son preset et son orthographe au pad-down. Changer de fondamentale, d’octave ou de preset ne réharmonise pas les degrés déjà tenus ; les changements de slot suivants utilisent toujours leur contexte d’origine. Les nouveaux appuis utilisent les réglages courants. Les voix hors de MIDI 0–127 sont repliées par octaves. Sans pad harmonique tenu, les degrés redeviennent des notes seules.

Une hauteur partagée par plusieurs degrés ou le run ne reçoit qu’un NoteOn au premier propriétaire et un NoteOff au dernier. Les notes communes aux transitions ne sont pas retriggées.

## Page PATTERN

Les huit pads supérieurs portent chacun un pattern assignable, énumérés de SW13 à SW20 par défaut : `RUN UP`, `RUN DOWN`, `UP DOWN`, `DOWN UP`, `THIRDS UP`, `THIRDS DN`, `ARP UP`, `ARP DOWN`. Tous sont actifs ; il n’y a pas de slot `EMPTY`. L’assignation vit en RAM et revient à ces valeurs par défaut à chaque démarrage.

Deux ordres de déclenchement sont équivalents :

- tenir un pad inférieur puis un pad supérieur déclenche le pattern depuis le pad inférieur tenu le plus récent ;
- tenir un pad supérieur puis appuyer un pad inférieur déclenche le pattern depuis ce pad.

Le pattern remplace temporairement la voix manuelle du pad source (l’accord entier s’il tenait un rôle harmonique). À la fin du run ou à son annulation, la voix tenue est restaurée si le pad est encore physiquement enfoncé ; relâcher la source pendant le run empêche la restauration. Un seul run joue à la fois : un nouveau déclenchement transfère la suppression et la restauration de façon transactionnelle. Le pad supérieur tenu le plus récent s’applique aux appuis inférieurs suivants ; relâcher ce pad restaure le précédent encore tenu.

Chaque contour part de la note choisie : les runs de gamme montent ou descendent de huit notes (une octave, extrémités comprises), les ping-pong `UP DOWN` / `DOWN UP` font quinze notes sans redoubler les sommets, les tierces montantes jouent les paires explicites 0,2 · 1,3 · 2,4 · 3,5 · 4,6 · 5,7 et les descendantes leur miroir 0,−2 · −1,−3 · −2,−4 · −3,−5 · −4,−6 · −5,−7, et les arpèges déploient la triade aux degrés 0,2,4,7 en montant et 0,−3,−5,−7 en descendant. Le run s’arrête avant de sortir de MIDI 0–127, sans repli ni repliage. La gamme, le départ et l’espacement sont figés au déclenchement ; la rotation E3 pendant le run modifie l’assignation future sans altérer le run actif.

## Page NONE

En page `NONE`, les huit pads supérieurs deviennent des notes : les vingt pads jouent vingt degrés consécutifs de la gamme sélectionnée, SW13–SW20 poursuivant l’échelle là où SW1–SW12 s’arrêtent (en Do majeur, SW13 = A5, SW14 = B5, SW15 = C6… ; en `CHROMATIC`, une octave et huit demi-tons). Chaque pad fige son contexte comme les autres degrés et son orthographe apparaît sur l’OLED. Les nouveaux appuis y sont toujours des notes seules, même si un slot harmonique reste tenu depuis `HARMONY` ; les tenues antérieures gardent leur rôle jusqu’au relâchement, conformément au contrat général. Aucun pattern ne se déclenche en `NONE` ; entrer dans `NONE` depuis `PATTERN` annule le run actif et restaure la source tenue.

Revenir en `HARMONY` ou passer en `NONE` avec E3 annule le run et restaure la source tenue. Chaque pad conserve son rôle jusqu’au relâchement, même à travers les changements de page. Un pad supérieur tenu comme pattern en page `HARMONY` ou `NONE` ne déclenche rien. Chaque note dure un pas, dernière note comprise. L'horloge est interne et libre, sans synchronisation MIDI. Si la boucle prend du retard, les pas expirés sont sautés plutôt que rejoués en rafale. Pour des appuis reçus dans le même snapshot, E3 est traité d'abord, puis les pads supérieurs, puis les inférieurs.

## Écran

L'accueil utilise trois lignes en caractères doubles : octave et fondamentale, preset et dernière note tenue, puis harmonie/description de gamme ou état du pattern courant suivi de `IDLE`, `READY` ou `PLAY`. `HARM`, `PATT` ou `NONE` reste visible en haut à droite, y compris pendant les overlays. Un `*` après le preset indique une ancienne tenue appartenant à un autre preset ; le nom harmonique affiché concerne le preset sélectionné. L'orthographe des notes tenues reste mémorisée.

Tourner E1 ouvre temporairement un écran `OCTAVE`. Cliquer ou tourner E2 ouvre temporairement un écran explicite `ROOT` ou `PRESET`; les deux points de pagination n’apparaissent que sur ces écrans E2. Appuyer sur SW13–SW20 ouvre temporairement un écran `HARMONY` : le nom de la recette est en caractères quadruples s’il tient, sinon doubles. Ces overlays durent 800 ms.

En page pattern, les pads supérieurs ouvrent `PATTERN` avec l'état du run ; la rotation E3 sur un pad tenu ouvre `SLOT` avec l'index et le nom réassigné. E3 ouvre `PAGE`, E4 `STEP MS`. Les points E2 sont placés en bas à droite pour laisser la page de jeu visible.

## Teensy 4.1 — mapping issu du netlist réel

Les nombres sont les numéros Arduino lus dans `pinfunction`, et non les numéros de pads du symbole :

- colonnes : COL0=0, COL1=1, COL2=2, COL3=3, COL_SHIFT=4 (SW21 Shift seul) ;
- lignes : ROW0=5, ROW1=6, ROW2=9, ROW3=14, ROW4=15 ;
- I2C : SDA=18, SCL=19 ;
- E1 A/B/SW=16/17/35 ; E2=22/24/36 ; E3=25/26/37 ; E4=27/28/38 ; E5=29/30/39 ; E6=31/32/40 ; E7=33/34/41.

Le document Pico historique n’est pas une source valable.
