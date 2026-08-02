// ---------------------------------------------------------------------------
// Interface Web sur la carte - banc d'essai avant le firmware ESP-IDF.
//
// Ce croquis sert les fichiers de `toshiba-climate-controller/web/` depuis la
// memoire flash de la carte et implemente les cinq routes de docs/api-v2.md.
// Il rend donc la page accessible depuis un telephone du reseau local, ce que
// la maquette Python (tools/mock_api.py) ne pouvait pas faire.
//
// Ce qu'il apporte en plus de la maquette :
//   - chaque commande de la page EMET une vraie trame TOSHIBA_AC sur la LED IR ;
//   - le TSOP de la meme carte la relit, et la comparaison octet par octet
//     remonte dans le panneau « Diagnostic infrarouge » de l'interface.
//     C'est le controle du plan §14.2 - verifier que l'on emet bien ce que
//     l'on croit - sans console serie.
//
// Ce qu'il n'est PAS : le firmware definitif. Celui-ci sera en ESP-IDF, avec
// OTA, authentification et NVS versionnee (plan §7). Ici, pas de mot de passe :
// a n'utiliser que sur le reseau domestique.
//
// CABLAGE - identique a tx_loopback.cpp, les deux etages montes en meme temps :
//   emission (§6.1)  5V -> 220 ohms -> anode LED IR
//                    cathode LED IR -> collecteur PN2222A
//                    D1 (GPIO2)     -> resistance de base -> base
//                    emetteur       -> GND
//   reception        TSOP VS  -> D3 (GPIO4), sortie a l'etat haut
//                    TSOP GND -> GND
//                    TSOP OUT -> D4 (GPIO5)
//
// Ne pas coller le TSOP a la LED : a moins de 10 cm son AGC sature et la
// trame ressort deformee. Viser 20-50 cm.
//
// PREPARATION
//   1. copier include/secrets.example.h en include/secrets.h et le remplir ;
//   2. televerser les fichiers de l'interface :  pio run -e webui -t uploadfs
//   3. televerser le firmware :                  pio run -e webui -t upload
//   4. lire l'adresse IP sur la console :        pio device monitor
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <esp_system.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Toshiba.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #error "Copier include/secrets.example.h en include/secrets.h et y mettre le SSID et le mot de passe du Wi-Fi."
#endif

// --- Brochage (plan §6.0, cablage provisoire du README §4) -----------------

const uint16_t kPinTx    = 2;   // D1 - base du transistor
const uint8_t  kPowerPin = 4;   // D3 - alimente VS du TSOP
const uint16_t kRecvPin  = 5;   // D4 - lit OUT du TSOP
const uint8_t  kLedPin   = 21;  // LED de la carte, allumee a l'etat BAS

// --- Reception -------------------------------------------------------------

const uint16_t kCaptureBufferSize = 1024;
const uint8_t  kTimeout           = 50;   // ms, fin de trame
const uint32_t kDecodeWindowMs    = 400;  // attente du retour apres emission

// --- Divers ----------------------------------------------------------------

const char *kFirmware = "0.2.0-webui";
const char *kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";  // Europe/Paris
const size_t kMaxScheduleBytes = 3500;                 // marge sous la limite NVS
const uint32_t kSchedCheckMs = 5000;                   // cadence d'evaluation du planning

// --- Verrouillage « maintenir eteint » -------------------------------------
//
// L'infrarouge ne permet PAS d'empecher la clim de recevoir la telecommande :
// elle s'allumera. Le boitier ne peut que renvoyer l'ordre d'arret ensuite.
// C'est donc « elle s'eteint aussitot », jamais « elle ne s'allume pas ».

// Delai avant de renvoyer l'arret : laisser l'unite finir de reagir a la
// commande de la telecommande, sinon elle peut ignorer la seconde trame.
const uint32_t kLockReassertDelayMs = 2500;

// Silence minimal entre deux rappels, garde-fou contre l'emballement.
const uint32_t kLockMinIntervalMs = 8000;

// Rappel periodique meme sans rien avoir vu passer. Couvre le cas ou la
// telecommande a ete pointee hors du champ du TSOP. DESACTIVE par defaut (0) :
// la plupart des Toshiba emettent un bip a chaque trame recue, et un bip
// toutes les dix minutes dans une chambre serait pire que le mal.
const uint32_t kLockSweepMs = 0;

IRToshibaAC ac(kPinTx);
IRrecv      irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
WebServer   server(80);
Preferences prefs;
decode_results results;

// --- Etat estime (plan §9.1) ----------------------------------------------

struct ClimateState {
  bool     power   = false;
  String   mode    = "cool";
  uint8_t  temp    = 22;
  String   fan     = "auto";
  bool     swing   = false;
  // Vide au demarrage : ce croquis ne conserve pas l'etat en NVS, il n'y a donc
  // rien de « restaure » a annoncer. L'interface n'affiche alors aucune origine.
  String   source  = "";
  String   confidence = "unknown";
  time_t   updated = 0;
  uint32_t revision = 1;
} state;

// --- Planning --------------------------------------------------------------

String   scheduleJson;
bool     overrideActive = false;
int      appliedPointMinutes = -1;   // point de consigne actuellement en vigueur
bool     schedulePrimed = false;     // voir evaluateSchedule()

// --- Verrouillage ----------------------------------------------------------

bool     lockActive = false;         // le segment en cours est marque « lock »
uint32_t rxIgnoreUntil = 0;          // fenetre pendant laquelle le RX est notre echo
uint32_t lockPendingAt = 0;          // date du rappel d'arret a envoyer (0 = aucun)
uint32_t lastLockAssert = 0;
uint32_t counterLockAsserts = 0;

// --- Diagnostic ------------------------------------------------------------

String   lastTxBytes, lastRxBytes, lastRxProtocol, lastRxSource;
time_t   lastTxAt = 0, lastRxAt = 0, lastCheckAt = 0;
uint16_t lastRxBits = 0;
bool     lastRxChecksumOk = false;
int8_t   lastMatch = -1;             // -1 inconnu, 0 ecart, 1 identique
uint32_t counterTx = 0, counterRxValid = 0, counterRxRejected = 0;

// Decodages NON reconnus. Ils n'etaient comptes nulle part, ce qui rendait
// invisible le scenario « le TSOP voit du bruit en permanence » - lumiere du
// jour, halogene, autre telecommande. Or ce bruit coute du temps processeur a
// chaque passage du decodeur (plan §5.3).
uint32_t counterRxUnknown = 0;
bool     rxEnabled = true;   // debrayable a chaud, pour trancher par l'essai

// --- Sante ----------------------------------------------------------------

uint32_t wifiDisconnects = 0, wifiReconnects = 0;
uint8_t  lastDisconnectReason = 0;
uint32_t wifiDownSince = 0;
uint32_t loopCount = 0, loopWindowAt = 0, loopsPerSec = 0;

// ===========================================================================
// Utilitaires
// ===========================================================================

String isoTime(time_t t) {
  if (t < 1600000000) return String();       // heure jamais synchronisee
  struct tm g;
  gmtime_r(&t, &g);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
  return String(buf);
}

bool timeSynced() { return time(nullptr) > 1600000000; }

String hexBytes(const uint8_t *data, uint16_t len) {
  String out;
  out.reserve(len * 3);
  char buf[4];
  for (uint16_t i = 0; i < len; i++) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    if (i) out += ' ';
    out += buf;
  }
  return out;
}

// Traduction vers les constantes de la bibliotheque. ATTENTION : seul
// l'encodage de la temperature est MESURE (journal, 1er aout 2026). Le mode,
// la ventilation et le swing viennent du modele « Remote A » d'IRremoteESP8266
// et restent des hypotheses tant que la campagne du §8.2 n'a pas eu lieu :
// c'est pourquoi l'API les declare dans `unverified_fields`.
uint8_t modeToLib(const String &m) {
  if (m == "cool") return kToshibaAcCool;
  if (m == "heat") return kToshibaAcHeat;
  if (m == "dry")  return kToshibaAcDry;
  return kToshibaAcAuto;
}

uint8_t fanToLib(const String &f) {
  if (f == "low")    return 1;
  if (f == "medium") return 3;
  if (f == "high")   return 5;
  return 0;  // auto
}

// ===========================================================================
// Emission et controle par le TSOP
// ===========================================================================

// Emet l'etat courant, puis ecoute sa propre emission et compare. C'est la
// boucle de tx_loopback.cpp, ramenee a une fonction et exposee par HTTP.
void transmitAndVerify() {
  ac.setPower(state.power);
  ac.setMode(modeToLib(state.mode));
  ac.setTemp(state.temp);
  ac.setFan(fanToLib(state.fan));
  ac.setSwing(state.swing ? kToshibaAcSwingOn : kToshibaAcSwingOff);

  const uint8_t *raw = ac.getRaw();
  const uint16_t len = ac.getStateLength();

  digitalWrite(kLedPin, LOW);          // flash a chaque emission (plan §6.3)

  irrecv.resume();                     // vider ce qui traine avant d'emettre
  ac.send();

  lastTxBytes = hexBytes(raw, len);
  lastTxAt    = time(nullptr);
  counterTx++;

  lastMatch = -1;
  uint32_t start = millis();
  while (rxEnabled && millis() - start < kDecodeWindowMs) {
    if (!irrecv.decode(&results)) {
      delay(1);
      continue;
    }
    if (results.decode_type == decode_type_t::TOSHIBA_AC) {
      lastRxBytes      = hexBytes(results.state, results.bits / 8);
      lastRxBits       = results.bits;
      lastRxProtocol   = "TOSHIBA_AC";
      lastRxSource     = "self";
      lastRxAt         = time(nullptr);
      lastRxChecksumOk = true;         // le decodeur rejette deja un checksum faux
      counterRxValid++;
      lastMatch = (lastRxBytes == lastTxBytes) ? 1 : 0;
    } else {
      counterRxRejected++;
      lastMatch = 0;
    }
    lastCheckAt = time(nullptr);
    break;
  }
  if (rxEnabled && lastMatch < 0) {     // rien recu dans la fenetre
    counterRxRejected++;
    lastCheckAt = time(nullptr);
    lastMatch = 0;
  }

  digitalWrite(kLedPin, HIGH);
  irrecv.resume();

  // Le paquet est emis deux fois : la copie qui traine encore ne doit pas etre
  // prise pour un appui sur la telecommande, sans quoi le verrou se
  // declencherait sur son propre echo, indefiniment (plan §9.2).
  rxIgnoreUntil = millis() + 1500;
}

// ===========================================================================
// Planning
// ===========================================================================

const char *kDefaultSchedule =
  "{\"schema_version\":1,\"enabled\":false,\"timezone\":\"Europe/Paris\","
  "\"revision\":1,\"curves\":{\"week\":[],\"weekend\":[]}}";

void loadSchedule() {
  scheduleJson = prefs.getString("sched", "");
  if (scheduleJson.length() < 10) scheduleJson = kDefaultSchedule;
}

void saveSchedule() { prefs.putString("sched", scheduleJson); }

int minutesOf(const char *hhmm) {
  if (!hhmm || strlen(hhmm) < 4) return -1;
  int h = atoi(hhmm);
  const char *colon = strchr(hhmm, ':');
  int m = colon ? atoi(colon + 1) : 0;
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

const char *activeCurveName() {
  struct tm ti;
  if (!getLocalTime(&ti)) return "week";
  return (ti.tm_wday == 0 || ti.tm_wday == 6) ? "weekend" : "week";
}

// Point en vigueur a `nowMin` : le dernier point dont l'heure est <= nowMin,
// ou, si aucun, le dernier de la journee - la courbe est un escalier qui
// deborde sur le lendemain (api-v2.md §5).
int findCurrentPoint(JsonArray pts, int nowMin) {
  int best = -1, bestMin = -1, lastIdx = -1, lastMin = -1;
  for (size_t i = 0; i < pts.size(); i++) {
    int m = minutesOf(pts[i]["at"] | "");
    if (m < 0) continue;
    if (m > lastMin) { lastMin = m; lastIdx = (int)i; }
    if (m <= nowMin && m > bestMin) { bestMin = m; best = (int)i; }
  }
  return best >= 0 ? best : lastIdx;
}

String nextPointAt() {
  JsonDocument doc;
  if (deserializeJson(doc, scheduleJson)) return String();
  JsonArray pts = doc["curves"][activeCurveName()].as<JsonArray>();
  if (pts.isNull() || pts.size() == 0) return String();

  struct tm ti;
  if (!getLocalTime(&ti)) return String();
  int nowMin = ti.tm_hour * 60 + ti.tm_min;

  int bestMin = -1, firstMin = -1;
  for (JsonObject p : pts) {
    int m = minutesOf(p["at"] | "");
    if (m < 0) continue;
    if (firstMin < 0 || m < firstMin) firstMin = m;
    if (m > nowMin && (bestMin < 0 || m < bestMin)) bestMin = m;
  }
  int target = bestMin >= 0 ? bestMin : firstMin;
  if (target < 0) return String();
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", target / 60, target % 60);
  return String(buf);
}

// Applique le planning. Une commande manuelle tient jusqu'au point suivant
// (cahier v2 §3) : c'est le CHANGEMENT de point en vigueur qui reprend la
// main, pas le simple fait qu'un planning soit actif.
void evaluateSchedule() {
  lockActive = false;              // recalcule a chaque passage, jamais suppose
  if (!timeSynced()) return;

  JsonDocument doc;
  if (deserializeJson(doc, scheduleJson)) return;
  if (!(doc["enabled"] | false)) { appliedPointMinutes = -1; return; }

  JsonArray pts = doc["curves"][activeCurveName()].as<JsonArray>();
  if (pts.isNull() || pts.size() == 0) return;

  struct tm ti;
  if (!getLocalTime(&ti)) return;
  int idx = findCurrentPoint(pts, ti.tm_hour * 60 + ti.tm_min);
  if (idx < 0) return;

  // Le verrou appartient au SEGMENT en cours, pas a l'instant de transition :
  // il doit donc etre relu a chaque passage, y compris quand rien ne change.
  lockActive = !(pts[idx]["power"] | true) && (pts[idx]["lock"] | false);

  int at = minutesOf(pts[idx]["at"] | "");
  if (at == appliedPointMinutes) return;
  appliedPointMinutes = at;

  // Au premier passage on note seulement quel point s'applique, sans emettre :
  // un redemarrage ne doit pas envoyer une commande que personne n'a demandee.
  if (!schedulePrimed) { schedulePrimed = true; return; }

  JsonObject p = pts[idx];
  state.power = p["power"] | true;
  if (state.power) {
    state.mode  = String((const char *)(p["mode"] | "auto"));
    state.temp  = (uint8_t)(p["temperature_c"] | 22);
    state.fan   = String((const char *)(p["fan"] | "auto"));
    state.swing = p["swing"] | false;
  }
  state.source     = "schedule";
  state.confidence = "fresh";
  state.updated    = time(nullptr);
  state.revision++;
  overrideActive   = false;

  Serial.printf("[planning] point %02d:%02d applique\n", at / 60, at % 60);
  transmitAndVerify();
}

// Le verrou est-il en vigueur a cet instant ? Une commande passee depuis la
// PAGE le suspend jusqu'au point suivant, comme n'importe quel reglage manuel
// (cahier v2 §3) : l'interface reste maitresse. La telecommande, elle, ne le
// suspend jamais - c'est tout l'objet de la fonction.
bool lockInForce() { return lockActive && !overrideActive; }

void assertLockOff(const char *why) {
  lastLockAssert = millis();
  if (!lastLockAssert) lastLockAssert = 1;
  counterLockAsserts++;
  state.power      = false;
  state.source     = "lock";
  state.confidence = "fresh";
  state.updated    = time(nullptr);
  state.revision++;
  Serial.printf("[verrou] arret renvoye (%s)\n", why);
  transmitAndVerify();
}

// ===========================================================================
// HTTP
// ===========================================================================

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  server.sendHeader("Access-Control-Allow-Methods", "GET, PUT, POST, OPTIONS");
}

void sendJson(int code, const String &body) {
  sendCors();
  server.sendHeader("Cache-Control", "no-store");   // l'etat, jamais en cache
  server.send(code, "application/json; charset=utf-8", body);
}

void sendError(int code, const char *msg) {
  JsonDocument doc;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  sendJson(code, out);
}

void fillState(JsonObject s) {
  s["power"] = state.power;
  s["mode"] = state.mode;
  s["target_temperature_c"] = state.temp;
  s["fan"] = state.fan;
  s["swing"] = state.swing;
  if (state.source.length()) s["source"] = state.source;
  s["confidence"] = state.confidence;
  String at = isoTime(state.updated);
  if (at.length()) s["updated_at"] = at;
  s["revision"] = state.revision;
}

void handleStatus() {
  // Fraicheur de l'etat (plan §9.2) : quinze minutes.
  if (state.updated && timeSynced()) {
    state.confidence = (time(nullptr) - state.updated < 900) ? "fresh" : "stale";
  } else if (!timeSynced()) {
    state.confidence = "unknown";
  }

  JsonDocument doc;
  doc["schema_version"] = 1;
  doc["device_id"] = CLIM_DEVICE_ID;
  doc["display_name"] = CLIM_DEVICE_NAME;
  doc["online"] = true;
  fillState(doc["state"].to<JsonObject>());

  JsonObject caps = doc["capabilities"].to<JsonObject>();
  caps["temperature_min_c"] = 17;
  caps["temperature_max_c"] = 30;
  JsonArray modes = caps["modes"].to<JsonArray>();
  modes.add("auto"); modes.add("cool"); modes.add("heat"); modes.add("dry");
  JsonArray fans = caps["fans"].to<JsonArray>();
  fans.add("auto"); fans.add("low"); fans.add("medium"); fans.add("high");
  caps["swing"] = true;
  // Tant que la campagne du plan §8.2 n'a pas mesure ces encodages, ils sont
  // annonces comme non prouves : l'interface l'affiche en clair.
  JsonArray unver = caps["unverified_fields"].to<JsonArray>();
  unver.add("mode"); unver.add("fan"); unver.add("swing");

  JsonDocument sched;
  bool schedOk = !deserializeJson(sched, scheduleJson);
  JsonObject so = doc["schedule"].to<JsonObject>();
  so["enabled"] = schedOk ? (bool)(sched["enabled"] | false) : false;
  so["active_curve"] = activeCurveName();
  String np = nextPointAt();
  if (np.length()) so["next_point_at"] = np;
  so["override_active"] = overrideActive;
  so["lock_active"] = lockInForce();
  so["revision"] = schedOk ? (uint32_t)(sched["revision"] | 1) : 1;

  JsonObject tm_ = doc["time"].to<JsonObject>();
  String now = isoTime(time(nullptr));
  if (now.length()) tm_["now"] = now;
  tm_["synced"] = timeSynced();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["rssi_dbm"] = WiFi.RSSI();
  wifi["ip"] = WiFi.localIP().toString();

  doc["firmware"] = kFirmware;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleClimatePut() {
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain"))) return sendError(400, "json invalide");

  if (!body["expected_revision"].isNull()) {
    uint32_t exp = body["expected_revision"];
    if (exp != state.revision) {
      JsonDocument doc;
      doc["accepted"] = false;
      doc["reason"] = "revision obsolete";
      fillState(doc["state"].to<JsonObject>());
      String out;
      serializeJson(doc, out);
      return sendJson(409, out);
    }
  }

  String mode = body["mode"] | state.mode;
  String fan  = body["fan"]  | state.fan;
  int    temp = body["target_temperature_c"] | (int)state.temp;
  if (temp < 17 || temp > 30) return sendError(400, "temperature hors plage");
  if (mode != "auto" && mode != "cool" && mode != "heat" && mode != "dry")
    return sendError(400, "mode inconnu");
  if (fan != "auto" && fan != "low" && fan != "medium" && fan != "high")
    return sendError(400, "ventilation inconnue");

  state.power = body["power"] | state.power;
  state.mode  = mode;
  state.temp  = (uint8_t)temp;
  state.fan   = fan;
  state.swing = body["swing"] | state.swing;
  state.source     = "web";
  state.confidence = "fresh";
  state.updated    = time(nullptr);
  state.revision++;
  overrideActive   = true;   // tient jusqu'au prochain point de consigne

  transmitAndVerify();

  JsonDocument doc;
  doc["accepted"] = true;
  doc["transmitted"] = true;
  doc["confirmed_by_ac"] = false;   // l'IR ne confirme jamais (plan §9.3)
  fillState(doc["state"].to<JsonObject>());
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleScheduleGet() { sendJson(200, scheduleJson); }

void handleSchedulePut() {
  const String &raw = server.arg("plain");
  if (raw.length() > kMaxScheduleBytes) return sendError(413, "planning trop grand");

  JsonDocument body;
  if (deserializeJson(body, raw)) return sendError(400, "json invalide");

  JsonDocument cur;
  bool curOk = !deserializeJson(cur, scheduleJson);
  uint32_t rev = curOk ? (uint32_t)(cur["revision"] | 1) : 1;

  if (!body["expected_revision"].isNull()) {
    uint32_t exp = body["expected_revision"];
    if (exp != rev) return sendJson(409, scheduleJson);
  }

  JsonDocument out;
  out["schema_version"] = 1;
  out["enabled"] = body["enabled"] | false;
  out["timezone"] = "Europe/Paris";
  out["revision"] = rev + 1;
  JsonObject curves = out["curves"].to<JsonObject>();
  const char *names[2] = {"week", "weekend"};
  for (uint8_t c = 0; c < 2; c++) {
    JsonArray dst = curves[names[c]].to<JsonArray>();
    JsonArray src = body["curves"][names[c]].as<JsonArray>();
    if (src.isNull()) continue;
    for (JsonObject p : src) {
      int m = minutesOf(p["at"] | "");
      if (m < 0) return sendError(400, "point sans heure valide");
      JsonObject o = dst.add<JsonObject>();
      o["at"] = p["at"];
      o["power"] = p["power"] | true;
      o["mode"] = p["mode"] | "auto";
      o["temperature_c"] = p["temperature_c"] | 22;
      o["fan"] = p["fan"] | "auto";
      o["swing"] = p["swing"] | false;
      // « Maintenir eteint » : n'a de sens que sur un point d'arret.
      o["lock"] = (p["lock"] | false) && !(p["power"] | true);
    }
  }

  String serialized;
  serializeJson(out, serialized);
  if (serialized.length() > kMaxScheduleBytes) return sendError(413, "planning trop grand");

  scheduleJson = serialized;
  saveSchedule();
  appliedPointMinutes = -1;   // le point en vigueur est a recalculer
  schedulePrimed = true;      // ... mais sans emettre pour autant
  overrideActive = false;
  sendJson(200, scheduleJson);
}

void handleDiagnostics() {
  JsonDocument doc;
  if (lastTxBytes.length()) {
    JsonObject tx = doc["last_tx"].to<JsonObject>();
    tx["bytes"] = lastTxBytes;
    String at = isoTime(lastTxAt);
    if (at.length()) tx["at"] = at;
  }
  if (lastRxBytes.length()) {
    JsonObject rx = doc["last_rx"].to<JsonObject>();
    rx["bytes"] = lastRxBytes;
    rx["protocol"] = lastRxProtocol;
    rx["bits"] = lastRxBits;
    rx["checksum_valid"] = lastRxChecksumOk;
    rx["source"] = lastRxSource;
    String at = isoTime(lastRxAt);
    if (at.length()) rx["at"] = at;
  }
  if (lastMatch >= 0) {
    JsonObject sc = doc["self_check"].to<JsonObject>();
    sc["match"] = (lastMatch == 1);
    String at = isoTime(lastCheckAt);
    if (at.length()) sc["checked_at"] = at;
  }
  JsonObject c = doc["counters"].to<JsonObject>();
  c["tx"] = counterTx;
  c["rx_valid"] = counterRxValid;
  c["rx_rejected"] = counterRxRejected;
  c["lock_asserts"] = counterLockAsserts;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "mise sous tension";
    case ESP_RST_EXT:      return "reset externe";
    case ESP_RST_SW:       return "reset logiciel";
    case ESP_RST_PANIC:    return "PANIC (exception)";
    case ESP_RST_INT_WDT:  return "watchdog interruption";
    case ESP_RST_TASK_WDT: return "watchdog de tache";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (chute d'alimentation)";
    case ESP_RST_SDIO:     return "sdio";
    case ESP_RST_DEEPSLEEP:return "sortie de veille";
    default:               return "inconnu";
  }
}

void handleHealth() {
  JsonDocument doc;
  doc["status"] = "ok";
  doc["uptime_s"] = (uint32_t)(millis() / 1000);
  doc["ir_rx"] = rxEnabled;
  doc["ir_tx"] = true;
  doc["nvs"] = true;

  // De quoi diagnostiquer une lenteur sans avoir a brancher la console.
  doc["reset_reason"] = resetReasonText();
  JsonObject heap = doc["heap"].to<JsonObject>();
  heap["free"] = ESP.getFreeHeap();
  heap["min_free"] = ESP.getMinFreeHeap();
  heap["largest_block"] = ESP.getMaxAllocHeap();   // ecart avec free = fragmentation

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["rssi_dbm"] = WiFi.RSSI();       // ce que la carte ENTEND du routeur
  wifi["channel"] = WiFi.channel();
  wifi["bssid"] = WiFi.BSSIDstr();
  wifi["tx_power_dbm"] = WiFi.getTxPower() / 4.0;
  wifi["disconnects"] = wifiDisconnects;
  wifi["reconnects"] = wifiReconnects;
  wifi["last_disconnect_reason"] = lastDisconnectReason;

  JsonObject ir = doc["ir"].to<JsonObject>();
  ir["rx_valid"] = counterRxValid;
  ir["rx_rejected"] = counterRxRejected;
  ir["rx_unknown"] = counterRxUnknown;   // bruit : le chiffre a surveiller

  // Cadence de la boucle principale : l'indicateur le plus direct d'un
  // processeur accapare. Une boucle saine tourne a plusieurs milliers par
  // seconde ; quelques dizaines signifient que quelque chose mange tout.
  doc["loops_per_s"] = loopsPerSec;

  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// Balayage de la bande depuis la carte : c'est son point de vue qui compte,
// pas celui du PC. Bloque deux a trois secondes, a n'appeler qu'a la demande.
void handleWifiScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  doc["found"] = n;
  doc["our_channel"] = WiFi.channel();
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n && i < 24; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["channel"] = WiFi.channel(i);
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// Debrayage du recepteur, pour trancher par l'essai plutot que par le
// raisonnement : si la page redevient rapide RX coupe, le coupable est trouve.
void handleRxToggle() {
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain"))) return sendError(400, "json invalide");
  bool want = body["enabled"] | true;
  if (want != rxEnabled) {
    rxEnabled = want;
    if (rxEnabled) irrecv.enableIRIn();
    else           irrecv.disableIRIn();
    Serial.printf("[ir] reception %s\n", rxEnabled ? "activee" : "coupee");
  }
  JsonDocument doc;
  doc["rx_enabled"] = rxEnabled;
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

// Les adresses des boitiers sont servies par le boitier lui-meme : rien a
// saisir sur le telephone (api-v2.md §2).
void handleConfig() {
  JsonDocument doc;
  JsonArray devs = doc["devices"].to<JsonArray>();
  JsonObject me = devs.add<JsonObject>();
  me["id"] = CLIM_DEVICE_ID;
  me["name"] = CLIM_DEVICE_NAME;
  me["base"] = "";
  if (strlen(CLIM_PEER_BASE) > 0) {
    JsonObject peer = devs.add<JsonObject>();
    peer["id"] = "peer";
    peer["name"] = strlen(CLIM_PEER_NAME) ? CLIM_PEER_NAME : "Second boitier";
    peer["base"] = CLIM_PEER_BASE;
  }
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

const char *contentType(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "application/octet-stream";
}

// Empreinte de l'ensemble des fichiers servis, calculee une fois au demarrage.
// Elle sert d'ETag : le navigateur ne retelecharge l'interface que si elle a
// reellement change. Sur une liaison mediocre, c'est la difference entre
// 53 ko a chaque ouverture de page et un aller-retour de quelques octets.
uint32_t assetsVersion = 0;

void computeAssetsVersion() {
  uint32_t h = 2166136261u;                  // FNV-1a
  uint8_t buf[512];
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0)
      for (int i = 0; i < n; i++) { h ^= buf[i]; h *= 16777619u; }
  }
  assetsVersion = h;
}

void handleStatic() {
  if (server.method() == HTTP_OPTIONS) {   // preflight CORS
    sendCors();
    return server.send(204);
  }
  String path = server.uri();
  if (path.endsWith("/")) path += "index.html";
  if (!LittleFS.exists(path)) {
    sendCors();
    server.sendHeader("Cache-Control", "no-store");
    return server.send(404, "text/plain; charset=utf-8",
                       "Fichier absent. Televerser l'interface : pio run -e webui -t uploadfs");
  }
  File f = LittleFS.open(path, "r");
  const String etag = "\"" + String(assetsVersion, 16) + "-" + String((uint32_t)f.size(), 16) + "\"";

  sendCors();
  // `no-cache` ne veut pas dire « ne pas mettre en cache » mais « revalider
  // avant de servir » : le navigateur garde le fichier et ne redemande qu'une
  // confirmation, a laquelle on repond 304 sans corps.
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("ETag", etag);

  if (server.hasHeader("If-None-Match") && server.header("If-None-Match") == etag) {
    f.close();
    return server.send(304);
  }
  server.streamFile(f, contentType(path));
  f.close();
}

// ===========================================================================
// Mise en route
// ===========================================================================

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    wifiDisconnects++;
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
    Serial.printf("[wifi] deconnexion, motif %u\n", lastDisconnectReason);
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.print(F("[wifi] connecte, IP "));
    Serial.println(WiFi.localIP());
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CLIM_DEVICE_ID);
  WiFi.onEvent(onWifiEvent);
  // Sans cela, une coupure du point d'acces est definitive : le croquis
  // n'avait aucune reprise, et la carte restait injoignable jusqu'au
  // redemarrage (plan §14.4, « perte et retour du Wi-Fi »).
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  // L'economie d'energie du Wi-Fi endort la radio entre deux balises : sur un
  // serveur qui doit repondre a tout instant, elle ajoute des centaines de
  // millisecondes de latence, tres variables.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connexion a « %s »", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("ECHEC : pas de connexion Wi-Fi."));
    Serial.println(F("  - verifier SSID et mot de passe dans include/secrets.h ;"));
    Serial.println(F("  - le XIAO n'a pas d'antenne integree : l'antenne IPEX"));
    Serial.println(F("    doit etre branchee AVANT la mise sous tension ;"));
    Serial.println(F("  - le Wi-Fi doit etre en 2,4 GHz, l'ESP32 ne voit pas le 5 GHz."));
    return;
  }

  Serial.println(F("-----------------------------------------------------------"));
  Serial.print(F(" Interface accessible sur : http://"));
  Serial.println(WiFi.localIP());
  Serial.printf(" ou sur : http://%s.local/  (si le telephone gere le mDNS)\n",
                CLIM_DEVICE_ID);
  Serial.printf(" RSSI : %d dBm\n", WiFi.RSSI());
  Serial.println(F("-----------------------------------------------------------"));

  if (MDNS.begin(CLIM_DEVICE_ID)) MDNS.addService("http", "tcp", 80);
  configTzTime(kTimezone, "pool.ntp.org", "fr.pool.ntp.org");
}

void setup() {
  pinMode(kPinTx, OUTPUT);
  digitalWrite(kPinTx, LOW);          // base au niveau bas avant tout
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, HIGH);        // LED eteinte (actif a l'etat bas)

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(300);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Interface Web sur la carte - banc d'essai"));
  Serial.printf(" IRremoteESP8266 v%s, firmware %s\n",
                _IRREMOTEESP8266_VERSION_STR, kFirmware);
  Serial.println(F("==========================================================="));

  if (!LittleFS.begin(true)) {
    Serial.println(F("ECHEC : systeme de fichiers illisible."));
  } else if (!LittleFS.exists("/index.html")) {
    Serial.println(F("ATTENTION : l'interface n'est pas televersee."));
    Serial.println(F("  pio run -e webui -t uploadfs"));
  } else {
    computeAssetsVersion();
    Serial.printf("Interface : empreinte %08x\n", assetsVersion);
  }

  prefs.begin("clim", false);
  loadSchedule();

  pinMode(kPowerPin, OUTPUT);         // alimentation du TSOP par GPIO
  digitalWrite(kPowerPin, HIGH);
  delay(150);

  ac.begin();
  irrecv.enableIRIn();

  connectWifi();

  server.on("/api/v1/status", HTTP_GET, handleStatus);
  server.on("/api/v1/climate", HTTP_PUT, handleClimatePut);
  server.on("/api/v1/schedule", HTTP_GET, handleScheduleGet);
  server.on("/api/v1/schedule", HTTP_PUT, handleSchedulePut);
  server.on("/api/v1/diagnostics/ir", HTTP_GET, handleDiagnostics);
  server.on("/api/v1/diagnostics/rx", HTTP_PUT, handleRxToggle);
  server.on("/api/v1/diagnostics/wifi", HTTP_GET, handleWifiScan);
  server.on("/healthz", HTTP_GET, handleHealth);
  server.on("/config.json", HTTP_GET, handleConfig);
  server.onNotFound(handleStatic);
  // Sans cette declaration, le serveur jette les en-tetes qu'il ne connait
  // pas, et la revalidation par ETag ne pourrait jamais aboutir.
  const char *collect[] = { "If-None-Match" };
  server.collectHeaders(collect, 1);
  server.begin();

  Serial.println(F("Serveur demarre."));
}

uint32_t lastSchedCheck = 0;
uint32_t lastRxPoll = 0;

void loop() {
  server.handleClient();

  // Cadence de boucle, mesuree sur une fenetre d'une seconde.
  loopCount++;
  if (millis() - loopWindowAt >= 1000) {
    loopsPerSec = loopCount;
    loopCount = 0;
    loopWindowAt = millis();
  }

  // Reprise du Wi-Fi. `setAutoReconnect` suffit dans la plupart des cas, mais
  // pas quand le point d'acces a disparu assez longtemps pour que la pile
  // abandonne : il faut alors relancer la connexion nous-memes.
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiDownSince) wifiDownSince = millis();
    if (millis() - wifiDownSince > 15000) {
      wifiDownSince = millis();
      wifiReconnects++;
      Serial.println(F("[wifi] plus de connexion, relance"));
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  } else {
    wifiDownSince = 0;
  }

  // Ecoute passive : la telecommande d'origine met a jour l'etat estime
  // (plan §3.1). Ici on se contente de tracer ce qui passe.
  if (rxEnabled && millis() - lastRxPoll > 50) {
    lastRxPoll = millis();
    if (irrecv.decode(&results)) {
      const bool echo = millis() < rxIgnoreUntil;   // reste de notre emission
      if (results.decode_type == decode_type_t::TOSHIBA_AC) {
        lastRxBytes      = hexBytes(results.state, results.bits / 8);
        lastRxBits       = results.bits;
        lastRxProtocol   = "TOSHIBA_AC";
        lastRxSource     = echo ? "self" : "remote";
        lastRxAt         = time(nullptr);
        lastRxChecksumOk = true;
        counterRxValid++;
        if (!echo) {
          Serial.printf("[telecommande] %s\n", lastRxBytes.c_str());
          // On ne cherche PAS a savoir si la telecommande demandait la marche :
          // l'encodage du mode n'est pas encore prouve (campagne §8.2). Pendant
          // un segment verrouille, toute trame recue vaut donc rappel d'arret.
          // Si elle demandait deja l'arret, le rappel est sans effet.
          if (lockInForce() && !lockPendingAt) {
            lockPendingAt = millis() + kLockReassertDelayMs;
          }
        }
      } else if (results.decode_type != decode_type_t::UNKNOWN) {
        counterRxRejected++;
      } else {
        counterRxUnknown++;   // bruit optique : visible dans /healthz
      }
      irrecv.resume();
    }
  }

  if (lockPendingAt && millis() >= lockPendingAt) {
    lockPendingAt = 0;
    if (lockInForce() &&
        (!lastLockAssert || millis() - lastLockAssert >= kLockMinIntervalMs)) {
      assertLockOff("commande vue a la telecommande");
    }
  }

  // Rappel periodique, desactive par defaut : voir kLockSweepMs.
  if (kLockSweepMs && lockInForce() &&
      (!lastLockAssert || millis() - lastLockAssert >= kLockSweepMs)) {
    assertLockOff("rappel periodique");
  }

  if (millis() - lastSchedCheck > kSchedCheckMs) {
    lastSchedCheck = millis();
    evaluateSchedule();
  }
}
