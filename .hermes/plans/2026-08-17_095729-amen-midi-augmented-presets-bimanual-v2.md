# AMEN MIDI — Presets augmentés et jeu bimanuel — Implementation Plan V2

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Status:** Ce document remplace le plan AMEN MIDI précédent lorsqu’ils se contredisent. Il conserve la même architecture générale, mais verrouille les deux décisions approuvées par Arthur : presets harmoniques augmentés et interaction bimanuale.

**Goal:** Transformer le second PCB AMEN_MINI en surface MIDI harmonique portable : la main basse joue les 12 degrés avec une forme choisie librement par E5, tandis que la main haute applique huit gestes MIDI assignables. Les presets Cinematic, Dark, Debussy, Ambient, etc. définissent de vrais vocabulaires harmoniques et restent combinables avec des profils Serum, Falcon ou orchestraux.

**Architecture:** Trois couches de données indépendantes évitent la duplication : `HarmonyPreset` définit le langage musical, `InstrumentProfile` définit la cible MIDI, `PerformanceScene` définit les huit FX et le tempo. Le cœur portable transforme des `SourceIntent` issus des 12 pads en `NotePlan`, puis en événements MIDI via l’algorithme bimanuel actif. La couche Teensy ne fait que scanner le PCB, afficher l’OLED et émettre l’USB-MIDI.

**Tech Stack:** C++17 portable, Teensy 4.1, USB-MIDI Teensyduino, OLED SSD1306 128×32, matrice 5×5, 7 encodeurs poussoirs, tests natifs `g++`, horloge simulée, harness PC.

---

## 1. Contrat produit verrouillé

### Les 12 pads de la main basse

- Ils représentent toujours douze degrés successifs du vocabulaire courant.
- Leur géographie ne change pas lorsque la tonique ou le preset change.
- E5 sélectionne librement la forme harmonique globale.
- Base proposée : `1/NOTE`, `SUS2`, `TRI`, `SUS4`, `6`, `7`, `9`, `11`, `13`.
- `1/NOTE` émet une seule note.
- Les autres formes consultent le preset pour construire le voicing préparé du degré pressé.
- Un changement d’E5 ne modifie jamais rétroactivement les notes déjà tenues.

### Les 8 pads de la main haute

- Chaque pad contient un algorithme MIDI assignable.
- P0 : `BLANK`, `STRUM`, `ARP`, `RUN UP`, `RUN DOWN`, `TRANCE GATE`, `NOTE REPEAT`, `RANDOM`, `VELOCITY`.
- Maintenir un pad FX active son geste et le rend cible des encodeurs.
- Pad FX tenu + E1 : browser ; clic E1 : assignation.
- Chaque slot mémorise ses paramètres et son comportement `GATE/LATCH`.
- P0 autorise un seul algorithme temporel principal ; le dernier pad FX pressé prend la priorité. Les modificateurs non temporels, comme Velocity/Humanize, pourront être composés en P1.

### Pourquoi ce design

Nopia garde la même disposition harmonique dans toutes les tonalités et sépare notamment les rôles Keys, Bass, Arp et Pad.[3] HiChord combine degrés diatoniques, inversions préparables, voice leading et plusieurs modes appliqués aux mêmes boutons.[2] L’AKT-0.1 valorise explicitement le jeu bimanuel et le strum.[4] AMEN MIDI adapte ces primitives au hardware existant : 12 sources en bas, 8 transformations en haut, sans moteur audio interne.

---

## 2. Les trois couches de preset

### A. HarmonyPreset — ce que l’on joue

Un preset harmonique n’est pas seulement une gamme. Il contient :

- identifiant stable et nom affiché ;
- famille esthétique : Basic, Cinematic, Dark, Impressionist, Ambient, etc. ;
- variantes internes ;
- douze degrés et leur hauteur relative ;
- orthographe préférée des notes ;
- formes disponibles sur E5 ;
- pour chaque degré × forme : intervalles, ordre, décalages d’octave, basse alternative et notes omises ;
- registre recommandé et limites grave/aiguë ;
- polyphonie maximale ;
- accords empruntés ou dominantes secondaires explicitement préparés ;
- valeur E5 par défaut ;
- nom d’accord attendu pour l’OLED.

Représentation cible :

```cpp
struct VoicingRecipe {
    std::array<std::int8_t, kMaxVoices> scaleDegreeOffsets;
    std::array<std::int8_t, kMaxVoices> octaveOffsets;
    std::uint8_t voiceCount;
    std::int8_t bassDegreeOverride;
    ChordDisplayQuality displayQuality;
};

struct HarmonyPreset {
    PresetId id;
    const char* name;
    const HarmonyVariation* variations;
    std::uint8_t variationCount;
};
```

La structure finale peut différer, mais les données restent statiques, bornées et sans allocation pendant le jeu.

### B. InstrumentProfile — où le MIDI va

Le profil d’instrument est indépendant du preset harmonique :

- nom : Serum Lead, Falcon Multi, Strings Legato, Brass Short, etc. ;
- canal principal ;
- rôles optionnels `MAIN`, `BASS`, `ARP/RUN`, `PAD` et leur canal ;
- vélocité de base ;
- plage MIDI autorisée ;
- quatre macros CC configurables ;
- politique E6 : Velocity, CC1, CC11, CC1+CC11 ou Macro ;
- key-switch optionnel ;
- durée/release policy optionnelle pour instruments courts ;
- Program Change optionnel, désactivé par défaut.

Ainsi, `Debussy + Serum` et `Debussy + Falcon Multi` réutilisent exactement les mêmes notes mais pas le même routage.

### C. PerformanceScene — comment on le joue

La scène contient :

- les huit assignations FX ;
- les paramètres de chaque slot ;
- Gate/Latch ;
- BPM ;
- divisions rythmiques ;
- preset harmonique sélectionné ;
- profil d’instrument sélectionné ;
- paramètres globaux E2–E7 ;
- version et checksum pour la persistence.

Le preset harmonique ne stocke pas les assignations FX. Le profil Serum ne stocke pas les accords. La scène ne duplique pas les tables de voicing.

---

## 3. Première bibliothèque harmonique

Les presets artistiques ne seront pas remplis automatiquement puis déclarés « musicaux ». Chaque preset passe par export MIDI, écoute et validation d’Arthur.

### P0 — presets structurels

1. `MAJOR BASIC`
   - Référence de test.
   - Harmonisation fonctionnelle attendue.
   - Voicings resserrés et prévisibles.

2. `MINOR BASIC`
   - Minor naturelle.
   - Référence pour accords mineurs, diminués et extensions.

3. `CHROMATIC`
   - Douze demi-tons.
   - Utile pour Serum/Falcon et jeu non diatonique.
   - Les formes E5 sont construites par intervalles absolus lorsque la logique diatonique ne s’applique pas.

### P1 — presets artistiques approuvés

4. `CINEMATIC`
   - Registres larges, quintes ouvertes, basses solides, tierces parfois omises.
   - Extensions réparties sans amas grave.
   - Voicings adaptés aux grands pads, synthés larges et ensembles orchestraux.

5. `DARK`
   - Palette mineure/harmonique/modalement sombre.
   - Secondes mineures, tritons et basses alternatives seulement lorsqu’ils sont composés intentionnellement.
   - Variantes possibles : Dark Minor, Phrygian, Harmonic.

6. `DEBUSSY`
   - Catégorie Impressionist, pas imitation automatique d’un compositeur.
   - Variantes candidates : Whole Tone, Pentatonic, Lydian/Modal, Parallel Planing.
   - Voicings quartaux, notes ajoutées, mouvements parallèles et ambiguïtés fonctionnelles préparés à l’écoute.
   - Les labels E5 restent des gestes d’extension, mais la recette peut s’écarter de la tertialité classique selon la variante.

7. `AMBIENT`
   - Voicings ouverts, quintes, neuvièmes, notes communes conservées, faible densité dans le grave.
   - Variantes : Open, Suspended, Floating.
   - Pensé aussi bien pour pads Falcon/Serum que pour strings lentes.

### P2 — extensions

- Neo-Soul, Jazz, Medieval, Sci-Fi, Minimal, Gospel, Modal Interchange.
- Aucun ajout avant validation des quatre presets P1.

---

## 4. Interaction bimanuale précise

### État logique

Les pads du bas ne possèdent pas directement les notes MIDI. Ils créent un `SourceIntent` :

```text
pad physique + snapshot tonalité/preset/E5/octave
→ NotePlan préparé
→ renderer direct ou algorithme FX actif
→ événements MIDI possédés par cette source
```

Cela permet de changer proprement d’algorithme pendant qu’un pad musical reste tenu.

### Cas 1 — FX d’abord, pad musical ensuite

1. Arthur maintient ARP avec la main haute.
2. Il presse un ou plusieurs degrés avec la main basse.
3. Les nouveaux `SourceIntent` entrent directement dans ARP.
4. Relâcher un degré enlève uniquement sa matière de l’arpège.
5. Relâcher ARP en Gate arrête l’arpège et reprend le rendu direct des degrés encore tenus.

### Cas 2 — pad musical d’abord, FX ensuite

1. Arthur tient un accord direct.
2. Il presse TRANCE GATE, STRUM ou ARP.
3. Le moteur ferme proprement la génération directe de cette source.
4. L’algorithme adopte le `NotePlan` déjà tenu, sans demander de retrigger physique.
5. À la sortie du FX, les notes directes reprennent si le pad musical est encore tenu.

### Cas 3 — passage d’un FX à un autre

- En P0, le dernier algorithme temporel pressé gagne.
- L’ancien reçoit `cancel(sourceGeneration)` ; ses événements futurs sont supprimés et ses notes actives sont fermées.
- Le nouveau repart depuis les `SourceIntent` actuellement tenus.
- Relâcher le dernier pad FX retombe sur le pad FX temporel précédent encore physiquement tenu ; sinon retour direct.

### Gate/Latch

- Gate : actif uniquement tant que le pad FX est tenu.
- Latch : premier appui active ; second appui désactive.
- Un latch actif accepte ensuite les pads musicaux normalement.
- Un pad Gate peut temporairement prendre la priorité sur un latch, puis le latch reprend au relâchement.
- Panic annule Gate, Latch, scheduler, ownership et pads tenus logiques.

### Règle de sécurité

Chaque transition change de génération :

```text
SourceId + GenerationId
```

Les événements retardés d’une ancienne génération sont ignorés. Cette règle empêche un ancien arpège ou run de réactiver une note après changement de FX.

---

## 5. Mapping des encodeurs

### Aucun FX tenu

- E1 PRESET : parcourir les HarmonyPreset ; clic = charger.
- E2 ROOT : tonique ; clic = orthographe dièse/bémol lorsque disponible.
- E3 VARIATION : variante du preset, par exemple Ambient Open/Floating ou Debussy Whole Tone/Pentatonic.
- E4 RANGE : octave de base ; clic = valeur recommandée du preset.
- E5 CHORD : `1/NOTE`, `SUS2`, `TRI`, `SUS4`, `6`, `7`, `9`, `11`, `13` ; clic secondaire à décider plus tard.
- E6 MACRO : comportement déterminé par InstrumentProfile.
- E7 BPM : tempo global ; clic = tap tempo candidat.

### FX tenu

- E1 : browser/assign.
- E2 : amount/depth.
- E3 : division/rate.
- E4 : range/direction.
- E5 : variante ; clic = Gate/Latch.
- E6 : chance/velocity/humanize/density.
- E7 : BPM global, jamais masqué.

### Shift

- Sans Shift, les sept encodeurs contrôlent exclusivement le moteur musical AMEN MIDI : preset, tonique, variation, registre, forme harmonique, expression AMEN et BPM, ou les paramètres du FX AMEN tenu.
- Shift + E1…E7 contrôle les sept paramètres du `InstrumentProfile` actif. Ces destinations sont configurables par profil pour Serum, Pigments, Falcon et les banques orchestrales ; aucun numéro CC commercial n’est supposé universel.
- Les mouvements commencent en mode relatif/soft takeover afin qu’un passage normal → Shift ne provoque aucun saut brutal de paramètre.
- Shift + clic E7 : Panic immédiat.
- Double-clic sur Shift : bascule le Hold global des pads musicaux.

### Hold global par double-clic Shift

- Le détecteur utilise deux clics complets press/release dans une fenêtre configurable (valeur initiale : 350 ms), avec debounce séparé.
- Activer Hold capture les `SourceIntent` musicaux actuellement tenus et maintient leur rendu après relâchement physique.
- Une fois Hold actif, tout nouveau pad musical rejoint l’ensemble tenu ; appuyer de nouveau sur un degré déjà tenu le retire individuellement.
- Un second double-clic Shift libère toutes les sources détenues par Hold, sans désactiver les FX Latch indépendants.
- Hold conserve les intentions musicales, pas seulement les numéros de notes : les transitions Direct ↔ FX continuent donc de fonctionner sur l’ensemble tenu.
- Changer preset, tonique, variation, E5 ou registre n’altère pas rétroactivement les sources déjà détenues ; les nouveaux pads utilisent le nouvel état.
- Panic annule Hold, FX Gate/Latch, scheduler et registre de notes.
- Un simple appui Shift reste uniquement un modificateur de la couche encodeur et ne doit jamais déclencher Hold.

---

## 6. Écrans OLED

### Home

```text
DEBUSSY      F#
WHOLE        9
BPM 096  SRM
```

Le suffixe de profil peut afficher `SRM`, `FLC`, `STR`, etc.

### Accord tenu

```text
Abmaj9/C
VOICING D3
CH 1+2
```

### FX tenu

```text
FX3  ARP
UPDOWN 1/16T
GATE  OCT 2
```

### Browser preset

Trois lignes visibles, nom long défilant après 0,5 s :

```text
  CINEMATIC
> DEBUSSY
  AMBIENT
```

### Priorités d’affichage

Panic/erreur > browser > FX tenu > accord tenu > home.

---

## 7. Organisation des fichiers

```text
firmware_midi/
├── docs/
│   ├── CONCEPT.md
│   ├── CONTROLS.md
│   ├── HARMONY_PRESETS.md
│   ├── INSTRUMENT_PROFILES.md
│   └── BMANUAL_CONTRACT.md
├── src/
│   ├── music/
│   │   ├── musical_state.h
│   │   ├── harmony_preset.h/.cpp
│   │   ├── preset_catalog.h/.cpp
│   │   ├── chord_builder.h/.cpp
│   │   ├── note_plan.h
│   │   └── presets/
│   │       ├── basic_presets.cpp
│   │       ├── cinematic.cpp
│   │       ├── dark.cpp
│   │       ├── debussy.cpp
│   │       └── ambient.cpp
│   ├── profiles/
│   │   ├── instrument_profile.h/.cpp
│   │   └── profile_catalog.h/.cpp
│   ├── performance/
│   │   ├── source_intent.h
│   │   ├── bimanual_engine.h/.cpp
│   │   ├── performance_scene.h/.cpp
│   │   └── pad_assignments.h/.cpp
│   ├── midi/
│   │   ├── midi_event.h
│   │   ├── midi_scheduler.h/.cpp
│   │   └── note_registry.h/.cpp
│   ├── algorithms/
│   ├── ui/
│   └── teensy/
└── test_native/
    ├── harmony_preset_test.cpp
    ├── preset_audition.cpp
    ├── bimanual_engine_test.cpp
    ├── transition_matrix_test.cpp
    └── performance_harness.cpp
```

Le firmware sampler actuel reste sous `firmware/`. Aucun fichier audio ne doit être modifié pour implémenter AMEN MIDI.

---

## 8. Plan d’implémentation

### Task 1 — Écrire les contrats avant le code

**Files:**
- Create: `firmware_midi/docs/HARMONY_PRESETS.md`
- Create: `firmware_midi/docs/BIMANUAL_CONTRACT.md`
- Create: `firmware_midi/docs/INSTRUMENT_PROFILES.md`

**Steps:**
1. Copier les décisions verrouillées de ce plan.
2. Écrire la matrice complète des transitions bimanuales.
3. Définir la différence HarmonyPreset / InstrumentProfile / PerformanceScene.
4. Faire valider l’ordre E5 et les variantes des quatre presets artistiques.
5. Commit: `docs(midi): lock augmented presets and bimanual grammar`.

### Task 2 — Créer les types sans allocation

**Files:**
- Create: `src/music/note_plan.h`
- Create: `src/music/harmony_preset.h`
- Create: `src/profiles/instrument_profile.h`
- Create: `src/performance/performance_scene.h`
- Create: `test_native/harmony_preset_test.cpp`

**TDD:** test rouge sur tailles, bornes et catalogue absent ; implémentation minimale ; compilation stricte.

**Checks:** aucune chaîne propriétaire dynamique dans le chemin temps réel ; tailles maximales explicites ; IDs stables.

**Commit:** `feat(midi): define bounded preset and scene contracts`.

### Task 3 — Implémenter Basic Major/Minor/Chromatic

**Files:**
- Create: `src/music/preset_catalog.h/.cpp`
- Create: `src/music/presets/basic_presets.cpp`
- Create: `src/music/chord_builder.h/.cpp`
- Extend tests.

**Checks:**
- 12 degrés croissants.
- Toutes les formes E5 valides.
- Voicings préparés dans 0–127.
- Noms d’accord cohérents.
- Même entrée = mêmes notes.

**Commit:** `feat(harmony): add reference augmented presets`.

### Task 4 — Créer le validateur de preset

**Files:**
- Create: `src/music/preset_validator.h/.cpp`
- Create: `test_native/preset_validation_test.cpp`

**Validation:** IDs uniques, voiceCount, degrés, classes de hauteur, basse, limites MIDI, nom OLED, formes manquantes, amas grave configurable, polyphonie maximale.

**Commit:** `test(harmony): validate every preset recipe at build time`.

### Task 5 — Créer l’outil d’audition

**Files:**
- Create: `test_native/preset_audition.cpp`
- Create: `test_native/auditions/README.md`

**Output:** progression de référence par preset et forme, export MIDI ou émission vers port MIDI virtuel, log des notes/accords/voicings.

**Progressions minimales:** I–V–vi–IV, ii–V–I, degrés ascendants 1–12, alternance proche/lointaine, basses alternatives.

**Gate:** aucune étiquette Cinematic/Dark/Debussy/Ambient n’est validée sans écoute d’Arthur.

**Commit:** `feat(harmony): add deterministic preset audition harness`.

### Task 6 — Composer les quatre presets artistiques

**Files:**
- Create: `src/music/presets/cinematic.cpp`
- Create: `src/music/presets/dark.cpp`
- Create: `src/music/presets/debussy.cpp`
- Create: `src/music/presets/ambient.cpp`
- Document: `docs/HARMONY_PRESETS.md`

**Workflow par preset:**
1. Une variation seulement.
2. Export des progressions.
3. Écoute avec patch neutre.
4. Écoute Serum/Falcon.
5. Écoute banque orchestrale si pertinente.
6. Arthur note ce qui fonctionne.
7. Corriger voicings.
8. Ajouter une deuxième variation seulement après validation.

**Commit:** un commit par preset validé, pas un gros commit de quatre presets non écoutés.

### Task 7 — Modéliser SourceIntent et ownership

**Files:**
- Create: `src/performance/source_intent.h`
- Create: `src/midi/note_registry.h/.cpp`
- Create: `test_native/source_intent_test.cpp`

**Checks:** snapshot complet au pad-down ; Note Off exact après changement de preset/E5 ; SourceId+GenerationId ; Panic vide tout.

**Commit:** `feat(performance): snapshot pad intentions and own emitted notes`.

### Task 8 — Implémenter le moteur bimanuel direct/Gate

**Files:**
- Create: `src/performance/bimanual_engine.h/.cpp`
- Create: `test_native/bimanual_engine_test.cpp`

**Scénarios:** FX-first, note-first, relâchement FX avec note tenue, relâchement note pendant FX, priorité dernier FX tenu, retour au précédent FX tenu, retour direct.

**Commit:** `feat(performance): add gate-based bimanual transitions`.

### Task 9 — Ajouter Latch et matrice exhaustive de transitions

**Files:**
- Extend: `src/performance/bimanual_engine.cpp`
- Create: `test_native/transition_matrix_test.cpp`

**Matrix:** direct↔Gate, direct↔Latch, Latch↔Gate temporaire, FX A↔FX B, changement preset/E5 pendant chaque état, Panic à chaque état.

**Invariant final de chaque test:** scheduler vide ou attendu, registre de notes exact, aucun événement d’ancienne génération.

**Commit:** `feat(performance): harden latch and nested FX transitions`.

### Task 10 — Intégrer les algorithmes P0

Ordre vertical : STRUM → ARP → RUN → TRANCE GATE/REPEAT → RANDOM/VELOCITY.

Pour chaque algorithme : test rouge, implémentation, annulation, bimanual transitions, Panic, écoute harness, commit séparé.

### Task 11 — Profils Serum, Falcon et générique orchestral

**Files:**
- Create: `src/profiles/profile_catalog.h/.cpp`
- Create: `docs/INSTRUMENT_PROFILES.md`
- Create: `test_native/instrument_profile_test.cpp`

**Profils de départ:** Generic Synth, Serum User, Falcon User, Generic Orchestral. Les numéros CC réels restent configurables ; aucun mapping commercial n’est supposé universel.

**Test:** même NotePlan, événements différents selon routage, mais hauteurs musicales identiques.

**Commit:** `feat(profiles): separate synth and orchestral MIDI mappings`.

### Task 12 — UI et harness complet

**Files:**
- Create: `src/ui/midi_screen_ui.h/.cpp`
- Create: `test_native/performance_harness.cpp`
- Create: `start_midi_firmware.ps1`

**Demo:** choisir Debussy, F#, forme 9 ; maintenir ARP puis jouer ; passer à Ambient ; changer profil Serum→Falcon ; vérifier écran et événements.

### Task 13 — Persistence

Sauvegarder uniquement PerformanceScene et choix de catalogues, avec version/checksum. Les catalogues de presets compilés ne sont pas recopiés en flash mutable.

### Task 14 — Teensy et PCB

Intégrer matrice, encodeurs, OLED et USB-MIDI seulement après passage des tests natifs et du harness. Le netlist réel `hardware/AMEN_MINI.net` donne les pins Arduino suivantes : `COL0…3=0…3`, `COL_SHIFT=4`, `ROW0=5`, `ROW1=6`, `ROW2=9`, `ROW3=14`, `ROW4=15`, `SDA=18`, `SCL=19`, `E1 A/B/SW=16/17/35`, `E2=22/24/36`, `E3=25/26/37`, `E4=27/28/38`, `E5=29/30/39`, `E6=31/32/40`, `E7=33/34/41`. `SW21` est Shift, seul sur `COL_SHIFT`. Vérifier ces valeurs de nouveau depuis les vrais fichiers `hardware/AMEN_MINI.*` avant upload ; ne jamais utiliser les anciennes docs AKOR/Pico.

### Task 15 — Validation finale musicale

- 30 minutes sans note bloquée.
- Test bimanuel rapide avec changement de FX.
- Démo Serum.
- Démo Falcon.
- Démo orchestrale.
- Validation d’au moins Basic, Cinematic, Dark, Debussy et Ambient.
- Enregistrement MIDI de 30–60 secondes montrant qu’une même gestuelle change de cible sans changer de logique.

---

## 9. Definition of Done

- Les 12 pads gardent une géographie stable.
- E5 choisit librement la forme harmonique.
- Chaque preset fournit des voicings/inversions préparés et validés à l’écoute.
- HarmonyPreset, InstrumentProfile et PerformanceScene sont réellement indépendants.
- Le jeu bimanuel fonctionne FX-first et note-first.
- Gate/Latch et changement de FX ne laissent aucune note bloquée.
- Serum, Falcon et une banque orchestrale fonctionnent via des profils différents.
- L’OLED affiche le résultat musical réel.
- Panic est toujours accessible.
- Aucun `delay()`, allocation ou I/O de stockage dans scheduler/tick.
- Le firmware sampler existant reste intact.

## 10. Décisions encore ouvertes

1. Ordre E5 exact : confirmer si `TRI` et `11` restent dans P0, et si `1` est bien le label physique de note seule.
2. Première variation de Debussy : Whole Tone, Pentatonic, Lydian/Modal ou Planing.
3. Rôles MIDI P1 : MAIN/BASS/ARP/PAD activés dès le premier profil Falcon ou après le P0 mono-canal.
4. Clic E5 libre : reset vers `1`, forme favorite ou fonction laissée vide.

## Sources

[2] https://manual.hichord.shop — HiChord manual
[3] https://nopia.io — Nopia official site
[4] https://www.musicradar.com/news/akuto-studio-chord-machine-akt-01 — MusicRadar: AKT-0.1
