# AMEN_MINI - Controls

Ce document est la source claire des controles de performance actuels. Le harness PC reduit la facade a 6 pads voix et 3 pads FX.

## Pads

- Pads voix 1-6 (`Numpad 1-6`) : l'appui declenche le break et fait du pad la **cible des encodeurs** tant qu'il est tenu. Toujours maintenir le pad pour editer ses parametres (règle : le dernier pad appuye gagne, relacher retombe sur le pad encore tenu le plus recent).
- Le pad voix maintenu + `E1` ouvre le navigateur SD (le relacher le ferme). L'appui seul ne change plus l'ecran.
- Pads FX 7-9 (`Numpad 7-9`) : le maintien active l'effet assigne, le relachement le coupe.
- Liste d'assignation : `BLANK`, `REPEAT`, `REVERSE`, `TRANCE GATE`, `FREEZE`, `PHASE DIST`.
- Le Repeat capture l'audio du mix global immediatement anterieur a l'appui. Il ne retrigger pas une voix individuelle.
- `BLANK` desassigne le pad. `REVERSE` boucle a l'envers la fenetre de 2 s precedant l'appui. `PHASE DIST` plie la phase du mix par allpass a coefficient variable (modulateur SINE/SAW/SQUARE/SELF, cf. E6). `TRANCE GATE` applique la spectral gate 8 bandes et `FREEZE` le gel spectral, decrits dans Transitions Audio.
- **Un seul effet global est actif a la fois en V0** : le pad FX tenu gagne (REPEAT, REVERSE, PHASE DIST, TRANCE GATE et FREEZE sont mutuellement exclusifs par conception).

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
- `E2 AMOUNT` : mix dry/wet du Repeat, de 0 a 100 %. La valeur par defaut est 100 %. Sur un pad voix **GRANULAR tenu** : plage de hauteur par grain, de 0 a +/-24 demi-tons (valeur par defaut +/-12).
- `E3 DIVISION` : longueur du Repeat, choix live `1/4`, `1/8`, `1/12`, `1/16`, `1/24`, `1/32` (les `1/12` et `1/24` sont les triplets de croche et de double-croche — le stutter swingue). La valeur par defaut est `1/4`. Sur un pad voix **GRANULAR tenu** : densite des grains, de x0.25 a x4.0 par pas de x0.25 (valeur par defaut x1.0).
- `E4 SPEED` : vitesse du pad voix **tenu** (plus jamais le "dernier joue"), de 25 a 400 % par pas de 5 %. Appliquee en direct a la voix active du pad (rampe de 128 frames, sans retrigger) et memorisee pour son prochain trigger. Le clic remet **ce pad** a 100 %. Sans pad tenu : hint `E4 TENIR PAD`, aucun effet.
- `E5 MODE` : deux axes independants pour le pad voix **tenu**. La rotation alterne le mode de lecture `ONE SHOT` / `LOOP` / `GRANULAR`. Le clic alterne le comportement de trigger `GATE` / `LATCH`. L'overlay et la console affichent la valeur reelle choisie. Sans pad tenu : hint `E5 TENIR PAD`, aucun effet.
- `E6 MODE` : contextuel. Sur un pad voix **GRANULAR tenu** : cycle du mode granulaire `CLOUD` / `PITCH` / `RISE`. Sur un pad FX tenu assigne `REPEAT` : bascule `LOOP` / `SHEPARD` (montee infinie, profondeur liee a `E2 AMOUNT`). Sur un pad FX tenu assigne `PHASE DIST` : cycle du modulateur `SINE` / `SAW` / `SQUARE` / `SELF` (enveloppe du signal : le break plie sa propre phase). Sinon : reserve pour un futur LFO.
- `E7 BPM` : tempo global de 20 a 300 BPM. La longueur du Repeat est recalculee en direct ; la grille 16 pas de la spectral gate suit aussi ce tempo.

Chaque pad voix memorise sa propre vitesse, son mode de lecture et son comportement de trigger ; l'ecran d'accueil affiche le BPM et le mode de lecture du dernier pad joue.

## Ecran OLED

- Chaque interaction de parametre renouvelle son overlay pour exactement 1 seconde a partir de la derniere interaction. Le navigateur et l'ecran d'un pad FX restent prioritaires sur cet overlay.
- Dans le navigateur WAV, les noms longs restent d'abord immobiles et tronques avec `..`. Apres 500 ms sans changement de selection ou de dossier, seule la ligne selectionnee defile horizontalement a 30 pixels/s, en boucle avec un espace. Seule une nouvelle selection ou un changement de dossier remet le defilement au debut ; restaurer ou rouvrir le navigateur conserve sa chronologie.
- Les lignes non selectionnees et les noms assez courts pour tenir dans la largeur disponible ne defilent jamais.
- Le menu d'assignation affiche le nom du fichier (tronque avec `..`, puis defile a 30 px/s apres 500 ms en restant a gauche du rappel `E1 OK`) et les trois options `ALL PADS` / `TRANSIENT` / `CANCEL`, avec un marqueur sur la ligne selectionnee. `TRANSIENT` decoupe le WAV en douze plages par detection d'attaques ; le menu ne modifie jamais le mode `E5`, le latch ni la vitesse des pads.
- Les douze plages sont publiees d'un bloc apres validation : en cas d'echec de chargement ou de detection, le fichier et le plan en place restent inchanges. **Reduction temporaire du harness PC** : seuls les pads 1-6 declenchent leur plage du plan ; les pads 7-12 recoivent leurs plages mais ne sont pas jouables sur PC (en attente du materiel).

Dans le harness Windows, `F1-F7` choisit l'encodeur, les fleches le tournent et `Entree` le clique. `Espace` simule deterministiquement un nouvel appui sur le dernier pad voix avec tous ses reglages : il retrigger dans les trois premiers cas et bascule lecture/arret en `LOOP + LATCH`. `Retour arriere` remonte dans le browser et `q` quitte. La touche `v` fait **COMMIT** : les 15 dernieres secondes du mix global (post-FX) deviennent une nouvelle assignation en douze plages, publiee atomiquement — l'ancienne matiere reste intacte jusqu'a la validation. La capture est alimentee en continu par le callback audio sans aucune allocation (anneau statique, futur PSRAM) ; la conversion float->int16 vit sur le chemin de controle.

## Transitions Audio

L'activation et le relachement de chaque FX (Repeat, Reverse, Phase Dist) passent par une **courbe de transition (slew)** lineaire de 15 ms reglable (`RampGain`, partage par les trois effets) : aucun etat ne commute sec, l'entree et la sortie des effets glissent comme un filtre. Les changements de division/BPM du Repeat conservent leurs crossfades de 128 frames et chaque retour periodique au debut de la boucle est lisse sans modifier sa periode BPM. Le moteur Repeat est place apres `VoiceManager`, traite le mix stereo global et n'alloue aucune memoire : l'appelant fournit quatre buffers float dimensionnables avec `LiveRepeat::requiredBufferFrames(sampleRate)`, prets a etre places en PSRAM par la future couche Teensy. Ce chemin n'a pas encore ete mesure sur Teensy.

## Reverse (REVERSE)

Le Reverse fige la fenetre de **2 s precedant l'appui** et la lit a l'envers, en boucle, tant que le pad FX est tenu : le mix devient un stutter inversé. Le lissage de couture est identique a celui du Repeat (aucun saut au wrap), le buffer d'historique tourne en permanence (l'appui capture toujours du materiau), et les frames actives sur le point d'etre ecrasees passent dans des buffers de gel (protection d'ecrasement, meme architecture que le Repeat). Memoire : quatre buffers `ReversePlayer::requiredBufferFrames(sampleRate)` fournis par l'appelant.

## Phase Dist (PHASE DIST)

La phase distortion plie la phase du mix par un **allpass du premier ordre a coefficient variable** : `y[n] = c*x[n] + x[n-1] - c*y[n-1]`, avec `c = 0.95 * amount * modulateur`. Le modulateur est un LFO (SINE/SAW/SQUARE, 1 Hz) ou l'**enveloppe du signal lui-meme** (mode SELF : le break plie sa propre phase — le squelchy Casio-CZ). Le chemin `keep bass` re-injecte une copie filtree passe-bas du dry (~120 Hz) dans le wet : les fondations du break survivent a la deformation. Un clamp d'etat borne la recursion de l'allpass a coefficient variable. Aucune memoire, aucun blocage : l'etat tient entierement dans l'objet.

## Spectral Gate (TRANCE GATE)

La spectral gate decoupe le mix stereo global en **8 bandes** (echelle de passe-bas Linkwitz-Riley d'ordre 2 aux coupures 200/400/800/1600/3200/6400/12800 Hz) dont la somme reconstruit le signal exactement : aucune perte de niveau quand toutes les bandes sont ouvertes. Chaque bande est ponderee par un **motif rythmique de 16 pas** (1/16 de note) synchronise au BPM global d'E7. Les ouvertures et fermetures sont **lissees par un filtre une-pole de ~5 ms** applique a chaque echantillon : aucun clic, meme en plein signal, y compris lors d'un changement de BPM en direct (la phase se cale sur le pas courant).

- **Motif par defaut deterministe** : la bande b est ouverte tous les b+1 pas (bande 1 : un pas sur deux ; bande 4 : un pas sur cinq ; bande 8 : un pas sur huit). Pas d'editeur de motif en V0 ; les motifs restent programmables par code (`SpectralGate::setPattern`).
- La porte traite le mix apres le Repeat (chaine : voix -> Repeat -> Reverse -> Phase Dist -> Spectral Gate) et n'alloue aucune memoire : tout l'etat (14 biquads, gains, phase) vit dans l'objet, pret a tourner sur Teensy.
- Un seul effet global actif a la fois en V0 : tenir un pad FX desactive l'autre.

## Spectral Freeze (FREEZE)

Le freeze capture le **spectre d'amplitude des 512 dernieres frames** du mix (FFT 512, fenetre de Hann periodique), abandonne les phases, puis re-synthetise une **boucle 512-periodique** avec des phases deterministes. La boucle est rejouee en circulaire : aucune couture, aucune ondulation, et le spectre de magnitude de la sortie est exactement celui de la capture. L'activation et le relachement passent par une **rampe une-pole de ~10 ms** sur le gain wet : aucun clic, meme en plein signal. Tant que l'effet n'a pas ete actif, la chaine est un passthrough bit-exact.

- Le freeze fige **le passe immediat** : l'anneau de capture suit le mix en permanence, donc l'appui gele les dernieres ~11,6 ms sans latence supplementaire.
- Apres le relachement, le wet retombe a zero et l'etat gele est remis a zero : le passthrough exact est restaure.
- Le freeze traite le mix apres la spectral gate (chaine : voix -> Repeat -> Reverse -> Phase Dist -> Gate -> Freeze) et n'alloue aucune memoire dans `process()` : tout l'etat (anneau, fenetre, boucles, FFT sans table de twiddles) vit dans l'objet.
- Mesure native : ~5 ms CPU pour 1 s d'audio 44,1 kHz (0,5 % du temps reel), capture FFT 512 incluse.

## Granulaire (mode GRANULAR, E5)

Le mode `GRANULAR` transforme la **plage assignee** du pad en nuage granulaire : des grains de 30 a 150 ms naissent toutes les ~22 ms (densite bornee, 8 grains simultanes maximum), positions et longueurs tirees d'une sequence deterministe propre a chaque pad. Chaque grain est enveloppe par une fenetre de Hann aux deux extremites : aucune coupure franche a la naissance ni a la mort d'un grain. Le relachement (ou le deuxieme appui en LATCH) arrete le nuage par un fondu de ~10 ms.

- **Modes de grains** (`E6` sur le pad tenu) : `CLOUD` (lecture naturelle), `PITCH` (chaque grain lit a une hauteur fixe tiree au sort dans +/- la plage `E2`, de 0 a 24 demi-tons — la machine a vocal chops), `RISE` (chaque grain glisse de -plage a +plage demi-tons sur sa vie : mini-risers en serie). La plage par defaut est +/-12 st.
- **Densite** (`E3` sur le pad tenu) : x0.25 a x4.0 — la cadence de spawn est recalculee a chaque grain, c'est un reglage live sans redemarrer le nuage.
- Le PCM est **emprunte, jamais copie** : le nuage lit directement la plage du WAV charge ou du plan TRANSIENT, et le budget memoire reste fixe (8 grains par pad). La lecture est **interpolee lineairement** : les hauteurs par grain restent propres et la lecture ne sort jamais de la plage assignee, meme a 400 %.
- La vitesse du pad (`E4`) s'applique a la lecture des grains ; le mode ne modifie ni le latch ni les autres pads.
- Mesure native : ~3 ms CPU pour 1 s de nuage 44,1 kHz (0,3 % du temps reel, 8 grains maximum, lecture interpolee).

## Shepard (mode SHEPARD du REPEAT)

Le mode `SHEPARD` du Repeat accelere lineairement la position de lecture de la boucle capturee sur 4 passages puis retombe au wrap : chaque repetition est plus aigue que la precedente, l'effet "montee infinie" (riser par rampe de temps de lecture, aucun pitch shifter). La profondeur de la pente est liee au `E2 AMOUNT` (taux de lecture 1x -> 2x a 100 %). Le lissage de couture masque le retour de la rampe. Le mode `LOOP` reste le comportement historique.

## Enregistrement direct (Shift + pad, J15)

`Shift` maintenu + pad voix appuye = **enregistrement direct sur CE pad** : le press demarre la prise, le relachement (du pad ou du Shift) la stoppe. La source est le **mix post-FX** (exactement ce qu'on entend — meme point de prelevement que le ring COMMIT) ; sur Teensy ce sera le micro J15. Capacite : **6 s par pad**, arret automatique avec message console si la capacite est atteinte. L'overlay affiche `REC PAD n` avec les secondes ecoulees.

- **Une source par pad, persistante** : la matiere enregistree survit a tout chargement WAV, TRANSIENT ou COMMIT. Seul un nouvel enregistrement sur ce pad la remplace. Le pad enregistre joue sa matiere en entier dans tous ses modes (ONE SHOT / LOOP / GRANULAR), vitesse et latch inchanges.
- La matiere est **mono int16** (somme L+R, arrondi symetrique, clamp) : le rendu stereo duplique le mono sur les deux canaux.
- Un seul pad enregistre a la fois : armer un autre pad arrete le precedent (ses frames restent).
- Le press est **consomme** par l'enregistrement : pendant la prise, le pad n'est ni declenche ni cible des encodeurs.
- Harness Windows : `Shift` gauche ou droit + `Numpad 1-6`. Harness Linux : touche `r` (bascule sur le dernier pad).
- Le COMMIT retrospectif (`v`, 15 s -> 12 plages) reste intact : c'est le pipeline du break partage, orthogonal a la matiere par pad. Son geste materiel reste a reassigner (candidat : Shift + pad FX).
