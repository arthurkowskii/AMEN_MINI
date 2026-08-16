# PLAN — Revue et ajustements post-analyse (Août 2026)

## Contexte

Analyse complète du projet AMEN_MINI menée le 16/08/2026 sur la base du roadmap,
des contrôles, du PCB, du firmware moteur et de l'interface OLED. Ce document
enregistre les décisions d'ajustement et le plan d'action qui en découle.

## 1. Paradigme d'interaction : Mode = comportement de lecture

**Décision :** Le mode de lecture (ONE SHOT / LOOP / GRANULAR / SLICE SYNC)
définit le comportement de latch, pas un mécanisme séparé.

| Mode | Appui simple | Maintien | Arrêt |
|------|-------------|----------|-------|
| ONE SHOT | Lecture complète du range | Gate : lecture tant que tenu | Fin de range ou relâchement (gate) |
| LOOP | Lecture en boucle (latch) | Lecture en boucle | Deuxième appui stoppe |
| GRANULAR | Nuage de grains (latch, futur) | — | Deuxième appui stoppe |
| SLICE SYNC | Slices BPM (latch, futur) | — | Deuxième appui stoppe |

**Question ouverte :** Comment l'utilisateur choisit-il entre ONE SHOT "full auto"
et ONE SHOT "gate" ? Candidats :
- E6 (actuellement réservé) → `TRIG MODE: AUTO / GATE`
- Ou clic long sur E5 pour basculer la variante

**À trancher avant implémentation.**

## 2. Navigateur SD : liste verticale avec scroll horizontal

- Affichage : liste verticale de 3 lignes (10 px par ligne), dossier en haut.
- Navigation : E1 tourne = déplace la flèche de sélection.
- Fichiers longs : après 0.5 s d'arrêt sur une entrée, le nom défile
  horizontalement à 1 colonne/pixel par frame (30 fps).
- Préfixes : `>` pour dossier, `-` pour fichier WAV.
- Optimisation : noms tronqués à l'affichage avec `..` si pas en scroll.

## 3. Crossfade de vol de voix

- Durée : 64 frames (~1.5 ms à 44.1 kHz). Inaudible en tant que "crossfade"
  mais suffisant pour éliminer le clic de coupure nette.
- Implémentation : `VoiceManager::trigger()` applique un fade out linéaire sur
  la voix volée pendant 64 frames, synchronisé avec le début de la nouvelle voix.
- Pas de paramètre utilisateur : comportement transparent par défaut.

## 4. Architecture firmware : principes maintenus

- Moteur C++17 pur dans `src/engine/` — zéro include Arduino.
- PcmView non propriétaire, buffers caller-provided pour le Repeat.
- Tests natifs pour chaque module DSP.
- Optimisation jusqu'à la lecture d'octets (pas de `std::vector` dans le chemin
  audio, pas d'allocation dans le callback).

## 5. Interface OLED : principes validés et ajustements

**Conservé :**
- Framebuffer 128×32 monochrome portable.
- Tile 32×32 réservée pour l'iconographie religieuse (3 états : calme/tendu/furieux).
- Overlay paramètre 1 s avec barre de progression.
- Police 3×5 compacte.

**Ajusté :**
- Overlay paramètre : reste affiché tant que l'encodeur sélectionné tourne
  (disparaît 1 s après la dernière interaction, pas 1 s fixe).
- Noms techniques longs → abréviations : `1/8T` au lieu de `EIGHTH TRIPLET`,
  `1/16T` au lieu de `SIXTEENTH TRIPLET`.
- La tile 32×32 reste en placeholder "ART" — les sprites sont produits après
  validation fonctionnelle complète.

## 6. FX et DSP : feuille de route mise à jour

**Rappel mapping physique :** 12 pads voix + 8 pads FX + 1 Shift = 21 switches MX.

**Liste FX assignables (8 slots) :**

| Slot | Nom | DSP | Priorité |
|------|-----|-----|----------|
| 1 | BLANK | — (désassigne) | — |
| 2 | REPEAT | ✅ Implémenté | P1 |
| 3 | REVERSE | ❌ À faire | P1 |
| 4 | TRANCE GATE | ❌ À faire | P2 |
| 5 | FILTER | ❌ Nouveau | P2 |
| 6 | DELAY | ❌ Nouveau | P2 |
| 7 | BITCRUSH | ❌ Nouveau | P2 |
| 8 | CHAOS | ❌ Nouveau | P2 |

**Nouveaux DSP proposés (post-démo septembre) :**
- **FILTER** : state-variable (LP/BP/HP) avec cutoff + résonance.
- **DELAY** : ping-pong stéréo avec feedback, synchro BPM (divisions).
- **BITCRUSH** : réduction de résolution + sample rate reduction.
- **CHAOS** : transformation globale du mix en un geste (option A du roadmap :
  devient un slot FX, pas un nouveau bouton).

**E6 (actuellement réservé) :** proposition de lui assigner une fonction de
modulation — LFO assignable (forme, vitesse, destination : pitch ou filter cutoff).

## 7. Chemin critique septembre 2026 (priorités révisées)

| Priorité | Tâche | Statut |
|----------|-------|--------|
| **URGENT** | J4 — PSRAM + J12 — Couche Teensy | Non commencé |
| **URGENT** | Crossfade vol de voix (64 frames) | Non commencé |
| P1 | J5 — Slices 12 pads | Suspendu |
| P1 | J7 — Mode Loop (latch) | Non commencé |
| P1 | REVERSE DSP | Non commencé |
| P1 | Overlay persistant tant qu'encodeur tourne | Non commencé |
| P1 | Noms abrégés dans overlays (1/8T, 1/16T) | Non commencé |
| P2 | Navigateur SD avec scroll horizontal | Non commencé |
| P2 | GRANULAR (J8) | Suspendu |
| P2 | TRANCE GATE DSP | Non commencé |
| P2 | FILTER + DELAY + BITCRUSH | Non commencé |

## 8. Points restant à trancher

1. **ONE SHOT auto vs gate** : quel mécanisme de sélection ? E6 dédié ?
2. **E6 fonction** : modulation LFO ou autre ?
3. **Soft clipper sur le bus de mix** : à ajouter maintenant ou après mesure
   CPU sur Teensy ?
4. **Hauteur physique encodeurs vs touches MX** : vérifier à l'assemblage que
   les capuchons d'encodeurs ne gênent pas l'accès aux touches.
