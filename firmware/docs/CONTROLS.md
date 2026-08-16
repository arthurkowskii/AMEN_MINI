# AMEN_MINI - Controls

Ce document est la source claire des controles de performance actuels. Le harness PC reduit la facade a 6 pads voix et 3 pads FX.

## Pads

- Pads voix 1-6 (`Numpad 1-6`) : l'appui declenche le break et fait du pad la **cible des encodeurs** tant qu'il est tenu. Toujours maintenir le pad pour editer ses parametres (règle : le dernier pad appuye gagne, relacher retombe sur le pad encore tenu le plus recent).
- Le pad voix maintenu + `E1` ouvre le navigateur SD (le relacher le ferme). L'appui seul ne change plus l'ecran.
- Pads FX 7-9 (`Numpad 7-9`) : le maintien active l'effet assigne, le relachement le coupe.
- Le Repeat capture l'audio du mix global immediatement anterieur a l'appui. Il ne retrigger pas une voix individuelle.
- Liste d'assignation : `BLANK`, `REPEAT`, `REVERSE`, `TRANCE GATE`.
- `BLANK` desassigne le pad. `REVERSE` et `TRANCE GATE` sont assignables mais n'ont pas encore de DSP.

## Encodeurs

- `E1 NAV` : sur un pad voix maintenu, navigue dans la SD et le clic entre/charge. Sur un pad FX maintenu, navigue dans la liste FX et le clic assigne.
- `E2 AMOUNT` : mix dry/wet du Repeat, de 0 a 100 %. La valeur par defaut est 100 %.
- `E3 DIVISION` : longueur du Repeat, choix live `1/4`, `1/8`, `1/12`, `1/16`, `1/24`, `1/32` (les `1/12` et `1/24` sont les triplets de croche et de double-croche — le stutter swingue). La valeur par defaut est `1/4`.
- `E4 SPEED` : vitesse du pad voix **tenu** (plus jamais le "dernier joue"), de 25 a 400 % par pas de 5 %. Appliquee en direct a la voix active du pad (rampe de 128 frames, sans retrigger) et memorisee pour son prochain trigger. Le clic remet **ce pad** a 100 %. Sans pad tenu : hint `E4 TENIR PAD`, aucun effet.
- `E5 MODE` : mode de lecture du pad voix **tenu** (`ONE SHOT` / `LOOP` / `GRANULAR` / `SLICE SYNC`). Le clic remet ce pad en `ONE SHOT`. Sans pad tenu : hint `E5 TENIR PAD`, aucun effet.
- `E6 J10` : reserve.
- `E7 BPM` : tempo global de 20 a 300 BPM. La longueur du Repeat est recalculee en direct.

Chaque pad voix memorise sa propre vitesse et son propre mode ; l'ecran d'accueil affiche le BPM et le mode du dernier pad joue.

Dans le harness Windows, `F1-F7` choisit l'encodeur, les fleches le tournent et `Entree` le clique. `Espace` retrigger le dernier pad voix avec SA propre vitesse, `Retour arriere` remonte dans le browser et `q` quitte.

## Transitions Audio

L'activation, le relachement et les changements de division/BPM utilisent des rampes ou crossfades de 128 frames. Chaque retour periodique au debut de la boucle est aussi lisse sans modifier sa periode BPM. Le moteur Repeat est place apres `VoiceManager`, traite le mix stereo global et n'alloue aucune memoire : l'appelant fournit quatre buffers float dimensionnables avec `LiveRepeat::requiredBufferFrames(sampleRate)`, prets a etre places en PSRAM par la future couche Teensy. Ce chemin n'a pas encore ete mesure sur Teensy.
