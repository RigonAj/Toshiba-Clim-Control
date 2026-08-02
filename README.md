# Pilotage des climatiseurs Toshiba par ESP32 — organisation des fichiers

Pilotage infrarouge de climatiseurs Toshiba par ESP32-S3 : rétro-ingénierie du protocole de la
télécommande WH-TG01NE, étage d'émission IR, interface Web servie par la carte et planning horaire.
Deux boîtiers autonomes, sans serveur domotique.

> Document d'orientation : **où se trouve quoi, et pourquoi**.
> Il ne remplace pas le dossier de conception, il explique comment celui-ci se traduit sur le disque.
>
> Dossier de conception : [docs/plan-projet-clim-toshiba-esp32.md](docs/plan-projet-clim-toshiba-esp32.md)
> — c'est lui que tous les « §x.y » de ce README désignent.
>
> **Périmètre étendu au 1er août 2026** — voir [docs/cahier-des-charges-v2.md](docs/cahier-des-charges-v2.md).
>
> **Nouvelle machine ?** Commencer par [docs/migration-nouveau-pc.md](docs/migration-nouveau-pc.md).

---

## 0. Où en est le projet

| Phase | État |
|---|---|
| 0 — Documentation et préparation | ✅ terminée |
| 1a — Prototype réception | ✅ terminée |
| 1b — Étage d'émission | ✅ terminée, boucle locale à 12 trames sur 12 |
| 2 — Capture de la WH-TG01NE | 🟡 en cours, protocole identifié |
| 3 — Preuve sur la clim | 🟡 débloquée, exige d'être devant l'unité intérieure |
| 4 — Codec Toshiba | ⬜ bloquée par la campagne de captures |
| 5 — Interface Web | 🟡 servie par la carte, accessible depuis le réseau local, émission vérifiée au TSOP |
| 6 et suivantes | ⬜ |

**Le verrou principal du projet est levé.** La télécommande émet un `TOSHIBA_AC` standard de 72 bits,
préambule `F2 0D`, modèle *Remote A*. Le checksum et l'encodage de la température sont connus et
vérifiés. Détails et relevés dans [docs/journal-de-tests.md](docs/journal-de-tests.md).

**L'étage d'émission fonctionne** depuis la reprise sur la nouvelle machine : la carte émet une trame
`TOSHIBA_AC` que son propre récepteur redécode octet pour octet. Il ne manque plus à la preuve n° 2
du §2.3 qu'une chose, et ce n'est pas de l'électronique : **se trouver devant le climatiseur.**

---

## 1. Les deux chemins vers le même dossier

Ce projet est accessible par **deux chemins qui désignent physiquement le même dossier** :

| Chemin | Usage |
|---|---|
| `C:\Users\rigon\Documents\Esp Controle clim` | Chemin réel. C'est ce que montre l'explorateur Windows. |
| `C:\esp-clim` | Jonction NTFS créée vers le précédent. **C'est par là qu'il faut compiler.** |

**Pourquoi.** ESP-IDF ne supporte pas les espaces dans les chemins de projet ; la compilation échoue
avec des erreurs difficiles à interpréter. Le chemin réel contient deux espaces. La jonction fournit un
chemin propre sans rien déplacer : les deux vues sont le même dossier, un fichier créé d'un côté
apparaît instantanément de l'autre.

Si la jonction est supprimée par erreur, elle se recrée avec :

```bash
cmd /c mklink /J C:\esp-clim "C:\Users\rigon\Documents\Esp Controle clim"
```

---

## 2. Arborescence du projet

```text
C:\esp-clim\
├── README.md                       ← ce document
├── .gitignore
│
├── ir-capture/                     PlatformIO — outils de mise au point
│   ├── platformio.ini              sept environnements, voir §4
│   ├── include/
│   │   ├── secrets.example.h       modèle versionné, ne contient rien
│   │   └── secrets.h               [local] SSID et mot de passe — jamais versionné
│   ├── src/
│   │   ├── main.cpp                capture et décodage IR          (env: capture)
│   │   ├── pin_scan.cpp            scan des onze broches           (env: scan)
│   │   ├── autodetect.cpp          auto-détection du récepteur     (env: autodetect)
│   │   ├── tx_blink.cpp            commutation du transistor       (env: txtest)
│   │   ├── tx_carrier.cpp          diagnostic de la porteuse       (env: carrier)
│   │   ├── tx_loopback.cpp         boucle émission → réception     (env: loopback)
│   │   └── webui.cpp               interface Web sur la carte      (env: webui)
│   └── .pio/                       [généré] toolchain et binaires
│
├── captures/                       mesures de la télécommande (plan §8.2)
│   ├── README.md                   convention de nommage et format JSON
│   └── raw/                        relevés validés, checksum vérifié
│
├── toshiba-climate-controller/     firmware définitif — seule l'interface existe (§5)
│   └── web/                        fichiers servis depuis la partition `storage`
│       ├── index.html
│       ├── app.css
│       ├── app.js
│       └── config.example.json     adresses des boîtiers, à copier en `config.json`
│
├── tools/
│   ├── capture_export.py           journal de console → JSON, vérifie le checksum
│   └── mock_api.py                 maquette d'un boîtier : sert `web/` et l'API, sans matériel
│
└── docs/
    ├── plan-projet-clim-toshiba-esp32.md   dossier de conception — la référence
    ├── journal-de-tests.md                 relevés datés, diagnostics, jalons
    ├── cahier-des-charges-v2.md            périmètre étendu : planification et interface
    ├── api-v2.md                           contrat entre l'interface et le firmware
    └── migration-nouveau-pc.md             reprendre le projet sur une autre machine
```

Le firmware définitif n'existe pas encore — voir §5.

### Ce qui est versionné et ce qui ne l'est pas

| Dossier | Versionné | Raison |
|---|:---:|---|
| `ir-capture/src/`, `tools/`, `docs/` | oui | C'est le code et les notes |
| `ir-capture/.pio/` | **non** | ~40 Mo d'artefacts de compilation, entièrement reconstructibles. La chaîne d'outils elle-même, ~1 Go, est ailleurs : dans `~\.platformio` (§3). |
| `captures/` | **oui** | Ce sont des **mesures physiques**, impossibles à reproduire sans le matériel. C'est le contenu le plus précieux du projet. |
| Mots de passe, clés WireGuard, jetons | **jamais** | Plan §16.3 |

---

## 3. Où sont les outils (hors du projet)

Les chaînes de compilation ne sont **pas** dans le dossier projet. Elles sont partagées entre tous les
projets ESP32 de la machine.

| Outil | Emplacement | Rôle |
|---|---|---|
| ESP-IDF v5.4 et v6.0.1 | `C:\esp\v5.4\esp-idf`, `C:\esp\v6.0.1\esp-idf` | Déjà installées sur cette machine. **Le plan §7.1 fige v5.5.3** — à trancher avant d'écrire le firmware définitif |
| Outils ESP-IDF | `C:\Espressif\tools` | Compilateur Xtensa, CMake, Ninja, OpenOCD |
| PlatformIO Core 6.1.19 | `%LOCALAPPDATA%\Programs\Python\Python312\Scripts` | Chaîne Arduino pour les outils de mise au point |
| Python 3.12.10 | `%LOCALAPPDATA%\Programs\Python\Python312` | Socle des deux précédents |

> **Piège de cette machine.** `C:\Espressif\tools\python` (3.11) est fourni par l'installeur ESP-IDF
> et répond à `python` s'il passe devant dans le `PATH`. PlatformIO n'y est **pas** installé, pour ne
> pas polluer l'environnement de l'IDF. Vérifier au besoin :
>
> ```bash
> python -c "import sys; print(sys.executable)"
> ```

**Conséquence pratique :** sauvegarder le dossier projet suffit — et depuis le 1er août 2026 c'est
littéralement vrai, le dossier de conception ayant été rapatrié dans `docs/`. Les outils se
réinstallent, les mesures non. Procédure complète de reprise sur une autre machine :
[docs/migration-nouveau-pc.md](docs/migration-nouveau-pc.md).

---

## 4. Les six firmwares de mise au point

`ir-capture/` regroupe six croquis qui partagent la même carte et la même plaque d'essai. Les trois
premiers travaillent sur la réception, les trois suivants sur l'émission. Ils se sélectionnent par
environnement PlatformIO.

| Environnement | Fichier | Sert à |
|---|---|---|
| `capture` *(défaut)* | `main.cpp` | Décoder les trames de la télécommande et exporter les durées brutes |
| `scan` | `pin_scan.cpp` | Trouver sur quelle broche un signal arrive, quand plus rien ne fonctionne |
| `autodetect` | `autodetect.cpp` | Alimenter le TSOP depuis un GPIO et trouver son orientation tout seul |
| `txtest` | `tx_blink.cpp` | Vérifier que le transistor commute, en continu, sans porteuse |
| `carrier` | `tx_carrier.cpp` | Séparer le continu de la modulation 38 kHz quand la boucle ne reçoit rien |
| `loopback` | `tx_loopback.cpp` | Émettre une trame `TOSHIBA_AC` et la faire redécoder par le TSOP |
| `webui` | `webui.cpp` | Servir l'interface depuis la carte, émettre pour de vrai, et contrôler l'émission au TSOP |

Ces outils sont jetables une fois la phase 3 terminée, **sauf `capture_export.py`** et le contenu de
`captures/`, qui servent de vecteurs de test au codec (plan §14.1).

### L'environnement `webui` — l'interface sur la carte

C'est le seul environnement qui demande une préparation : le Wi-Fi.

1. Copier `include/secrets.example.h` en `include/secrets.h` et y mettre le SSID et le mot de passe.
   Ce fichier est exclu du dépôt par `.gitignore` (plan §16.3) ; le modèle, lui, est versionné parce
   qu'il ne contient rien.
2. Téléverser les fichiers de l'interface — ils sont pris **directement** dans
   `toshiba-climate-controller/web/`, il n'y a jamais deux copies à maintenir.
3. Téléverser le firmware, puis lire l'adresse IP sur la console.

Le Wi-Fi doit être en **2,4 GHz** : l'ESP32-S3 ne voit pas les réseaux 5 GHz. Et le XIAO n'ayant pas
d'antenne intégrée, l'antenne IPEX doit être branchée avant la mise sous tension (plan §5.1).

Ce croquis n'est **pas** le firmware définitif : ni mot de passe, ni OTA, ni NVS versionnée. Il ne
s'utilise que sur le réseau domestique. Ce qu'il apporte, et que la maquette Python ne peut pas
donner : chaque commande de la page émet une vraie trame, que le TSOP de la même carte relit et
compare — le contrôle du plan §14.2, visible dans le panneau *Diagnostic infrarouge* de l'interface.

> **Le contrôle statique de câblage ment.** Le `checkWiring()` de `main.cpp` met la broche en entrée
> avec tirage vers le bas et conclut d'un niveau haut que le TSOP tient la ligne. Il s'est trompé
> dans les deux sens au cours d'une même session : « OK » avec un récepteur muet, « ÉCHEC » avec un
> récepteur parfait. La sortie du TSOP est à collecteur ouvert avec un tirage interne trop faible
> face à celui de l'ESP32. **Le seul indicateur fiable est le comptage de fronts pendant une émission
> connue**, tel que `probeActivity()` l'implémente dans `tx_loopback.cpp`.

### Câblage actuel — provisoire mais validé

Le TSOP est alimenté par une sortie GPIO plutôt que par la broche `3V3`. Il consomme 1,5 mA au
maximum contre 40 mA admissibles par broche : c'est électriquement sain, et ça supprime une source
d'erreur récurrente au montage.

| Patte du TSOP38238 | Broche du XIAO | GPIO |
|---|---|---:|
| `VS` (alimentation) | `D3` — sortie à l'état haut | 4 |
| `GND` (patte du milieu) | `GND` | — |
| `OUT` (signal) | `D4` | 5 |

Le montage définitif reviendra au §6.2 du plan : `VS` → `3V3` au travers de 100 Ω, `OUT` → `D2`.

### Câblage d'émission — monté et **validé**

Faute des composants du §5.1, l'étage du §6.1 est monté avec des substitutions. Chaîne en série
entre le rail 5 V et la masse, la LED IR **anode côté 5 V** :

| Élément | Valeur du plan §6.1 | Valeur montée | Raison |
|---|---|---|---|
| Résistance de collecteur | 22 Ω / 0,5 W | **220 Ω** | seule valeur connue en stock |
| Résistance de base | 1 kΩ | **résistance fixe, 1 kΩ ou 10 kΩ** | valeur non identifiée faute de multimètre ; l'étage fonctionne avec celle en place |
| Rappel de base | 10 kΩ | **absent** | remplacé par une mise à l'état bas dès le `setup()` |
| Réservoir 5 V | 100 µF | **absent** | sans objet au courant réduit |

Le courant tombe de 155 mA à ≈ 15 mA, soit une portée d'environ **1,5 m** au lieu de 1 à 5 m —
assez pour la preuve n° 2 du §2.3, pas pour l'installation définitive.

Le potentiomètre du montage d'origine a été remplacé par une résistance fixe, **1 kΩ ou 10 kΩ, non
identifiée** faute de multimètre. Elle fait l'affaire : l'étage commute jusqu'à 38 kHz avec celle en
place. À trancher si la portée se révélait insuffisante devant la clim — une base sous-attaquée
sature moins bien et coûte du courant de LED.

**L'étage est validé.** Il commute en continu et tient les 38 kHz : la boucle locale émet une trame
`TOSHIBA_AC` de 72 bits que le TSOP redécode sans un octet d'écart, douze fois sur douze.

> La panne du 1er août — « la LED IR reste allumée quel que soit l'état de GPIO2 » — n'était ni un
> transistor claqué ni une erreur de plaque d'essai : **la résistance de base était sur `D2` au lieu
> de `D1`.** `D2` est GPIO3, que `tx_blink.cpp` ne pilote jamais. Récit complet dans
> [docs/journal-de-tests.md](docs/journal-de-tests.md).

---

## 5. Ce qui n'existe pas encore, et pourquoi

Aucun firmware définitif n'est écrit. **C'est volontaire, et la raison a changé.**

Jusqu'au 1er août, la raison était le jalon du §2.3 : ne rien construire avant de connaître le
protocole réel. Ce jalon est maintenant franchi à deux tiers — le protocole est identifié et l'étage
d'émission fonctionne. Il ne manque que la preuve de retransmission sur la clim, qui n'est plus
bloquée par le matériel mais **par le lieu** : elle exige d'être devant l'unité intérieure.

La raison est désormais **la campagne de captures**. L'architecture, elle, est tranchée depuis le
1er août : faute de machine allumée en permanence, la voie ESPHome + Home Assistant est écartée et
le projet part sur un **firmware ESP-IDF autonome**. Instruction complète dans
[docs/cahier-des-charges-v2.md](docs/cahier-des-charges-v2.md).

Deux décisions de conception en découlent et modifient le plan d'origine :

- **Boîtiers symétriques**, sans rôle principal/secondaire (alternative du plan §4.4). Chaque boîtier
  doit exécuter son planning même isolé, ce qui rend le relais du §4.1 inutile.
- **Interface servie en fichiers statiques** depuis la partition `storage`, et non compilée dans le
  firmware. Modifier l'interface devient un téléversement de fichier, sans recompilation ni OTA.

Le firmware sera créé sous `toshiba-climate-controller/`, avec la structure du plan §7.2. Il n'est pas
encore initialisé : la campagne du §8.2 doit d'abord révéler l'encodage des champs mode, ventilation
et swing, faute de quoi le codec serait écrit sur des suppositions.

### L'interface, elle, existe déjà

C'est la conséquence directe de la décision « interface en fichiers statiques » : `web/` ne dépend ni
du codec, ni du réseau, ni de la carte. Elle a donc été écrite pendant que la clim est inaccessible,
et testée contre une maquette d'API — `tools/mock_api.py` — qui joue deux boîtiers sur deux ports.

Ce qui reste à écrire côté firmware pour la brancher est décrit dans
[docs/api-v2.md](docs/api-v2.md) : cinq routes, aucune surprise.

Deux points de conception à connaître avant de la modifier :

- **L'interface ne code en dur aucune capacité.** Les modes et les vitesses de ventilation proposés
  sont ceux que le boîtier déclare dans `capabilities`. Tant que la campagne du §8.2 n'a pas mesuré
  l'encodage de `mode`, `fan` et `swing`, le boîtier les déclare dans `unverified_fields` et
  l'interface affiche la pastille **non vérifié** en face de chacun. Aucune fonction non prouvée
  n'est présentée comme acquise.
- **La courbe de planning est un escalier, pas une interpolation** : un point vaut jusqu'au suivant,
  et le dernier point de la journée déborde sur le début de la suivante.
- **Un point d'arrêt peut être « maintenu »** (`lock`). Pendant ce segment, toute trame vue par le
  TSOP est suivie d'un ordre d'arrêt : c'est ce qui neutralise un rallumage à la télécommande.
  L'infrarouge ne permettant pas d'empêcher la clim de démarrer, elle s'allume puis s'éteint —
  l'interface l'annonce, et le détail est dans [docs/api-v2.md](docs/api-v2.md).

---

## 6. Commandes courantes

> **Le port est `COM4` sur cette machine, plus `COM3`.** `COM3` existe aussi mais désigne un autre
> appareil, un adaptateur CH343. La bonne carte se reconnaît à sa MAC `14:c1:9f:c4:fc:40`, affichée
> au téléversement.

Compiler et flasher le firmware de capture :

```bash
cd C:\esp-clim\ir-capture && pio run -e capture --target upload --upload-port COM4
```

Basculer sur un outil de diagnostic :

```bash
cd C:\esp-clim\ir-capture && pio run -e autodetect --target upload --upload-port COM4
```

Tester la commutation du transistor — observer la LED IR à la caméra d'un téléphone :

```bash
cd C:\esp-clim\ir-capture && pio run -e txtest --target upload --upload-port COM4
```

Vérifier la boucle complète émission → réception :

```bash
cd C:\esp-clim\ir-capture && pio run -e loopback --target upload --upload-port COM4
```

Diagnostiquer la porteuse quand la boucle ne reçoit rien :

```bash
cd C:\esp-clim\ir-capture && pio run -e carrier --target upload --upload-port COM4
```

Lire la console :

```bash
cd C:\esp-clim\ir-capture && pio device monitor --port COM4
```

Exporter un journal de console en captures JSON :

```bash
cd C:\esp-clim && python tools\capture_export.py journal.txt --label cool_22_auto_off
```

Retrouver la carte si le port a changé :

```bash
pio device list
```

Mettre l'interface sur la carte — les fichiers d'abord, le firmware ensuite :

```bash
cd C:\esp-clim\ir-capture && pio run -e webui --target uploadfs --upload-port COM4
```

```bash
cd C:\esp-clim\ir-capture && pio run -e webui --target upload --upload-port COM4
```

Après modification de `web/`, seul le premier est à rejouer : le firmware ne change pas.

Travailler sur l'interface sans matériel — deux boîtiers simulés, un par terminal :

```bash
cd C:\esp-clim && python tools\mock_api.py --port 8081 --name Salon --peer "Chambre=http://127.0.0.1:8082"
```

```bash
cd C:\esp-clim && python tools\mock_api.py --port 8082 --name Chambre --peer "Salon=http://127.0.0.1:8081"
```

Puis ouvrir `http://127.0.0.1:8081/`. Les fichiers de `web/` sont servis tels quels : il suffit de
recharger la page après modification.

Ouvrir un terminal ESP-IDF (pour le firmware définitif, quand il existera) :

```bash
powershell -NoExit -ExecutionPolicy Bypass -File C:\Espressif\tools\Microsoft.v5.4.PowerShell_profile.ps1
```

---

## 7. Matériel et protocole de référence

### Carte

| Élément | Valeur relevée |
|---|---|
| Carte | Seeed XIAO ESP32-S3, SoC ESP32-S3 rev v0.2 |
| Flash | 8 Mo — valide la table de partitions du plan §7.5 |
| PSRAM | 8 Mo embarquée — confirme un ESP32-S3**R8** |
| Adresse MAC | `14:c1:9f:c4:fc:40` — à réserver en DHCP (plan §11.2) |
| Port série | `COM3`, USB-Serial-JTAG natif |
| Récepteur IR | TSOP38**2**38 (AGC2) et non TSOP38438 (AGC4) — fonctionne, voir §4 |

Brochage de référence (plan §6.0) — **les GPIO 26 à 37 sont interdits sur ESP32-S3**, ils sont câblés
à la flash et à la PSRAM :

| Fonction | Sérigraphie | GPIO |
|---|---|---:|
| Émission IR (montée et **validée**) | `D1` | 2 |
| Réception IR (montage définitif) | `D2` | 3 |
| Bouton de maintenance | `BOOT` | 0 |
| LED d'état (allumée à l'état bas) | — | 21 |
| Console USB | — | 19 / 20 |

### Protocole de la WH-TG01NE — mesuré, plus supposé

| Caractéristique | Valeur |
|---|---|
| Protocole | `TOSHIBA_AC`, modèle *Remote A* |
| Longueur | 72 bits — 9 octets |
| Préambule | `F2 0D` |
| Répétition | le paquet est émis **deux fois** par appui, séparation ≈ 5956 µs |
| En-tête | 4396 / 4384 µs |
| Marque | ≈ 546 µs |
| Espace court (0) | ≈ 536 µs |
| Espace long (1) | ≈ 1620 µs |
| Porteuse | 38 kHz |

Encodage connu à ce jour :

```text
octet    0    1    2    3    4    5    6    7    8
        F2   0D   03   FC   01   4 0  00   00   41
                                 │              └── checksum
                                 └── quartet haut = température − 17
```

**Checksum** = OU exclusif des huit premiers octets. Vérifié sur tous les états capturés.

Les champs mode, ventilation et swing restent à identifier : ils demandent la campagne complète de
la matrice du plan §8.2.
