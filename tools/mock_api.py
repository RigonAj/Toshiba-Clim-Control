#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Maquette d'un boitier, pour mettre au point l'interface sans materiel.

Sert les fichiers statiques de `toshiba-climate-controller/web/` et implemente
l'API du document `docs/api-v2.md`. L'etat vit en memoire : il repart neuf a
chaque demarrage, comme un boitier sans NVS.

Deux instances simulent les deux boitiers symetriques du cahier v2 §4.1 :

    python tools/mock_api.py --port 8081 --name Salon   --peer "Chambre=http://127.0.0.1:8082"
    python tools/mock_api.py --port 8082 --name Chambre --peer "Salon=http://127.0.0.1:8081"

Puis ouvrir http://127.0.0.1:8081/. Le second boitier est declare dans
`config.json`, servi dynamiquement par cette maquette.

Ce fichier ne fait PAS partie du firmware. C'est un outil de mise au point,
au meme titre que les croquis de `ir-capture/`.
"""

import argparse
import json
import mimetypes
import os
import posixpath
import random
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

WEB_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "toshiba-climate-controller", "web")

START = time.time()


def now_iso():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


# --------------------------------------------------------------------------
# Encodage Toshiba : uniquement ce qui est MESURE (journal, 1er aout 2026).
# Prefixe F2 0D 03 FC 01, quartet haut de l'octet 5 = temperature - 17,
# checksum = OU exclusif des huit premiers octets. Les champs mode, ventilation
# et swing ne sont pas encore identifies : ils restent a zero ici.
# --------------------------------------------------------------------------

def encode_frame(state):
    b = [0xF2, 0x0D, 0x03, 0xFC, 0x01, 0x00, 0x00, 0x00]
    if state["power"]:
        b[5] = ((state["target_temperature_c"] - 17) & 0x0F) << 4
    chk = 0
    for x in b:
        chk ^= x
    return b + [chk]


def hexs(frame):
    return " ".join("%02X" % b for b in frame)


class Device(object):
    """Etat d'un boitier simule, protege par un verrou."""

    def __init__(self, dev_id, name):
        self.lock = threading.Lock()
        self.id = dev_id
        self.name = name
        self.state = {
            "power": False,
            "mode": "cool",
            "target_temperature_c": 22,
            "fan": "auto",
            "swing": False,
            "source": "restored",
            "confidence": "stale",       # etat restaure => stale (plan §9.2)
            "updated_at": now_iso(),
            "revision": 1,
        }
        self.capabilities = {
            "temperature_min_c": 17,
            "temperature_max_c": 30,
            "modes": ["auto", "cool", "heat", "dry"],
            "fans": ["auto", "low", "medium", "high"],
            "swing": True,
            # Champs dont l'encodage n'est pas encore prouve par la mesure.
            # L'interface les signale explicitement (campagne plan §8.2).
            "unverified_fields": ["mode", "fan", "swing"],
        }
        self.schedule = {
            "schema_version": 1,
            "enabled": True,
            "timezone": "Europe/Paris",
            "revision": 1,
            "curves": {
                "week": [
                    {"at": "06:30", "power": True,  "mode": "heat", "temperature_c": 21, "fan": "auto", "swing": False},
                    {"at": "08:00", "power": False, "mode": "heat", "temperature_c": 21, "fan": "auto", "swing": False},
                    {"at": "18:00", "power": True,  "mode": "cool", "temperature_c": 24, "fan": "auto", "swing": False},
                    {"at": "22:30", "power": True,  "mode": "cool", "temperature_c": 26, "fan": "low",  "swing": False},
                ],
                "weekend": [
                    {"at": "08:30", "power": True,  "mode": "cool", "temperature_c": 24, "fan": "auto", "swing": False},
                    {"at": "23:00", "power": True,  "mode": "cool", "temperature_c": 26, "fan": "low",  "swing": False},
                ],
            },
        }
        self.override = False
        self.counters = {"tx": 0, "rx_valid": 0, "rx_rejected": 0}
        self.last_tx = None
        self.last_rx = None
        self.self_check = None

    # -- planning ---------------------------------------------------------

    def active_curve(self):
        return "weekend" if datetime.now().weekday() >= 5 else "week"

    def current_point(self):
        """Point en vigueur : le dernier dont l'heure est passee, sinon le
        dernier de la journee - la courbe deborde sur le lendemain."""
        pts = sorted(self.schedule["curves"].get(self.active_curve(), []),
                     key=lambda p: p["at"])
        if not pts:
            return None
        now = datetime.now().strftime("%H:%M")
        passed = [p for p in pts if p["at"] <= now]
        return passed[-1] if passed else pts[-1]

    def lock_active(self):
        if not self.schedule["enabled"] or self.override:
            return False
        p = self.current_point()
        return bool(p) and not p.get("power", True) and bool(p.get("lock"))

    def next_point(self):
        pts = sorted(self.schedule["curves"].get(self.active_curve(), []),
                     key=lambda p: p["at"])
        if not pts:
            return None
        now = datetime.now().strftime("%H:%M")
        for p in pts:
            if p["at"] > now:
                return p["at"]
        return pts[0]["at"]

    # -- emission ---------------------------------------------------------

    def transmit(self):
        """Simule une emission IR et sa relecture par le TSOP du meme boitier.

        Sur le vrai boitier, `last_rx` viendra du recepteur, pas d'une copie :
        c'est precisement ce que ce panneau doit permettre de verifier.
        """
        frame = encode_frame(self.state)
        self.counters["tx"] += 1
        self.last_tx = {"at": now_iso(), "bytes": hexs(frame)}
        self.counters["rx_valid"] += 1
        self.last_rx = {
            "at": now_iso(), "bytes": hexs(frame), "protocol": "TOSHIBA_AC",
            "bits": 72, "checksum_valid": True, "source": "self",
        }
        self.self_check = {"match": True, "checked_at": now_iso()}

    def status(self):
        return {
            "schema_version": 1,
            "device_id": self.id,
            "display_name": self.name,
            "online": True,
            "state": dict(self.state),
            "capabilities": dict(self.capabilities),
            "schedule": {
                "enabled": self.schedule["enabled"],
                "active_curve": self.active_curve(),
                "next_point_at": self.next_point(),
                "override_active": self.override,
                "lock_active": self.lock_active(),
                "revision": self.schedule["revision"],
            },
            "time": {"now": now_iso(), "synced": True},
            "wifi": {"rssi_dbm": random.randint(-70, -45), "ip": "127.0.0.1"},
            "firmware": "0.0.0-mock",
        }


class Handler(BaseHTTPRequestHandler):
    server_version = "clim-mock/1"
    protocol_version = "HTTP/1.1"

    # -- utilitaires ------------------------------------------------------

    def log_message(self, fmt, *args):
        if self.server.verbose:
            BaseHTTPRequestHandler.log_message(self, fmt, *args)

    def cors(self):
        # Boitiers symetriques : la page servie par l'un appelle l'autre,
        # donc requetes inter-origines (cahier v2 §4.1).
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.send_header("Access-Control-Allow-Methods", "GET, PUT, POST, OPTIONS")

    def send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.cors()
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
            return json.loads(self.rfile.read(n).decode("utf-8")) if n else {}
        except Exception:
            return None

    # -- routage ----------------------------------------------------------

    def do_OPTIONS(self):
        self.send_response(204)
        self.cors()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        dev = self.server.device
        path = self.path.split("?", 1)[0]

        if path == "/healthz":
            return self.send_json({"status": "ok", "uptime_s": int(time.time() - START),
                                   "ir_rx": True, "ir_tx": True, "nvs": True})

        if path == "/api/v1/status":
            with dev.lock:
                return self.send_json(dev.status())

        if path == "/api/v1/schedule":
            with dev.lock:
                return self.send_json(dev.schedule)

        if path == "/api/v1/diagnostics/ir":
            with dev.lock:
                return self.send_json({
                    "last_tx": dev.last_tx,
                    "last_rx": dev.last_rx,
                    "self_check": dev.self_check,
                    "counters": dict(dev.counters),
                    "note": "Maquette : la trame relue est une copie de la trame emise. "
                            "Sur le boitier reel elle vient du TSOP.",
                })

        if path == "/config.json":
            return self.send_json({"devices": self.server.device_list})

        return self.serve_static(path)

    def do_PUT(self):
        dev = self.server.device
        path = self.path.split("?", 1)[0]
        body = self.read_json()
        if body is None:
            return self.send_json({"error": "json invalide"}, 400)

        if path == "/api/v1/climate":
            with dev.lock:
                exp = body.get("expected_revision")
                if exp is not None and exp != dev.state["revision"]:
                    return self.send_json({"accepted": False, "reason": "revision obsolete",
                                           "state": dict(dev.state)}, 409)
                caps = dev.capabilities
                t = body.get("target_temperature_c", dev.state["target_temperature_c"])
                if not isinstance(t, int) or not (caps["temperature_min_c"] <= t <= caps["temperature_max_c"]):
                    return self.send_json({"error": "temperature hors plage"}, 400)
                if body.get("mode", dev.state["mode"]) not in caps["modes"]:
                    return self.send_json({"error": "mode inconnu"}, 400)
                if body.get("fan", dev.state["fan"]) not in caps["fans"]:
                    return self.send_json({"error": "ventilation inconnue"}, 400)

                dev.state.update({
                    "power": bool(body.get("power", dev.state["power"])),
                    "mode": body.get("mode", dev.state["mode"]),
                    "target_temperature_c": t,
                    "fan": body.get("fan", dev.state["fan"]),
                    "swing": bool(body.get("swing", dev.state["swing"])),
                    "source": "web",
                    "confidence": "fresh",
                    "updated_at": now_iso(),
                    "revision": dev.state["revision"] + 1,
                })
                dev.override = dev.schedule["enabled"]
                dev.transmit()
                return self.send_json({"accepted": True, "transmitted": True,
                                       "confirmed_by_ac": False, "state": dict(dev.state)})

        if path == "/api/v1/schedule":
            with dev.lock:
                exp = body.get("expected_revision")
                if exp is not None and exp != dev.schedule["revision"]:
                    return self.send_json(dev.schedule, 409)
                curves = body.get("curves") or {}
                for name in ("week", "weekend"):
                    for p in curves.get(name, []):
                        if not isinstance(p.get("at"), str):
                            return self.send_json({"error": "point sans heure"}, 400)
                dev.schedule["enabled"] = bool(body.get("enabled", dev.schedule["enabled"]))
                dev.schedule["curves"] = {
                    "week": sorted(curves.get("week", []), key=lambda p: p["at"]),
                    "weekend": sorted(curves.get("weekend", []), key=lambda p: p["at"]),
                }
                dev.schedule["revision"] += 1
                dev.override = False
                return self.send_json(dev.schedule)

        return self.send_json({"error": "inconnu"}, 404)

    # -- fichiers statiques ----------------------------------------------

    def serve_static(self, path):
        rel = posixpath.normpath(path).lstrip("/")
        if rel in ("", "."):
            rel = "index.html"
        full = os.path.join(WEB_DIR, rel.replace("/", os.sep))
        if not os.path.abspath(full).startswith(os.path.abspath(WEB_DIR)) or not os.path.isfile(full):
            self.send_error(404, "fichier absent")
            return
        ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
        if ctype.startswith("text/") or ctype in ("application/javascript", "application/json"):
            ctype += "; charset=utf-8"
        with open(full, "rb") as fh:
            data = fh.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.cors()
        self.end_headers()
        self.wfile.write(data)


def main():
    ap = argparse.ArgumentParser(description="Maquette d'un boitier de climatisation")
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--name", default="Salon", help="nom affiche du boitier servi")
    ap.add_argument("--id", default=None, help="identifiant, par defaut derive du nom")
    ap.add_argument("--peer", action="append", default=[],
                    help='autre boitier a declarer, "Nom=http://adresse" (repetable)')
    ap.add_argument("-v", "--verbose", action="store_true", help="journaliser chaque requete")
    args = ap.parse_args()

    dev_id = args.id or args.name.lower()
    device = Device(dev_id, args.name)

    device_list = [{"id": dev_id, "name": args.name, "base": ""}]
    for spec in args.peer:
        name, _, base = spec.partition("=")
        device_list.append({"id": name.lower(), "name": name, "base": base.rstrip("/")})

    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    srv.device = device
    srv.device_list = device_list
    srv.verbose = args.verbose

    print("Boitier simule  : %s" % args.name)
    print("Fichiers servis : %s" % WEB_DIR)
    print("Interface       : http://127.0.0.1:%d/" % args.port)
    if len(device_list) > 1:
        print("Boitiers declares : %s" % ", ".join(d["name"] for d in device_list))
    print("Ctrl+C pour arreter.")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nArret.")


if __name__ == "__main__":
    main()
