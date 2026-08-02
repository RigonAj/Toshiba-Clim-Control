# Captures de la télécommande WH-TG01NE

Ce dossier contient des **mesures physiques**. C'est le contenu le plus précieux du projet : le code se réécrit, les captures ne se reproduisent qu'avec la télécommande, la carte et le montage sous la main. Elles servent ensuite de vecteurs de test du codec (plan §14.1).

À versionner et à sauvegarder systématiquement.

## Organisation

```text
captures/
├── README.md
└── raw/
    ├── cool_22_auto_off-1.json
    ├── cool_22_auto_off-2.json
    └── ...
```

Un fichier par pression de touche. **Cinq échantillons par commande**, espacés d'au moins deux secondes (plan §8.2).

## Convention de nommage

`<identifiant>-<numéro d'échantillon>.json`, où l'identifiant vient de la matrice du plan §8.2 :

| Identifiant | Commande |
|---|---|
| `off` | Arrêt |
| `cool_17_auto_off` | Froid, 17 °C, ventilation auto, swing arrêté |
| `cool_22_auto_off` | Froid, 22 °C, ventilation auto, swing arrêté |
| `cool_30_auto_off` | Froid, 30 °C, ventilation auto, swing arrêté |
| `heat_17_auto_off` | Chauffage, 17 °C |
| `heat_22_auto_off` | Chauffage, 22 °C |
| `heat_30_auto_off` | Chauffage, 30 °C |
| `auto_22_auto_off` | Auto, 22 °C |
| `dry_22_auto_off` | Déshumidification, 22 °C |
| `cool_22_fan_N_off` | Une capture par vitesse de ventilation |
| `cool_22_auto_on` | Swing activé |
| `cool_22_auto_off_2` | Swing désactivé après activation |

## Format d'un fichier

Format défini au plan §8.1 :

```json
{
  "schema_version": 1,
  "device": "WH-TG01NE",
  "label": "cool_22_auto_off",
  "sample": 1,
  "captured_at": "2026-08-01T14:32:00Z",
  "carrier_hz_assumed": 38000,
  "durations_us": [4380, 4370, 540, 1620, 540, 540],
  "decoded_bytes": [],
  "checksum_valid": false
}
```

Le tableau `durations_us` se remplit avec la sortie `uint16_t rawData[]` affichée par `ir-capture` sur la console.

## Conditions de mesure

- Piles de la télécommande neuves ou vérifiées.
- Télécommande à **20–50 cm** du TSOP38438.
- Pas de soleil direct ni d'halogène dans le champ du récepteur.
- **L'étage d'émission ne doit pas être monté** pendant cette phase : il pollue les captures (plan, phase 1a).
- Noter dans `label` l'état réellement affiché sur l'écran de la télécommande, pas celui qu'on croit avoir envoyé.

## Tri

Écarter et ne pas versionner :

- les captures marquées `ATTENTION : depassement de tampon` ;
- les captures dont la longueur diffère nettement des quatre autres échantillons de la même commande.

Une capture tronquée prise pour une variante de protocole est un risque identifié au plan §15.
