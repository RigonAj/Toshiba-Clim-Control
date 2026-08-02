// ---------------------------------------------------------------------------
// Test de l'etage d'emission - clignotement lent sur D1 (GPIO2).
//
// Ce croquis repond a une seule question : le transistor commute-t-il ?
//
// Il n'emet AUCUNE porteuse 38 kHz. La LED est allumee en continu pendant les
// phases hautes, ce qui la rend franchement observable a la camera d'un
// telephone : les capteurs CMOS voient le 940 nm comme un eclat blanc-violet.
//
// Deroulement :
//   phase 1 - base au niveau BAS  pendant 5 s -> LED IR attendue ETEINTE
//   phase 2 - base au niveau HAUT pendant 5 s -> LED IR attendue ALLUMEE
//   phase 3 - clignotement a 1 Hz, en boucle
//
// La LED d'etat du XIAO (GPIO21) suit la LED IR. Elle donne une reference
// visible a l'oeil nu : les deux doivent toujours etre dans le meme etat,
// et on peut les cadrer ensemble dans l'objectif du telephone.
//
// CABLAGE (plan §6.1) :
//   5V                  -> resistance de collecteur -> anode (+) LED IR
//   cathode (-) LED IR  -> collecteur PN2222A
//   D1 (GPIO2)          -> resistance de base       -> base PN2222A
//   base PN2222A        -> 10 kOhm                  -> GND
//   emetteur PN2222A    -> GND
//
// Le TSOP n'est pas alimente par ce croquis : D3 et D4 restent en entree.
//
// Compilation :  pio run -e txtest -t upload
// ---------------------------------------------------------------------------

#include <Arduino.h>

const uint8_t kPinTx     = 2;   // D1 - base du transistor
const uint8_t kPinStatus = 21;  // LED utilisateur du XIAO, allumee a l'etat BAS

const uint32_t kPhaseMs = 5000;  // duree des deux phases statiques
const uint32_t kBlinkMs = 1000;  // demi-periode du clignotement

void setTx(bool on) {
  digitalWrite(kPinTx, on ? HIGH : LOW);
  digitalWrite(kPinStatus, on ? LOW : HIGH);  // LED d'etat inversee
}

void setup() {
  // Avant toute autre chose : la base au niveau bas. Tant que cette ligne n'a
  // pas tourne, GPIO2 est en haute impedance et la base flotte - c'est ce que
  // la resistance de 10 kOhm est la pour couvrir.
  pinMode(kPinTx, OUTPUT);
  digitalWrite(kPinTx, LOW);
  pinMode(kPinStatus, OUTPUT);
  digitalWrite(kPinStatus, HIGH);

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Test de l'etage d'emission - D1 (GPIO2)"));
  Serial.println(F("==========================================================="));
  Serial.println(F(" Regarder la LED IR a travers la camera d'un telephone."));
  Serial.println(F(" Cadrer la LED d'etat du XIAO dans la meme image : les"));
  Serial.println(F(" deux doivent s'allumer et s'eteindre ensemble."));
  Serial.println();

  Serial.println(F("--- Phase 1 : base au niveau BAS pendant 5 s ---"));
  Serial.println(F("    Attendu : LED IR ETEINTE"));
  Serial.println(F("    Si elle brille -> le transistor ne commande rien."));
  setTx(false);
  delay(kPhaseMs);

  Serial.println();
  Serial.println(F("--- Phase 2 : base au niveau HAUT pendant 5 s ---"));
  Serial.println(F("    Attendu : LED IR ALLUMEE"));
  Serial.println(F("    Si elle reste eteinte -> rien n'arrive a la LED."));
  setTx(true);
  delay(kPhaseMs);

  Serial.println();
  Serial.println(F("--- Phase 3 : clignotement 1 Hz, en boucle ---"));
  Serial.println(F("    Reset de la carte pour rejouer les phases 1 et 2."));
  Serial.println();
}

void loop() {
  setTx(true);
  Serial.println(F("  HAUT  -> LED IR allumee"));
  delay(kBlinkMs);

  setTx(false);
  Serial.println(F("  bas   -> LED IR eteinte"));
  delay(kBlinkMs);
}
