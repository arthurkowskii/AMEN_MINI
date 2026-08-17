# Profils instruments

Les profils sont indépendants de l'harmonie et des scènes. Ils fournissent sept libellés et sept CC modifiables dans le catalogue source.

- Serum: CC 16,17,18,19,74,11,91;
- Pigments: CC 20,21,22,23,74,11,91;
- Falcon: CC 24,25,26,27,74,11,92;
- Generic Orchestral: CC 1,2,73,72,74,11,91.

Ce sont des valeurs initiales configurables à l'exécution (`setProfileCc`), **pas** des mappings propriétaires ou universels revendiqués. Leur sens dépend du patch/plugin récepteur. Les encodeurs utilisent des deltas relatifs appliqués à un état local 0..127 puis émettent un CC MIDI standard. `setProfile` refuse les enums invalides; les accesseurs refusent aussi les index encodeur hors 0..6.

Le P0 route toutes les notes et tous les CC sur le canal MIDI 1. Aucun split de canal, rôle orchestral automatique ou routage MPE n'est revendiqué à ce stade.
