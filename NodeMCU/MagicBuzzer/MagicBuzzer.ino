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

#define LED1        2 // D4
#define BUZZER_LED 15 // D8
#define BUZZER_PIN 14 // D5

volatile bool isBuzzered = false;
volatile bool buzzerHandled = false;
volatile uint32_t buzzerTick;

// The delay threshold for debounce checking.
const int debounceDelay = 50;

void ICACHE_RAM_ATTR buzzerInterruptCallback() {
  // check if already buzzered
  if (isBuzzered)
  {
    return;
  }

  // Get the pin reading.
  bool buzzerPressed = !digitalRead(BUZZER_PIN); // pin is low active
  if (buzzerPressed) {
    buzzerTick = millis();
    isBuzzered = true;
    buzzerHandled = false;
  }
}

void setup() {
  Serial.begin(115200);

  digitalWrite(BUZZER_LED, LOW);
  pinMode(BUZZER_LED, OUTPUT);

  pinMode(BUZZER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUZZER_PIN), buzzerInterruptCallback, FALLING);

  digitalWrite(LED1, LOW);
  pinMode(LED1, OUTPUT);

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

    // reset buzzer
    digitalWrite(BUZZER_LED, LOW);
    isBuzzered = false;
  }

  if (isBuzzered && !buzzerHandled) {
    Serial.printf("Buzzered: %d\n", buzzerTick);
    digitalWrite(BUZZER_LED, HIGH);
    buzzerHandled = true;
  }
}

uint32_t getTick() {
  return masterTick + millis() - localTick;
}
