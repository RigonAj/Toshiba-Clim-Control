// ---------------------------------------------------------------------------
// Auto-detection du TSOP - outil de diagnostic.
//
// Supprime les trois variables qu'on ne peut pas verifier sans multimetre :
// la position de la broche 3V3, l'orientation du composant, et les rangees
// de la plaque d'essai.
//
// La carte alimente elle-meme le TSOP depuis une sortie GPIO (le composant
// consomme 1,5 mA au maximum, tres loin des limites d'une broche), et essaie
// les deux affectations possibles des pattes exterieures.
//
// Pourquoi seulement deux essais : en retournant un boitier a trois pattes,
// les pattes exterieures s'echangent mais celle du milieu reste au milieu.
// La masse est donc toujours centrale. Aucun essai n'inverse l'alimentation,
// le test est sans risque pour le composant.
//
// CABLAGE - trois fils, l'ordre des pattes exterieures n'a aucune importance :
//   patte du MILIEU      -> GND du XIAO
//   une patte exterieure -> D3  (GPIO4)
//   l'autre exterieure   -> D4  (GPIO5)
//
// Compilation :  pio run -e autodetect -t upload
// ---------------------------------------------------------------------------

#include <Arduino.h>

const uint8_t kPinA = 4;  // D3
const uint8_t kPinB = 5;  // D4

const char *kLabelA = "D3";
const char *kLabelB = "D4";

uint8_t vsPin = 0, outPin = 0;
const char *vsLabel = nullptr, *outLabel = nullptr;
bool detected = false;

// Alimente le TSOP par `supply` et regarde si `sense` est tenue haute.
// `sense` est en entree avec tirage vers le bas : seul un composant alimente
// et fonctionnel peut la maintenir haute.
uint8_t trial(uint8_t supply, uint8_t sense) {
  pinMode(sense, INPUT_PULLDOWN);
  pinMode(supply, OUTPUT);
  digitalWrite(supply, HIGH);

  delay(120);  // le TSOP a besoin de quelques dizaines de ms pour demarrer

  uint8_t high = 0;
  for (uint8_t i = 0; i < 20; i++) {
    if (digitalRead(sense) == HIGH) high++;
    delay(3);
  }

  pinMode(supply, INPUT);  // on relache avant l'essai suivant
  delay(50);
  return high;
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Auto-detection du recepteur IR"));
  Serial.println(F("==========================================================="));
  Serial.println(F(" Cablage attendu :"));
  Serial.println(F("   patte du MILIEU      -> GND"));
  Serial.println(F("   une patte exterieure -> D3"));
  Serial.println(F("   l'autre exterieure   -> D4"));
  Serial.println();

  Serial.println(F("--- Essai 1 : alimentation par D3, lecture sur D4 ---"));
  uint8_t r1 = trial(kPinA, kPinB);
  Serial.printf("  D4 haute : %d/20\n", r1);

  Serial.println(F("--- Essai 2 : alimentation par D4, lecture sur D3 ---"));
  uint8_t r2 = trial(kPinB, kPinA);
  Serial.printf("  D3 haute : %d/20\n", r2);
  Serial.println();

  if (r1 >= 19 && r2 < 19) {
    vsPin = kPinA; vsLabel = kLabelA;
    outPin = kPinB; outLabel = kLabelB;
    detected = true;
  } else if (r2 >= 19 && r1 < 19) {
    vsPin = kPinB; vsLabel = kLabelB;
    outPin = kPinA; outLabel = kLabelA;
    detected = true;
  }

  if (detected) {
    Serial.println(F("*** RECEPTEUR TROUVE ET FONCTIONNEL ***"));
    Serial.printf("  VS  (alimentation) sur %s\n", vsLabel);
    Serial.printf("  OUT (signal)       sur %s\n", outLabel);
    Serial.println();
    Serial.println(F("Le composant est bon. Il ne restera qu'a le recabler"));
    Serial.println(F("normalement : VS -> 3V3, milieu -> GND, OUT -> D2."));
    Serial.println();

    pinMode(vsPin, OUTPUT);
    digitalWrite(vsPin, HIGH);
    pinMode(outPin, INPUT_PULLDOWN);
    delay(150);

    Serial.println(F("--- Detection d'activite ---"));
    Serial.println(F("Appuyer sur une touche de la telecommande, dome vise a"));
    Serial.println(F("30 cm. Bilan toutes les 2 secondes."));
    Serial.println();
  } else if (r1 >= 19 && r2 >= 19) {
    Serial.println(F("ANORMAL : les deux essais repondent haut."));
    Serial.println(F("Les deux pattes exterieures sont probablement reliees"));
    Serial.println(F("entre elles, ou a la meme rangee de la plaque."));
  } else {
    Serial.println(F("AUCUNE REPONSE dans les deux sens."));
    Serial.println(F("Le composant ne s'alimente pas. Causes possibles :"));
    Serial.println(F("  - une des trois pattes ne fait pas contact"));
    Serial.println(F("  - la patte du milieu n'est pas reliee a GND"));
    Serial.println(F("  - le TSOP est hors service"));
    Serial.println();
    Serial.println(F("Verifier d'abord les contacts, puis essayer le composant"));
    Serial.println(F("de rechange."));
  }
}

void loop() {
  static uint32_t lastReport = 0;
  static uint32_t transitions = 0;
  static uint8_t last = 0;

  if (!detected) {
    delay(1000);
    return;
  }

  uint8_t level = digitalRead(outPin);
  if (level != last) {
    transitions++;
    last = level;
  }

  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    if (transitions > 0) {
      Serial.printf("  %lu transitions  <<< SIGNAL IR RECU\n",
                    (unsigned long)transitions);
      transitions = 0;
    } else {
      Serial.println(F("  (repos)"));
    }
  }
}
