#!/usr/bin/env python3
"""Convertit un journal de console `ir-capture` en fichiers JSON de capture.

Format de sortie : plan §8.1. Un fichier par trame décodée.

Usage :
    python tools/capture_export.py <journal.txt> --label cool_22_auto_off

Les trames non reconnues (`UNKNOWN`) sont écartées par défaut, conformément à
l'étape 1 de la méthode d'analyse du §8.3 : une capture tronquée prise pour une
variante de protocole est un risque identifié au §15. Utiliser --keep-unknown
pour les conserver dans captures/rejected/.
"""

import argparse
import json
import pathlib
import re
import sys
from datetime import datetime, timezone

BLOCK = re.compile(r"^=+\s*$\s*^TRAME #(\d+)", re.M)
PROTO = re.compile(r"^Protocole\s*:\s*(\S+)", re.M)
BITS = re.compile(r"^Longueur\s*:\s*(\d+) bits", re.M)
RAW = re.compile(r"uint16_t rawData\[\d+\]\s*=\s*\{([^}]*)\}", re.S)
STATE = re.compile(r"uint8_t state\[\d+\]\s*=\s*\{([^}]*)\}", re.S)


def xor_checksum(data):
    """Checksum Toshiba : OU exclusif de tous les octets sauf le dernier.

    Dérivé par comparaison différentielle de deux trames ne différant que par
    la consigne de température (§8.3, étapes 7 à 9).
    """
    acc = 0
    for byte in data[:-1]:
        acc ^= byte
    return acc


def split_frames(text):
    starts = [m.start() for m in BLOCK.finditer(text)]
    for i, start in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else len(text)
        yield text[start:end]


def parse_frame(chunk):
    proto = PROTO.search(chunk)
    raw = RAW.search(chunk)
    if not proto or not raw:
        return None

    durations = [int(v) for v in raw.group(1).replace("\n", " ").split(",") if v.strip()]

    state = None
    m = STATE.search(chunk)
    if m:
        state = [int(v, 16) for v in m.group(1).replace("\n", " ").split(",") if v.strip()]

    bits = BITS.search(chunk)

    return {
        "protocol": proto.group(1),
        "bits": int(bits.group(1)) if bits else None,
        "durations_us": durations,
        "state": state,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=pathlib.Path)
    ap.add_argument("--label", required=True, help="identifiant de la matrice §8.2")
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("captures/raw"))
    ap.add_argument("--keep-unknown", action="store_true")
    args = ap.parse_args()

    text = args.log.read_text(encoding="utf-8", errors="replace")
    captured_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")

    args.out.mkdir(parents=True, exist_ok=True)
    rejected_dir = args.out.parent / "rejected"

    sample = 0
    rejected = 0
    states_seen = {}

    for chunk in split_frames(text):
        frame = parse_frame(chunk)
        if frame is None:
            continue

        known = frame["protocol"] != "UNKNOWN"
        if not known and not args.keep_unknown:
            rejected += 1
            continue

        state = frame["state"]
        checksum_valid = False
        if state:
            checksum_valid = xor_checksum(state) == state[-1]
            key = tuple(state)
            states_seen[key] = states_seen.get(key, 0) + 1

        sample += 1
        record = {
            "schema_version": 1,
            "device": "WH-TG01NE",
            "label": args.label,
            "sample": sample,
            "captured_at": captured_at,
            "carrier_hz_assumed": 38000,
            "protocol": frame["protocol"],
            "bits": frame["bits"],
            "durations_us": frame["durations_us"],
            "decoded_bytes": state or [],
            "checksum_valid": checksum_valid,
        }

        target = args.out if known else rejected_dir
        target.mkdir(parents=True, exist_ok=True)
        path = target / f"{args.label}-{sample}.json"
        path.write_text(json.dumps(record, indent=2), encoding="utf-8")
        print(f"  ecrit {path}")

    print()
    print(f"{sample} capture(s) exportee(s), {rejected} ecartee(s) (non reconnues)")

    if states_seen:
        print("\nEtats distincts rencontres :")
        for state, count in states_seen.items():
            hexed = " ".join(f"{b:02X}" for b in state)
            ok = "OK" if xor_checksum(list(state)) == state[-1] else "INVALIDE"
            print(f"  {hexed}   x{count}   checksum {ok}")
        if len(states_seen) > 1:
            print("\nATTENTION : plusieurs commandes differentes dans ce journal.")
            print("Pour la campagne du §8.2, capturer une commande a la fois.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
