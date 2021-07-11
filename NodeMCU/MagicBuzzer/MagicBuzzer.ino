#include <assert.h>
#include <ESP8266WiFi.h>

#include "ESPAsyncUDP.h"

const char* ssid = "";
const char* password =  "";

#define UDP_PORT 4210 // listen port

AsyncUDP udp;
WiFiServer wifiServer(9999);  // TCP socket server on port 9999

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
      Serial.println("Sync");

      if (packet.length() == sizeof(uint32_t))
      {
        localTick = millis();
        memcpy((uint8_t*)&masterTick, packet.data(), sizeof(masterTick));
        udp.writeTo((uint8_t*)&localTick, sizeof(localTick), packet.remoteIP(), UDP_PORT);

        trigger = masterTick + 2000;
      }
    });
  }

  // start TCP server
  wifiServer.begin();
}

void loop() {
  if (getTick() > trigger) {
    digitalWrite(LED1, !digitalRead(LED1));
    digitalWrite(BUZZER_LED, !digitalRead(BUZZER_LED));
    trigger = getTick() + 2000;
  }

  WiFiClient client = wifiServer.available();
  if (client) {
    Serial.print("Connected to client: ");
    Serial.println(client.remoteIP());

    resetBuzzer();

    while (client.connected()) {
      //commHandler(client);  // handle incoming data

      if (isBuzzered && !buzzerHandled) {
        Serial.printf("Buzzered: %d\n", buzzerTick);
        digitalWrite(BUZZER_LED, HIGH);
        client.write((uint8_t*)&buzzerTick, sizeof(buzzerTick));
        buzzerHandled = true;
      }
    }
  }
}

void commHandler(WiFiClient& client) {
  static byte buf[32];
  static int idx = 0;

  // read all incoming byte into buffer
  while (client.available() > 0) {
    byte b = client.read();
    buf[idx] = b;
    idx++;

    if (idx >= sizeof(buf)) {
      assert(false);
      idx = 0; // overflow -> this should never happen
      Serial.println("Error: overflow");
    }

    // found msg delimiter
    if (b == 0xFF) {
      break;
    }
  }

  // if no data in buffer or msg delimiter missing -> nothing to do
  if (idx < 1 || buf[idx - 1] != 0xFF) {
    return;
  }

  handleCommand(client, buf, idx);  // handle received command
  idx = 0;
}

void handleCommand(WiFiClient& client, byte data[], int length) {

}

uint32_t getTick() {
  return masterTick + millis() - localTick;
}

void resetBuzzer() {
  isBuzzered = false;
  digitalWrite(BUZZER_LED, LOW);
  buzzerHandled = true;
}
