# Contrôles

## Couche AMEN (sans Shift)

- E1 preset; E2 root MIDI; E3 variation/inversion; E4 range; E5 forme (`NOTE`, `SUS2`, `TRIAD`, `SUS4`, `SIXTH`, `SEVENTH`, `NINTH`, `ELEVENTH`, `THIRTEENTH`); E6 expression; E7 BPM (30..300).
- Quand un FX est actif, E1..E6 sont contextuels au FX et E7 garde le tempo. L'implémentation hôte expose actuellement une valeur contextuelle commune pour E1..E6; l'adaptateur UI peut lui donner des libellés propres à chaque effet.

## Couche externe (Shift)

Shift + E1..E7 envoie les sept CC configurables du profil actif. Les deltas sont relatifs et bornés 0..127, donc aucun saut à la prise en main. Shift + clic E7 lance Panic.

Deux clics Shift complets en moins de 350 ms (configurable), avec 20 ms de debounce, basculent HOLD. Un clic simple ne le fait pas. À l'activation, les sources physiquement tenues sont capturées. En HOLD, tout nouveau degré rejoint l'ensemble; réappuyer sur un degré relâché physiquement le retire. Un second double-clic libère les sources HOLD sans couper les FX latch. Panic annule tout.

## Teensy 4.1 — mapping issu du netlist réel

Les nombres sont les numéros Arduino lus dans `pinfunction`, et non les numéros de pads du symbole:

- colonnes: COL0=0, COL1=1, COL2=2, COL3=3, COL_SHIFT=4 (SW21 Shift seul);
- lignes: ROW0=5, ROW1=6, ROW2=9, ROW3=14, ROW4=15;
- I2C: SDA=18, SCL=19;
- E1 A/B/SW=16/17/35; E2=22/24/36; E3=25/26/37; E4=27/28/38; E5=29/30/39; E6=31/32/40; E7=33/34/41.

`src/teensy/teensy_pinmap.h` encode ce mapping et vérifie à la compilation l'unicité des pins numériques matrice/encodeurs. Le document Pico historique n'est pas une source valable.
