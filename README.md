# Carlitaccuracy

Éditeur de voix note par note (VST3 / AU), dans la même famille que FreeTune et FreePitch.
Pour une voix ou un instrument qui joue une note à la fois.

## Utilisation

1. Mets Carlitaccuracy sur la piste de voix.
2. Clique sur **TRANSFÉRER**, puis lance la lecture : Carlitaccuracy enregistre la piste.
3. Arrête la lecture : les notes apparaissent sous forme de blobs, avec leur courbe de hauteur.
4. Édite, puis relance la lecture : Carlitaccuracy joue la voix corrigée, calée sur la timeline.
   (**PRÉ-ÉCOUTE** permet d'écouter sans lancer la lecture du logiciel ; clic sur la règle = écouter à partir de là.)

## Outils

| Outil | Glisser | Double-clic |
|---|---|---|
| **PRINCIPAL** | haut/bas = changer la note (aimant : libre, demi-tons, gamme ; Alt = libre), gauche/droite = déplacer, bords = étirer | recaler la note sur la hauteur juste |
| **MODULATION** | vibrato / variations rapides (0 % = note droite) | vibrato à 0 / revenir à 100 % |
| **DÉRIVE** | variations lentes autour de la note | dérive à 0 / revenir à 100 % |
| **FORMANT** | timbre plus clair / plus sombre sans changer la note | remettre à 0 |
| **VOLUME** | volume de la note | couper / rétablir la note |
| **SÉPARER** | clic = couper une note en deux | double-clic sur une coupe = recoller |

Macros : **CORRIGER LA HAUTEUR** (centre + dérive, sur la gamme choisie) et **QUANTIFIER LE TEMPS** (grille du projet).
Clavier : Cmd+Z / Maj+Cmd+Z, Cmd+A, flèches haut/bas (Maj = octave), Suppr = couper la note.

Les prises sont gardées dans `~/Musique/MeloDIG/Takes` pour que ton projet les retrouve.
Ce qui n'est pas modifié reste identique à l'enregistrement, au sample près.

## Compiler

JUCE 8.0.4 dans `libs/JUCE`, polices dans `fonts/` (voir `.github/workflows/build.yml`), puis :

```
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target MeloDIG_VST3 MeloDIG_AU
```

Test du moteur : `clang++ -std=c++17 -O2 -ISource tests/engine_test.cpp -o engine_test && ./engine_test`
