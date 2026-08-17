# Concept

AMEN MIDI transforme les 12 pads en sources musicales stables (degrés 0..11) et les 8 pads FX en transformations assignables. Une source reçoit un `SourceId + GenerationId`, capture au pad-down son preset, root, variation, range et forme E5, puis conserve ce snapshot jusqu'à sa libération. Les paramètres changés ensuite ne réécrivent jamais une source tenue.

Le registre MIDI compte les propriétaires par note: deux sources peuvent partager une note sans NoteOff prématuré. Toutes les files et tables sont des `std::array` bornés. `tick()` ne fait ni allocation, ni délai, ni I/O; il ne fait qu'ajouter des événements dans un buffer fixe, drainé ensuite par l'adaptateur.

`HarmonyPreset`, `PerformanceScene` et `InstrumentProfile` sont trois concepts séparés. Le premier produit les notes, le deuxième sauvegarde l'état de performance, le troisième route les CC d'un instrument externe.

Les prototypes Debussy/Impressionist, Cinematic, Dark et Ambient doivent être audités à l'oreille avant toute qualification musicale.
