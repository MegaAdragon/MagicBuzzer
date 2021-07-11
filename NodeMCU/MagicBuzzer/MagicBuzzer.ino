#include <ESP8266WiFi.h>

#include "ESPAsyncUDP.h"

const char* ssid = "";
const char* password =  "";

#define UDP_PORT 4210 // listen port

AsyncUDP udp;

uint32_t localTick;
uint32_t masterTick;

// TODO: remove -> only to test sync
uint32_t trigger;

#define LED1 2

void setup() {
  digitalWrite(LED1, LOW);
  pinMode(LED1, OUTPUT);

  Serial.begin(115200);

  delay(1000);
  WiFi.begin(ssid, password);

  // wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  Serial.print("Connected to WiFi. IP:");
  Serial.println(WiFi.localIP());

  if (udp.listen(UDP_PORT)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udp.onPacket([](AsyncUDPPacket packet) {
      if (packet.length() == sizeof(uint32_t))
      {
        localTick = millis();
        memcpy((uint8_t*)&masterTick, packet.data(), sizeof(masterTick));
        udp.writeTo((uint8_t*)&localTick, sizeof(localTick), packet.remoteIP(), UDP_PORT + 1);

        trigger = masterTick + 2000;
      }
    });
  }
}

void loop() {
  if (getTick() > trigger) {
    digitalWrite(LED1, !digitalRead(LED1));
    trigger = getTick() + 2000;
  }
}

uint32_t getTick() {
  return masterTick + millis() - localTick;
}
