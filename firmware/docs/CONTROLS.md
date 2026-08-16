# AMEN_MINI - Controls

Ce document est la source claire des controles de performance actuels. Le harness PC reduit la facade a 6 pads voix et 3 pads FX.

## Pads

- Pads voix 1-6 (`Numpad 1-6`) : l'appui declenche le break. Le maintien ouvre le navigateur SD.
- Pads FX 7-9 (`Numpad 7-9`) : le maintien active l'effet assigne, le relachement le coupe.
- Le Repeat capture l'audio du mix global immediatement anterieur a l'appui. Il ne retrigger pas une voix individuelle.
- Liste d'assignation : `BLANK`, `REPEAT`, `REVERSE`, `TRANCE GATE`.
- `BLANK` desassigne le pad. `REVERSE` et `TRANCE GATE` sont assignables mais n'ont pas encore de DSP.

## Encodeurs

- `E1 NAV` : sur un pad voix maintenu, navigue dans la SD et le clic entre/charge. Sur un pad FX maintenu, navigue dans la liste FX et le clic assigne.
- `E2 AMOUNT` : mix dry/wet du Repeat, de 0 a 100 %. La valeur par defaut est 100 %.
- `E3 DIVISION` : longueur du Repeat, choix live `1/4`, `1/8`, `1/16`, `1/32`. La valeur par defaut est `1/4`.
- `E4 SPEED` : vitesse du sample du dernier pad voix, de 25 a 400 % par pas de 5 %. Le clic revient a 100 %. E4 ne devient pas un parametre FX.
- `E5 MODE` : mode de lecture du sample.
- `E6 J10` : reserve.
- `E7 BPM` : tempo global de 20 a 300 BPM. La longueur du Repeat est recalculee en direct.

Dans le harness Windows, `F1-F7` choisit l'encodeur, les fleches le tournent et `Entree` le clique. `Espace` retrigger le dernier pad voix, `Retour arriere` remonte dans le browser et `q` quitte.

## Transitions Audio

L'activation, le relachement et les changements de division/BPM utilisent des rampes ou crossfades de 128 frames. Chaque retour periodique au debut de la boucle est aussi lisse sans modifier sa periode BPM. Le moteur Repeat est place apres `VoiceManager`, traite le mix stereo global et n'alloue aucune memoire : l'appelant fournit quatre buffers float dimensionnables avec `LiveRepeat::requiredBufferFrames(sampleRate)`, prets a etre places en PSRAM par la future couche Teensy. Ce chemin n'a pas encore ete mesure sur Teensy.
