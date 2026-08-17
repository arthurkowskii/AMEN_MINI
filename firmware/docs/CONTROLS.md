# AMEN_MINI - Controls

Ce document est la source claire des controles de performance actuels. Le harness PC reduit la facade a 6 pads voix et 3 pads FX.

## Pads

- Pads voix 1-6 (`Numpad 1-6`) : l'appui declenche le break et fait du pad la **cible des encodeurs** tant qu'il est tenu. Toujours maintenir le pad pour editer ses parametres (règle : le dernier pad appuye gagne, relacher retombe sur le pad encore tenu le plus recent).
- Le pad voix maintenu + `E1` ouvre le navigateur SD (le relacher le ferme). L'appui seul ne change plus l'ecran.
- Pads FX 7-9 (`Numpad 7-9`) : le maintien active l'effet assigne, le relachement le coupe.
- Le Repeat capture l'audio du mix global immediatement anterieur a l'appui. Il ne retrigger pas une voix individuelle.
- Liste d'assignation : `BLANK`, `REPEAT`, `REVERSE`, `TRANCE GATE`.
- `BLANK` desassigne le pad. `REVERSE` est assignable mais n'a pas encore de DSP. `TRANCE GATE` applique la spectral gate 8 bandes decrite dans Transitions Audio.
- **Un seul effet global est actif a la fois en V0** : le pad FX tenu gagne (REPEAT et TRANCE GATE sont mutuellement exclusifs par conception).

Chaque pad voix demarre en `ONE SHOT + GATE` et memorise independamment ses deux axes :

| Lecture | Trigger | Appui | Relachement |
|---|---|---|---|
| `ONE SHOT` | `GATE` | demarre/retrigger | arrete le pad (la fin naturelle l'arrete aussi) |
| `ONE SHOT` | `LATCH` | demarre/retrigger | ne fait rien ; lecture jusqu'a la fin naturelle |
| `LOOP` | `GATE` | demarre/retrigger la boucle | arrete le pad |
| `LOOP` | `LATCH` | premier appui = demarre ; appui suivant = arrete | ne fait rien |

L'arret cible uniquement le pad concerne, y compris ses queues de crossfade, sans couper les autres pads.

## Encodeurs

- `E1 NAV` : sur un pad voix maintenu, navigue dans la SD et le clic entre/charge. Sur un pad FX maintenu, navigue dans la liste FX et le clic assigne.
- Appui long `E1` (600 ms) sur un WAV : ouvre le menu d'assignation globale `ALL PADS` / `TRANSIENT` / `CANCEL` au lieu d'assigner le fichier au pad cible. Le relachement ne confirme pas : une nouvelle pression `E1` confirme l'action affichee, `Retour arriere` annule sans modifier l'assignation. L'appui long n'est jamais declenche par un dossier et ne se declenche qu'une seule fois par pression.
- `E2 AMOUNT` : mix dry/wet du Repeat, de 0 a 100 %. La valeur par defaut est 100 %.
- `E3 DIVISION` : longueur du Repeat, choix live `1/4`, `1/8`, `1/12`, `1/16`, `1/24`, `1/32` (les `1/12` et `1/24` sont les triplets de croche et de double-croche — le stutter swingue). La valeur par defaut est `1/4`.
- `E4 SPEED` : vitesse du pad voix **tenu** (plus jamais le "dernier joue"), de 25 a 400 % par pas de 5 %. Appliquee en direct a la voix active du pad (rampe de 128 frames, sans retrigger) et memorisee pour son prochain trigger. Le clic remet **ce pad** a 100 %. Sans pad tenu : hint `E4 TENIR PAD`, aucun effet.
- `E5 MODE` : deux axes independants pour le pad voix **tenu**. La rotation alterne uniquement le mode de lecture `ONE SHOT` / `LOOP`. Le clic alterne le comportement de trigger `GATE` / `LATCH`. L'overlay et la console affichent la valeur reelle choisie. Sans pad tenu : hint `E5 TENIR PAD`, aucun effet.
- `E6 LFO` : reserve pour un futur LFO ; aucun controle audio dans cette tache.
- `E7 BPM` : tempo global de 20 a 300 BPM. La longueur du Repeat est recalculee en direct ; la grille 16 pas de la spectral gate suit aussi ce tempo.

Chaque pad voix memorise sa propre vitesse, son mode de lecture et son comportement de trigger ; l'ecran d'accueil affiche le BPM et le mode de lecture du dernier pad joue.

## Ecran OLED

- Chaque interaction de parametre renouvelle son overlay pour exactement 1 seconde a partir de la derniere interaction. Le navigateur et l'ecran d'un pad FX restent prioritaires sur cet overlay.
- Dans le navigateur WAV, les noms longs restent d'abord immobiles et tronques avec `..`. Apres 500 ms sans changement de selection ou de dossier, seule la ligne selectionnee defile horizontalement a 30 pixels/s, en boucle avec un espace. Seule une nouvelle selection ou un changement de dossier remet le defilement au debut ; restaurer ou rouvrir le navigateur conserve sa chronologie.
- Les lignes non selectionnees et les noms assez courts pour tenir dans la largeur disponible ne defilent jamais.
- Le menu d'assignation affiche le nom du fichier (tronque avec `..`, puis defile a 30 px/s apres 500 ms en restant a gauche du rappel `E1 OK`) et les trois options `ALL PADS` / `TRANSIENT` / `CANCEL`, avec un marqueur sur la ligne selectionnee. `TRANSIENT` decoupe le WAV en douze plages par detection d'attaques ; le menu ne modifie jamais le mode `E5`, le latch ni la vitesse des pads.
- Les douze plages sont publiees d'un bloc apres validation : en cas d'echec de chargement ou de detection, le fichier et le plan en place restent inchanges. **Reduction temporaire du harness PC** : seuls les pads 1-6 declenchent leur plage du plan ; les pads 7-12 recoivent leurs plages mais ne sont pas jouables sur PC (en attente du materiel).

Dans le harness Windows, `F1-F7` choisit l'encodeur, les fleches le tournent et `Entree` le clique. `Espace` simule deterministiquement un nouvel appui sur le dernier pad voix avec tous ses reglages : il retrigger dans les trois premiers cas et bascule lecture/arret en `LOOP + LATCH`. `Retour arriere` remonte dans le browser et `q` quitte.

## Transitions Audio

L'activation, le relachement et les changements de division/BPM utilisent des rampes ou crossfades de 128 frames. Chaque retour periodique au debut de la boucle est aussi lisse sans modifier sa periode BPM. Le moteur Repeat est place apres `VoiceManager`, traite le mix stereo global et n'alloue aucune memoire : l'appelant fournit quatre buffers float dimensionnables avec `LiveRepeat::requiredBufferFrames(sampleRate)`, prets a etre places en PSRAM par la future couche Teensy. Ce chemin n'a pas encore ete mesure sur Teensy.

## Spectral Gate (TRANCE GATE)

La spectral gate decoupe le mix stereo global en **8 bandes** (echelle de passe-bas Linkwitz-Riley d'ordre 2 aux coupures 200/400/800/1600/3200/6400/12800 Hz) dont la somme reconstruit le signal exactement : aucune perte de niveau quand toutes les bandes sont ouvertes. Chaque bande est ponderee par un **motif rythmique de 16 pas** (1/16 de note) synchronise au BPM global d'E7. Les ouvertures et fermetures sont **lissees par un filtre une-pole de ~5 ms** applique a chaque echantillon : aucun clic, meme en plein signal, y compris lors d'un changement de BPM en direct (la phase se cale sur le pas courant).

- **Motif par defaut deterministe** : la bande b est ouverte tous les b+1 pas (bande 1 : un pas sur deux ; bande 4 : un pas sur cinq ; bande 8 : un pas sur huit). Pas d'editeur de motif en V0 ; les motifs restent programmables par code (`SpectralGate::setPattern`).
- La porte traite le mix apres le Repeat (chaine : voix -> Repeat -> Spectral Gate) et n'alloue aucune memoire : tout l'etat (14 biquads, gains, phase) vit dans l'objet, pret a tourner sur Teensy.
- Un seul effet global actif a la fois en V0 : tenir un pad FX desactive l'autre.
