# Presets harmoniques

Statut : le firmware actif contient quinze presets et onze palettes, définis dans `../teensy/amen_midi/musical_presets.h` et `harmony_recipes.h`. Le catalogue de recherche historique reste conservé plus bas.

## Famille NOIR / Protocol

Les cinq presets ajoutent une matière orchestrale sombre sans coupler automatiquement le preset à la banque de patterns. `NOIR HM` emploie le mineur harmonique et expose naturellement le mMaj7. `NOIR HM5` utilise le phrygien dominant et son bII occultiste. `NOIR JZ` utilise le mineur mélodique. `NOIR DIM` exploite l'octatonique demi-ton/ton et ses accords diminués. `NOIR WT` utilise les tons entiers pour la couleur pulp.

| Preset | Gamme | Palette de recettes, SW13 à SW20 |
|---|---|---|
| `NOIR HM` | Harmonic minor | `SEVENTH`, `NINTH`, `MIN6`, `TRIAD`, `HALF DIM`, `DIM7`, `11 NO5`, `CLUSTER` |
| `NOIR HM5` | Phrygian dominant | `SEVENTH`, `TRIAD`, `SUS4`, `MINMAJ7`, `MIN9`, `DIM7`, `7#5`, `PULP` |
| `NOIR JZ` | Melodic minor | `SEVENTH`, `NINTH`, `SIXTH`, `SIXTH`, `NINTH`, `AUGMENT`, `HALF DIM`, `TRITONE` |
| `NOIR DIM` | Octatonic | `SEVENTH`, `NINTH`, `TRIAD`, `ADD2`, `QUARTAL`, `SEVENTH`, `NINTH`, `PULP` |
| `NOIR WT` | Whole tone | `AUGMENT`, `7#5`, `SUS2`, `FIFTH`, `SUS9`, `TRITONE`, `SEVENTH`, `PULP` |

Les huit recettes chromatiques nouvelles sont `MINMAJ7` (0,3,7,11), `DIM7` (0,3,6,9), `HALF DIM` (0,3,6,10), `7#5` (0,4,8,10), `AUGMENT` (0,4,8), `MAJ7#11` (0,4,6,11), `TRITONE` (0,6) et `PULP` (0,1,2,3). Les autres noms du tableau peuvent désigner des recettes diatoniques ou chromatiques existantes ; les index exacts des palettes dans le code restent la source de vérité.

## Archive d'août 2026

Le catalogue statique contient **Major Basic**, **Minor Basic**, **Chromatic**, **Cinematic**, **Dark**, **Debussy / Impressionist** et **Ambient**.

Chaque preset possède:

- une géographie explicite des 12 pads;
- une table d'harmonisation préparée pour chacun des 12 degrés (`second`, `third`, `fourth`, `fifth`, `sixth`, `seventh`, `ninth`, `eleventh`, `thirteenth`);
- 2 ou 3 variations nommées et bornées (close, inversion, drop/open selon le preset).

Les neuf formes choisissent leurs notes dans la recette du degré; elles ne superposent donc plus des intervalles majeurs génériques. Le golden principal de Minor Basic est bien C–Eb–G, tandis que Chromatic, Dark et les presets artistiques conservent leurs recettes distinctes. Un tri par insertion explicite limité à six notes remplace `std::sort`. Range transpose par octaves, puis chaque note est repliée dans 0..127.

Les golden tests couvrent plusieurs degrés/formes de chacun des sept presets. Le parcours exhaustif couvre `7 × variationCount × 12 × 9`, vérifie bornage, ordre et déterminisme. **Cinematic, Dark, Debussy et Ambient restent des prototypes artistiques déterministes non validés à l'écoute.**
