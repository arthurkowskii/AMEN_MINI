# AMEN_MINI — Firmware

Sampler/chopper autonome : Teensy 4.1 + Audio Board SGTL5000 + microSD native.
21 switches MX (12 pads chops + 8 pads effets + 1 shift), 7 encodeurs poussoirs, OLED SSD1306 I²C.

## Outillage

- **arduino-cli** (toolchain officielle PJRC, sans l'IDE graphique)
- Édition du code : Zed (ou tout éditeur)
- Le moteur portable (`src/engine/`) se compile et se teste sur PC avec `g++` — aucun matériel requis

## Commandes

```bash
# Compiler le firmware pour Teensy 4.1
arduino-cli compile --fqbn teensy:teensy4:teensy41 .

# Flasher par USB (Teensy branchée)
arduino-cli upload -p /dev/ttyACM0 --fqbn teensy:teensy4:teensy41 .

# Tester le moteur sur PC (sans matériel)
g++ -std=c++17 -Wall -Wextra -I src src/engine/*.cpp test_native/main.cpp -o /tmp/amen_test && /tmp/amen_test
```

## Structure

```
firmware/
├── firmware.ino          # sketch principal : graphe audio, setup/loop
├── src/
│   └── engine/           # moteur portable (C++ pur, zéro dépendance matérielle)
│                         # wav_loader, slices, mapping, mixer, effets
└── test_native/          # tests moteur exécutés sur PC (génération WAV + écoute)
```

## Jalons

0. Hello world audio : oscillateur → SGTL5000 → casque ✅ (squelette)
1. Lecture d'un WAV 16 bits/44,1 kHz depuis la microSD
2. Contrôles : matrice 5×5, 7 encodeurs, OLED
3. Polyphonie 8 voix + mixage
4. Chopper : slices + mapping 12 pads
5. Effets : pitch, reverse, stutter, filtre (8 pads du haut)
6. UI : banques, assignations, USB-MIDI
7. Polish rentrée : stabilité, erreurs SD, démo
