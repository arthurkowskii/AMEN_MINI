# Contrôles

## Firmware MIDI actif

- SW1–SW12 jouent douze degrés consécutifs de la gamme sélectionnée.
- SW13–SW20 sélectionnent chacun un slot harmonique global tant qu’ils sont tenus : les degrés déjà enfoncés et ceux joués ensuite deviennent des accords. Un autre pad remplace temporairement le slot ; relâcher le pad actif restaure le précédent encore tenu (empilement LIFO). Chaque degré utilise la recette de ce slot dans sa palette d’origine.
- SW21 applique tant qu'il est tenu le mode Shift sélectionné. Maintenir SW21 et tourner **E1** parcourt `HOLD`, `MOD`, `+1 OCT`, `+1 ST`, `-1 OCT`, `-1 ST`; le changement est actif immédiatement et ne modifie ni le paramètre E1 sélectionné ni sa valeur. Un clic E1 pendant Shift est ignoré. `HOLD` envoie CC64=127 à l'appui et CC64=0 au relâchement. `MOD` fait monter CC1 linéairement de 0 à 127 en une seconde, puis redescendre en une seconde au relâchement. Les transpositions concernent uniquement les pads enfoncés pendant SW21 et restent figées jusqu'à leur relâchement, accords et patterns compris. Les notes hors MIDI 0 à 127 sont omises.
- **E1** regroupe les réglages musicaux. Chaque clic parcourt `OCTAVE` → `TEMPO` → `FREQUENCY` → `ROOT` → `PRESET`, puis revient à `OCTAVE`. La rotation règle le paramètre sélectionné : octave `O0` à `O8`, tempo de 20 à 300 BPM (120 au démarrage), fréquence de 0,5 à 50 Hz logarithmique, fondamentale chromatique, puis l'un des quinze presets. Consulter une page ne change pas l'horloge ; tourner `TEMPO` ou `FREQUENCY` active cette horloge sans casser la phase du pas courant. `ROOT` et `PRESET` constituent ensemble la tonalité et la gamme. Le démarrage sélectionne `OCTAVE`. En page `PATTERN`, maintenir un pad pattern et tourner E1 réassigne son slot sans modifier le réglage musical ; un clic E1 affiche `ASSIGN` sans changer de paramètre. Au relâchement du pad, E1 retrouve le paramètre sélectionné. Si plusieurs pads pattern sont tenus, le dernier appuyé est édité. Shift reste prioritaire sur cette réassignation.
- **E2** parcourt les pages de jeu `HARMONY` → `PATTERN` → `NOTES`; son clic parcourt les banques uniquement en `PATTERN`. Le démarrage se fait en `CHROMATIC`, page `NOTES`.
- **E3** règle la vélocité de 1 à 127 par pas de 3 ; son clic bascule la rotation sur la molette de modulation CC1, par pas de 5, sans réinitialiser les valeurs. La vélocité concerne les nouvelles attaques, pas le volume des notes déjà tenues.
- **E4** envoie **CC23** en rotation, de 0 à 127 avec le multiplicateur choisi et saturation sans rebouclage. Il fonctionne comme les CC20 à CC22, avec une valeur indépendante, volatile, initialisée à 0 sans émission au boot, sur le canal du preset courant (1, ou 10 en `GM KIT`). **Shift + clic E4** active ou désactive `SMART VOICING`, désactivé au démarrage. Le premier accord fixe la référence ; les suivants conservent leurs notes mais choisissent le renversement et l'octave qui minimisent le mouvement dans une fenêtre bornée autour du registre demandé. Le changement ne déplace pas un accord déjà tenu. Désactiver le mode ou changer l'octave efface sa mémoire.
- **E5, E6 et E7**, les trois encodeurs du haut, envoient respectivement **CC20, CC21 et CC22**. La rotation règle une valeur absolue de 0 à 127, avec le multiplicateur choisi et saturation sans rebouclage. Le clic change uniquement le multiplicateur, sans envoyer de CC. Ces contrôles restent indépendants de Shift, de la vélocité et de la modulation. Ils utilisent le canal du preset courant : 1, ou 10 en `GM KIT`. Les trois valeurs démarrent à 0 sans envoi au boot et sont volatiles.

### Multiplicateurs des encodeurs MIDI

Chaque encodeur E4 à E7 possède son propre multiplicateur : `×1` → `×2` → `×4` → `×8` → `×1` à chaque clic. Un cran ajoute ou retire respectivement 1, 2, 4 ou 8 unités MIDI. Le clic ne change pas la valeur et n'envoie aucun message MIDI. Les valeurs saturent à 0 ou 127 sans rebouclage ; tourner dans l'autre sens repart immédiatement de la butée avec le pas sélectionné. Les multiplicateurs démarrent à `×1` à chaque boot et ne sont pas sauvegardés. Sur E4, Shift + clic bascule uniquement Smart Voicing, sans changer son multiplicateur. La modulation fixe CC1 sur E3 conserve son pas de 5 et la vélocité son pas de 3.

### MIDI Learn dans un instrument

Activer le MIDI Learn du paramètre dans Pigments, Serum ou l'hôte, puis tourner E4, E5, E6 ou E7. Le clic ne sert plus au Learn : il règle le multiplicateur. Shift + clic E4 reste réservé au Smart Voicing. L'AMEN envoie des MIDI Control Change standard ; l'association à un paramètre et sa sauvegarde se font dans le VST ou l'hôte. L'entrée MIDI de l'AMEN doit être activée et routée vers l'instrument. Les encodeurs émettent des valeurs **absolues**, pas un protocole relatif. Sans retour MIDI du VST, une première rotation peut donc faire sauter un paramètre à la valeur mémorisée par l'AMEN. Le CC et sa valeur sont affichés à chaque geste.

`MAJOR` utilise l’ionien, `MINOR` l’éolien et `HARM MIN` le mineur harmonique. Ils partagent, de SW13 à SW20 : `TRIAD`, `SEVENTH`, `NINTH`, `ADD9`, `SUS2`, `SUS4`, `SIXTH`, `SIX9`.

`CINEMA` utilise le lydien avec `OPEN TRIAD`, `OPEN7`, `OPEN9`, `LYDIAN SUS`, `SUS9`, `QUARTAL`, `QUINTAL`, `OPEN69`. `LYDIAN SUS` rassemble fondamentale, quarte augmentée, quinte et neuvième dans un voicing ouvert. `DARK` utilise le phrygien avec `TRIAD`, `SEVENTH`, `NINTH`, `ADD2`, `CLUSTER`, `SUS2`, `QUARTAL`, `FIFTH`. `CHROMATIC` utilise les douze demi-tons : SW1 à SW12 couvrent exactement une octave, un demi-ton par pad. Sa palette reprend les huit noms de la palette partagée avec des intervalles en demi-tons — `TRIAD` = 0,4,7 (C E G depuis Do), `SEVENTH` = 0,4,7,11, `NINTH` = 0,4,7,11,14, `ADD9` = 0,4,7,14, `SUS2` = 0,2,7, `SUS4` = 0,5,7, `SIXTH` = 0,4,7,9, `SIX9` = 0,4,7,9,14. Les recettes suivent les degrés de la gamme : `TRIAD` en Do majeur donne C–E–G sur le premier degré, D–F–A sur le deuxième ; en CHROMATIC, un degré est un demi-ton et les mêmes noms donnent les intervalles ci-dessus depuis chaque pad. Les noms quartal/quintal désignent ici des empilements de degrés, pas des intervalles chromatiques fixes. Les données exactes sont dans `../teensy/amen_midi/musical_presets.h` et `harmony_recipes.h`.

## Preset GM KIT

`GM KIT` transforme les 20 pads en kit General MIDI sur le canal 10. Les douze pads inférieurs et les huit supérieurs jouent des sons fixes de la carte de percussion GM, sans harmonie : BD (36), SD (38), CP (39), ES (40), CH (42), PH (44), OH (46), MT (48), CR (49), HT (50), RD (51), RB (53), TB (54), SP (55), CB (56), C2 (57), RC (59), BG (60), OC (63), LC (64). L'octave et la fondamentale sont gelées ; les pages E1 `OCTAVE` et `ROOT` n'ont aucun effet, mais le tempo, la fréquence et la sélection du preset restent disponibles. En page `PATTERN`, chaque pattern devient un roll sur le son du pad à l'horloge BPM/Hz choisie ; l'OLED affiche le nom abrégé du son tenu. Les données sont dans `../teensy/amen_midi/gm_drum_kit.h`.

Chaque degré fige sa hauteur, son index, sa gamme, son preset et son orthographe au pad-down. Changer de fondamentale, d’octave ou de preset ne réharmonise pas les degrés déjà tenus ; les changements de slot suivants utilisent toujours leur contexte d’origine. Les nouveaux appuis utilisent les réglages courants. Les voix hors de MIDI 0–127 sont repliées par octaves. Sans pad harmonique tenu, les degrés redeviennent des notes seules.

Une hauteur partagée par plusieurs degrés ou le run ne reçoit qu’un NoteOn au premier propriétaire et un NoteOff au dernier. Les notes communes aux transitions ne sont pas retriggées.

## Page PATTERN

Les huit pads supérieurs portent chacun un pattern assignable. Le clic E2 parcourt six banques : `ORCH`, `FUT. ARP`, `FUT. PATTERN`, `KAWAII`, `REPEAT`, puis `NOIR`. E1 réassigne le slot tenu dans la banque active. `NOIR` fournit `N PULSE`, `N TRILL`, `N 16 BUILD`, `N CRAWL`, `N STAB`, `N LURCH`, `N MARCH` et `N CHIME`. Tous les slots sont actifs ; il n’y a pas de slot `EMPTY`. L’assignation vit en RAM et revient aux valeurs par défaut à chaque démarrage.

Deux ordres de déclenchement sont équivalents :

- tenir un pad inférieur puis un pad supérieur déclenche le pattern depuis le pad inférieur tenu le plus récent ;
- tenir un pad supérieur puis appuyer un pad inférieur déclenche le pattern depuis ce pad.

Le pattern remplace temporairement la voix manuelle du pad source (l’accord entier s’il tenait un rôle harmonique) et boucle tant que la note source et le pad pattern restent tenus. Relâcher l'un ou l'autre arrête immédiatement le run ; relâcher le pad pattern restaure la source encore tenue, tandis que relâcher la source ne la restaure pas. Un seul run joue à la fois : un nouveau déclenchement transfère la suppression et la restauration de façon transactionnelle. Le pad supérieur tenu le plus récent s’applique aux appuis inférieurs suivants ; hors run, relâcher ce pad restaure le précédent encore tenu.

Chaque contour part de la note choisie : les runs de gamme montent ou descendent de huit notes, les ping-pong `UP DOWN` / `DOWN UP` font quatorze pas et repartent au deuxième pas pour ne pas doubler la note basse à la couture, les tierces montantes jouent les paires explicites 0,2 · 1,3 · 2,4 · 3,5 · 4,6 · 5,7 et les descendantes leur miroir 0,−2 · −1,−3 · −2,−4 · −3,−5 · −4,−6 · −5,−7, et les arpèges déploient la triade aux degrés 0,2,4,7 en montant et 0,−3,−5,−7 en descendant. `REPEAT` rejoue la note source ; chaque impulsion occupe 75 % du pas et laisse 25 % de silence pour produire un vrai retrigger MIDI. Les notes hors MIDI 0–127 sont exclues du cycle, sans repli. La gamme et le départ sont figés au déclenchement ; la rotation E1 pendant le run modifie l’assignation future sans altérer le run actif.

## Page NOTES

En page `NOTES`, les huit pads supérieurs deviennent des notes : les vingt pads jouent vingt degrés consécutifs de la gamme sélectionnée, SW13–SW20 poursuivant l’échelle là où SW1–SW12 s’arrêtent (en Do majeur, SW13 = A5, SW14 = B5, SW15 = C6… ; en `CHROMATIC`, une octave et huit demi-tons). Chaque pad fige son contexte comme les autres degrés et son orthographe apparaît sur l’OLED. Les nouveaux appuis y sont toujours des notes seules, même si un slot harmonique reste tenu depuis `HARMONY` ; les tenues antérieures gardent leur rôle jusqu’au relâchement, conformément au contrat général. Aucun pattern ne se déclenche en `NOTES` ; entrer dans `NOTES` depuis `PATTERN` annule le run actif et restaure la source tenue.

Revenir en `HARMONY` ou passer en `NOTES` avec E2 annule le run et restaure la source tenue. Chaque pad conserve son rôle jusqu’au relâchement, même à travers les changements de page. Un pad supérieur tenu comme pattern en page `HARMONY` ou `NOTES` ne déclenche rien. En mode `TEMPO`, chaque note dure une double-croche ; en mode `FREQUENCY`, chaque pas dure l'inverse de la fréquence choisie. L'horloge est interne et sans synchronisation MIDI externe. Si la boucle prend du retard, les pas expirés sont sautés plutôt que rejoués en rafale. Pour des appuis reçus dans le même snapshot, Shift est traité avant les pads supérieurs, puis les inférieurs ; les encodeurs sont traités ensuite (clic avant rotation pour chaque encodeur).

## Écran

L'accueil utilise trois lignes en caractères doubles : octave et fondamentale, preset et dernière note tenue, puis harmonie/description de gamme ou état du pattern courant suivi de `IDLE`, `READY` ou `PLAY`. `HARM`, `PATT` ou `NOTES` reste visible en haut à droite, y compris pendant les overlays. Un `*` après le preset indique une ancienne tenue appartenant à un autre preset ; le nom harmonique affiché concerne le preset sélectionné. L'orthographe des notes tenues reste mémorisée.

Un clic ou une rotation E1 affiche son paramètre sélectionné, avec cinq points de navigation ; `*` indique l'horloge active sur `TEMPO` ou `FREQUENCY`. Après expiration, l'accueil conserve un rappel `OCT`, `BPM`, `HZ`, `ROOT` ou `PRESET`. E2 affiche la page ou la banque, E3 la vélocité ou la modulation, Shift + clic E4 le Smart Voicing, et les rotations/clics E4 à E7 le numéro d'encodeur, le CC, sa valeur, le canal MIDI et le multiplicateur `X1`, `X2`, `X4` ou `X8`. En page `HARMONY`, SW13–SW20 ouvre temporairement `HARMONY`. SW21 et SW21 + E1 affichent `SHIFT`. Les overlays durent 800 ms. Le backend transmet l'OLED par fragments afin de ne pas bloquer les patterns rapides.

En page pattern, les pads supérieurs ouvrent `PATTERN` avec l'état du run ; la rotation E1 sur un pad tenu ouvre `ASSIGN` avec l'index et le nom réassigné. E2 ouvre `MODE`. Les points de pagination sont placés en bas à droite pour laisser la page de jeu visible.

## Teensy 4.1 — mapping issu du netlist réel

Les nombres sont les numéros Arduino lus dans `pinfunction`, et non les numéros de pads du symbole :

- colonnes : COL0=0, COL1=1, COL2=2, COL3=3, COL_SHIFT=4 (SW21 Shift seul) ;
- lignes : ROW0=5, ROW1=6, ROW2=9, ROW3=14, ROW4=15 ;
- I2C : SDA=18, SCL=19 ;
- E1 A/B/SW=16/17/35 ; E2=22/24/36 ; E3=25/26/37 ; E4=27/28/38 ; E5=29/30/39 ; E6=31/32/40 ; E7=33/34/41.

Le document Pico historique n’est pas une source valable.
