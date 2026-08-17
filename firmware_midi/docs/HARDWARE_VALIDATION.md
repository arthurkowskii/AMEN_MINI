# AMEN MIDI — Validation matérielle (roadmap de bring-up)

Le cœur hôte est validé (Debug/Release/ASan/UBSan, revues conformité + qualité APPROVED).
Cette liste est la **seule partie non validée** : elle demande le PCB AMEN_MINI soudé + Teensy 4.1.

Le firmware Teensy se construit sans matériel : `scripts/build_teensy.sh` (hex dans `teensy_build/`).
Chaque ligne ci-dessous doit être cochée **sur le vrai appareil**, et le résultat re-reporté
dans cette page, sur Notion et dans un commit.

## 1. USB-MIDI

- [ ] Flash du Teensy 4.1 (fqbn `teensy:avr:teensy41:usb=serialmidi`; sur Linux, installer d'abord `/etc/udev/rules.d/00-teensy.rules` depuis pjrc.com pour autoriser l'upload)
- [ ] Enumération : l'appareil apparaît comme contrôleur MIDI sur le PC
- [ ] Serum reçoit notes/accords (canal 1)
- [ ] Pigments reçoit notes/accords
- [ ] Falcon reçoit notes/accords
- [ ] CC Shift+E1..E7 arrivent sur les bons numéros (profil actif)
- [ ] Aucun message fantôme au boot (pas de notes au branchement)

## 2. Matrice (5 rangées × 4 colonnes + COL_SHIFT, diodes D1..D21)

- [ ] Les 21 touches répondent individuellement, sans rebond perceptible
- [ ] Aucune touche fantôme (ghosting) en appui multiple
- [ ] Ordre physique des 12 pads : PAD01..PAD12 = SW1..SW12 (bas → haut, gauche → droite)
- [ ] Ordre des 8 pads FX : FX01..FX08 = SW13..SW20 (deux rangées du haut)
- [ ] SW21 (Shift, bas-gauche) : simple appui ne déclenche PAS le Hold

## 3. Encodeurs (ENC1..ENC7)

- [ ] Sens de rotation correct pour E1..E7 (sinon échanger A/B dans `src/teensy/teensy_pinmap.h`)
- [ ] Un cran = un pas, pas de saut ni d'inversion en rotation rapide
- [ ] Clics détectés (debounce OK), notamment Shift + clic E7 = Panic
- [ ] E5 parcourt bien les 9 formes NOTE → THIRTEENTH

## 4. OLED (SSD1306, I2C 18/19, adresse 0x3C)

- [ ] 4 lignes affichées, texte lisible
- [ ] Mise à jour en live (preset, root, forme, profil, BPM)
- [ ] Pas de freeze après 1 h

## 5. Jeu bimanuel

- [ ] Note-first : pad tenu puis FX tenu → le FX reprend les notes immédiatement
- [ ] FX-first : FX tenu puis pad → la note rejoint le FX sans NoteOn direct
- [ ] Gate au-dessus de Latch → Latch reprend au relâchement
- [ ] Gate A tenu, Gate B pressé/relâché → Gate A reprend
- [ ] Aucun stale event après changement rapide de FX

## 6. Hold (double-clic Shift, fenêtre 350 ms)

- [ ] Double-clic → Hold ON ; nouveau pad rejoint, re-appui sur degré tenu le retire
- [ ] Second double-clic → libère les sources non physiques ; pad encore tenu reste jouable
- [ ] Simple Shift ne bascule jamais le Hold

## 7. Panic et notes bloquées

- [ ] Shift + clic E7 → tout coupe (notes, hold, FX, arp), CC123 émis
- [ ] Aucune note MIDI bloquée après 1 h d'utilisation intense (pads, FX, encoders)

## 8. Latence et ressenti

- [ ] Pad → note : pas de latence perceptible (< ~10 ms)
- [ ] Rotation E5 pendant un accord tenu : le voicing change sans coupure sale

## 9. Audit musical (avec Arthur, à l'oreille)

- [ ] Cinematic, Dark, Debussy/Impressionist, Ambient : voicings et inversions ajustés
- [ ] Presets Basic Major/Minor et Chromatic corrects
- [ ] Notes omises des 9/11/13 acceptables à l'écoute
- [ ] Registre (E4) utile sur chaque preset

## 10. Hygiène

- [ ] `teensy_build/` reste ignoré par Git
- [ ] Statut coché reporté dans Notion (page Roadmap AMEN_MINI) et en commit
