# Contrat d'API — ce que l'interface attend du boîtier

> Rédigé le 1er août 2026, en même temps que l'interface.
> Complète le plan §10.2, qui reste la référence pour tout ce qui n'est pas repris ici.
> L'implémentation de référence — et le moyen de tester l'interface sans matériel — est
> [`tools/mock_api.py`](../tools/mock_api.py).

L'interface est écrite **avant** le firmware. Ce document fige donc l'interface entre les deux :
c'est lui que le composant `web_api` devra satisfaire, et lui qu'il faudra modifier — avant le code —
si un besoin nouveau apparaît.

---

## 1. Principes

| Règle | Raison |
|---|---|
| Tout est en UTF-8, `application/json`, `Cache-Control: no-store` | Plan §10.2 |
| Aucun boîtier n'est « principal » | Cahier v2 §4.1 : boîtiers symétriques, la page interroge les deux |
| En-têtes CORS sur toute l'API | Conséquence directe du point précédent : les appels vers le second boîtier sont inter-origines |
| Authentification par jeton porteur, en-tête `Authorization: Bearer …` | Un cookie `SameSite=Strict` ne serait pas transmis à l'autre boîtier (plan §4.4) |
| Les capacités sont **déclarées par le boîtier**, jamais codées dans la page | L'interface ne doit pas proposer un mode que le codec ne sait pas encore encoder |

Le préflight `OPTIONS` doit répondre `204` avec
`Access-Control-Allow-Headers: Content-Type, Authorization` et
`Access-Control-Allow-Methods: GET, PUT, POST, OPTIONS`.

---

## 2. Fichiers statiques

Servis depuis la partition `storage` (cahier v2 §4.2) :

| Chemin | Rôle |
|---|---|
| `/` → `index.html` | La page |
| `/app.css`, `/app.js` | Ses deux dépendances, aucune autre |
| `/config.json` | **Optionnel.** Adresses des boîtiers livrées avec les fichiers |

`config.json` évite d'avoir à saisir les adresses sur chaque appareil de la maison :

```json
{
  "devices": [
    { "id": "salon",   "name": "Salon",   "base": "" },
    { "id": "chambre", "name": "Chambre", "base": "http://192.168.1.41" }
  ]
}
```

`base` vide désigne le boîtier qui sert la page. Un réglage saisi dans l'interface est mémorisé dans
le `localStorage` du navigateur et **prend le pas** sur `config.json`.

---

## 3. `GET /api/v1/status`

Interrogé toutes les 5 secondes par appareil affiché. Au-delà de 3 secondes sans réponse, le boîtier
est déclaré hors ligne et sa carte est désactivée — les autres restent commandables.

```json
{
  "schema_version": 1,
  "device_id": "clim-salon",
  "display_name": "Salon",
  "online": true,
  "state": {
    "power": true,
    "mode": "cool",
    "target_temperature_c": 22,
    "fan": "auto",
    "swing": false,
    "source": "web",
    "confidence": "fresh",
    "updated_at": "2026-08-01T18:09:09Z",
    "revision": 42
  },
  "capabilities": {
    "temperature_min_c": 17,
    "temperature_max_c": 30,
    "modes": ["auto", "cool", "heat", "dry"],
    "fans": ["auto", "low", "medium", "high"],
    "swing": true,
    "unverified_fields": ["mode", "fan", "swing"]
  },
  "schedule": {
    "enabled": true,
    "active_curve": "week",
    "next_point_at": "22:30",
    "override_active": false,
    "revision": 7
  },
  "time": { "now": "2026-08-01T18:09:09Z", "synced": true },
  "wifi": { "rssi_dbm": -54, "ip": "192.168.1.40" },
  "firmware": "1.0.0"
}
```

### Champs ajoutés au plan §10.2

| Champ | Ce que l'interface en fait |
|---|---|
| `capabilities.unverified_fields` | Affiche la pastille **non vérifié** à côté du réglage concerné. Tant que la campagne du plan §8.2 n'a pas mesuré l'encodage de `mode`, `fan` et `swing`, le boîtier doit les y déclarer : l'interface les propose, mais dit qu'ils ne sont pas prouvés. |
| `schedule.active_curve` | `week` ou `weekend`, calculé par le boîtier. Sert à tracer le repère d'heure courante sur la bonne courbe. |
| `schedule.next_point_at` | Affiché en résumé du panneau *Planning*. |
| `schedule.override_active` | Affiche « réglage manuel en cours » : une commande manuelle tient **jusqu'au point de consigne suivant** (cahier v2 §3). |
| `schedule.lock_active` | Affiche la pastille **maintenu éteint**. Vrai quand le segment en cours porte `lock` et qu'aucun réglage manuel ne le suspend. |
| `time.synced` | À `false`, l'interface affiche « heure non synchronisée ». Aucune action programmée ne doit être exécutée dans cet état, et `state.confidence` vaut `unknown`. |

`role` et `GET /api/v1/devices` du plan §10.2 **disparaissent** : les boîtiers n'ont plus de rôle et
l'agrégation se fait dans le navigateur.

---

## 4. `PUT /api/v1/climate`

Corps envoyé — toujours l'état **complet**, jamais une variation relative (plan §16.2) :

```json
{
  "command_id": "9f2c…",
  "power": true,
  "mode": "cool",
  "target_temperature_c": 22,
  "fan": "auto",
  "swing": false,
  "expected_revision": 41
}
```

Réponse `200` :

```json
{
  "accepted": true,
  "transmitted": true,
  "confirmed_by_ac": false,
  "state": { "…": "état complet, revision incrémentée" }
}
```

- `409` si `expected_revision` ne correspond plus, **avec l'état courant dans `state`** : l'interface
  l'adopte sans discuter et prévient l'utilisateur. C'est ce qui se produit quand la télécommande, le
  planning ou un autre navigateur est passé entre-temps.
- `400` si une valeur sort des capacités déclarées.
- Les appuis rapides sur `+`/`−` sont regroupés côté navigateur : une seule requête part 450 ms après
  le dernier appui. Le boîtier n'a donc pas à amortir la cadence, mais reste tenu de sérialiser ses
  émissions IR (plan §7.3).
- `confirmed_by_ac` vaut toujours `false` en version IR (plan §9.3). L'interface affiche « état
  estimé » en permanence pour cette raison.

---

## 5. Planning — `GET` et `PUT /api/v1/schedule`

Deux courbes seulement, `week` et `weekend` (cahier v2 §3). Points libres sur 24 h, **pas de
30 minutes**.

```json
{
  "schema_version": 1,
  "enabled": true,
  "timezone": "Europe/Paris",
  "revision": 7,
  "curves": {
    "week": [
      { "at": "06:30", "power": true,  "mode": "heat", "temperature_c": 21, "fan": "auto", "swing": false, "lock": false },
      { "at": "08:00", "power": false, "mode": "heat", "temperature_c": 21, "fan": "auto", "swing": false, "lock": true  },
      { "at": "22:30", "power": true,  "mode": "cool", "temperature_c": 26, "fan": "low",  "swing": false, "lock": false }
    ],
    "weekend": []
  }
}
```

Règles de lecture, appliquées à l'identique par l'interface et par l'ordonnanceur :

1. Un point **s'applique jusqu'au point suivant** — la courbe est un escalier, pas une interpolation.
2. Le dernier point de la journée déborde sur le début de la suivante. Une courbe non vide n'a donc
   jamais de trou.
3. Un point avec `power: false` éteint la clim ; ses autres champs sont conservés mais ignorés.
4. Une commande manuelle tient jusqu'au point suivant, puis le planning reprend la main.
5. Une courbe vide ne programme rien. `enabled: false` désactive les deux courbes.
6. `lock` n'a de sens que sur un point d'arrêt (`power: false`) : le boîtier force la valeur à `false`
   sur un point de marche plutôt que de rejeter la requête.

### `lock` — maintien de l'arrêt

Pendant un segment verrouillé, **toute trame reçue du récepteur IR** déclenche, après un délai, le
renvoi d'un ordre d'arrêt.

Le boîtier ne cherche pas à savoir si la télécommande demandait la marche : l'encodage du mode
n'étant pas prouvé (`unverified_fields`), interpréter la trame serait un pari. Réagir à toute trame
est robuste — si elle demandait déjà l'arrêt, le rappel est sans effet.

Contraintes que l'implémentation doit respecter :

| Point | Règle |
|---|---|
| Écho de sa propre émission | Le paquet est émis deux fois : le RX doit être ignoré au moins 1,5 s après chaque émission, sinon le verrou se déclenche sur lui-même, indéfiniment |
| Délai avant rappel | ≈ 2,5 s, le temps que l'unité ait fini de traiter la commande de la télécommande |
| Cadence maximale | Un rappel toutes les 8 s au plus, garde-fou |
| Origine de l'état | `state.source` vaut alors `lock` |
| Suspension | Une commande passée par l'API suspend le verrou jusqu'au point suivant, comme tout réglage manuel. Une trame de la télécommande, jamais. |

**Limite de principe, à énoncer dans l'interface :** l'infrarouge ne permet pas d'empêcher la clim de
recevoir la télécommande. Elle démarre, puis s'arrête. Et le boîtier ne réagit que s'il a *vu* la
trame : une télécommande pointée hors du champ du TSOP passe inaperçue. Un rappel périodique
inconditionnel couvrirait ce cas, au prix d'un bip de l'unité à chaque trame reçue — l'implémentation
de référence le prévoit, désactivé par défaut.

`PUT` accepte `expected_revision`, `enabled` et `curves`, et répond par le planning enregistré, ou
`409` avec le planning courant. Les points sont triés par heure croissante par le boîtier.

> L'heure des points est **locale**, exprimée en `HH:MM`. Le boîtier doit connaître son fuseau et le
> changement d'heure ; sans synchronisation SNTP, il n'exécute rien (`time.synced: false`).

---

## 6. `GET /api/v1/diagnostics/ir`

Réservé à l'administrateur. C'est le panneau qui répond à la question « est-ce que j'émets vraiment
ce que je crois émettre ? », en confrontant la trame émise à ce que le TSOP du même boîtier a relu.

```json
{
  "last_tx": { "at": "…", "bytes": "F2 0D 03 FC 01 50 00 00 51" },
  "last_rx": {
    "at": "…", "bytes": "F2 0D 03 FC 01 50 00 00 51",
    "protocol": "TOSHIBA_AC", "bits": 72,
    "checksum_valid": true, "source": "self"
  },
  "self_check": { "match": true, "checked_at": "…" },
  "counters": { "tx": 12, "rx_valid": 12, "rx_rejected": 0 }
}
```

`source` vaut `self` pour une trame relue de sa propre émission, `remote` pour la télécommande
d'origine. `bytes` est accepté en chaîne hexadécimale ou en tableau d'entiers.

Le plan §10.2 prévoit en plus les dix dernières captures : elles ne sont pas encore utilisées par
l'interface, elles peuvent être ajoutées à cette réponse sans la casser.

---

## 7. `GET /healthz`

Étendu par rapport au plan §10.2, à la suite du diagnostic de lenteur du 2 août : un symptôme réseau
ne se distingue d'un symptôme logiciel que si le boîtier expose de quoi les séparer.

```json
{
  "status": "ok", "uptime_s": 86400, "ir_rx": true, "ir_tx": true, "nvs": true,
  "reset_reason": "mise sous tension",
  "heap": { "free": 276284, "min_free": 269872, "largest_block": 262132 },
  "wifi": { "connected": true, "rssi_dbm": -59, "channel": 1, "bssid": "…",
            "tx_power_dbm": 20, "disconnects": 4, "reconnects": 0,
            "last_disconnect_reason": 201 },
  "ir": { "rx_valid": 12, "rx_rejected": 0, "rx_unknown": 0 },
  "loops_per_s": 24030
}
```

| Champ | Ce qu'il tranche |
|---|---|
| `heap.largest_block` | L'écart avec `free` mesure la fragmentation, qu'un simple « tas libre » masque |
| `loops_per_s` | Processeur accaparé ou non. Quelques milliers = sain, quelques dizaines = quelque chose mange tout |
| `ir.rx_unknown` | Bruit optique : un TSOP ébloui décode en continu sans rien reconnaître |
| `wifi.disconnects` / `last_disconnect_reason` | Liaison instable, avec le code de motif 802.11 |
| `reset_reason` | Distingue un `PANIC`, un watchdog et une chute d'alimentation d'un redémarrage voulu |

### Deux routes de mise au point

- `PUT /api/v1/diagnostics/rx` — corps `{"enabled": false}` : débraye le récepteur infrarouge à
  chaud. Permet de trancher par l'essai si on le soupçonne de peser sur le reste.
- `GET /api/v1/diagnostics/wifi` — fait balayer la bande **par le boîtier** : c'est son point de vue
  qui compte, pas celui du poste de travail. Bloque deux à trois secondes.

### Mise en cache

L'API répond `Cache-Control: no-store`. Les fichiers statiques, eux, portent un `ETag` — empreinte
de l'ensemble des fichiers, calculée au démarrage — et `Cache-Control: no-cache`, qui signifie
« revalider », pas « ne pas garder ». Le navigateur ne retélécharge l'interface que lorsqu'elle a
changé. Sur une liaison mediocre, la différence mesurée est de 14,8 s contre 0,5 s.

---

## 8. Codes d'erreur

Ceux du plan §10.3. Comportement de l'interface :

| Code | Réaction |
|---|---|
| `401` / `403` | Le panneau de diagnostic invite à saisir le jeton ; le reste continue de fonctionner |
| `409` | L'état ou le planning du boîtier est adopté, sans confirmation demandée |
| autre | Message d'erreur temporaire, l'état affiché revient à celui du boîtier |
| pas de réponse en 3 s | Boîtier marqué hors ligne, sa carte est désactivée |

---

## 9. Ce qu'il reste à trancher

- **Comment le jeton est obtenu.** L'interface se contente aujourd'hui de le stocker et de l'envoyer ;
  le plan §11.3 décrit une session par mot de passe, à réconcilier avec le jeton porteur du §4.4.
- **`/ws`** (plan §10.2) : l'interface interroge périodiquement plutôt que d'ouvrir un WebSocket.
  C'est suffisant à 5 secondes et cela supprime une dépendance ; le WebSocket reste utile si l'on veut
  voir la télécommande d'origine agir en moins d'une seconde (critère du plan §3.4).
- **OTA** : `POST /api/v1/system/ota` n'a pas encore d'écran.
