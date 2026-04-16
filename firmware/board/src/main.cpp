#include <Arduino.h>
#include <Wire.h>

#define ESP_1_ADDR 0x10

void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(100000);
    Serial.println("Mega ready — sending test messages...");
}

void loop() {
    Serial.println("Sending ping to ESP32...");
    
    Wire.beginTransmission(ESP_1_ADDR);
    Wire.write("test from arduino");
    byte error = Wire.endTransmission();

    if (error == 0) {
        Serial.println("Transmission successful");
    } else {
        Serial.print("Transmission failed, error: ");
        Serial.println(error);
    }

    delay(2000);
}