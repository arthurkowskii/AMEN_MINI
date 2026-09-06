# Presets harmoniques

Statut : archive du moteur d’août 2026 sous `firmware_midi/src/`, exclu du build actif. Le catalogue ci-dessous est historique. Les cinq presets actuels (`MAJOR`, `MINOR`, `HARM MIN`, `CINEMA`, `DARK`) et leurs trois palettes sont définis dans `../teensy/amen_midi/musical_presets.h` et `harmony_recipes.h`, et décrits dans [CONTROLS.md](CONTROLS.md). Aucun preset d’artiste n’est disponible dans le firmware actif.

Le catalogue statique contient **Major Basic**, **Minor Basic**, **Chromatic**, **Cinematic**, **Dark**, **Debussy / Impressionist** et **Ambient**.

Chaque preset possède:

- une géographie explicite des 12 pads;
- une table d'harmonisation préparée pour chacun des 12 degrés (`second`, `third`, `fourth`, `fifth`, `sixth`, `seventh`, `ninth`, `eleventh`, `thirteenth`);
- 2 ou 3 variations nommées et bornées (close, inversion, drop/open selon le preset).

Les neuf formes choisissent leurs notes dans la recette du degré; elles ne superposent donc plus des intervalles majeurs génériques. Le golden principal de Minor Basic est bien C–Eb–G, tandis que Chromatic, Dark et les presets artistiques conservent leurs recettes distinctes. Un tri par insertion explicite limité à six notes remplace `std::sort`. Range transpose par octaves, puis chaque note est repliée dans 0..127.

Les golden tests couvrent plusieurs degrés/formes de chacun des sept presets. Le parcours exhaustif couvre `7 × variationCount × 12 × 9`, vérifie bornage, ordre et déterminisme. **Cinematic, Dark, Debussy et Ambient restent des prototypes artistiques déterministes non validés à l'écoute.**
