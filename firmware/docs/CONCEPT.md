# AMEN_MINI — Concept firmware V0.2 (verrouillé le 15/08/2026)

## 1. L'identité de la machine

AMEN_MINI est une machine à breaks : on pose un break, elle le découpe, on le joue live.

- Le nom dit tout : l'Amen Break est le cas d'usage canonique (6 s, ~33 coups, 16th).
- Tout est orienté jeu live, pas édition. L'édition nourrit la performance, jamais l'inverse.
- Instrument autonome (son dans le casque), pas un contrôleur.

## 2. Verrouillé (hardware, plan du 8/08)

- Teensy 4.1 + Audio Board SGTL5000, sortie casque + ligne
- microSD native, WAV 16 bits / 44,1 kHz
- 21 switches MX : 12 pads chops (bas) + 8 pads effets (haut) + 1 shift
- 7 encodeurs poussoirs, OLED SSD1306 I2C, USB-MIDI
- 8 voix simultanées

## 3. Le modèle audio (décision d'architecture)

- Sélection d'un fichier → lecture SD → décodage WAV → copie complète en PSRAM (8 Mo).
  Un break de 6 s stéréo ≈ 1 Mo. Plusieurs breaks en mémoire.
- Les voix lisent depuis la RAM (accès aléatoire). Jamais de lecture SD dans le callback audio.
- Pourquoi : granulaire, reverse, scrub = accès aléatoire — impossible en streaming SD.
  La RAM donne : retrig instantané, 8 voix, modes exotiques.
- Couches : moteur portable src/engine/ (C++ pur, testé PC, WavLoader du module 3) +
  couche Teensy (Audio Library, SD, GPIO, OLED).

## 4. Les 7 encodeurs en pages

7 encodeurs = 7 paramètres continus, contexte affiché sur l'OLED.

- Page pad : pitch (ralentir/accélérer), début de chop, fin de chop, + 1 param selon le mode
- Page globale : tempo (BPM), volume maître, niveau fx
- Page browser : navigation des WAV de la SD
- Shift = couches secondaires : page 2 d'un pad, tap tempo (shift + pad 12), etc.
- Principe : encodeur incrémental = jamais de saut de valeur ; l'OLED donne le contexte.
  Les pads jouent toujours directement (pas de menu pour déclencher).

## 5. Modes par pad (V1)

- One-shot — le chop joue une fois
- Loop — le chop boucle
- Granular — taille de grain, densité, scan, direction, pitch par grain
- Slice sync — le chop suit le tempo global (retrig en temps)

## 6. Les innovations (recherche 15/08/2026, sources en fin de doc)

Constat 2025-26 : le slicing devient performable, le granulaire tactile, la frontière
slice/granular/loop se brouille (Digitakt II Slice Machine, Roland P-6, Blackbox 2
Shredder, Tasty Chips GR-2).

- Auto-chop intelligent — 3 modes au choix (verrouillé) : transients, grille 16th, random.
  On pose le break, on joue direct.
- Morph de slices — crossfade entre deux chops adjacents (héritage Shredder, jouable live)
- Chance par pad — probabilité de déclenchement 0-100 %
- Humanize — micro-décalages de timing par pad
- Roll/stutter — retrig à la volée, taux par encodeur (geste signature)
- 8 pads fx : reverse, stutter/roll, filtre, pitch bend, tape stop, bitcrush, half-speed, delay
- Tempo-sync global : stutter, delay, retrig granulaire calés sur le BPM. Tap tempo (shift+pad 12)

## 7. Hors V1

- Pas d'enregistrement / resampling (exclu du plan verrouillé)
- Pas d'écran couleur, pas de LED par touche
- Pas de lecture SD directe en voix
- Séquenceur : V1 en version minimale — pattern unique 16 pas, trigger + chance,
  play/stop, édition via pads. Version complète multi-patterns après la rentrée.

## 8. Décisions verrouillées le 15/08

- Auto-chop : transients + grille 16th + random (au choix) ✓
- Séquenceur : oui en V1, version minimale ✓
- Vélocité : non (switches MX) — pas de compensation ✓
- Stéréo : conservée si le fichier est stéréo ✓
- Format : WAV uniquement. OGG écarté (15/08) — le décodeur Vorbis coûte ~3-6× une voix
  PCM en CPU, et le granulaire exige du PCM en accès aléatoire (donc décompression au
  chargement de toute façon). Le goulot est le calcul temps réel, pas le stockage
  (SD énorme, breaks 6-30 s, PSRAM = ~45 s de stéréo). Les effets extrêmes visés
  (tape stop, bitcrush, stutter, reverse…) sont quasi gratuits par design ; seuls
  delay + reverb pèsent, en envoi global unique.

## 9. Plan de travail — 2 semaines (17 → 30 août, rentrée début septembre)

Structure par jour : notions → quiz (5-10 min) → code (Arthur) → vérification (Boris) → correctifs.
Piste parallèle (Boris/opencode) : squelette du moteur, drivers Teensy (SD, matrice, encodeurs,
OLED), intégration Audio Library, vérification des pushes.
Priorités : P1 = chemin critique démo (J1-J5, J7, J12-J13). P2 = J6-J11 (modes avancés,
séquenceur) — glisse après rentrée si retard, la démo tient quand même.

### Semaine 1 — Le moteur sur PC (aucun matériel requis)

1. J1 (17/08) — WAV : format, PCM 16-bit, stéréo. WavLoader du module 3 → moteur. Quiz.
2. J2 (18/08) — Pitch : lecture à vitesse différente de 1, resampling linéaire. Quiz.
3. J3 (19/08) — Voix : pool de 8 voix, allocation, mixage, clipping. Quiz.
4. J4 (20/08) — PSRAM : RAM interne 1 Mo vs PSRAM 8 Mo, extmem_malloc, chargement. Quiz.
5. J5 (21/08) — Slices : découpe + mapping 12 pads.
6. J6 (22/08) — Auto-chop : transients / grille / random. Quiz.
7. J7 (23/08) — One-shot + loop + test d'écoute (rendu WAV PC).

### Semaine 2 — Modes avancés, effets, séquenceur, Teensy

1. J8 (24/08) — Granulaire : grains, taille, densité, scan, direction, clock retrig. Quiz.
2. J9 (25/08) — Slice sync + morph.
3. J10 (26/08) — Effets 8 pads : reverse, stutter/roll, filtre, pitch bend, tape stop, bitcrush, delay.
4. J11 (27/08) — Séquenceur minimal 16 pas + chance + humanize.
5. J12 (28/08) — Couche Teensy : Audio Library, SD→PSRAM, matrice, encodeurs, OLED
   (drivers Boris/opencode ; Arthur intègre le moteur au graphe audio).
6. J13 (29/08) — UI pages + USB-MIDI + intégration finale.
7. J14 (30/08) — Polish, démo, test réel, git. Si matériel pas arrivé : démo PC + flash dès réception.

## Sources (recherche 15/08/2026)

- Elektron Digitakt II — Slice Machine : gearnews.com/elektron-digitakt-ii-review
- Roland P-6 — Chop + granular : roland.com/us/products/p-6
- 1010music Blackbox 2 — Shredder : matrixsynth.com
- Tasty Chips GR-2 — slice mode, scan, pitch par grain : synthanatomy.com/2026/05/tasty-chips-gr-2
