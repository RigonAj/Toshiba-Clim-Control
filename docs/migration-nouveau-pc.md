# Reprendre le projet sur une autre machine

> Rédigé le 1er août 2026, au moment du changement de PC.
> À lire en premier sur la nouvelle machine, avant le [README](../README.md).
>
> **✅ Migration effectuée le 1er août 2026** sur le profil `rigon`. Ce document reste la référence
> pour la *prochaine* machine ; il n'a pas été réécrit après coup. Les écarts constatés à
> l'exécution — PlatformIO installé par `pip` et non par `get-platformio.py`, port `COM4` et non
> `COM3`, ESP-IDF v5.4/v6.0.1 déjà présentes au lieu de v5.5.3 — sont consignés dans le
> [journal de tests](journal-de-tests.md). Le §7 ci-dessous décrit l'état *au départ* de la
> migration : il est dépassé, l'étage d'émission fonctionne depuis.

Ce document existe parce que le projet dépend de trois choses qui ne se copient pas : une chaîne
d'outils installée hors du dossier, une jonction NTFS, et un numéro de port série. Rien de
compliqué, mais rien de devinable non plus.

---

## 1. Ce qu'il faut emporter

**Un seul dossier :**

```text
C:\Users\<utilisateur>\Documents\Esp Controle clim
```

Environ **200 Ko**. Il est désormais autosuffisant : le dossier de conception, qui vivait
auparavant dans `Documents\Codex\...`, a été rapatrié dans `docs/plan-projet-clim-toshiba-esp32.md`
précisément pour que cette phrase soit vraie. C'est lui que le README et le journal citent à
longueur de paragraphe sous la forme « §5.1 », « §6.1 », « §8.2 ».

Le dossier `ir-capture/.pio/` a été supprimé avant la copie — voir §2.

### Ce qui compte vraiment dedans

| Chemin | Reproductible ? | Remarque |
|---|:---:|---|
| `captures/raw/` | **non** | Mesures physiques de la WH-TG01NE. Sans le matériel et la télécommande, elles sont irremplaçables. C'est le contenu le plus précieux du projet. |
| `docs/` | non | Plan de conception, journal de tests, cahier des charges v2, ce document |
| `ir-capture/src/`, `tools/` | non | Le code écrit à la main |
| `ir-capture/.pio/` | oui | Supprimé volontairement |

---

## 2. Ce qui reste derrière, volontairement

| Quoi | Taille | Pourquoi on ne le copie pas |
|---|---|---|
| `ir-capture\.pio\` | 39 Mo | Bases de dépendances SCons contenant des **chemins absolus figés** vers l'ancien profil utilisateur. Recopiées, elles produisent des erreurs de compilation incompréhensibles. PlatformIO reconstruit le tout en quelques minutes. |
| `~\.platformio\` | ~1 Go | Le `penv` est un environnement virtuel Python à chemins absolus en dur : recopié, il ne démarre pas. |
| `~\.espressif\` | ~5 Go | Idem, et de toute façon retéléchargeable. |
| `~\esp\v5.5.3\esp-idf\` | ~500 Mo | Clone git superficiel, se refait en une commande. |
| `~\esp\smoketest\` | — | Projets de validation jetables, leur rôle est terminé. |
| `plan-...-esp32.html` | 92 Ko | Rendu HTML **périmé** : daté du 25 juillet alors que le `.md` a été révisé le 1er août. Ne pas le réimporter, il contredirait la version de référence. |

> **Note sur `.claude\settings.local.json`.** Il est dans le dossier et sera donc copié. Il ne
> contient que des autorisations ponctuelles de commandes d'installation passées, toutes avec des
> chemins `C:\Users\win-ks.com\...` en dur. Sur la nouvelle machine elles ne correspondront à rien.
> Le fichier peut être vidé à `{}` sans rien perdre.

---

## 3. Installation sur la nouvelle machine

### 3.1 Python 3.12

Socle de PlatformIO **et** de `tools/capture_export.py`. Attention au piège de l'ancienne machine :
Windows fournit un faux `python.exe` qui ouvre le Microsoft Store. Installer une vraie version :

```powershell
winget install --id Python.Python.3.12 --scope user --silent --accept-package-agreements --accept-source-agreements
```

Elle atterrit dans `%LOCALAPPDATA%\Programs\Python\Python312`. Vérifier que c'est bien elle qui
répond :

```powershell
python --version
```

Si la réponse est vide ou ouvre le Store, préfixer le PATH avec ce dossier — le raccourci
WindowsApps masque l'installation réelle.

### 3.2 PlatformIO Core

```powershell
python -m pip install --upgrade platformio
```

L'exécutable arrive dans `~\.platformio\penv\Scripts`. Ajouter ce dossier au PATH utilisateur,
sinon la commande `pio` reste introuvable.

### 3.3 La jonction NTFS — indispensable

ESP-IDF refuse les espaces dans les chemins de projet, et le dossier en contient deux. La jonction
donne un chemin propre sans rien déplacer. **À recréer, elle ne se copie pas :**

```powershell
cmd /c mklink /J C:\esp-clim "C:\Users\<utilisateur>\Documents\Esp Controle clim"
```

À partir de là, **toujours compiler depuis `C:\esp-clim`**, jamais depuis le chemin avec espaces.

### 3.4 Première compilation

Elle retélécharge le toolchain Xtensa et la bibliothèque IRremoteESP8266. Prévoir de l'internet et
une dizaine de minutes ; les suivantes prennent quelques secondes.

```powershell
Set-Location C:\esp-clim\ir-capture; pio run -e capture
```

### 3.5 Retrouver le port série

Le XIAO ESP32-S3 a un USB-Serial-JTAG natif : **aucun pilote à installer** sur Windows 11. En
revanche le numéro de port ne sera plus `COM3`.

```powershell
pio device list
```

La bonne carte se reconnaît à son adresse MAC, `14:c1:9f:c4:fc:40`, visible au téléversement.
Adapter ensuite les `--upload-port` des commandes du README §6.

### 3.6 ESP-IDF — à repousser

Le firmware définitif n'existe pas encore : il n'y a aucun dossier `toshiba-climate-controller/`.
**Cette installation n'est donc pas urgente.** Elle ne le deviendra qu'après la campagne de
captures du §8.2. Le tag exact figé par le plan §7.1 est **v5.5.3**, à ne pas remplacer par une
version plus récente sans relire le §7.1.

---

## 4. Vérification que tout est reparti

Dans l'ordre, chaque étape ne coûte que quelques secondes :

| # | Commande ou geste | Attendu |
|---|---|---|
| 1 | `Test-Path C:\esp-clim` | `True` |
| 2 | `pio --version` | PlatformIO Core 6.x |
| 3 | `pio run -e capture` depuis `C:\esp-clim\ir-capture` | `SUCCESS` |
| 4 | `pio device list` | la carte apparaît sur un `COMx` |
| 5 | `pio run -e scan -t upload --upload-port COMx` | la console affiche le tableau des onze broches |
| 6 | `python tools\capture_export.py --help` | l'aide s'affiche |

Si l'étape 3 passe et l'étape 5 aussi, la chaîne complète est reconstituée.

---

## 5. Si le nom d'utilisateur Windows est différent

La jonction `C:\esp-clim` absorbe le problème pour tout ce qui compile. Restent les documents, qui
citent des chemins absolus devenus faux :

| Fichier | Endroits à corriger |
|---|---|
| `README.md` | §1 (tableau des deux chemins), §3 (tableau des outils) |
| `docs/journal-de-tests.md` | rien — le journal ne cite que des chemins relatifs |
| `docs/migration-nouveau-pc.md` | §1 et §3.3 de ce document |

Ce n'est pas bloquant, seulement trompeur à la relecture dans six mois.

---

## 6. Le matériel à emporter

Le logiciel ne sert à rien sans la plaque d'essai. Inventaire au 1er août 2026 :

| Élément | Note |
|---|---|
| Seeed XIAO ESP32-S3 + câble USB-C | la carte, MAC `14:c1:9f:c4:fc:40` |
| Antenne externe IPEX | fournie, pas encore utilisée — connecteur u.FL fragile |
| TSOP38238 | récepteur, **validé et fonctionnel** |
| LED IR | émetteur |
| Transistor `PN2222A` | monté, mais l'étage ne commute pas — voir §7 |
| Résistance 220 Ω | seule valeur connue en stock |
| Potentiomètre de kit | sert de résistance de base |
| Lot de résistances non identifiées | à trier, chercher du 22 à 47 Ω |
| Plaque d'essai et fils | **le câblage en place vaut mieux que sa reconstitution** |
| Télécommande `WH-TG01NE` | indispensable à toute nouvelle capture |

> **Contrainte de lieu.** Le climatiseur ne se déplace pas. Sur la nouvelle machine, la boucle
> locale émetteur → TSOP reste possible partout, mais **la preuve n° 2 du §2.3 — la clim qui réagit
> réellement — ne peut se faire que devant l'unité intérieure.**

### À acheter pour terminer l'étage d'émission

Tous à moins d'un euro, ce sont les composants les moins chers de la nomenclature du §5.1 :
**22 Ω / 0,5 W**, **1 kΩ**, **10 kΩ**, **100 µF**, et un `PN2222A` de rechange.

---

## 7. Où reprendre exactement

L'état complet est dans le [README §0](../README.md) et le
[journal de tests](journal-de-tests.md). En une phrase :

**Le protocole est décodé, l'étage d'émission ne fonctionne pas encore.**

La télécommande émet un `TOSHIBA_AC` de 72 bits, préambule `F2 0D`, modèle *Remote A*, checksum et
encodage de la température vérifiés. Le jalon du §2.3 est franchi à deux tiers.

**L'action suivante est un geste, pas du code.** L'étage d'émission est monté mais la LED IR reste
allumée en permanence quel que soit l'état de GPIO2 : le transistor ne commute pas. Le test
discriminant, décrit en fin de journal :

> Carte alimentée, débrancher le seul fil **émetteur → `GND`** et observer la LED IR à la caméra
> d'un téléphone.
> - Elle s'éteint → le transistor est dans le chemin du courant ; l'erreur est sur la base ou le
>   transistor est claqué.
> - Elle reste allumée → le transistor n'y est pas du tout ; la cathode de la LED retombe dans la
>   rangée du `GND` au lieu du collecteur.

Pour rejouer le test après recâblage :

```powershell
Set-Location C:\esp-clim\ir-capture; pio run -e txtest -t upload --upload-port COMx
```

Puis appuyer sur `RESET` : phases statiques de 5 s bas puis 5 s haut, ensuite clignotement à 1 Hz.
La LED d'état du XIAO suit la LED IR, cadrer les deux dans la même image.

Une fois l'étage validé, la suite est la campagne de captures du §8.2 — cinq échantillons par
commande — qui débloque l'écriture du codec.

---

## 8. Une suggestion pour la prochaine fois

Le projet **n'est pas un dépôt git**, alors qu'il a déjà un `.gitignore` complet et que
`captures/` contient des mesures qu'aucune réinstallation ne rendra. Un `git init` suivi d'un
premier commit transformerait la prochaine migration en `git clone`, et donnerait au passage un
historique aux relevés. Rien ne l'impose, mais la copie manuelle de dossiers est le seul point
fragile qui reste.
