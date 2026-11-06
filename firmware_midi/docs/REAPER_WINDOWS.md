# Reconnexion MIDI dans REAPER sous Windows

## Symptôme

Lorsque l'AMEN est branché alors que REAPER est déjà ouvert, Windows peut reconnaître `Teensy MIDI` sans que REAPER rouvre son entrée. Le bouton **Reset all MIDI devices** rétablit alors le fonctionnement.

Ce symptôme oriente vers la réouverture du port côté hôte, mais ne prouve pas à lui seul une cause précise dans REAPER ou dans le pilote Windows. Le firmware conserve l'identité USB matérielle du Teensy ; aucun message MIDI de démarrage ni changement de nom USB n'a été ajouté pour contourner ce problème.

## Solution utilisée

L'extension tierce [reaper_automidireset de Jeremy Bernstein](https://github.com/jeremybernstein/reaper_automidireset) détecte les changements de périphériques et demande à REAPER de réactualiser ses ports MIDI. Elle attend environ 1,5 seconde après le dernier événement avant d'effectuer cette opération.

La configuration essayée est **REAPER 7.81 Windows x64**, avec **reaper_automidireset v1.4** et le firmware **AMEN MIDI PANEL V2.3**. Le chargement de la DLL dans REAPER a été vérifié ; Arthur rapporte que la reconnexion fonctionne après installation. Ce retour d'usage ne constitue pas une campagne exhaustive de tests sur tous les contrôleurs, ports USB ou pilotes.

L'extension n'est pas une dépendance du firmware et n'est pas incluse dans ce dépôt.

## Installation Windows x64

1. Fermer REAPER et sauvegarder ses fichiers `reaper.ini` et `reaper-midihw.ini` depuis le dossier de ressources. Pour une installation standard, celui-ci se trouve dans `%APPDATA%\REAPER`. Pour une installation portable, utiliser le dossier réellement indiqué par REAPER.
2. Télécharger `reaper_automidireset_win_x64.zip` depuis la [release officielle v1.4](https://github.com/jeremybernstein/reaper_automidireset/releases/tag/v1.4).
3. Vérifier l'empreinte SHA256 de l'archive :

   ```text
   51e99e505b698a4f9238c802b9985564bf8e2b0840e4b228647a834557422d76
   ```

4. Extraire uniquement `reaper_automidireset_x64.dll` dans le sous-dossier `UserPlugins` du dossier de ressources. Ne pas écraser une autre version sans la sauvegarder, ni conserver plusieurs copies de cette extension dans `UserPlugins`.
5. L'empreinte SHA256 de la DLL utilisée est :

   ```text
   54331c3f86a8d41646b5cdb856439b43a23cd793e67202bc86ee84e36891ab8e
   ```

6. Ouvrir REAPER, puis effectuer le test ci-dessous. L'entrée `Teensy MIDI` doit toujours être activée et routée vers l'instrument ; l'extension ne crée pas ce routage et n'effectue pas le MIDI Learn.

Ces noms et empreintes concernent cette release précise, pas les versions futures ni REAPER 32 bits.

## Vérification et limites

- Avec REAPER ouvert sur un projet de test, débrancher puis rebrancher l'AMEN.
- Attendre environ deux secondes, puis jouer des pads et tourner les encodeurs MIDI.
- Vérifier que le MIDI est reçu sans utiliser **Reset all MIDI devices**, puis refaire un cycle.
- Vérifier également les autres contrôleurs utilisés dans la session.

L'extension agit dans tout REAPER, pas uniquement sur l'AMEN. Une notification de changement de périphérique peut entraîner une réinitialisation MIDI ; éviter les branchements et débranchements pendant une prise importante. Il s'agit d'une extension native tierce : l'absence de conflit sur cette configuration ne garantit pas une absence de problème sur toutes les installations.

## Désinstallation

Fermer REAPER, déplacer `reaper_automidireset_x64.dll` hors de `UserPlugins`, puis rouvrir REAPER. Il n'est pas nécessaire de restaurer toute la configuration pour retirer cette seule extension. Le firmware et les associations MIDI Learn restent indépendants de cette installation.
