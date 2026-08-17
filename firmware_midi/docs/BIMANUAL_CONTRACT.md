# Contrat bimanuel

## Sources et tokens

Chaque `noteDown` accepté reçoit un `SourceToken` global `uint64_t`; zéro est invalide. Le compteur n'est lié ni au degré ni au slot réutilisé. Un `noteUp` retardé d'une ancienne source est donc ignoré, y compris si un autre degré réutilise le même emplacement de stockage.

## Arbitrage des huit slots FX

Chaque slot conserve séparément `physicalDown`, son état `latched`, et un ordre monotone de dernière pression. Le gagnant est le slot éligible le plus récemment pressé: un Gate est éligible tant qu'il est physiquement tenu; un Latch tant qu'il est verrouillé. Au relâchement du gagnant Gate, le prochain Gate tenu ou Latch sous-jacent reprend immédiatement. FX→FX ne traverse jamais un voicing direct. Direct→FX ferme immédiatement le direct; FX→Direct restaure les snapshots des sources encore actives.

- **Note-first**: l'activation ferme le voicing direct et émet immédiatement la première frame FX.
- **FX-first**: la nouvelle source rejoint directement la frame active; le voicing direct complet n'est jamais émis.
- Strum et Run attendent une source sans progresser. Une nouvelle source redémarre un effet fini déjà terminé.

## HOLD

À l'activation, les sources physiquement tenues deviennent `held`. À la désactivation, `held` est retiré partout: les sources non physiques sont libérées, celles encore physiques restent actives jusqu'à leur vrai `noteUp`, sous le FX courant.

## Sortie bornée et Panic

Aucune allocation, I/O ou attente n'a lieu dans `tick`. Le premier propriétaire d'une note n'est enregistré que si son NoteOn a été accepté par la queue. Les NoteOff utilisent le chemin prioritaire: un événement non critique est évincé si la queue est pleine (et, dans une queue composée uniquement de récupérations, la récupération la plus récente remplace la plus ancienne).

`panic` prend possession d'un buffer fraîchement vidé, y place les NoteOff des notes connues puis CC123 All Notes Off sur le canal 1 actuellement routé, et seulement ensuite réinitialise registre, sources, HOLD, slots et ordonnanceur. Un `tick` ultérieur ne peut donc réémettre une frame obsolète.

## Sémantique FX

- **Strum** accumule une note à chaque frame jusqu'à l'accord complet, sans couper les notes déjà étagées.
- **Arp**, **Run Up**, **Run Down** et **Random** jouent une note à la fois; les Run sont finis.
- **Trance Gate** alterne accord complet ouvert et silence.
- **Note Repeat** réarticule l'accord complet à chaque frame.
- **Velocity** réarticule l'accord complet avec le cycle 32/63/94/125.

Les tests couvrent plusieurs frames, annulation, reprise, ajout/retrait de source, ordonnancement wrap-safe et traces MIDI exactes.
