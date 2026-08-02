// ---------------------------------------------------------------------------
// Reconnaissance IR - plan §8.0
//
// Objectif unique : savoir si la telecommande Toshiba WH-TG01NE est reconnue
// par IRremoteESP8266, et sinon recuperer le tableau brut des durees pour la
// matrice de capture du §8.2.
//
// Brochage impose par le plan §6.0 :
//   TSOP38438 OUT -> D2 = GPIO3
//   TSOP38438 VS  -> 3V3 au travers de 100 ohms
//   TSOP38438 GND -> GND
//
// Ce croquis ne fait que recevoir. L'etage d'emission (§6.1) n'est monte
// qu'en phase 1b, une fois des captures exploitables obtenues.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>

// --- Parametres de capture -------------------------------------------------

// Cablage provisoire, valide par l'auto-detection du 1er aout 2026 : le TSOP
// est alimente par une sortie GPIO plutot que par la broche 3V3. Il consomme
// 1,5 mA au maximum, tres loin des 40 mA admissibles par broche. Cela supprime
// la dependance au reperage de la broche 3V3 et aux rangees de la plaque.
//
// Montage definitif (plan §6.2) : VS -> 3V3, OUT -> D2, kPowerPin disparait.
const uint8_t  kPowerPin = 4;  // D3 - alimente VS du TSOP
const uint16_t kRecvPin  = 5;  // D4 - lit OUT du TSOP

// Un climatiseur envoie des trames longues : le tampon doit etre genereux.
const uint16_t kCaptureBufferSize = 1024;

// Fin de trame. Le plan §7.4 retient 20 ms cote RMT ; IRremoteESP8266
// recommande 50 ms pour les protocoles de climatiseur, qui comportent des
// silences internes plus longs que ceux d'une telecommande TV.
const uint8_t kTimeout = 50;

// En dessous de cette taille, un signal "inconnu" est du bruit, pas une trame.
// Abaisse a 6 pendant la mise au point : un signal faible ou partiel doit
// remonter a la console plutot que d'etre filtre en silence. A remonter a 12
// pour la campagne de captures du §8.2.
const uint16_t kMinUnknownSize = 6;

// Seuil du filtre anti-parasite de la bibliotheque, en microsecondes.
const uint8_t kTolerancePercent = kTolerance;  // 25 % par defaut

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

uint32_t frameCount = 0;

// --- Verification de cablage ----------------------------------------------
//
// La sortie du TSOP38438 est a collecteur ouvert avec tirage interne : au
// repos elle est tiree au niveau HAUT. En activant un tirage vers le bas
// cote ESP32, on distingue trois cas :
//   - lecture HAUT  -> le TSOP est alimente et pilote la ligne : cablage OK
//   - lecture BAS   -> rien de connecte, ou TSOP non alimente, ou OUT/GND
//                      inverses, ou saturation lumineuse permanente
void checkWiring() {
  pinMode(kRecvPin, INPUT_PULLDOWN);
  delay(50);

  // Plusieurs lectures : un TSOP sature par la lumiere peut osciller.
  uint8_t highCount = 0;
  for (uint8_t i = 0; i < 20; i++) {
    if (digitalRead(kRecvPin) == HIGH) highCount++;
    delay(5);
  }

  Serial.println(F("--- Verification du cablage (plan §1a) ---"));
  Serial.printf("GPIO%d au repos : %d lectures HAUT sur 20\n", kRecvPin, highCount);

  if (highCount >= 19) {
    Serial.println(F("OK : le TSOP38438 est alimente et tient la ligne au niveau haut."));
  } else if (highCount == 0) {
    Serial.println(F("ECHEC : ligne au niveau bas en permanence."));
    Serial.println(F("  Verifier : alimentation 3V3 du TSOP, resistance 100 ohms,"));
    Serial.println(F("  brochage 1=OUT 2=GND 3=VS dome vers soi, et la liaison OUT -> D2."));
  } else {
    Serial.println(F("SUSPECT : la ligne bascule au repos."));
    Serial.println(F("  Cause probable : saturation lumineuse (soleil, halogene, LED a"));
    Serial.println(F("  decoupage) ou decouplage absent (100 nF + 4,7 uF, plan §6.2)."));
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  // L'USB-Serial-JTAG reenumere a chaque reset : laisser le temps a l'hote.
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Reconnaissance IR - Toshiba WH-TG01NE - plan §8.0"));
  Serial.printf(" IRremoteESP8266 v%s\n", _IRREMOTEESP8266_VERSION_STR);
  Serial.printf(" Reception sur GPIO%d (D4), alimentation par GPIO%d (D3)\n",
                kRecvPin, kPowerPin);
  Serial.printf(" Tampon %d symboles, fin de trame %d ms\n",
                kCaptureBufferSize, kTimeout);
  Serial.println(F("==========================================================="));
  Serial.println();

  // Alimente le TSOP et laisse son etage d'entree se stabiliser.
  pinMode(kPowerPin, OUTPUT);
  digitalWrite(kPowerPin, HIGH);
  delay(150);

  checkWiring();

  irrecv.setUnknownThreshold(kMinUnknownSize);
  irrecv.setTolerance(kTolerancePercent);
  irrecv.enableIRIn();

  Serial.println(F("Pret. Pointer la telecommande vers le TSOP a 20-50 cm"));
  Serial.println(F("et appuyer sur une touche (plan §8.2)."));
  Serial.println();
}

void loop() {
  if (!irrecv.decode(&results)) return;

  frameCount++;

  Serial.println(F("==========================================================="));
  Serial.printf("TRAME #%lu   -   %lu ms depuis le demarrage\n",
                (unsigned long)frameCount, (unsigned long)millis());
  Serial.println(F("-----------------------------------------------------------"));

  if (results.overflow) {
    Serial.printf("ATTENTION : depassement de tampon, augmenter kCaptureBufferSize"
                  " au-dela de %d.\n", kCaptureBufferSize);
  }

  // 1. Verdict de reconnaissance : c'est LA question du §8.0.
  Serial.printf("Protocole      : %s\n", typeToString(results.decode_type,
                                                      results.repeat).c_str());
  Serial.printf("Longueur       : %d bits\n", results.bits);
  Serial.printf("Nb de durees   : %d\n", getCorrectedRawLength(&results));

  if (results.decode_type == decode_type_t::UNKNOWN) {
    Serial.println(F("=> NON RECONNU. Le plan se deroule tel que prevu :"));
    Serial.println(F("   les durees brutes ci-dessous alimentent la matrice §8.2."));
  } else {
    Serial.println(F("=> RECONNU. Voir §8.0 : l'option ESPHome (§17.3) devient"));
    Serial.println(F("   competitive, decision a prendre en fin de phase 2."));
  }
  Serial.println();

  // 2. Octets d'etat et checksum interne, si la bibliotheque sait les lire.
  String description = IRAcUtils::resultAcToString(&results);
  if (description.length()) {
    Serial.println(F("--- Etat decode par la bibliotheque ---"));
    Serial.println(description);
    Serial.println();
  }

  // 3. Sortie lisible + code source reutilisable.
  Serial.println(F("--- Resume ---"));
  Serial.println(resultToHumanReadableBasic(&results));

  Serial.println(F("--- Durees brutes (a coller dans les captures) ---"));
  Serial.println(resultToSourceCode(&results));

  Serial.println();
  irrecv.resume();
}
