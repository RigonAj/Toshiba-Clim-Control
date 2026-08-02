# Journal de tests

Relevés datés, dans l'ordre chronologique. Sert de trace pour la fiche de recette du plan §14.6.

---

## 1er août 2026 — Phase 1a, mise en route

### Chaîne d'outils

| Élément | Version | Résultat |
|---|---|---|
| ESP-IDF | v5.5.3 | `hello_world` compilé pour `esp32s3`, 528/528 cibles — **OK** |
| PlatformIO Core | 6.1.19 | croquis Arduino + IRremoteESP8266 compilé pour `seeed_xiao_esp32s3` — **OK** |
| Python | 3.12.10 | — |
| IRremoteESP8266 | 2.9.0 | prend bien en charge l'ESP32-S3 (point de vigilance du plan §8.0) — **OK** |

### Carte

Relevé à l'`esptool` sur `COM3` :

```text
Chip is ESP32-S3 (QFN56) (revision v0.2)
Features: WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
Crystal is 40MHz
USB mode: USB-Serial/JTAG
MAC: 14:c1:9f:c4:fc:40
Detected flash size: 8MB
```

Conclusions :

- 8 Mo de flash → la table de partitions du plan §7.5 (2 × 3 Mo OTA + 1,875 Mo `storage`) est applicable.
- 8 Mo de PSRAM embarquée → module ESP32-S3**R8**, donc `CONFIG_SPIRAM_MODE_OCT` comme prévu au §7.1.
- USB-Serial-JTAG confirmé → pas de puce USB-UART, le §7.1.1 s'applique (coupures de console au reset).
- MAC `14:c1:9f:c4:fc:40` à réserver en DHCP sur le routeur Asus (§11.2).

### Téléversement

Croquis `ir-capture` flashé sur `COM3` : 446 048 octets écrits, **hash vérifié**, démarrage confirmé.
Le téléversement fonctionne sans manipulation du bouton `BOOT`.

### Test de câblage du récepteur — **ÉCHEC attendu à ce stade**

```text
--- Verification du cablage (plan §1a) ---
GPIO3 au repos : 0 lectures HAUT sur 20
ECHEC : ligne au niveau bas en permanence.
```

Le test met GPIO3 en entrée avec tirage vers le bas et vérifie que le TSOP38438 tient malgré tout la ligne au niveau haut. Vingt lectures basses sur vingt : **rien ne pilote la ligne**.

À vérifier dans cet ordre (plan §6.2 et §6.4) :

1. Le TSOP38438 est-il monté ? Si seule la carte est branchée en USB, ce résultat est normal.
2. Brochage : dôme vers soi, pattes vers le bas → `1 = OUT`, `2 = GND`, `3 = VS`. Un échange VS/GND donne ce résultat.
3. `OUT` (broche 1) relié à `D2`, c'est-à-dire GPIO3 — pas à `D3`.
4. `VS` (broche 3) alimenté depuis `3V3` **au travers de la résistance de 100 Ω**.
5. `GND` commun entre le TSOP et la carte.
6. Condensateurs de découplage 100 nF et 4,7 µF en place près du composant.

Aucune résistance de tirage externe n'est nécessaire : la sortie du TSOP38438 possède un tirage interne.

**Suite :** relancer le test après câblage. Le résultat attendu est `20 lectures HAUT sur 20`, puis basculement à l'appui d'une touche de la télécommande.

### Diagnostic — outil de scan des broches

Le test précédent ne disait pas *si* le TSOP était mal branché ou *où*. Un second croquis a été ajouté
(`ir-capture/src/pin_scan.cpp`, environnement `scan`) : il met les onze entrées-sorties du XIAO en
entrée avec tirage vers le bas et affiche laquelle est tenue au niveau haut.

Premier passage : **les onze broches en l'air**. Le diagnostic bascule alors de « mauvaise broche » vers
« TSOP non alimenté » — déplacer le fil `OUT` n'aurait rien changé.

Fausse piste écartée au passage : l'absence de la résistance de 100 Ω n'est **pas** en cause. Ce n'est pas
une résistance de limitation mais un filtre de découplage (plan §6.2) ; le TSOP38438 fonctionne
sans, alimenté directement en 3,3 V.

Autre point clarifié : la sortie du TSOP est haute **au repos, sans aucun infrarouge**, grâce à son
tirage interne. Un niveau bas permanent signifie donc « pas d'alimentation », jamais « pas de signal ».
Aucune émission IR n'est nécessaire à ce stade — la source, c'est la télécommande.

Incident pendant le recâblage : disparition complète du périphérique USB (VID `303A`), port `COM3`
inclus. Réapparition normale après rebranchement. À surveiller si cela se reproduit — un
court-circuit `3V3`/`GND` produit exactement ce symptôme.

### Test de câblage du récepteur — **SUCCÈS**

Après reprise du câblage :

```text
Broche  GPIO  HAUT/20  Verdict
D2      3     20/20    <<< quelque chose tient cette ligne HAUTE
```

Toutes les autres broches en l'air, comme attendu. Le TSOP38438 est alimenté, sa sortie est tenue au
niveau haut au repos, et elle arrive bien sur `D2` = GPIO3, conforme au plan §6.0.

**Sortie de phase 1a atteinte** pour le volet réception.

**Reste à faire :** la première capture réelle.

### Diagnostic — auto-détection du récepteur

Six scans successifs ont rendu `0/20` sur les onze broches malgré des recâblages répétés. Le montage
dépendait de trois choses invérifiables sans multimètre : la position de la broche `3V3`, l'orientation
du TSOP, et les rangées de la plaque d'essai.

Un troisième croquis (`ir-capture/src/autodetect.cpp`, environnement `autodetect`) supprime les trois
d'un coup : la carte **alimente elle-même le TSOP depuis une sortie GPIO** — le composant consomme
1,5 mA au maximum contre 40 mA admissibles par broche — et essaie les deux affectations possibles
des pattes extérieures.

Deux essais suffisent, et aucun n'inverse l'alimentation : en retournant un boîtier à trois pattes,
les pattes extérieures s'échangent mais celle du milieu reste au milieu, donc la masse est toujours
centrale. Le test est sans risque pour le composant.

```text
--- Essai 1 : alimentation par D3, lecture sur D4 ---
  D4 haute : 20/20
*** RECEPTEUR TROUVE ET FONCTIONNEL ***
  VS  (alimentation) sur D3
  OUT (signal)       sur D4
```

Le TSOP38238 était sain depuis le début. La panne était uniquement dans le câblage d'alimentation.

**Décision :** on conserve l'alimentation par GPIO plutôt que de recâbler vers `3V3`. C'est
électriquement sain et ça supprime une source d'erreur. Le montage définitif reviendra au §6.2.

### Première capture — **PROTOCOLE RECONNU**

Câblage retenu : `VS` ← D3 (GPIO4, sortie à l'état haut), `OUT` → D4 (GPIO5), milieu → `GND`.

```text
Protocole : TOSHIBA_AC
Longueur  : 72 bits (9 octets)
Code      : 0xF20D03FC0140000041
Model: 0 (TOSHIBA REMOTE A), Temp: 21C, Power: On, Mode: Auto, Fan: Auto
```

Quatre trames valides sur cinq. La cinquième, `UNKNOWN` à 148 bits, a été écartée (§8.3, étape 1).

Résultats d'analyse, obtenus par appuis sur les touches température bas puis haut :

| Point du §2.2 | Statut | Valeur mesurée |
|---|---|---|
| Préambule | **Confirmé** | `F2 0D`, exactement l'hypothèse communautaire |
| Longueur de trame | **Mesurée** | 72 bits, 9 octets, paquet émis **deux fois** par appui |
| Signification de `40525B` | **Tranchée** | Remote **A**, et non B comme on aurait pu le supposer |
| Formule du checksum | **Trouvée** | OU exclusif des 8 premiers octets = 9ᵉ octet. Vérifié sur les deux états. |
| Encodage de la température | **Trouvé** | quartet haut de l'octet 5 = température − 17 |

Analyse différentielle (§8.3, étape 7), deux trames ne différant que d'un degré :

```text
21 °C : F2 0D 03 FC 01 40 00 00 41
20 °C : F2 0D 03 FC 01 30 00 00 31
                    ^^       ^^
                    |        checksum recalculé
                    température
```

La plage 17–30 °C retenue au §9.2 est cohérente avec cet encodage : 17 °C donnerait `0x0_`, 30 °C
donnerait `0xD_`.

Timings médians : en-tête 4396 / 4384 µs, marque ≈ 546 µs, espace court ≈ 536 µs, espace long
≈ 1620 µs, séparation entre les deux paquets ≈ 5956 µs.

**Jalon de passage du §2.3 — 2 preuves sur 3 :**

1. Captures cohérentes d'une même commande — trois trames identiques au bit près. Il en faut cinq
   par commande pour la campagne complète du §8.2.
2. Retransmission brute commandant réellement la clim — **non fait**, l'étage d'émission (§6.1)
   n'est pas monté. C'est la phase 1b.
3. Identification reproductible du préambule, de la longueur et du checksum — **fait**.

Outil créé au passage : `tools/capture_export.py`, qui convertit un journal de console en fichiers
JSON au format §8.1 et vérifie le checksum. Réutilisable pour toute la campagne §8.2.

---

## 1er août 2026 — Phase 1b, étage d'émission

### Contrainte de départ : la nomenclature du §5.1 n'est pas disponible

Matériel réellement en main au moment du montage :

| Composant demandé au §5.1 | Disponible | Substitution retenue |
|---|:---:|---|
| LED IR | oui | — |
| Transistor `PN2222A` | oui | — |
| 22 Ω / 0,5 W — collecteur | **non** | 220 Ω, seule résistance de valeur connue |
| 1 kΩ — base | **non** | potentiomètre de kit réglé vers 1 kΩ |
| 10 kΩ — rappel de base | **non** | mise à l'état bas dès la première ligne du `setup()` |
| 100 µF — réservoir 5 V | **non** | sans objet au courant réduit |

Le reste du lot de résistances n'est pas identifié et la machine n'a toujours pas de multimètre.

### Conséquence sur le courant et la portée

Le §5.2 dimensionne l'étage pour 155 mA crête, soit 1 à 5 m. Avec 220 Ω au collecteur :

```text
I ≈ (5,0 − 1,35 − 0,2) / 220 ≈ 15 mA
```

L'éclairement décroît en 1/d², donc la portée varie en √I : `√(15/155) ≈ 0,31`, soit **environ
1,5 m**. Suffisant pour la preuve n° 2 du §2.3, insuffisant pour l'installation définitive.

Solution de repli si le transistor s'avérait inutilisable : attaque directe du GPIO, 3,3 V au
travers de la même 220 Ω, soit ≈ 8 mA et ≈ 1,1 m. Sans risque pour la carte — très loin des
40 mA admissibles par broche — et **de même polarité de commande** que l'étage transistorisé,
donc sans impact sur le firmware.

### Deux montages écartés

**Potentiomètre comme résistance de collecteur.** Écarté sur la puissance, pas sur la valeur : le
calibre d'un potentiomètre s'applique à la piste entière, donc n'utiliser que la fraction *f* de
la piste ne donne droit qu'à *f* × P. Pour 22 Ω sur une piste de 10 kΩ / 0,1 W, cela fait 0,22 %
de piste, soit **0,22 mW admissibles contre 530 mW crête** — plus de 300 fois au-dessus. S'y
ajoutent 0,7° de course utile et une résistance de contact du curseur de 1 à 3 Ω, soit 5 à 15 %
de la valeur visée. En revanche il convient très bien en **résistance de base**, où le courant
n'excède pas 10 mA sous 3,3 V.

**LED visible en guise de résistance.** Écarté sans réserve : une LED est une jonction à tension
de seuil, elle ne limite pas le courant. Une LED rouge (≈ 1,9 V) en série avec la LED IR
(≈ 1,3 V) donnerait 3,2 V de seuil pour 3,3 V d'alimentation, avec un courant dépendant de la
température et de la dispersion de fabrication.

### Outil créé — croquis de clignotement

`ir-capture/src/tx_blink.cpp`, environnement `txtest`. Il n'émet **aucune porteuse 38 kHz** : la
LED est allumée en continu pendant les phases hautes, ce qui la rend franchement observable à la
caméra d'un téléphone. Déroulement : base au niveau bas 5 s, base au niveau haut 5 s, puis
clignotement à 1 Hz. La LED d'état GPIO21 suit la LED IR et fournit une référence visible à
l'œil nu, cadrable dans la même image.

Compilé et téléversé sur `COM3` : 254 625 octets, 7,6 % de la partition, **hash vérifié**.

### Premier essai — **ÉCHEC : le transistor ne commute pas**

```text
Phase 1 : base au niveau BAS  -> LED IR ALLUMEE   (attendu : eteinte)
Phase 2 : base au niveau HAUT -> LED IR ALLUMEE   (attendu : allumee)
```

La LED brille en permanence, indépendamment de l'état de GPIO2. Aucun dommage : 17 mA continus au
travers de 220 Ω, très en dessous des 100 mA que la LED admet en continu.

**Fausse piste écartée d'emblée.** Une inversion base/collecteur n'est pas possible : sur un
`PN2222A` en TO-92 — `E · B · C` — comme sur la quasi-totalité des boîtiers à trois pattes, **la
base est la patte du milieu**. Retourner le composant échange les pattes extérieures et laisse le
milieu au milieu. C'est le raisonnement déjà appliqué au TSOP plus haut. Une inversion E/C, elle,
ferait fonctionner le transistor en mode inverse — gain ≈ 3 au lieu de 100 — et donnerait un
clignotement faible, pas un allumage permanent.

Hypothèses restantes, par ordre de probabilité :

1. **La cathode de la LED n'atteint pas le collecteur** et retombe dans la rangée du `GND`. La LED
   est alors câblée directement entre 5 V et masse, le transistor hors circuit. Une rangée d'écart
   sur la plaque d'essai suffit — jumelle exacte de la panne d'alimentation du TSOP.
2. Le fil du potentiomètre n'est pas sur la patte du milieu, auquel cas la vraie base reçoit la
   branche 5 V et conduit en permanence.
3. Transistor claqué en court-circuit C-E.

**Test discriminant retenu :** débrancher le seul fil émetteur → `GND`, carte alimentée. Si la LED
s'éteint, le transistor est dans le chemin du courant et l'erreur est en 2 ou 3 ; si elle reste
allumée, il n'y est pas du tout et l'erreur est en 1.

**Suite :** diagnostic en cours. La preuve n° 2 du §2.3 reste non acquise.

### Changement de machine — préparation du dossier

Le projet quitte cette machine avant la fin du diagnostic. Trois opérations pour qu'un seul
dossier suffise à tout reprendre :

1. **Dossier de conception rapatrié.** Il vivait hors du projet, dans `Documents\Codex\...`, alors
   que le README §3 affirmait déjà « sauvegarder le dossier projet suffit ». C'était faux : toute
   la documentation cite ce plan à chaque paragraphe. Il est désormais dans
   `docs/plan-projet-clim-toshiba-esp32.md`. Le rendu HTML du 25 juillet n'a **pas** été repris,
   il est antérieur à la révision du 1er août et contredirait la référence.
2. **`ir-capture/.pio/` supprimé** — 39 Mo d'artefacts contenant des chemins absolus figés vers
   l'ancien profil utilisateur. Recopiés, ils cassent la compilation de façon peu lisible.
3. **Procédure de reprise écrite** dans [migration-nouveau-pc.md](migration-nouveau-pc.md) :
   installation, jonction NTFS, port série, inventaire du matériel à emporter, et le geste précis
   par lequel reprendre le diagnostic.

Le dossier fait **206 Ko** pour 20 fichiers. Erreur corrigée au passage dans le README §2 : les
« ~1 Go » attribués à `.pio` désignaient en réalité la chaîne d'outils de `~\.platformio`, qui
n'a jamais été dans le projet.

---

## 1er août 2026 — Nouvelle machine, phase 1b **terminée**

Reprise du projet sur le PC de destination, profil `rigon`. La procédure de
[migration-nouveau-pc.md](migration-nouveau-pc.md) a été suivie sans surprise ; trois écarts
seulement, notés au §« Chaîne d'outils » ci-dessous.

### Chaîne d'outils reconstituée

| Élément | Version | Emplacement | Écart avec la procédure |
|---|---|---|---|
| Python | 3.12.10 | `%LOCALAPPDATA%\Programs\Python\Python312` | — |
| PlatformIO Core | 6.1.19 | `Python312\Scripts` | **et non `~\.platformio\penv\Scripts`** : le §3.2 décrit l'installeur `get-platformio.py`, ici c'est `pip` |
| Jonction `C:\esp-clim` | — | → `C:\Users\rigon\Documents\Esp Controle clim` | — |
| ESP-IDF | v5.4 et v6.0.1 | `C:\esp\`, outils dans `C:\Espressif` | **déjà présentes**, mais le §7.1 fige **v5.5.3** — sans objet tant que le firmware définitif n'existe pas (§3.6) |

Deux pièges rencontrés :

- `C:\Espressif\tools\python` (3.11, fourni par l'installeur ESP-IDF) répondait à `python`.
  PlatformIO n'y a **pas** été installé, pour ne pas polluer l'environnement de l'IDF. L'installeur
  winget place de lui-même Python 3.12 devant dans le `PATH` utilisateur.
- **Le port n'est plus `COM3` mais `COM4`.** `COM3` existe sur cette machine et désigne un autre
  appareil, un adaptateur CH343. La carte se reconnaît à sa MAC `14:c1:9f:c4:fc:40`, comme prévu au
  §3.5. Toutes les commandes du README §6 ont été corrigées.

Première compilation `pio run -e capture` : **SUCCESS** en 71 s, toolchain Xtensa et
IRremoteESP8266 2.9.0 retéléchargés.

### Cause de la panne du transistor — **une broche, pas un composant**

Le test discriminant prévu (débrancher émetteur → `GND`) n'a pas eu à être joué. La description du
montage a suffi : la résistance de base était reliée à **`D2`**, alors que `tx_blink.cpp` pilote
**GPIO2, c'est-à-dire `D1`**. La base n'était donc reliée à aucune sortie pilotée — GPIO3 reste en
entrée haute impédance dans ce croquis.

Les trois hypothèses du 1er août étaient toutes fausses, et pour la même raison : elles cherchaient
la panne dans le chemin du courant de collecteur, alors qu'elle était dans la commande.

Aucun des symptômes n'était donc à interpréter — le transistor était sain depuis le début, comme le
TSOP l'avait été avant lui. **Le fil déplacé de `D2` vers `D1` a suffi.** Phases 1 et 2 conformes,
clignotement observé.

Le brochage du §6.0 est conservé sans discussion : `D2`/GPIO3 est réservé à la **réception** pour le
montage définitif du §6.2, y mettre l'émission créerait un conflit ultérieur.

### Outil créé — boucle locale émission → réception

`ir-capture/src/tx_loopback.cpp`, environnement `loopback`. **Seul croquis où les deux étages sont
montés en même temps.** Il émet une trame `TOSHIBA_AC` de 72 bits sur `D1`, la fait redécoder par le
TSOP, et compare les neuf octets envoyés à ceux reçus.

Trois trames d'essai : celle réellement mesurée le 1er août (21 °C), et deux dérivées à 17 et 30 °C
dont le checksum est recalculé au démarrage par la formule du §8.3. Les dérivées ne prouvent rien sur
le protocole — elles ne servent qu'à éprouver les timings sur des motifs de bits différents.

Ce que la boucle prouve : porteuse 38 kHz présente, durées correctes, chaîne GPIO → transistor → LED
sans déformation. Ce qu'elle ne prouve pas : que la clim obéit. La preuve n° 2 du §2.3 exige l'unité
intérieure.

### Fausse piste — le récepteur, une fois de plus

Premier passage de la boucle : **zéro trame reçue**, et surtout **zéro front** sur la sortie du TSOP.
Écarter la LED du récepteur puis le réorienter n'a rien changé : toujours zéro. Une saturation par
proximité produirait des trames déformées, pas un silence total — l'hypothèse a été écartée sur ce
raisonnement.

Un troisième croquis a servi à descendre d'un cran : `ir-capture/src/tx_carrier.cpp`, environnement
`carrier`. Il sépare quatre régimes — éteint, continu, porteuse 38 kHz permanente, salves de
600/600 µs — et compte les fronts vus par le TSOP dans chacun. La porteuse y est produite par le
**PWM matériel (LEDC)** et non par la boucle logicielle de la bibliothèque, ce qui retire le logiciel
de l'équation.

Le verdict est tombé pendant l'exécution, entre deux cycles, au moment où l'inversion `OUT`/`VS` du
TSOP était corrigée :

```text
                        cycle 1        cycle 2
salves 600/600 µs       0 front        4868 fronts
porteuse continue       0 front          76 fronts
```

**L'étage d'émission n'a jamais été en cause.** Il tient les 38 kHz sans réserve, même à 15 mA.

### Le contrôle statique de câblage est trompeur — à ne plus utiliser seul

Le `checkWiring()` de `main.cpp`, repris dans la boucle, s'est trompé **dans les deux sens au cours de
la même session** :

| Câblage réel | Verdict de `checkWiring()` | Réception effective |
|---|---|---|
| `OUT`/`VS` inversés | `20/20 HAUT` → « OK » | **aucune** |
| correct | `0/20 HAUT` → « ÉCHEC » | **parfaite, 12 trames sur 12** |

Le test met la broche en entrée avec tirage vers le bas et conclut d'un niveau haut que le TSOP tient
la ligne. Mais la sortie du TSOP est à collecteur ouvert avec un tirage interne d'environ 30 kΩ, face
à un tirage vers le bas interne de l'ESP32 du même ordre : le pont diviseur tombe **sous le seuil
d'entrée haut**. Un niveau lu bas ne dit donc rien de l'état du récepteur.

Cela invalide la conclusion posée le 1er août plus haut dans ce journal — « un niveau bas permanent
signifie donc "pas d'alimentation", jamais "pas de signal" ». Elle n'est vraie que lorsque la broche
lue est tenue par une source basse impédance, ce qui était le cas par accident quand `OUT` et `VS`
étaient inversés.

**Le seul indicateur fiable est le comptage de fronts pendant une émission connue**, tel que
`probeActivity()` l'implémente dans `tx_loopback.cpp`. Il distingue « ne voit rien » de « voit mais ne
décode pas », ce qu'aucune mesure statique ne peut faire.

`main.cpp` n'a **pas** été modifié : c'est l'instrument validé de la campagne du §8.2 et ses relevés
sont irremplaçables. Son `checkWiring()` reste donc à lire comme indicatif, jamais comme un verdict.

### Résultat — boucle locale **SUCCÈS, 12 trames sur 12**

```text
Fronts detectes pendant l'emission : 296

Emission : mesuree 21 C
  envoye  : F2 0D 03 FC 01 40 00 00 41
  protocole : TOSHIBA_AC, 72 bits
  recu    : F2 0D 03 FC 01 40 00 00 41
  OK : les neuf octets reviennent identiques.

Cumul : 12 reussites, 0 echecs
```

Quatre passes consécutives, les trois trames à chaque fois, **aucun écart d'un seul octet**. Le
décodeur reconnaît `TOSHIBA_AC` 72 bits sur notre propre émission, au même titre que sur la
télécommande d'origine.

**Sortie de phase 1b atteinte.**

### Jalon de passage du §2.3 — état

1. **Cinq captures cohérentes d'une même commande** — partiel. Quatre relevés validés dans
   `captures/raw/`, il en faut cinq par commande sur toute la matrice du §8.2.
2. **Retransmission brute commandant réellement la clim** — **toujours non acquise.** Ce n'est plus
   un problème d'électronique mais de lieu : la mesure exige d'être devant l'unité intérieure
   (migration-nouveau-pc.md §6).
3. **Identification reproductible du préambule, de la longueur et du checksum** — fait le 1er août.

**Suite, dans l'ordre :**

- Preuve n° 2, devant la clim, avec `loopback` ou une variante n'émettant que `off` et
  `cool_22_auto_off` — c'est ce que recommande la stratégie de repli du §8.4.
- Puis la campagne du §8.2. Attention : `captures/README.md` impose de **démonter l'étage
  d'émission** pendant cette phase, il pollue les relevés. Les deux étapes ne peuvent pas être
  menées de front.
- Le codec ne s'écrit qu'après, faute de quoi les champs mode, ventilation et swing seraient
  supposés plutôt que mesurés.

---

## 1er août 2026 — Interface Web, écrite avant le firmware

La clim est momentanément inaccessible. C'est précisément le travail que cela n'empêche pas :
la décision « interface servie en fichiers statiques » (cahier v2 §4.2) rend `web/` indépendant du
codec, du réseau et de la carte. L'interface a donc été écrite et mise au point contre une maquette.

### Ce qui a été créé

| Fichier | Rôle |
|---|---|
| `toshiba-climate-controller/web/index.html` | La page : deux cartes, planning, diagnostic, réglages |
| `toshiba-climate-controller/web/app.css` | Style unique, clair et sombre, sans police externe |
| `toshiba-climate-controller/web/app.js` | Toute la logique : interrogation, commandes, éditeur de courbe |
| `toshiba-climate-controller/web/config.example.json` | Adresses des boîtiers, livrées avec les fichiers |
| `tools/mock_api.py` | Maquette d'un boîtier : sert `web/` et l'API, deux instances = deux boîtiers |
| `docs/api-v2.md` | Le contrat que le composant `web_api` devra satisfaire |

Aucune ressource externe, aucune bibliothèque : trois fichiers, **51 ko** en tout, à loger dans une
partition `storage` de 1,875 Mo — 2,7 % de la place disponible. Le graphe à points déplaçables — le seul composant que le cahier v2 §4
identifiait comme « à construire de toute façon » — tient dans un SVG construit à la main.

### Décisions prises en écrivant

**Les capacités viennent du boîtier, jamais de la page.** Modes et vitesses de ventilation sont
lus dans `capabilities`. Le boîtier déclare en plus `unverified_fields` : tant que la campagne du
§8.2 n'a pas mesuré l'encodage de `mode`, `fan` et `swing`, l'interface affiche la pastille **non
vérifié** en face de chacun. C'est la traduction à l'écran de l'état réel des connaissances — la
température, elle, est mesurée, et ne porte pas de pastille.

**La courbe est un escalier.** Un point vaut jusqu'au suivant, le dernier déborde sur le lendemain.
Pas d'interpolation : un thermostat ne rampe pas entre deux consignes, et l'ordonnanceur du firmware
appliquera exactement la même règle de lecture que le tracé.

**Pas de WebSocket.** L'interrogation périodique à 5 s suffit et supprime une dépendance. Conséquence
assumée et notée dans `api-v2.md` : le critère du plan §3.4 — « l'usage de la télécommande met à jour
l'état en moins d'une seconde » — n'est pas tenu par ce choix. Il le sera si `/ws` est ajouté.

**`GET /api/v1/devices` et le champ `role` disparaissent**, conformément au cahier v2 §4.1 : les
boîtiers sont symétriques, l'agrégation se fait dans le navigateur. En contrepartie, l'API doit
porter des en-têtes CORS et accepter un jeton porteur.

### Un mauvais choix corrigé en cours de route

Le graphe était d'abord large de 480 px minimum, dans un conteneur défilant horizontalement. Sur
téléphone, c'était inutilisable : le déplacement des points impose `touch-action: none` sur le SVG,
donc **le seul geste qui aurait pu faire défiler le graphe est celui qui déplace un point.** Les
heures 20 h à 24 h devenaient inatteignables.

Le graphe se met désormais à l'échelle de la carte. Les libellés et les cibles tactiles sont
dimensionnés pour cette réduction : à 375 px de large, l'axe reste à 11 px et une cible de point fait
29 px. La saisie précise, elle, ne passe pas par le doigt mais par l'éditeur de point situé juste en
dessous, où l'heure se règle au champ `time` par pas de 30 minutes.

### Ce qui a été vérifié, dans un navigateur, sur la maquette

| Contrôle | Résultat |
|---|---|
| Deux boîtiers affichés, chacun interrogé à son adresse | OK, requêtes inter-origines comprises |
| Marche/arrêt, température, mode, ventilation, oscillation | OK, `PUT /api/v1/climate` accepté, `revision` incrémentée |
| Ajout d'un point par appui sur la courbe | OK, hérite du réglage courant |
| Déplacement d'un point, 13 h 00 → 19 h 30 à 19 °C | OK, aimantation au pas de 30 min et au degré |
| Point lâché dans la bande basse | OK, devient un point d'arrêt |
| Deux points sur la même demi-heure | Refusé, le point ne se superpose pas |
| Suppression, annulation, enregistrement | OK, `revision` du planning incrémentée |
| Commande concurrente pendant une modification | `409` reçu, état du boîtier adopté, message affiché |
| Boîtier injoignable | Carte grisée et désactivée, l'autre reste commandable |
| Rendu à 375 px | Aucun débordement horizontal, graphe entier visible |

Ce qui n'est **pas** vérifié par ces essais : que les octets envoyés commandent réellement la clim.
La maquette renvoie une trame construite à partir du seul encodage mesuré — préfixe `F2 0D 03 FC 01`,
température dans le quartet haut de l'octet 5, checksum en OU exclusif. Les octets de mode, de
ventilation et de swing y sont à zéro, parce qu'on ne les connaît pas.

### Suite : brancher le TSOP dessus

Le panneau **Diagnostic infrarouge** de chaque carte est fait pour cela. Il affiche côte à côte la
dernière trame émise et celle que le TSOP du même boîtier a relue, avec le verdict de comparaison.
La boucle locale du 1er août prouve déjà que la chaîne émission → réception ne déforme rien : il
reste à l'exposer par HTTP, ce qui donne le contrôle du §14.2 — *vérifier que l'on émet bien ce que
l'on croit* — directement dans l'interface, sans console série.

Cela ne demande ni la clim, ni le codec définitif : un croquis PlatformIO servant `web/` et
réutilisant `IRremoteESP8266` suffirait à valider l'ensemble sur la carte réelle.

---

## 1er août 2026 — L'interface tourne sur la carte, l'émission est contrôlée

Le croquis annoncé ci-dessus a été écrit : `ir-capture/src/webui.cpp`, environnement `webui`. Il sert
`web/` depuis la flash de la carte et implémente les cinq routes de [api-v2.md](api-v2.md). La page
est donc accessible depuis n'importe quel appareil du réseau, téléphone compris.

### Deux points d'organisation

**Le Wi-Fi est dans un fichier à part.** `include/secrets.h`, exclu du dépôt par `.gitignore`
(plan §16.3), avec un modèle versionné `secrets.example.h` qui ne contient rien. Le croquis refuse de
compiler si le fichier manque, avec le message qui dit quoi faire — `#error` plutôt qu'une panne à
l'exécution.

**Il n'y a qu'une copie de l'interface.** `data_dir` de `platformio.ini` pointe sur
`toshiba-climate-controller/web/` : `uploadfs` prend les fichiers à la source. Modifier l'interface
et la redéployer ne touche pas au firmware — c'est exactement la propriété recherchée au cahier v2
§4.2, vérifiée pour de vrai.

### Résultats mesurés sur la carte

| Contrôle | Résultat |
|---|---|
| Compilation | 915 ko, **27,4 %** de la partition applicative de 3,3 Mo |
| Interface en flash | 51 ko écrits à `0x670000`, hash vérifié |
| Connexion Wi-Fi | OK, DHCP `192.168.1.232`, RSSI **−59 dBm** |
| Nom mDNS | `http://clim-salon.local/` répond, sans avoir à connaître l'IP |
| Heure | SNTP synchronisé, fuseau Europe/Paris appliqué |
| Page servie depuis la carte | OK, une seule carte affichée — le second boîtier n'est pas déclaré |
| Commande depuis la page | `PUT /api/v1/climate` accepté, `revision` incrémentée |
| Planning enregistré | OK, `409` correct sur révision obsolète |
| Planning après redémarrage | **conservé** — NVS relu au démarrage |
| `next_point_at` | `23:00` à 20 h 35 un samedi, courbe week-end : correct |
| Ordonnanceur | `[planning] point 08:30 applique` — le point en vigueur est bien celui qui déborde depuis le matin |

### Le contrôle par le TSOP — **il fonctionne**

C'est le point que la maquette ne pouvait pas donner. Consigne portée à 22 puis 23 °C depuis la page :

```text
Derniere emission   F2 0D 03 FC 01 50 01 00 50
Derniere reception  F2 0D 03 FC 01 50 01 00 50   TOSHIBA_AC, 72 bits, propre emission
Checksum            valide
Controle            les octets recus par le TSOP sont identiques a ceux emis
Compteurs           2 emises, 2 valides, 0 rejetees
```

À 23 °C, l'octet 5 passe de `50` à `60` — soit `température − 17` dans le quartet haut, conforme à la
mesure du 1er août. Le checksum suit. **La chaîne page → HTTP → codec → LED → TSOP → décodeur est
donc close, et vérifiable depuis un téléphone sans console série.**

### Ce que ce résultat ne dit pas

L'octet 6 vaut `01` pour le mode froid. Cette valeur ne vient **pas** d'une mesure : elle vient du
modèle *Remote A* d'`IRremoteESP8266`, que le croquis utilise pour construire la trame. La seule
trame réellement mesurée le 1er août était en mode Auto, où cet octet vaut `00` — cohérent, mais un
seul point ne fait pas une preuve.

C'est exactement ce que l'interface annonce en affichant **non vérifié** sur mode, ventilation et
oscillation. La campagne du §8.2 reste donc entièrement à faire, et la preuve n° 2 du §2.3 — la clim
qui obéit — attend toujours d'être devant l'unité intérieure.

### Un piège à noter pour la suite

Ouvrir puis refermer le port série **redémarre la carte** : le DTR de l'USB-Serial-JTAG provoque un
reset, et la ré-énumération USB fait perdre la bannière de démarrage. C'est la manifestation concrète
du §7.1.1 du plan. Conséquence pratique : l'état estimé repart à zéro à chaque ouverture de console —
le planning, lui, est en NVS et survit. Pour connaître l'adresse IP sans console, `clim-salon.local`
ou la page `/healthz` suffisent.

---

## 2 août 2026 — Maintien de l'arrêt contre la télécommande

Besoin exprimé : qu'une plage du planning puisse **maintenir la clim éteinte**, même si quelqu'un
la rallume à la télécommande.

### Ce que l'infrarouge permet, et ce qu'il ne permet pas

L'IR est unidirectionnel et le boîtier n'est pas en série sur le récepteur de l'unité : **il ne peut
pas empêcher la clim de recevoir la télécommande.** Elle démarre. Le seul levier est de renvoyer un
ordre d'arrêt derrière. La fonction s'appelle donc « maintenir éteint » et non « empêcher
l'allumage », et l'interface le dit en toutes lettres à l'endroit où on l'active.

Deuxième limite, du même ordre : le boîtier ne réagit qu'à ce que **son TSOP a vu**. Une télécommande
pointée vers l'unité depuis un angle que le récepteur ne couvre pas passe inaperçue.

### Deux décisions de conception

**Le verrou est un attribut du point de planning**, pas un mode à part : un point d'arrêt peut porter
`"lock": true`, et le segment qui court jusqu'au point suivant maintient l'arrêt. Aucun concept
nouveau — la courbe en escalier portait déjà toute la sémantique nécessaire.

**Le boîtier ne cherche pas à savoir si la télécommande demandait la marche.** C'était la tentation :
décoder la trame reçue, lire le champ mode, n'agir que si ce n'était pas un arrêt. Mais l'encodage du
mode n'est pas prouvé — c'est précisément ce que la campagne du §8.2 doit établir. Bâtir le verrou
dessus, ce serait faire dépendre une fonction de sûreté d'une hypothèse. **Pendant un segment
verrouillé, toute trame reçue vaut donc rappel d'arrêt.** Si elle demandait déjà l'arrêt, le rappel
est sans effet : l'erreur est gratuite dans ce sens-là.

### Le piège évité : l'auto-déclenchement

Le boîtier entend sa propre émission — c'est même ce qui rend l'auto-contrôle possible. Sans
précaution, le rappel d'arrêt aurait été entendu comme une commande de télécommande, aurait déclenché
un nouveau rappel, et ainsi de suite : **une boucle d'émission infinie.** Le RX est donc sourd 1,5 s
après chaque émission — le paquet étant émis deux fois, une fenêtre courte ne suffisait pas. C'est le
masquage de 150 ms du plan §9.2, élargi parce qu'ici on veut aussi couvrir la répétition.

Vérifié sur la carte, et c'est le contrôle qui comptait :

| Contrôle | Résultat |
|---|---|
| Segment verrouillé actif | `lock_active: true`, arrêt appliqué, `source: schedule` |
| **Aucun auto-déclenchement** | après l'émission du planning puis 30 s d'attente : `tx: 1`, **`lock_asserts: 0`** |
| Commande « marche » depuis la page | acceptée, `override_active: true`, `lock_active: false` |
| Retour du verrou après ré-enregistrement du planning | `lock_active: true`, arrêt réappliqué |

La règle de suspension est celle du cahier v2 §3, déjà retenue pour tout le reste : un réglage
manuel tient jusqu'au point suivant. L'interface reste donc maîtresse, la télécommande non — ce qui
est exactement la dissymétrie demandée.

### Ce qui reste à vérifier, et qui demande la télécommande

Le déclenchement réel n'a **pas** pu être testé ici : la seule source infrarouge disponible est la
carte elle-même, et ses émissions sont précisément celles que le masquage ignore. Il faut une trame
extérieure, donc la `WH-TG01NE`.

Le contrôle se lit ensuite dans le compteur `lock_asserts` du diagnostic, ou sur la console :

```text
[telecommande] F2 0D 03 FC 01 50 01 00 50
[verrou] arret renvoye (commande vue a la telecommande)
```

### Outil de secours noté au passage — interface

Le pilote de navigateur s'est bloqué en fin de séance. Edge en mode `--headless=new --dump-dom` a
servi de remplaçant : il exécute réellement le JavaScript et rend le DOM final, ce qui suffit à
vérifier qu'une page se construit — cartes générées, boutons de mode issus des `capabilities`,
pastille de verrou présente. À réutiliser.

---

## 2 août 2026 — « La page est devenue lente » : le réseau, pas le logiciel

Symptôme signalé : l'interface met plusieurs secondes à s'ouvrir, parfois pas du tout.

### Mesures, dans l'ordre où elles ont été prises

```text
temps de reponse (ms)
/healthz             464 / 1380 / 3917 / 2331 / 413
/api/v1/status      9358 / 2907 /  417 / 2080 / 1418
/app.js             7262 / 9910 / 7002            <- 31 ko, soit ~3 ko/s
```

Puis, quelques minutes plus tard, plus rien du tout : huit requêtes de suite en échec, aucun ping,
plus de résolution mDNS — alors que la carte énumérait toujours sur l'USB. **Elle tournait, mais
n'était plus sur le réseau.**

### Instrumentation avant hypothèses

Rien dans le croquis ne permettait de trancher. `/healthz` a donc été étoffé : tas libre, plus gros
bloc allouable, motif du dernier redémarrage, état Wi-Fi, nombre de déconnexions et motif, compteur
de décodages IR **non reconnus** — jusque-là comptés nulle part, ce qui rendait invisible le scénario
« le TSOP voit du bruit en permanence » — et surtout **la cadence de la boucle principale**.

S'y ajoutent deux outils d'essai : `PUT /api/v1/diagnostics/rx` pour débrayer le récepteur à chaud,
et `GET /api/v1/diagnostics/wifi` pour faire balayer la bande par la carte elle-même.

### Ce que l'instrumentation a éliminé

| Hypothèse | Mesure | Verdict |
|---|---|---|
| Fuite ou fragmentation mémoire | 277 ko libres, plus gros bloc 262 ko | **écartée** |
| Processeur accaparé | **24 030 tours de boucle par seconde** | **écartée** |
| Récepteur IR noyé sous le bruit optique | `rx_unknown` = 0 | **écartée** |
| Bande 2,4 GHz encombrée | 3 réseaux en tout, le nôtre seul sur le canal 1 | **écartée** |
| Puissance d'émission réduite | 20 dBm, le maximum | **écartée** |
| Récepteur IR coûteux en temps machine | essai A/B, RX actif puis coupé : 4,7-9,3 s contre 3,8-8,0 s | **écartée**, l'écart est dans le bruit |

### Ce qui reste, et qui est sans appel

```text
             perte   RTT moyen   RTT max
carte ESP32   20 %     480 ms     1510 ms
passerelle     0 %       0 ms        0 ms
```

Le PC est en Ethernet : le trajet PC↔routeur est parfait. **Toute la dégradation est sur le saut
sans fil routeur↔carte.** Un RTT de 480 ms avec 20 % de perte sur un réseau local, bande libre, c'est
une liaison radio en souffrance — un lien sain donne 5 à 30 ms.

**Et la liaison est dissymétrique** : la carte *entend* le routeur à −60 dBm, ce qui est correct,
mais une réponse sur cinq n'arrive pas jusqu'à lui. Elle entend bien, elle est mal entendue.

C'est la signature d'un défaut d'antenne côté carte. Or le plan §5.1 est explicite : *« Le XIAO
ESP32-S3 ne dispose pas d'antenne céramique intégrée : l'antenne externe doit être branchée avant
toute mise sous tension avec le Wi-Fi actif »*, l'inventaire de `migration-nouveau-pc.md` porte
« antenne IPEX — fournie, **pas encore utilisée** », et le §15 liste le risque nommément. Sans
antenne, la carte rayonne par le moignon du connecteur u.FL : assez pour s'associer à quelques
mètres, pas pour tenir un débit.

Le RSSI ne contredit pas ce diagnostic, il ne le voit simplement pas : c'est une mesure de ce que la
carte **reçoit**. Le §14.2 propose de contrôler l'antenne par le RSSI — **cette recette est
insuffisante** et devrait être complétée par une mesure de perte de paquets.

### Trois défauts corrigés au passage — aucun n'est la cause

Ils aggravaient le symptôme sans le créer :

1. **Aucune reprise du Wi-Fi.** `connectWifi()` ne tournait qu'une fois. Une coupure était donc
   définitive jusqu'au redémarrage — exactement ce qui a été observé. Ajout de
   `setAutoReconnect`, d'un gestionnaire d'événements qui journalise le motif de déconnexion, et
   d'une relance explicite après 15 s sans lien. C'est le test §14.4 du plan, qui n'était pas tenu.
2. **Veille radio active.** Réglage par défaut de l'Arduino : la radio s'endort entre deux balises,
   ce qui ajoute des centaines de millisecondes très variables sur un serveur censé répondre à tout
   instant. `WiFi.setSleep(false)`.
3. **Aucun cache sur l'interface.** Le `Cache-Control: no-store` posé pour l'API s'appliquait aussi
   aux fichiers statiques : **53 ko retéléchargés à chaque ouverture de page.** Désormais un ETag,
   empreinte FNV de tous les fichiers calculée au démarrage, et une revalidation qui répond `304`.

   ```text
   app.js, corps complet          14 781 ms
   app.js, revalidation (304)         503 ms
   ```

   Trente fois moins, sur une liaison inchangée. Le `no-store` reste sur l'API, où il a un sens.

### Suite

L'action est matérielle : **brancher l'antenne IPEX, carte hors tension** (§5.1 : le connecteur u.FL
est fragile et la radio ne doit pas démarrer sans antenne). Contrôle attendu après remise sous
tension : la perte de paquets tombe à zéro et le RTT sous 30 ms.

Si l'antenne était déjà en place, les suspects suivants sont, dans l'ordre : connecteur u.FL mal
enclenché ou abîmé — d'où l'antenne de rechange conseillée au §5.1 —, puis les réglages du routeur
Asus BE88U sur la bande 2,4 GHz (802.11ax et WPA3 en mode transition mettent certains ESP32 en
difficulté), puis l'alimentation de la carte.
