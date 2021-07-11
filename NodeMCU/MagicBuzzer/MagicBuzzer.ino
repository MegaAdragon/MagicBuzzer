#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include "ESPAsyncUDP.h"

const char* ssid = "";
const char* password =  "";

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.display();

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
