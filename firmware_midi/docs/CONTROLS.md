# Contrôles

## Firmware MIDI actif

- SW1–SW12 jouent douze degrés consécutifs de la gamme sélectionnée.
- SW13–SW20 sont réservés aux futurs pads harmoniques et restent inactifs.
- SW21 reste réservé à Shift.
- E1 transpose les douze degrés par octaves, de -5 à +3.
- E2 contrôle la fondamentale au démarrage. Un clic alterne entre `ROOT` et `SCALE`; tourner sélectionne la fondamentale chromatique ou l’un des sept modes diatoniques.
- E3–E7 et les autres poussoirs restent inactifs.

Les modes disponibles sont ionien (majeur), dorien (mineur avec sixte majeure), phrygien (mineur avec seconde mineure), lydien (majeur avec quarte augmentée), mixolydien (majeur avec septième mineure), éolien (mineur naturel) et locrien (mineur avec seconde mineure et quinte diminuée). Aucun de ces sept modes n’est la gamme mineure harmonique. Les changements de fondamentale, de mode et d’octave n’affectent pas le NoteOff des notes déjà tenues.

## Teensy 4.1 — mapping issu du netlist réel

Les nombres sont les numéros Arduino lus dans `pinfunction`, et non les numéros de pads du symbole :

- colonnes : COL0=0, COL1=1, COL2=2, COL3=3, COL_SHIFT=4 (SW21 Shift seul) ;
- lignes : ROW0=5, ROW1=6, ROW2=9, ROW3=14, ROW4=15 ;
- I2C : SDA=18, SCL=19 ;
- E1 A/B/SW=16/17/35 ; E2=22/24/36 ; E3=25/26/37 ; E4=27/28/38 ; E5=29/30/39 ; E6=31/32/40 ; E7=33/34/41.

Le document Pico historique n’est pas une source valable.
