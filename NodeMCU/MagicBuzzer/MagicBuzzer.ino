#include <assert.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include "ESPAsyncUDP.h"

#define HARDWARE_V2

#define LED1        2 // D4
#define BUZZER_LED 15 // D8
#define BUZZER_PIN 14 // D5
#define B1_PIN     12
#define B2_PIN     13

typedef struct {
  const char* ssid;
  const char* password;
} WifiInfo;

const WifiInfo wifiList[] = {
  {
    .ssid = "BuzzerWifi",
    .password = "BuzzerWifi"
  },
/*
 * add additional Wifi networks here
 */
//  {
//    .ssid = "",
//    .password = ""
//  }
};

typedef struct __attribute__ ((packed)) {
  uint16_t voltage;
  int8_t rssi;
} BuzzerState;

typedef enum {
  SRC_BUZZER = 0,
  SRC_B1 = 1,
  SRC_B2 = 2  
} BUZZER_SRC;

typedef enum {
  MODE_BUZZER_ONLY = 0, // default
  MODE_BTN_ONLY = 1,
  MODE_ALL = 2
} BUZZER_MODE;

typedef struct {
  bool isBuzzered;
  bool isHandled;
  uint32_t buzzerTick;
  BUZZER_SRC src;
} BuzzerEvent;

#define OLED_RESET      -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS  0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define SCREEN_WIDTH    128 // OLED display width, in pixels
#define SCREEN_HEIGHT   32 // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define UDP_PORT 4210 // listen port

AsyncUDP udp;
WiFiServer wifiServer(9999);  // TCP socket server on port 9999

uint32_t localTick;
uint32_t masterTick;

uint8_t buzzerPos = 0xFF;
volatile BUZZER_MODE mode = MODE_BUZZER_ONLY;

volatile BuzzerEvent evt;
BuzzerState buzzerState;

void ICACHE_RAM_ATTR buzzerInterruptCallback() {
  // check mode and if already buzzered
  if (mode == MODE_BTN_ONLY || evt.isBuzzered)
  {
    return;
  }

  // get the pin
  if (!digitalRead(BUZZER_PIN)) {
    evt.buzzerTick = getTick();
    evt.isBuzzered = true;
    evt.isHandled = false;
    evt.src = SRC_BUZZER;
  }
}

void ICACHE_RAM_ATTR btnInterruptCallback() {
  // check mode and if already buzzered
  if (mode == MODE_BUZZER_ONLY || evt.isBuzzered)
  {
    return;
  }

  if (!digitalRead(B1_PIN)) {
    evt.buzzerTick = getTick();
    evt.isBuzzered = true;
    evt.isHandled = false;
    evt.src = SRC_B1;
  }
  else if (!digitalRead(B2_PIN)) {
    evt.buzzerTick = getTick();
    evt.isBuzzered = true;
    evt.isHandled = false;
    evt.src = SRC_B2;
  }
}

void setup() {
  Serial.begin(115200);

  // configure output pin
  digitalWrite(BUZZER_LED, LOW);
  pinMode(BUZZER_LED, OUTPUT);

  // LED on chip (only for debug purposes)
  digitalWrite(LED1, LOW);
  pinMode(LED1, OUTPUT);

  // configure input pins + register interrupt callbacks
  pinMode(BUZZER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUZZER_PIN), buzzerInterruptCallback, FALLING);
  pinMode(B1_PIN, INPUT_PULLUP);
  pinMode(B2_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(B1_PIN), btnInterruptCallback, FALLING);
  attachInterrupt(digitalPinToInterrupt(B2_PIN), btnInterruptCallback, FALLING);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  // flip the display
  display.setRotation(2);

  display.clearDisplay();
  display.setTextSize(1); // -> font height: 8px
  display.setTextColor(SSD1306_WHITE); // Draw white text

  delay(100);

  // wait for WiFi connection
  int wifiIdx = 0;
  while (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Connecting to WiFi:");
    display.setCursor(0, 12);
    display.print(wifiList[wifiIdx].ssid);
    display.display();

    WiFi.begin(wifiList[wifiIdx].ssid, wifiList[wifiIdx].password);
    Serial.println("Connecting...");

    // wait 10s for connection
    int timeoutCnt = 0;
    while (WiFi.status() != WL_CONNECTED && timeoutCnt < 100) {
      delay(100);
      timeoutCnt++;
    }

    // try next WiFi in list
    wifiIdx++;
    if (wifiIdx >= sizeof(wifiList) / sizeof(wifiList[0])) {
      wifiIdx = 0;
    }
  }

  Serial.print("Connected to WiFi. IP:");
  Serial.println(WiFi.localIP());

  // open UDP listen port + register callback
  if (udp.listen(UDP_PORT)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());

    // callback handler is called for every received UDP paket
    udp.onPacket([](AsyncUDPPacket packet) {
      // valid sync paket
      if (packet.length() == sizeof(uint32_t)) {
        localTick = millis();
        memcpy((uint8_t*)&masterTick, packet.data(), sizeof(masterTick));
        udp.writeTo((uint8_t*)&localTick, sizeof(localTick), packet.remoteIP(), UDP_PORT);
        Serial.println("Sync");
      }
    });
  }

  // start TCP server
  wifiServer.begin();
}

void loop() {
  WiFiClient client = wifiServer.available();
  static uint32_t lastHeartbeatTick = 0;
  static uint32_t lastBlinkTick = 0;

  if (client) {
    Serial.print("Connected to client: ");
    Serial.println(client.remoteIP());

    resetBuzzer();

    while (client.connected()) {
      // handle incoming data
      commHandler(client);

      // handle buzzer
      if (evt.isBuzzered && !evt.isHandled) {
        // header + payload
        uint8_t buf[sizeof(evt.buzzerTick) + 3];
        Serial.printf("Buzzered: %d\n", evt.buzzerTick);
        digitalWrite(BUZZER_LED, HIGH);
        buf[0] = 0x01;
        buf[1] = sizeof(evt.buzzerTick) + 1;
        memcpy(&buf[2], (uint8_t*)&evt.buzzerTick, sizeof(evt.buzzerTick));
        buf[6] = evt.src;
        client.write(buf, sizeof(buf));
        evt.isHandled = true;
      }

      // check buzzer position
      if (evt.isBuzzered && buzzerPos == 1) {
        if (millis() - lastBlinkTick > 100) {
          digitalWrite(BUZZER_LED, !digitalRead(BUZZER_LED));
          lastBlinkTick = millis();
        }
      }

      // handle heartbeat
      if (millis() - lastHeartbeatTick > 1000) {
        sendHeartbeat(client);
        lastHeartbeatTick = millis();
      }

      updateBuzzerState();
      updateDisplay(client);
      delay(50);
    }

    client.stop();
    Serial.println("Client disconnected");
  }

  // idle blinking
  if (millis() - lastBlinkTick > 2500) {
    digitalWrite(LED1, !digitalRead(LED1));
    digitalWrite(BUZZER_LED, !digitalRead(BUZZER_LED));
    lastBlinkTick = millis();
  }

  updateBuzzerState();
  updateDisplay(client);
  delay(200);
}

// read incoming commands from data stream
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
  byte cmd = data[0];
  switch (cmd) {
    case 0x10:  // reset buzzer
      resetBuzzer();
      buzzerPos = 0xFF;
      break;
    case 0x20:  // assign buzzer position
      buzzerPos = data[1];
      break;
    case 0xA0:  // set buzzer mode
      mode = (BUZZER_MODE)data[1];
      break;
    default:
      break;
  }
}

// get tick based on network timestamp
uint32_t getTick() {
  return masterTick + millis() - localTick;
}

void resetBuzzer() {
  evt.isBuzzered = false;
  evt.isHandled = true;
  digitalWrite(BUZZER_LED, LOW);
}

void updateDisplay(WiFiClient& client) {
  display.clearDisplay();
  display.setTextSize(1); // -> font height: 8px
  display.setRotation(2);

  if (evt.isBuzzered) {
    display.setRotation(3);
    display.setTextSize(1);
    
    if (evt.src == SRC_BUZZER) {
      display.setCursor(0, 80);
      display.printf("BUZ");
    }
    else if (evt.src == SRC_B1) {
      display.setCursor(0, 80);
      display.printf("B1");
    }
    else if (evt.src == SRC_B2) {
      display.setCursor(10, 80);
      display.printf("B2");
    }
    
    if (buzzerPos != 0xFF) {
      display.setTextSize(4);
      display.setCursor(0, 20);
      display.printf("%d", buzzerPos);
    }

    display.display();
    return;
  }             

  display.setCursor(0, 0);
  if (client.connected()) {
    display.print("Connected");
  }
  else {
    display.print("Wait for host...");
  }

  display.setCursor(0, 10);
  display.print(WiFi.localIP());

  display.setCursor(0, 24);
  display.printf("VBAT: %.2f\n", buzzerState.voltage / 1000.0f);

  display.display();
}

void updateBuzzerState() {
      // apply small offset to ADC value -> seems to improve accuracy
#if defined(HARDWARE_V1)
      buzzerState.voltage = (uint32_t)(((float)((analogRead(A0) - 25) / 1023.0f)) / (8.2f / (8.2f + 33.0f)) * 1000);
#elif defined(HARDWARE_V2)
      buzzerState.voltage = (uint32_t)(((float)((analogRead(A0) - 25) / 1023.0f)) / (10.0f / (10.0f + 33.0f)) * 1000);
#endif

      buzzerState.rssi = WiFi.RSSI();
}

void sendHeartbeat(WiFiClient& client) {
  uint8_t buf[20];
  buf[0] = 0xAA; // heartbeat
  buf[1] = sizeof(buzzerState); // msg size
  memcpy(&buf[2], &buzzerState, sizeof(buzzerState));
  client.write(buf, sizeof(buzzerState) + 2);
}
