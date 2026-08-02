// ---------------------------------------------------------------------------
// Scanner de broches - outil de diagnostic de cablage.
//
// Teste les onze entrees-sorties du XIAO ESP32-S3 (D0 a D10) et indique
// laquelle est reellement pilotee par le TSOP38438.
//
// Principe : chaque broche est mise en entree avec tirage vers le bas. Une
// broche en l'air lit BAS. Une broche reliee a la sortie du TSOP38438, qui
// possede un tirage interne, lit HAUT malgre ce tirage.
//
// Compilation :  pio run -e scan -t upload
// ---------------------------------------------------------------------------

#include <Arduino.h>

struct PinDef {
  const char *label;
  uint8_t gpio;
  const char *note;
};

// Brochage du XIAO ESP32-S3, plan §6.0. La serigraphie ne correspond pas
// aux numeros de GPIO.
const PinDef kPins[] = {
    {"D0", 1, ""},
    {"D1", 2, "prevu pour l'emission IR"},
    {"D2", 3, "ATTENDU pour la reception IR"},
    {"D3", 4, ""},
    {"D4", 5, ""},
    {"D5", 6, ""},
    {"D6", 43, "UART0 TX"},
    {"D7", 44, "UART0 RX"},
    {"D8", 7, ""},
    {"D9", 8, ""},
    {"D10", 9, ""},
};
const size_t kPinCount = sizeof(kPins) / sizeof(kPins[0]);

uint8_t lastLevel[kPinCount];
uint32_t transitions[kPinCount];

void staticScan() {
  Serial.println(F("--- 1. Niveau au repos (tirage vers le bas actif) ---"));
  Serial.println(F("Broche  GPIO  HAUT/20  Verdict"));

  bool found = false;
  for (size_t i = 0; i < kPinCount; i++) {
    pinMode(kPins[i].gpio, INPUT_PULLDOWN);
  }
  delay(100);

  for (size_t i = 0; i < kPinCount; i++) {
    uint8_t high = 0;
    for (uint8_t n = 0; n < 20; n++) {
      if (digitalRead(kPins[i].gpio) == HIGH) high++;
      delay(2);
    }

    const char *verdict;
    if (high >= 19) {
      verdict = "<<< quelque chose tient cette ligne HAUTE";
      found = true;
    } else if (high == 0) {
      verdict = "en l'air";
    } else {
      verdict = "instable";
    }

    Serial.printf("%-6s  %-4d  %2d/20    %s %s\n", kPins[i].label, kPins[i].gpio,
                  high, verdict, kPins[i].note);
  }

  Serial.println();
  if (!found) {
    Serial.println(F("AUCUNE broche n'est tenue au niveau haut."));
    Serial.println(F("Le TSOP38438 n'est donc pas alimente du tout. Verifier"));
    Serial.println(F("dans cet ordre :"));
    Serial.println(F("  a) 3V3 present sur la broche VS du TSOP (mesurer au"));
    Serial.println(F("     multimetre entre VS et GND : environ 3,3 V attendus)"));
    Serial.println(F("  b) resistance de 100 ohms en serie non coupee, et reliee"));
    Serial.println(F("     a 3V3 et non a 5V"));
    Serial.println(F("  c) brochage dome vers soi, pattes en bas : 1=OUT 2=GND 3=VS"));
    Serial.println(F("  d) masse commune entre le TSOP et la carte"));
    Serial.println(F("  e) contacts de la plaque d'essai : une rangee de la"));
    Serial.println(F("     plaque n'est pas toujours celle qu'on croit"));
  } else {
    Serial.println(F("Si la broche trouvee n'est pas D2, deplacer le fil OUT"));
    Serial.println(F("vers D2 (GPIO3), ou adapter kRecvPin dans main.cpp."));
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(500);

  Serial.println();
  Serial.println(F("==========================================================="));
  Serial.println(F(" Scanner de broches - XIAO ESP32-S3"));
  Serial.println(F("==========================================================="));
  Serial.println();

  staticScan();

  for (size_t i = 0; i < kPinCount; i++) {
    lastLevel[i] = digitalRead(kPins[i].gpio);
    transitions[i] = 0;
  }

  Serial.println(F("--- 2. Detection d'activite ---"));
  Serial.println(F("Appuyer sur une touche de la telecommande, pointee vers le"));
  Serial.println(F("TSOP a 20-50 cm. Bilan toutes les 3 secondes."));
  Serial.println();
}

void loop() {
  static uint32_t lastReport = 0;

  for (size_t i = 0; i < kPinCount; i++) {
    uint8_t level = digitalRead(kPins[i].gpio);
    if (level != lastLevel[i]) {
      transitions[i]++;
      lastLevel[i] = level;
    }
  }

  if (millis() - lastReport >= 3000) {
    lastReport = millis();

    bool any = false;
    for (size_t i = 0; i < kPinCount; i++) {
      if (transitions[i] > 0) {
        Serial.printf("  %-4s (GPIO%-2d) : %lu transitions\n", kPins[i].label,
                      kPins[i].gpio, (unsigned long)transitions[i]);
        transitions[i] = 0;
        any = true;
      }
    }
    if (any) {
      Serial.println();
    } else {
      Serial.println(F("  (aucune activite)"));
    }
  }
}
