#!/usr/bin/env python3
"""Append the AMEN MIDI hardware-validation checklist to the Notion roadmap page."""
import json
import os
import sys
import urllib.request

PAGE_ID = os.environ.get("AMEN_ROADMAP_PAGE", "3bdc9566-7341-8189-a50f-f32f7f92e6bb")

CHECKLIST = """## AMEN MIDI — Validation matérielle (à vérifier sur le PCB soudé)

Le cœur hôte est validé (Debug/Release/ASan/UBSan, revues conformité + qualité APPROVED). Cette liste est la seule partie non validée : elle demande le Teensy 4.1 branché. Le firmware se compile sans matériel : `firmware_midi/scripts/build_teensy.sh` (PlatformIO), checklist complète dans `firmware_midi/docs/HARDWARE_VALIDATION.md`.

1. USB-MIDI : appareil énuméré (fqbn teensy41 usb=SERIAL_MIDI), Serum/Pigments/Falcon reçoivent notes et accords, CC Shift+E1..E7 corrects, aucun message fantôme au boot
2. Matrice 5x4 + COL_SHIFT : 21 touches sans rebond, aucun ghosting multi-appui, ordre PAD01..12 = SW1..12 (bas→haut, gauche→droite), FX01..08 = SW13..20, SW21 = Shift bas-gauche
3. Encodeurs ENC1..7 : sens correct (sinon échanger A/B dans teensy_pinmap.h), un cran = un pas, clics OK, Shift+clic E7 = Panic
4. OLED SSD1306 (I2C 18/19, 0x3C) : 4 lignes lisibles, live update, pas de freeze après 1 h
5. Bimanuel : note-first et FX-first, Gate au-dessus de Latch, Gate A tenu repris après Gate B, aucun stale event
6. Hold : double-clic Shift 350 ms ON/OFF, re-appui degré tenu le retire, simple Shift ne bascule jamais
7. Panic : Shift+clic E7 coupe tout + CC123, aucune note bloquée après 1 h d'usage intense
8. Latence pad→note < 10 ms ressenti ; E5 pendant accord tenu sans coupure sale
9. Audit musical (avec Arthur) : Cinematic, Dark, Debussy, Ambient, Basic Major/Minor, Chromatic — voicings et registre ajustés à l'oreille
10. Hygiène : teensy_build/ et .pio/ ignorés, statut coché reporté ici et en commit"""


def api(method, url, body=None):
    req = urllib.request.Request(url, method=method)
    req.add_header("Authorization", f"Bearer {os.environ['NOTION_API_KEY']}")
    req.add_header("Notion-Version", "2025-09-03")
    req.add_header("Content-Type", "application/json")
    data = json.dumps(body).encode() if body is not None else None
    with urllib.request.urlopen(req, data=data) as resp:
        return json.loads(resp.read())


def main():
    payload = {
        "type": "insert_content",
        "insert_content": {"content": CHECKLIST},
    }
    resp = api("PATCH", f"https://api.notion.com/v1/pages/{PAGE_ID}/markdown", payload)
    print("OK:", resp.get("message", "inserted"))


if __name__ == "__main__":
    sys.exit(main())
