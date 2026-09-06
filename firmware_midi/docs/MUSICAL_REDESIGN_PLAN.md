# AMEN MIDI - Plan de refondation musicale

Première tranche patterns poussée jusqu'à un catalogue de huit patterns assignables : clic E3 pour les pages persistantes HARMONY/PATTERN, huit pads supérieurs portant `RUN UP`, `RUN DOWN`, `UP DOWN`, `DOWN UP`, `THIRDS UP`, `THIRDS DN`, `ARP UP`, `ARP DOWN`, déclenchement dans les deux ordres (note tenue puis pattern, ou pattern tenu puis note), remplacement/restauration transactionnel de la source, édition par rotation E3 sur le pad tenu, E4 pour l'espacement. Ce parcours remplace l'accès principal par Shift envisagé ci-dessous. Horloge externe, expression et autres familles de patterns restent futurs. Validation native effectuée ; écoute et ergonomie du nouveau geste à confirmer sur carte.

Statut au 6 septembre 2026 : socle jouable implémenté et auditionné (six presets, dont `CHROMATIC` ajouté ce jour avec palette d'accords en demi-tons) ; catalogue de patterns compilé et testé nativement, non encore flashé ni écouté. Recherche conservée pour la suite.

Le firmware actif joue douze degrés et huit slots harmoniques momentanés globaux en LIFO. E2 alterne `ROOT` / `PRESET` : `MAJOR` (ionien), `MINOR` (éolien), `HARM MIN` (mineur harmonique), `CINEMA` (lydien ouvert), `DARK` (phrygien avec clusters), `CHROMATIC` (douze demi-tons, accord majeur et couleurs par pad). Les six presets partagent quatre palettes ; les recettes suivent les degrés de la gamme (en CHROMATIC, un degré est un demi-ton). Chaque tenue fige son contexte musical complet, puis interprète le slot global selon sa palette d’origine ; un nouveau preset concerne les nouveaux appuis, et `*` signale les anciennes tenues. L’ownership global préserve les hauteurs partagées, avec premier NoteOn / dernier NoteOff et buffer transactionnel. La page `PATTERN` ajoute un run one-shot par pad supérieur, le run remplaçant puis restaurant la voix de sa source tenue ; en CHROMATIC, les contours jouent en demi-tons.

GCC strict / CTest et compilation Teensy du catalogue de patterns validés ; aucun upload de cette tranche. Arthur a auditionné la version à cinq presets : « super, c’est BEAUCOUP mieux ». La direction musicale est approuvée, sans prétendre à une validation matérielle exhaustive. Le voice leading automatique, Shift, E5–E7 et les presets d’artistes ne sont pas implémentés. Demander à Arthur quelle micro-étape il souhaite avant de poursuivre.

Les sections suivantes conservent la recherche d’origine : leurs exemples, questions ouvertes et séquences de travail ne constituent pas la liste des fonctions livrées. Le comportement actif fait référence à [CONTROLS.md](CONTROLS.md), aux tables `teensy/amen_midi/musical_presets.h` et `harmony_recipes.h`, et au catalogue `teensy/amen_midi/run_pattern.h` ; le plan ci-dessous reste une direction, pas une implémentation à lancer en bloc.

## 1. Intention

AMEN MIDI doit être un instrument MIDI portable destiné à la composition et à la performance en direct. Il ne cherche pas à reproduire un clavier, un séquenceur complet ou un DAW miniature.

Question directrice :

> Comment permettre à deux mains de choisir une matière musicale, de l'harmoniser puis de l'animer sans interrompre le jeu ?

Priorités :

- immédiateté : les actions musicales principales sont directes ;
- prévisibilité : le même geste conserve le même sens ;
- réversibilité : relâcher une transformation ramène proprement à l'état précédent ;
- continuité : une transformation peut agir sur des notes déjà tenues ;
- profondeur progressive : l'instrument est simple au premier contact et riche avec l'apprentissage ;
- sécurité MIDI : aucun changement d'état ne doit laisser de note bloquée.

## 2. Contrat physique retenu

### Pads inférieurs

Les 12 pads inférieurs représentent 12 degrés consécutifs de la gamme sélectionnée. La fondamentale et la gamme sont choisies par encodeur.

Exemples :

```text
Do majeur       C D E F G A B | C D E F G
Do pentatonique C D E G A | C D E G A | C D
Chromatique     C C# D D# E F F# G G# A A# B
```

Le comportement de base reste littéral :

```text
C seul     -> note C
C + D      -> notes C + D
```

Une note inférieure n'est jamais transformée silencieusement en accord par le seul fait que plusieurs notes sont tenues.

### Pads supérieurs

Sans Shift, les 8 pads supérieurs représentent des accords ou couleurs harmoniques, par exemple :

```text
MIN  MAJ  DARK  7  9  ...
```

La sélection exacte des huit fonctions reste à établir par des essais musicaux. Le principe, lui, est fixé :

```text
note inférieure + pad harmonique -> accord construit depuis cette note
```

Exemples conceptuels :

```text
C + MAJ -> accord de C majeur
D + MIN -> accord de D mineur
```

Les accords peuvent être empilés. AMEN ne doit pas programmer, capturer ou deviner des polychords à la place du musicien.

Le geste exact qui associe une couleur à une racine lorsque plusieurs notes sont déjà tenues reste à définir. Cette interaction doit être testée lentement sur le matériel. Elle ne doit pas reposer sur une capture cachée ou une attribution difficile à prévoir.

### Shift et patterns

Shift transforme momentanément les 8 pads supérieurs en 8 slots de patterns MIDI assignables.

```text
Shift + pad supérieur -> pattern du slot correspondant
```

Gate, Latch et one-shot restent des comportements d'activation possibles. Le rôle d'un pad doit être fixé à son enfoncement afin qu'un changement de Shift pendant sa tenue ne change pas le sens de son relâchement.

### Encodeurs

Décisions actuelles :

- un encodeur choisit l'intention de voice leading ;
- un encodeur active ou désactive une basse séparée ;
- la basse optionnelle joue par défaut la fondamentale de l'accord ;
- les autres affectations seront définies après validation de la grammaire harmonique.

## 3. Sweet spot

Le coeur de l'instrument tient en quatre lignes :

> Les pads inférieurs jouent des notes. Les pads supérieurs en font des accords. Les accords peuvent être empilés. AMEN choisit automatiquement des renversements musicaux.

Répartition des responsabilités :

```text
musicien : note, couleur, empilement, intention
AMEN     : renversement, registre, voice leading
```

L'intelligence intervient après le geste et ne remplace jamais le geste.

Les pistes suivantes ne sont pas retenues :

- huit pads supérieurs contenant huit accords autonomes préprogrammés ;
- capture cachée d'une recette harmonique par chaque racine ;
- reconnaissance automatique d'une fondamentale parmi plusieurs notes ambiguës ;
- programmation de polychords pendant la performance.

## 4. Vocabulaires harmoniques et presets

AMEN doit proposer de nombreux presets couvrant des langages harmoniques variés. Un preset n'est pas seulement une gamme ou un accord isolé : c'est un vocabulaire cohérent reliant la géographie des notes aux huit propositions harmoniques.

Pour le musicien, un preset complet charge en une seule action :

```text
gamme des 12 pads inférieurs
+ huit accords des pads supérieurs
+ comportement diatonique, chromatique ou hybride
+ famille de voicing par défaut
+ registre et densité conseillés
+ intention de voice leading initiale
```

Exemple conceptuel :

```text
Preset OPEN WORLD

Gamme    : mode ou collection choisie
Accords  : MAJ, MIN, SUS, ADD9, 6/9, QUARTAL, QUINTAL, CLUSTER
Voicing  : ouvert
Mouvement: notes communes et trajectoires lentes
```

Le nom ci-dessus est provisoire. Chaque preset devra être validé à l'oreille et décrit par ses mécanismes réels.

### Séparation interne

Même si l'utilisateur charge un preset unique, ses constituants doivent rester conceptuellement séparés :

```text
Scale Definition
    collection de notes et ordre des 12 degrés

Chord Recipe
    intervalles, degrés imposés et notes optionnelles

Harmonic Palette
    huit recettes assignées aux pads supérieurs

Voicing Profile
    disposition tertiaire, quartale, quintale, ouverte ou en cluster

Performance Preset
    références vers une gamme, une palette et un profil de voicing
```

Cette composition évite de recopier les mêmes accords dans des centaines de presets. Elle permet aussi, plus tard, de conserver une palette tout en changeant de gamme, ou de conserver une gamme tout en changeant d'esthétique harmonique.

### Responsabilité d'une recette d'accord

Une recette ne se limite pas à un nom comme `MAJ` ou `DARK`. Elle peut préciser :

- une formule d'intervalles absolus ;
- une construction depuis les degrés de la gamme ;
- les notes essentielles et les notes supprimables ;
- les tensions autorisées ;
- une structure tertiaire, quartale, quintale ou secundale ;
- un nombre de voix cible ;
- des contraintes de registre et d'espacement ;
- une préférence de basse et de voix supérieure ;
- les transformations acceptables lors du voice leading.

Exemple :

```text
QUINTAL
contenu relatif : 1, 5, 9, 13, 3
voicing initial : empilement de quintes
contrainte      : préserver le caractère quintal pendant les transitions
```

Sur D, cette recette peut produire `D A E B F#`, soit les classes de notes d'un `D6/9` dans un voicing quintal.

### Familles à étudier

Le catalogue doit partir de systèmes musicaux documentés, puis être validé musicalement. Premiers territoires de recherche :

- harmonie tonale fonctionnelle : triades, septièmes, dominantes et emprunts ;
- harmonie modale : dorien, phrygien, lydien, mixolydien et autres collections ;
- impressionnisme : modes, extensions, planing diatonique ou chromatique ;
- jazz modal : accords liés aux gammes, voicings quartaux et pédales ;
- jazz moderne : tensions, altérations et upper structures ;
- blues, soul, gospel et neo-soul ;
- pop et musiques électroniques ;
- ambient et minimalisme ;
- harmonie orchestrale et cinématique ;
- musique de jeu : pédales, ambiguïtés modales, structures ouvertes et clusters contrôlés ;
- langages du XXe siècle : quartal, quintal, secundal, tons entiers et collections symétriques ;
- traditions non occidentales compatibles avec le tempérament et les limites MIDI retenus.

Cette liste décrit des axes de recherche, pas encore des presets validés.

### Compositeurs et artistes de référence

Les compositeurs et artistes servent de corpus d'analyse, pas d'étiquette vague. Un preset ne doit pas être nommé d'après une personne simplement parce qu'il semble évoquer son univers.

Pour chaque référence retenue :

1. Identifier plusieurs passages précis et leurs partitions ou transcriptions vérifiables.
2. Extraire les mécanismes récurrents : gamme, intervalles, mouvements de voix, pédales, densité et registre.
3. Transformer ces mécanismes en recettes transposables.
4. Vérifier que les huit pads forment un vocabulaire cohérent plutôt qu'une collection de citations.
5. Nommer le preset d'après son comportement musical ; conserver les influences dans ses métadonnées et sa documentation.
6. Auditer le résultat à l'oreille avant de l'intégrer au catalogue principal.

### Deux niveaux de catalogue

Le grand nombre de possibilités ne doit pas produire un menu illisible :

- **Core** : petit ensemble de presets immédiatement compréhensibles et fortement validés ;
- **Library** : catalogue étendu filtrable par famille, humeur, densité, époque, gamme et technique harmonique.

Les favoris ou banques utilisateur permettront ensuite de préparer un concert sans parcourir toute la bibliothèque.

## 5. Voice leading automatique

Le voicing d'un nouvel accord dépend du voicing réellement entendu auparavant. AMEN ne choisit pas seulement un renversement valide : il cherche une transition cohérente.

Exemple :

```text
C majeur précédent C4 E4 G4
F majeur naïf      F4 A4 C5
F majeur lié       C4 F4 A4
```

Le choix d'un voicing candidat peut prendre en compte :

- conservation des notes communes ;
- somme des déplacements en demi-tons ;
- pénalité pour le plus grand saut individuel ;
- absence de croisement des voix ;
- tessiture globale ;
- espacement minimal et maximal ;
- préférence de basse ;
- direction de la voix supérieure ;
- densité dans le grave ;
- direction musicale demandée.

La minimisation pure des déplacements ne suffit pas. Elle peut immobiliser la progression ou empêcher une montée dramatique. L'encodeur d'intention doit donc orienter le moteur.

Modes candidats à comparer :

- `ROOT` : position fondamentale stable, sans mémoire ;
- `CLOSE` : déplacements minimaux et notes communes favorisées ;
- `BASS ROOT` : voice leading doux avec fondamentale à la basse ;
- `TOP LINE` : continuité de la voix supérieure ;
- `SPREAD` : registre plus ouvert ;
- `ASCEND` : trajectoire globale ascendante ;
- `DESCEND` : trajectoire globale descendante ;
- `RESET` : oubli du voicing précédent.

La transition MIDI maintient une hauteur commune sans NoteOff/NoteOn inutile, sauf si un pattern demande explicitement une réarticulation.

## 6. Empilement harmonique

Un empilement peut être entendu comme plusieurs accords, une upper structure ou une harmonie étendue. AMEN n'a pas besoin d'imposer un nom théorique unique au résultat. Il doit rendre les structures demandées de manière lisible.

Le moteur peut agir sur :

- les inversions ;
- le registre de chaque accord ;
- l'espacement entre les voix ;
- les notes communes ;
- la densité globale ;
- la trajectoire depuis l'état précédent.

Il ne doit pas changer l'identité des accords demandés pour simplifier son calcul.

### Inspiration Breath of the Wild

La sonorité identifiée pendant la recherche utilise les classes de notes suivantes :

```text
D A E B F#
```

Elles correspondent à un `D6/9` disposé en intervalles ouverts :

```text
D  fondamentale
F# tierce majeure
A  quinte
B  sixte
E  neuvième
```

La sonorité contient aussi les notes de D majeur et de B mineur, mais l'union de ces deux triades ne contient pas E. L'intérêt pour AMEN n'est donc pas seulement d'empiler davantage d'accords. Une seule recette peut devenir riche grâce à sa construction intervallique, son registre et son voicing.

## 7. Patterns assignables

Un pattern sépare autant que possible les dimensions suivantes :

- source de hauteurs ;
- rythme ;
- vélocité ;
- gate ;
- CC et expression ;
- articulation ;
- contour ;
- registre ;
- routage instrumental ;
- retrigger et condition de fin.

Familles initiales :

- déploiement d'accord : Strum, Harp Sweep, Arp, Broken Chord, Alberti ;
- mouvement de gamme : Run, vague, tierces, approche chromatique ;
- rythme : pulse, ostinato, euclidien, ratchet, polymètre simple ;
- geste orchestral : Brass Rise, swell, fanfare, woodwind run, harp glissando ;
- expression : velocity, CC1, CC11, gate, articulation, humanisation ;
- harmonie : quantification, filtre de notes d'accord, inversion et registre.

Chaque pattern doit d'abord être décrit musicalement avant d'être traduit en code :

```text
Nom
Intention
Déclenchement
Source de hauteurs
Contour
Rythme
Durée
Gate
Velocity
Dynamics
Expression
Articulation
Résolution
Retrigger
Paramètres de façade
Cas limites
```

## 8. Expression orchestrale

Les patterns décrivent des intentions abstraites. Un profil instrumental les traduit vers une bibliothèque ou un patch précis.

```text
Dynamics     -> CC approprié
Expression   -> CC approprié
Articulation -> keyswitch, CC ou canal
Legato       -> articulation et chevauchement
Register     -> tessiture jouable
```

Cette séparation évite de coder un `Brass Rise` directement pour un plugin particulier.

## 9. Principes techniques à préserver

- Le moteur musical portable reste indépendant d'Arduino et de Teensy.
- Le catalogue de patterns ne connaît pas le scan physique des pads.
- Un slot référence un type de pattern et ses paramètres.
- Une horloge musicale commune alimente les comportements temporels.
- Le rendu MIDI possède un ownership strict des notes.
- Les notes et événements continus ont des budgets séparés.
- Les structures temps réel restent bornées et sans allocation.
- Les patterns sont testables avec une horloge et des entrées déterministes.
- Toute activation possède une politique explicite de fin et d'annulation.
- Les presets sauvegardent des paramètres, pas l'état temporaire d'exécution.

## 10. Questions ouvertes

### Geste harmonique

- Comment associer sans ambiguïté un pad harmonique à une note lorsque plusieurs notes sont tenues ?
- L'ordre note puis accord doit-il toujours être équivalent à accord puis note ?
- Les qualités sont-elles exclusives ou certaines extensions sont-elles cumulables ?
- L'appui est-il momentané, verrouillable, ou les deux ?
- Comment revenir instantanément à la note seule ?

### Huit pads supérieurs

- Quelles huit fonctions offrent le meilleur vocabulaire avec le moins de doublons ?
- `DARK` est-il une recette déterministe ou une famille dépendante du contexte ?
- `7` et `9` désignent-ils des accords complets ou des extensions ?
- Les recettes sont-elles chromatiques depuis la racine ou harmonisées dans la gamme ?

### Presets

- Quels presets appartiennent au noyau immédiatement accessible ?
- La gamme et la palette peuvent-elles être dissociées depuis la façade ?
- Comment parcourir une grande bibliothèque sans menu profond ?
- Quelles métadonnées permettent une recherche musicale plutôt qu'une liste de noms ?
- Quel protocole d'écoute valide une palette avant son intégration ?

### Empilement

- Combien d'accords et de voix restent utiles en performance ?
- Comment réduire la densité sans trahir les notes caractéristiques ?
- Comment afficher un empilement sans imposer une analyse harmonique discutable ?

### Patterns

- Un seul slot actif ou plusieurs simultanément ?
- Dans quel ordre appliquer plusieurs transformations ?
- Que devient un pattern lors d'un changement d'accord ?
- Quel comportement sans horloge MIDI externe ?

## 11. Plan de recherche

Le développement du nouveau moteur ne commence qu'après validation des gestes fondamentaux.

1. Tester physiquement le jeu littéral des 12 degrés sur plusieurs gammes.
2. Prototyper sur papier deux gestes maximum pour associer une note et une couleur.
3. Vérifier chaque geste avec une note, deux notes, un accord puis deux accords empilés.
4. Définir deux jeux candidats de huit commandes harmoniques.
5. Définir le format conceptuel d'une gamme, d'une recette, d'une palette et d'un preset complet.
6. Construire trois palettes contrastées et les tester avec plusieurs gammes avant d'étendre le catalogue.
7. Comparer `ROOT`, `CLOSE` et `BASS ROOT` sur dix progressions réelles.
8. Tester les transitions avec notes communes, changements de registre et relâchements dans plusieurs ordres.
9. Déterminer les limites musicales de densité avant de fixer les limites techniques.
10. Décrire cinq patterns orchestraux complets avec leurs dimensions indépendantes.
11. Comparer Gate, Latch et one-shot sur chaque famille de pattern.
12. Définir le minimum d'informations affichées pour jouer sans menu.
13. Séparer le périmètre V1 des extensions futures.
14. Seulement ensuite, rédiger le contrat logiciel et le plan de migration du moteur actuel.

## 12. Critères de validation du geste central

Le système harmonique n'est validé que si :

- une note seule reste immédiatement accessible ;
- deux notes seules peuvent être jouées sans interprétation cachée ;
- un accord simple demande un geste évident ;
- deux accords peuvent être empilés sans opération de programmation ;
- le résultat ne dépend pas d'une règle invisible impossible à anticiper ;
- le relâchement dans n'importe quel ordre ne laisse aucune note MIDI bloquée ;
- le musicien peut expliquer le geste sans expliquer l'algorithme ;
- le voice leading améliore le rendu sans changer l'accord demandé.

## 13. Sources de recherche

- Nopia : https://nopia.io/
- Nopia, MIDI Association : https://midi.org/innovation-award/nopia-semi-modular-midi-harmony-generator
- Orchid ORC-1, manuel : https://help.telepathicinstruments.com/en-US/user-manual-%E2%80%93-complete-1348749
- Chord Machine AKT-0.1 : https://akutostudio.com/
- Squarp Hapax, Live Mode : https://squarp.net/hapax/manual/modelive/
- Squarp Hapax, MIDI Effects : https://squarp.net/hapax/manual/modefx/
- Torso T-1, pitch et accords : https://docs.torsoelectronics.com/t1/core-concepts/pitch-chords/generating-melodies/
- Torso T-1, rythmes euclidiens : https://docs.torsoelectronics.com/t1/core-concepts/euclidean-rhythms/
- OXI One : https://oxiinstruments.com/oxi-one
- Dmitri Tymoczko, The Geometry of Musical Chords : https://dmitri.mycpanel.princeton.edu/voiceleading.pdf
- Open Music Theory, Chord-Scale Theory : https://human.libretexts.org/Bookshelves/Music/Music_Theory/Open_Music_Theory_2e_(Gotham_et_al.)/06%3A_Jazz/6.07%3A_Chord-Scale_Theory
- Music Theory for the 21st-Century Classroom, Impressionism : https://musictheory.pugetsound.edu/mt21c/Impressionism.html
- Music Theory for the 21st-Century Classroom, Quartal, Quintal and Secundal Harmony : https://musictheory.pugetsound.edu/mt21c/QuintalHarmony.html
- Berklee, Modal Harmony in Jazz Composition : https://online.berklee.edu/takenote/harmonic-considerations-modal-harmony/
