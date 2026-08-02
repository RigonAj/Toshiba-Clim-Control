// ---------------------------------------------------------------------------
// Boucle locale emission -> reception - plan §2.3, preuve n°2 (etape
// intermediaire).
//
// Ce croquis repond a la question laissee ouverte par tx_blink.cpp : le
// transistor commute (valide), mais l'etage sait-il restituer une porteuse
// 38 kHz et des timings assez propres pour qu'un recepteur les redecode ?
//
// Il emet une trame TOSHIBA_AC de 72 bits sur D1, la fait recevoir par le
// TSOP, et compare octet par octet ce qui est parti avec ce qui revient.
//
// Ce que la boucle prouve :
//   - la porteuse 38 kHz est presente et au bon rapport cyclique ;
//   - les marques et espaces sortent aux durees attendues ;
//   - la chaine GPIO -> transistor -> LED ne deforme pas la trame.
//
// Ce qu'elle NE prouve PAS : que la clim obeit. Elle ignore la portee reelle
// et la sensibilite du recepteur de l'unite interieure. La preuve n°2 du §2.3
// ne sera acquise que devant l'unite (voir migration-nouveau-pc.md §6).
//
// CABLAGE - les deux etages sont montes en meme temps, c'est le seul croquis
// dans ce cas :
//   emission (§6.1)  5V -> 220 ohms -> anode LED IR
//                    cathode LED IR -> collecteur PN2222A
//                    D1 (GPIO2)     -> resistance de base -> base
//                    emetteur       -> GND
//   reception        TSOP VS  -> D3 (GPIO4), sortie a l'etat haut
//                    TSOP GND -> GND
//                    TSOP OUT -> D4 (GPIO5)
//
// PLACEMENT : ne pas coller le TSOP a la LED. A moins de 10 cm son AGC sature
// et la trame ressort deformee, ce qui ferait conclure a tort a un defaut
// d'emission. Viser 20-50 cm, comme pour les captures (§8.2), ou faire
// rebondir le faisceau sur un mur clair.
//
// Compilation :  pio run -e loopback -t upload
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

// --- Brochage --------------------------------------------------------------

const uint16_t kPinTx    = 2;  // D1 - base du transistor (plan §6.0)
const uint8_t  kPowerPin = 4;  // D3 - alimente VS du TSOP (cablage provisoire)
const uint16_t kRecvPin  = 5;  // D4 - lit OUT du TSOP

// --- Parametres de reception ----------------------------------------------
//
// Repris de main.cpp pour que la boucle soit jugee avec la meme severite que
// les captures de la telecommande.

const uint16_t kCaptureBufferSize = 1024;
const uint8_t  kTimeout           = 50;   // ms, fin de trame
const uint8_t  kTolerancePercent  = kTolerance;

// Fenetre d'attente apres l'emission. Le paquet est emis deux fois, separes
// d'environ 5956 us, et il faut encore laisser passer le silence de fin de
// trame de kTimeout.
const uint32_t kDecodeWindowMs = 500;

// Repos entre deux trames : au moins les 2 s du §8.2, pour que l'AGC du TSOP
// revienne a son etat de veille entre deux mesures.
const uint32_t kRestMs = 2000;

IRsend irsend(kPinTx);
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

// --- Trames d'essai --------------------------------------------------------
//
// La premiere est la trame REELLEMENT MESUREE le 1er aout 2026 (README §7) :
// c'est la seule qui soit une observation. Les deux autres sont DERIVEES de
// l'encodage documente - quartet haut de l'octet 5 = temperature - 17 - et ne
// valent que comme jeux de bits differents pour eprouver les timings.
//
// Elles ne prouvent rien sur le protocole : seule la campagne du §8.2 le fera.

const uint16_t kStateLength = 9;

struct TestFrame {
  const char *label;
  uint8_t     state[kStateLength];
};

TestFrame frames[] = {
  {"mesuree 21 C", {0xF2, 0x0D, 0x03, 0xFC, 0x01, 0x40, 0x00, 0x00, 0x41}},
  {"derivee 17 C", {0xF2, 0x0D, 0x03, 0xFC, 0x01, 0x00, 0x00, 0x00, 0x00}},
  {"derivee 30 C", {0xF2, 0x0D, 0x03, 0xFC, 0x01, 0xD0, 0x00, 0x00, 0x00}},
};

const uint8_t kFrameCount = sizeof(frames) / sizeof(frames[0]);

// Checksum = OU exclusif des huit premiers octets (README §7, verifie sur tous
// les etats captures).
uint8_t toshibaChecksum(const uint8_t *state) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < kStateLength - 1; i++) sum ^= state[i];
  return sum;
}

void printState(const uint8_t *state) {
  for (uint8_t i = 0; i < kStateLength; i++) Serial.printf("%02X ", state[i]);
}

// --- Comptage --------------------------------------------------------------

uint32_t passCount = 0;
uint32_t failCount = 0;
uint32_t roundNo   = 0;

// --- Diagnostics de depart -------------------------------------------------
//
// Sans eux, un echec de decodage a deux causes indiscernables : le TSOP ne
// voit rien, ou il voit une trame deformee. Les deux controles ci-dessous
// tranchent avant la premiere passe.

// Identique a checkWiring() de main.cpp : la sortie du TSOP est a collecteur
// ouvert avec tirage interne, donc HAUT au repos. Un tirage vers le bas cote
// ESP32 distingue "alimente et pilote la ligne" de "rien au bout du fil".
void checkWiring() {
  pinMode(kRecvPin, INPUT_PULLDOWN);
  delay(50);

  uint8_t highCount = 0;
  for (uint8_t i = 0; i < 20; i++) {
    if (digitalRead(kRecvPin) == HIGH) highCount++;
    delay(5);
  }

  Serial.println(F("--- Verification du cablage du TSOP ---"));
  Serial.printf("GPIO%d au repos : %d lectures HAUT sur 20\n",
                kRecvPin, highCount);

  if (highCount >= 19) {
    Serial.println(F("OK : le TSOP est alimente et tient la ligne au niveau haut."));
  } else if (highCount == 0) {
    Serial.println(F("ECHEC : ligne au niveau bas en permanence."));
    Serial.println(F("  Le recepteur n'est pas dans le circuit. Verifier VS -> D3,"));
    Serial.println(F("  GND -> GND, OUT -> D4, et le brochage 1=OUT 2=GND 3=VS"));
    Serial.println(F("  dome vers soi."));
  } else {
    Serial.println(F("SUSPECT : la ligne bascule au repos."));
    Serial.println(F("  Cause probable : saturation lumineuse, ou LED IR allumee"));
    Serial.println(F("  en permanence juste devant le capteur."));
  }
  Serial.println();
}

// Compte les fronts vus sur la sortie du TSOP pendant une emission reelle.
// A executer avant enableIRIn() : la bibliotheque prend la broche ensuite.
volatile uint32_t edgeCount = 0;

void IRAM_ATTR countEdge() { edgeCount++; }

void probeActivity() {
  Serial.println(F("--- Sonde : le TSOP reagit-il a notre LED ? ---"));

  pinMode(kRecvPin, INPUT);
  edgeCount = 0;
  attachInterrupt(digitalPinToInterrupt(kRecvPin), countEdge, CHANGE);

  irsend.sendToshibaAC(frames[0].state, kStateLength);
  delay(100);

  detachInterrupt(digitalPinToInterrupt(kRecvPin));

  uint32_t seen = edgeCount;
  Serial.printf("Fronts detectes pendant l'emission : %lu\n",
                (unsigned long)seen);
  Serial.println(F("Attendu : plusieurs centaines (72 bits emis deux fois)."));

  if (seen == 0) {
    Serial.println(F("=> Le TSOP ne voit RIEN. Le probleme est en amont du"));
    Serial.println(F("   decodage : porteuse absente, LED hors du champ, ou"));
    Serial.println(F("   recepteur non cable. Le clignotement de tx_blink ne"));
    Serial.println(F("   prouvait que la commutation, pas la modulation."));
  } else if (seen < 100) {
    Serial.println(F("=> Le TSOP voit quelque chose, mais trop peu. Piste :"));
    Serial.println(F("   saturation par proximite, ou porteuse hors du 38 kHz."));
  } else {
    Serial.println(F("=> Le TSOP recoit bien la trame. Si le decodage echoue"));
    Serial.println(F("   ensuite, ce sont les durees qui sont fausses."));
  }
  Serial.println();
}

void setup() {
  // La base au niveau bas avant tout, comme dans tx_blink.cpp : tant que
  // irsend.begin() n'a pas tourne, GPIO2 flotterait.
  pinMode(kPinTx, OUTPUT);
  digitalWrite(kPinTx, LOW);

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Boucle locale emission -> reception - plan §2.3"));
  Serial.printf(" IRremoteESP8266 v%s\n", _IRREMOTEESP8266_VERSION_STR);
  Serial.printf(" Emission GPIO%d (D1), reception GPIO%d (D4)\n",
                kPinTx, kRecvPin);
  Serial.println(F("==========================================================="));
  Serial.println();
  Serial.println(F("Placer le TSOP a 20-50 cm de la LED IR, pas plus pres :"));
  Serial.println(F("colles l'un a l'autre, l'AGC sature et la trame ressort"));
  Serial.println(F("deformee. Un echec de trop pres n'est pas un echec."));
  Serial.println();

  // Auto-verification des trames avant de les emettre : si le checksum ecrit
  // en dur ne correspond pas a la formule, autant le savoir maintenant plutot
  // que de chercher la panne cote materiel.
  Serial.println(F("--- Verification des trames d'essai ---"));
  for (uint8_t i = 0; i < kFrameCount; i++) {
    uint8_t expected = toshibaChecksum(frames[i].state);
    if (frames[i].state[kStateLength - 1] != expected) {
      Serial.printf("  %s : checksum corrige %02X -> %02X\n",
                    frames[i].label, frames[i].state[kStateLength - 1],
                    expected);
      frames[i].state[kStateLength - 1] = expected;
    } else {
      Serial.printf("  %s : checksum %02X OK\n", frames[i].label, expected);
    }
  }
  Serial.println();

  pinMode(kPowerPin, OUTPUT);
  digitalWrite(kPowerPin, HIGH);
  delay(150);

  irsend.begin();

  checkWiring();
  probeActivity();

  irrecv.setTolerance(kTolerancePercent);
  irrecv.enableIRIn();

  Serial.println(F("Pret."));
  Serial.println();
}

// Emet une trame, attend son retour, compare. Renvoie true si les neuf octets
// reviennent identiques.
bool runFrame(const TestFrame &frame) {
  Serial.println(F("-----------------------------------------------------------"));
  Serial.printf("Emission : %s\n", frame.label);
  Serial.print(F("  envoye  : "));
  printState(frame.state);
  Serial.println();

  // Vider ce qui traine avant d'emettre, sinon on redecoderait la trame
  // precedente et le verdict serait faux.
  irrecv.resume();

  irsend.sendToshibaAC(frame.state, kStateLength);

  uint32_t start = millis();
  while (millis() - start < kDecodeWindowMs) {
    if (!irrecv.decode(&results)) {
      delay(1);
      continue;
    }

    Serial.printf("  protocole : %s, %d bits\n",
                  typeToString(results.decode_type, results.repeat).c_str(),
                  results.bits);

    if (results.overflow) {
      Serial.println(F("  ATTENTION : depassement de tampon."));
    }

    if (results.decode_type != decode_type_t::TOSHIBA_AC) {
      Serial.println(F("  ECHEC : protocole non reconnu."));
      Serial.println(F("  Durees brutes :"));
      Serial.println(resultToSourceCode(&results));
      return false;
    }

    if (results.bits != kStateLength * 8) {
      Serial.printf("  ECHEC : %d bits au lieu de %d.\n",
                    results.bits, kStateLength * 8);
      return false;
    }

    Serial.print(F("  recu    : "));
    printState(results.state);
    Serial.println();

    bool identical = true;
    for (uint8_t i = 0; i < kStateLength; i++) {
      if (results.state[i] != frame.state[i]) {
        Serial.printf("  ECART octet %d : envoye %02X, recu %02X\n",
                      i, frame.state[i], results.state[i]);
        identical = false;
      }
    }

    if (identical) {
      Serial.println(F("  OK : les neuf octets reviennent identiques."));
    }
    return identical;
  }

  Serial.println(F("  ECHEC : rien recu dans la fenetre."));
  Serial.println(F("  Pistes : TSOP trop pres (saturation) ou mal oriente,"));
  Serial.println(F("  LED IR hors du champ, porteuse absente."));
  return false;
}

void loop() {
  roundNo++;
  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.printf(" PASSE #%lu\n", (unsigned long)roundNo);
  Serial.println(F("==========================================================="));

  for (uint8_t i = 0; i < kFrameCount; i++) {
    if (runFrame(frames[i])) {
      passCount++;
    } else {
      failCount++;
    }
    delay(kRestMs);
  }

  Serial.println();
  Serial.printf("Cumul : %lu reussites, %lu echecs\n",
                (unsigned long)passCount, (unsigned long)failCount);
}
