// ---------------------------------------------------------------------------
// Diagnostic de la porteuse 38 kHz - etage d'emission (plan §6.1).
//
// Pourquoi ce croquis existe : tx_blink.cpp a montre que le transistor commute
// en CONTINU, et la boucle tx_loopback.cpp ne recoit rien du tout - zero front
// sur la sortie du TSOP. Entre les deux il y a une inconnue : la chaine
// tient-elle les 38 kHz ? Commuter une fois par seconde et commuter toutes les
// 13 us ne demandent pas les memes courants de base.
//
// Ce croquis attaque la question par quatre phases, dont deux se lisent a la
// camera d'un telephone et deux se lisent toutes seules a la console.
//
//   Phase 1 - tout eteint, 3 s        reference noire pour la camera
//   Phase 2 - continu, 3 s            la LED doit etre au MAXIMUM de son eclat
//   Phase 3 - porteuse continue, 3 s  eclat attendu ~ moitie (rapport 50 %)
//   Phase 4 - salves 600/600 us, 2 s  LE test : le TSOP doit voir des fronts
//
// La porteuse est produite par le PWM materiel (LEDC), pas par la boucle
// logicielle de la bibliotheque. C'est deliberе : cela retire le logiciel de
// l'equation. Si le PWM materiel ne passe pas non plus, le defaut est
// franchement dans le transistor, la LED ou l'attaque de base.
//
// LECTURE DU RESULTAT
//   Phase 2 brillante + phase 3 eteinte -> la chaine ne suit pas les 38 kHz.
//     Suspect n°1 : resistance de base de 10 kOhm au lieu de 1 kOhm. Le
//     PN2222A recoit alors 0,26 mA au lieu de 2,6 mA : cela suffit en continu,
//     c'est juste a 38 kHz.
//   Phase 2 eteinte aussi -> la LED IR n'emet pas du tout. Le clignotement
//     valide precedemment etait peut-etre celui de la LED d'etat du XIAO
//     (GPIO21), qui suit le programme quoi qu'il arrive cote transistor.
//   Phase 4 avec des fronts -> la chaine est bonne, le probleme est ailleurs.
//
// CABLAGE : identique a tx_loopback.cpp, les deux etages montes.
//
// Compilation :  pio run -e carrier -t upload
// ---------------------------------------------------------------------------

#include <Arduino.h>

// --- Brochage --------------------------------------------------------------

const uint8_t kPinTx     = 2;   // D1 - base du transistor
const uint8_t kPinStatus = 21;  // LED d'etat du XIAO, allumee a l'etat BAS
const uint8_t kPowerPin  = 4;   // D3 - alimente VS du TSOP
const uint8_t kRecvPin   = 5;   // D4 - lit OUT du TSOP

// --- Porteuse --------------------------------------------------------------

const uint8_t  kLedcChannel = 0;
const uint32_t kCarrierHz   = 38000;
const uint8_t  kLedcBits    = 8;    // resolution
const uint32_t kDuty50      = 128;  // 128/255 ~ 50 %

// --- Salves ----------------------------------------------------------------
//
// 600 us de porteuse puis 600 us de silence : c'est l'ordre de grandeur d'une
// marque Toshiba (546 us mesures, README §7). Un TSOP est concu exactement
// pour ca, il doit produire deux fronts par salve.

const uint32_t kBurstUs   = 600;
const uint32_t kGapUs     = 600;
const uint32_t kBurstMs   = 2000;  // duree de la phase 4

volatile uint32_t edgeCount = 0;

void IRAM_ATTR countEdge() { edgeCount++; }

// --- Pilotage de la broche d'emission --------------------------------------
//
// Deux regimes incompatibles sur la meme broche : le PWM materiel pour la
// porteuse, le GPIO nu pour le continu. On bascule explicitement de l'un a
// l'autre plutot que de laisser les deux se disputer la broche.

void useCarrier() {
  ledcSetup(kLedcChannel, kCarrierHz, kLedcBits);
  ledcAttachPin(kPinTx, kLedcChannel);
  ledcWrite(kLedcChannel, 0);
}

void useDigital() {
  ledcDetachPin(kPinTx);
  pinMode(kPinTx, OUTPUT);
  digitalWrite(kPinTx, LOW);
}

void status(bool on) { digitalWrite(kPinStatus, on ? LOW : HIGH); }

// Compte les fronts vus sur le TSOP pendant une duree donnee.
uint32_t measureEdges(uint32_t ms) {
  edgeCount = 0;
  attachInterrupt(digitalPinToInterrupt(kRecvPin), countEdge, CHANGE);
  uint32_t start = millis();
  while (millis() - start < ms) delay(1);
  detachInterrupt(digitalPinToInterrupt(kRecvPin));
  return edgeCount;
}

void setup() {
  pinMode(kPinTx, OUTPUT);
  digitalWrite(kPinTx, LOW);
  pinMode(kPinStatus, OUTPUT);
  status(false);

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  pinMode(kPowerPin, OUTPUT);
  digitalWrite(kPowerPin, HIGH);
  pinMode(kRecvPin, INPUT);
  delay(150);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Diagnostic de la porteuse 38 kHz - plan §6.1"));
  Serial.printf(" Emission GPIO%d (D1), TSOP lu sur GPIO%d (D4)\n",
                kPinTx, kRecvPin);
  Serial.println(F("==========================================================="));
  Serial.println();
  Serial.println(F("Filmer la LED IR a la camera du telephone pendant un cycle"));
  Serial.println(F("complet (11 s). Comparer l'eclat des phases 2 et 3."));
  Serial.println(F("La LED d'etat du XIAO s'allume en phase 2 uniquement :"));
  Serial.println(F("elle sert de reperе pour se situer dans la video."));
  Serial.println();
}

void loop() {
  Serial.println(F("==========================================================="));

  // --- Phase 1 : tout eteint ------------------------------------------------
  Serial.println(F("Phase 1 : tout eteint (3 s) - reference noire"));
  useDigital();
  status(false);
  uint32_t idle = measureEdges(3000);
  Serial.printf("  fronts au repos : %lu   (attendu 0 ; sinon lumiere"
                " parasite modulee dans la piece)\n", (unsigned long)idle);

  // --- Phase 2 : continu ----------------------------------------------------
  Serial.println(F("Phase 2 : CONTINU (3 s) - eclat maximal attendu"));
  useDigital();
  digitalWrite(kPinTx, HIGH);
  status(true);
  uint32_t dc = measureEdges(3000);
  digitalWrite(kPinTx, LOW);
  status(false);
  Serial.printf("  fronts : %lu   (attendu ~0 : un TSOP ignore le continu,"
                " c'est normal)\n", (unsigned long)dc);

  // --- Phase 3 : porteuse continue -----------------------------------------
  Serial.println(F("Phase 3 : PORTEUSE 38 kHz continue (3 s) - eclat ~ moitie"));
  useCarrier();
  ledcWrite(kLedcChannel, kDuty50);
  uint32_t carrier = measureEdges(3000);
  ledcWrite(kLedcChannel, 0);
  Serial.printf("  fronts : %lu   (attendu faible : l'AGC coupe une porteuse"
                " permanente)\n", (unsigned long)carrier);

  // --- Phase 4 : salves, le test decisif ------------------------------------
  Serial.println(F("Phase 4 : SALVES 600/600 us (2 s) - le test decisif"));
  edgeCount = 0;
  attachInterrupt(digitalPinToInterrupt(kRecvPin), countEdge, CHANGE);

  uint32_t t0 = millis();
  while (millis() - t0 < kBurstMs) {
    ledcWrite(kLedcChannel, kDuty50);
    delayMicroseconds(kBurstUs);
    ledcWrite(kLedcChannel, 0);
    delayMicroseconds(kGapUs);
  }

  detachInterrupt(digitalPinToInterrupt(kRecvPin));
  uint32_t bursts = edgeCount;

  Serial.printf("  fronts : %lu   (attendu : plus de 1000)\n",
                (unsigned long)bursts);

  Serial.println(F("-----------------------------------------------------------"));
  if (bursts > 500) {
    Serial.println(F("VERDICT : la chaine emet et le TSOP recoit."));
    Serial.println(F("  L'etage d'emission tient les 38 kHz. Si tx_loopback"));
    Serial.println(F("  echoue quand meme, chercher du cote des durees de"));
    Serial.println(F("  trame, pas du materiel."));
  } else if (bursts > 0) {
    Serial.println(F("VERDICT : reception partielle."));
    Serial.println(F("  Signal trop faible ou deforme. Pistes : courant de LED"));
    Serial.println(F("  insuffisant (220 ohms au lieu de 22), ou attaque de"));
    Serial.println(F("  base trop molle."));
  } else {
    Serial.println(F("VERDICT : aucune lumiere modulee ne parvient au TSOP."));
    Serial.println(F("  Se fier maintenant a la camera :"));
    Serial.println(F("   - phase 2 brillante, phase 3 eteinte -> la chaine ne"));
    Serial.println(F("     suit pas les 38 kHz. Suspect : base 10k au lieu"));
    Serial.println(F("     de 1k. La remplacer par la 1 kOhm."));
    Serial.println(F("   - phases 2 ET 3 eteintes -> la LED IR n'emet pas."));
    Serial.println(F("     Verifier son sens (anode cote 5 V) et l'essayer en"));
    Serial.println(F("     direct : 5 V - 220 ohms - LED - GND, sans transistor."));
    Serial.println(F("   - phases 2 et 3 visibles -> c'est le TSOP qui est en"));
    Serial.println(F("     cause, malgre sa ligne au repos correcte."));
  }
  Serial.println();
}
