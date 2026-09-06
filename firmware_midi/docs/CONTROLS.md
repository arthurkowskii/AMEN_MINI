# Contrôles

## Firmware MIDI actif

- SW1–SW12 jouent douze degrés consécutifs de la gamme sélectionnée.
- SW13–SW20 sélectionnent chacun un slot harmonique global tant qu’ils sont tenus : les degrés déjà enfoncés et ceux joués ensuite deviennent des accords. Un autre pad remplace temporairement le slot ; relâcher le pad actif restaure le précédent encore tenu (empilement LIFO). Chaque degré utilise la recette de ce slot dans sa palette d’origine.
- SW21 reste réservé à Shift.
- E1 transpose les douze degrés sur neuf octaves affichées `O0` à `O8`.
- E2 contrôle la fondamentale au démarrage. Un clic alterne entre `ROOT` et `PRESET`; tourner sélectionne la fondamentale chromatique ou l’un des cinq presets, avec bouclage en fin de liste.
- E3–E7 restent inactifs.

`MAJOR` utilise l’ionien, `MINOR` l’éolien et `HARM MIN` le mineur harmonique. Ils partagent, de SW13 à SW20 : `TRIAD`, `SEVENTH`, `NINTH`, `ADD9`, `SUS2`, `SUS4`, `SIXTH`, `SIX9`.

`CINEMA` utilise le lydien avec `OPEN TRIAD`, `OPEN7`, `OPEN9`, `FIFTH`, `SUS9`, `QUARTAL`, `QUINTAL`, `OPEN69`. `DARK` utilise le phrygien avec `TRIAD`, `SEVENTH`, `NINTH`, `ADD2`, `CLUSTER`, `SUS2`, `QUARTAL`, `FIFTH`. Les recettes suivent les degrés de la gamme : `TRIAD` en Do majeur donne C–E–G sur le premier degré, D–F–A sur le deuxième. Les noms quartal/quintal désignent ici des empilements de degrés, pas des intervalles chromatiques fixes. Les données exactes sont dans `../teensy/amen_midi/musical_presets.h` et `harmony_recipes.h`.

Chaque degré fige sa hauteur, son index, sa gamme, son preset et son orthographe au pad-down. Changer de fondamentale, d’octave ou de preset ne réharmonise pas les degrés déjà tenus ; les changements de slot suivants utilisent toujours leur contexte d’origine. Les nouveaux appuis utilisent les réglages courants. Les voix hors de MIDI 0–127 sont repliées par octaves. Sans pad harmonique tenu, les degrés redeviennent des notes seules.

Une hauteur partagée par plusieurs degrés ne reçoit qu’un NoteOn au premier propriétaire et un NoteOff au dernier. Les notes communes aux transitions ne sont pas retriggées. Le voice leading automatique et les patterns ne sont pas encore disponibles ; aucun preset d’artiste n’est présent.

## Écran

L’accueil affiche l’octave de `O0` à `O8`, la fondamentale et le preset en caractères doubles. Un `*` après le preset indique qu’au moins un degré tenu appartient à un autre preset. Sans harmonie active, la ligne inférieure décrit la gamme au repos ou montre la dernière note encore tenue en caractères quadruples. Avec harmonie, elle montre cette note et le nom du slot dans le preset actuellement sélectionné, en caractères doubles ; en présence de `*`, ce nom ne décrit donc pas nécessairement les recettes des anciennes tenues. L’orthographe mémorisée suit les degrés de la gamme, doubles altérations comprises.

Tourner E1 ouvre temporairement un écran `OCTAVE`. Cliquer ou tourner E2 ouvre temporairement un écran explicite `ROOT` ou `PRESET`; les deux points de pagination n’apparaissent que sur ces écrans E2. Appuyer sur SW13–SW20 ouvre temporairement un écran `HARMONY` : le nom de la recette est en caractères quadruples s’il tient, sinon doubles. Ces overlays durent 800 ms.

## Teensy 4.1 — mapping issu du netlist réel

Les nombres sont les numéros Arduino lus dans `pinfunction`, et non les numéros de pads du symbole :

- colonnes : COL0=0, COL1=1, COL2=2, COL3=3, COL_SHIFT=4 (SW21 Shift seul) ;
- lignes : ROW0=5, ROW1=6, ROW2=9, ROW3=14, ROW4=15 ;
- I2C : SDA=18, SCL=19 ;
- E1 A/B/SW=16/17/35 ; E2=22/24/36 ; E3=25/26/37 ; E4=27/28/38 ; E5=29/30/39 ; E6=31/32/40 ; E7=33/34/41.

Le document Pico historique n’est pas une source valable.
