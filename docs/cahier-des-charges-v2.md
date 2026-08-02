# Cahier des charges v2 — périmètre étendu

> Rédigé le 1er août 2026, à la suite du franchissement du jalon protocole.
> Complète et modifie le dossier de conception d'origine, sans le remplacer.

---

## 1. Ce qui déclenche cette révision

Deux événements du 1er août 2026 changent les termes du projet.

**Le protocole est identifié.** La `WH-TG01NE` émet un `TOSHIBA_AC` standard de 72 bits, reconnu
directement par les bibliothèques communautaires. Le plan d'origine (§17.3) prévoyait explicitement ce
cas de figure : *« si la WH-TG01NE est reconnue directement et que le besoin est de piloter les clims
rapidement, ESPHome est le choix rationnel »*. Cette branche est donc active.

**Le périmètre s'étend.** La demande porte désormais sur de la planification horaire et une interface
riche, deux points que la version 1 excluait volontairement.

---

## 2. Besoins exprimés

| Réf | Besoin | Statut dans le plan d'origine |
|---|---|---|
| B1 | Consigne de température **variable selon l'heure**, éditée sur un graphe par points déplaçables | Hors périmètre (§3.3, « minuterie hebdomadaire ») |
| B2 | **Marche et arrêt programmés** à des heures données | Hors périmètre (§3.3) |
| B3 | **Toutes les fonctions** de la télécommande : mode, ventilation, swing, température, marche/arrêt | Prévu (§3.2) |
| B4 | Accès à l'interface **depuis tous les appareils de la maison, par navigateur** | Prévu (§10.1) |
| B5 | Deux climatiseurs pilotés depuis la même page | Prévu (§4) |
| B6 | Accès extérieur par le **WireGuard existant**, sans cloud | Prévu (§11) |
| B7 | Solution **facile à concevoir et entretenir par un agent IA** | Nouveau critère, structurant |

Le besoin B7 est le plus important pour l'arbitrage qui suit : il ne porte pas sur ce que le système
fait, mais sur le coût de chaque modification future.

---

## 3. Règles de conception — tranchées le 1er août 2026

| Question | Décision |
|---|---|
| Granularité du planning | **Deux courbes : semaine et week-end** |
| Planning indépendant par pièce, ou commun ? | Indépendant par pièce |
| Modification manuelle pendant un planning actif | **Elle tient jusqu'au point de consigne suivant**, puis le planning reprend la main |
| Heure indisponible au démarrage (pas de réseau) | Aucune action programmée tant que l'heure n'est pas synchronisée. L'état est marqué `unknown`. |
| Résolution du graphe | Pas de 30 minutes, points libres sur 24 h |

La règle de reprise au point suivant est le comportement habituel des thermostats programmables :
elle évite qu'un réglage manuel oublié fige l'installation, tout en respectant une intervention
ponctuelle.

---

## 4. Arbitrage d'architecture

Le protocole étant connu, le choix ne porte plus sur la faisabilité mais sur le coût d'entretien.

### Option A — ESPHome sur les ESP32 + Home Assistant

Les boîtiers deviennent de simples passerelles infrarouges décrites en YAML. Toute l'intelligence —
planification, interface, historique, authentification — est fournie par Home Assistant.

**Ce qui disparaît du plan d'origine :** le composant `peer_client`, le jeton d'appairage,
l'interrogation périodique du boîtier secondaire, le rôle principal/secondaire (§4.1 à §4.3),
l'authentification maison (§11.3), le mécanisme OTA signé (§12), et la totalité de l'API du §10.2.
Home Assistant les fournit tous.

**Reste à écrire :** un tableau de bord, et une carte personnalisée en JavaScript pour le graphe à
points déplaçables — ce composant n'existe pas en standard.

**Exigence :** une machine allumée en permanence pour héberger Home Assistant.

### Option B — Firmware ESP-IDF autonome

Le plan d'origine, augmenté d'un ordonnanceur et d'une interface plus riche. Chaque boîtier est
complètement autonome, aucun serveur tiers.

L'interface serait servie en fichiers statiques depuis la partition `storage` de 1,875 Mo, donc
modifiable sans recompiler le firmware — point important pour le besoin B7.

**Reste à écrire :** tout. Le codec, l'ordonnanceur, l'interface, l'API, l'authentification, l'OTA
signé, la synchronisation entre les deux boîtiers.

### Comparaison au regard du besoin B7

| Critère | A — ESPHome + HA | B — ESP-IDF autonome |
|---|---|---|
| Modifier l'interface | Éditer un fichier, recharger la page — quelques secondes | Éditer des fichiers statiques, les téléverser — une minute |
| Modifier la logique | Éditer du YAML, recharger | Modifier du C, recompiler, OTA sur deux boîtiers, risque de rollback |
| Ajouter une fonction de la télécommande | Un attribut dans le composant `climate` | Encoder le champ, tester, recompiler, redéployer |
| Volume de code à maintenir | ~50 lignes de YAML + une carte JS | ~10 composants C, une API, une interface |
| Historique et courbes de température | Fourni | À écrire |
| Sauvegarde et restauration | Fournie | À écrire |
| Autonomie des boîtiers | Dépend de Home Assistant pour la planification | Totale |
| Dépendance à maintenir | Une instance Home Assistant | Aucune |
| Charge estimée | Un à deux jours | Deux à trois semaines avec le périmètre étendu |

### Recommandation

**L'option A, à condition de disposer d'une machine allumée en permanence.**

Le besoin B7 tranche nettement. Un agent IA entretient bien mieux du texte déclaratif rechargé à chaud
que du C compilé déployé par OTA sur deux appareils : le cycle d'essai passe de quelques minutes à
quelques secondes, et une erreur ne peut pas briquer un boîtier.

S'y ajoute que le périmètre étendu tombe exactement dans ce que Home Assistant fait nativement —
planification, historique, accès multi-appareils, comptes utilisateurs — alors que ce sont précisément
les parties les plus longues à écrire en option B.

Le seul point réellement à construire en option A est la carte du graphe à points déplaçables, et ce
travail serait de toute façon à faire en option B.

**Sans machine allumée en permanence, l'option A est impossible** et l'option B s'impose, avec une
charge nettement supérieure. Un Raspberry Pi d'occasion suffit à basculer dans le cas favorable.

### Décision — 1er août 2026 : option B

Aucune machine n'est allumée en permanence. Le routeur Asus BE88U ne peut pas se substituer à un
hôte : un routeur grand public ne dispose ni de Docker, ni de la mémoire, ni du stockage nécessaires
à Home Assistant, et même contourné par un micrologiciel alternatif, ce serait une base fragile pour
un équipement de confort quotidien.

**Le routeur n'a cependant rien à héberger.** Chaque ESP32 sert lui-même sa page : le routeur se
contente de router, ce qu'il fait déjà. Le besoin B4 est donc satisfait sans lui demander autre chose
que ce que prévoit le plan §11.2 — une réservation DHCP par boîtier, et le WireGuard déjà actif pour
l'accès extérieur.

**L'option B est retenue : firmware ESP-IDF autonome, aucune dépendance à un serveur.**

#### Deux conséquences de conception

**1. Les boîtiers deviennent symétriques.** Le plan §4.1 prévoyait un boîtier principal relayant les
commandes vers un secondaire. Cette architecture n'a plus de sens dès lors que chaque boîtier doit
exécuter son propre planning même seul : si le principal tombe, la chambre doit continuer à suivre sa
courbe. On retient donc l'alternative du plan §4.4 — **deux firmwares strictement identiques, sans
rôle**, et une page qui interroge les deux adresses côté navigateur.

Cela supprime `peer_client`, le jeton d'appairage et l'interrogation périodique, au prix d'un jeton
porteur conservé par le navigateur et d'en-têtes CORS, comme le §4.4 l'avait anticipé.

**2. L'interface est servie en fichiers statiques, pas compilée dans le firmware.** C'est la réponse
au besoin B7. La partition `storage` de 1,875 Mo (plan §7.5) accueille `index.html`, `app.css` et
`app.js`. Modifier l'interface — donc l'essentiel du travail d'entretien — se fait par un téléversement
de fichier, sans recompiler ni risquer un rollback OTA.

Le C ne contient alors que ce qui doit vraiment y être : le codec Toshiba, l'ordonnanceur, la machine
d'état, l'API REST et le serveur de fichiers. Le graphe à points déplaçables vit entièrement côté
navigateur et n'échange avec le boîtier qu'un objet JSON de planning.

### Ce qui ne change pas, quelle que soit l'option

- Le matériel : XIAO ESP32-S3, TSOP38238, étage d'émission du §6.1.
- Le brochage du §6.0 et l'interdiction des GPIO 26 à 37.
- La campagne de captures du §8.2 : elle sert à vérifier que **toutes** les fonctions de la
  télécommande sont correctement encodées (besoin B3), y compris en option A où il faut confirmer que
  le composant Toshiba d'ESPHome couvre bien le modèle *Remote A*.
- L'accès extérieur par WireGuard, sans redirection de port.
- La phase 1b et la preuve de portée du §2.3.

---

## 5. Prochaines étapes indépendantes de l'arbitrage

Ces travaux sont utiles dans les deux options et peuvent commencer immédiatement :

1. **Campagne de captures §8.2** — une commande à la fois, cinq appuis chacune, pour identifier les
   champs mode, ventilation et swing encore inconnus.
2. **Étage d'émission §6.1** — LED `TSAL6400`, transistor, résistances 22 Ω et 1 kΩ. Nécessaire à la
   troisième preuve du jalon §2.3.
3. **Preuve de portée §3** — retransmission de `off` puis `cool_22_auto_off` depuis l'emplacement
   final du boîtier.

Rien de ce travail n'est perdu si l'arbitrage bascule d'une option à l'autre.
