#include <Arduino.h>
#include "config.h"



void setup() {
    Serial.begin(115200);
    Serial.println("Hello power monitor");
}

void loop() {
    Serial.println("Check new");
    delay(1000);
}
