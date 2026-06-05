// ============================================================
//  ESP32-C3 BLE Macropad — Firmware v2
//  Layers: 0 = Numbers, 1 = Custom Shortcuts
//  K9  = Volume Up (works in both layers)
//  K11 = Layer Toggle (switches between Layer 0 and Layer 1)
// ============================================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define DEVICE_NAME       "ESP32-C3 Macropad"
#define MANUFACTURER_NAME "DIY"

#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_ADDR  0x3C
#define OLED_SDA     8
#define OLED_SCL     9

#define ROWS 4
#define COLS 3
const int rowPins[ROWS] = {0, 1, 2, 3};
const int colPins[COLS] = {5, 6, 7};

#define DEBOUNCE_MS        150
#define ADV_RESTART_MS     500
#define MTU_SIZE           100

// ---- BLE State ----
enum class BleState {
  INIT, ADVERTISING, PAIRING, CONNECTED, DISCONNECTED
};

volatile BleState bleState    = BleState::INIT;
volatile bool     needReAdv   = false;
uint32_t          connectedAt = 0;

// ---- Layer State ----
// 0 = Numbers layer, 1 = Shortcuts layer
int currentLayer = 0;

// ---- HID Modifier / Keycode constants ----
#define MOD_NONE  0x00
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_GUI   0x08  // Windows key

// Number keycodes
#define KEY_0  0x27
#define KEY_1  0x1E
#define KEY_2  0x1F
#define KEY_3  0x20
#define KEY_4  0x21
#define KEY_5  0x22
#define KEY_6  0x23
#define KEY_7  0x24
#define KEY_8  0x25
#define KEY_9  0x26

// Letter keycodes
#define KEY_A  0x04
#define KEY_C  0x06
#define KEY_D  0x07
#define KEY_F4 0x3D  // F4
#define KEY_S  0x16
#define KEY_T  0x17
#define KEY_W  0x1A
#define KEY_X  0x1B
#define KEY_Y  0x1C
#define KEY_Z  0x1D

// Consumer / Media keycodes (sent via consumer report, not keyboard report)
#define CONSUMER_VOL_UP   0xE9
#define CONSUMER_VOL_DOWN 0xEA
#define CONSUMER_MUTE     0xE2

// ---- Special key actions ----
#define ACTION_NONE        0
#define ACTION_LAYER_TOGGLE 1
#define ACTION_VOLUME_UP   2

// ---- Key definition ----
struct KeyDef {
  uint8_t     mod;
  uint8_t     keycode;
  uint8_t     action;    // Special action (0 = normal key, see ACTION_* above)
  const char* label;
};

// ============================================================
//  KEY MAP
//  Layout:
//    K0  K1  K2
//    K3  K4  K5
//    K6  K7  K8
//    K9  K10 K11
//
//  Layer 0 (Numbers):
//    1   2   3
//    4   5   6
//    7   8   9
//   VOL  0  [LYR]
//
//  Layer 1 (Shortcuts):
//   C+Z  C+S  C+A
//   C+X  C+Y  W+D
//   A+F4 C+W  C+T
//   VOL   0  [LYR]
//
//  K9  = Volume Up   (same in both layers)
//  K10 = 0           (same in both layers)
//  K11 = Layer Toggle(same in both layers)
// ============================================================

const KeyDef keyMap[2][ROWS][COLS] = {

  // ---- LAYER 0: Numbers ----
  {
    { {MOD_NONE, KEY_1, ACTION_NONE,         "1"    },
      {MOD_NONE, KEY_2, ACTION_NONE,         "2"    },
      {MOD_NONE, KEY_3, ACTION_NONE,         "3"    } },

    { {MOD_NONE, KEY_4, ACTION_NONE,         "4"    },
      {MOD_NONE, KEY_5, ACTION_NONE,         "5"    },
      {MOD_NONE, KEY_6, ACTION_NONE,         "6"    } },

    { {MOD_NONE, KEY_7, ACTION_NONE,         "7"    },
      {MOD_NONE, KEY_8, ACTION_NONE,         "8"    },
      {MOD_NONE, KEY_9, ACTION_NONE,         "9"    } },

    { {MOD_NONE, 0x00,  ACTION_VOLUME_UP,    "VolUp"},
      {MOD_NONE, KEY_0, ACTION_NONE,         "0"    },
      {MOD_NONE, 0x00,  ACTION_LAYER_TOGGLE, "MODE" } },
  },

  // ---- LAYER 1: Custom Shortcuts ----
  {
    { {MOD_CTRL, KEY_Z, ACTION_NONE,         "Ctrl+Z"},
      {MOD_CTRL, KEY_S, ACTION_NONE,         "Ctrl+S"},
      {MOD_CTRL, KEY_A, ACTION_NONE,         "Ctrl+A"} },

    { {MOD_CTRL, KEY_X, ACTION_NONE,         "Ctrl+X"},
      {MOD_CTRL, KEY_Y, ACTION_NONE,         "Ctrl+Y"},
      {MOD_GUI,  KEY_D, ACTION_NONE,         "Win+D" } },

    { {MOD_ALT,  KEY_F4,ACTION_NONE,         "Alt+F4"},
      {MOD_CTRL, KEY_W, ACTION_NONE,         "Ctrl+W"},
      {MOD_CTRL, KEY_T, ACTION_NONE,         "Ctrl+T"} },

    { {MOD_NONE, 0x00,  ACTION_VOLUME_UP,    "VolUp" },
      {MOD_NONE, KEY_0, ACTION_NONE,         "0"     },
      {MOD_NONE, 0x00,  ACTION_LAYER_TOGGLE, "MODE"  } },
  }
};

// ============================================================
//  BLE / HID objects
// ============================================================
NimBLEHIDDevice*      hid         = nullptr;
NimBLECharacteristic* inputKbd    = nullptr;
NimBLECharacteristic* inputConsumer = nullptr;
NimBLEServer*         pServer     = nullptr;
Adafruit_SSD1306      display(SCREEN_W, SCREEN_H, &Wire, -1);
bool                  oledOk      = false;

// ---- Forward declarations ----
void updateOLED(const char* l1, const char* l2, const char* l3, const char* l4 = "");
void startAdvertising();
void sendKey(uint8_t modifier, uint8_t keycode);
void sendVolumeUp();
void showLayerScreen();
const char* stateName(BleState s);

// ============================================================
//  BLE Server Callbacks
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {

  void onConnect(NimBLEServer* pSrv, NimBLEConnInfo& connInfo) override {
    bleState = BleState::PAIRING;
    Serial.printf("[BLE] Host connected — addr: %s\n",
                  connInfo.getAddress().toString().c_str());
    updateOLED("BLE", "Pairing...", connInfo.getAddress().toString().c_str());
    pSrv->setDataLen(connInfo.getConnHandle(), MTU_SIZE);
    NimBLEDevice::setMTU(MTU_SIZE);
  }

  void onDisconnect(NimBLEServer* pSrv, NimBLEConnInfo& connInfo, int reason) override {
    bleState  = BleState::DISCONNECTED;
    needReAdv = true;
    Serial.printf("[BLE] Disconnected — reason: 0x%02X (%d)\n", reason, reason);
    updateOLED("BLE", "Disconnected", "Re-advertising...");
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (connInfo.isEncrypted()) {
      bleState    = BleState::CONNECTED;
      connectedAt = millis();
      Serial.println("[BLE] Authenticated + encrypted — HID ready");
      showLayerScreen();
    } else {
      Serial.println("[BLE] Auth failed — disconnecting");
      NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
    }
  }
};

// ============================================================
//  OLED helpers
// ============================================================
void updateOLED(const char* l1, const char* l2, const char* l3, const char* l4) {
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header bar
  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(DEVICE_NAME);
  display.setTextColor(SSD1306_WHITE);

  // Connected dot
  if (bleState == BleState::CONNECTED) {
    display.fillCircle(SCREEN_W - 5, 5, 3, SSD1306_BLACK);
  }

  display.setCursor(0, 13); display.print(l1);
  display.setCursor(0, 25); display.print(l2);
  display.setCursor(0, 37); display.print(l3);
  display.setCursor(0, 49); display.print(l4);
  display.display();
}

// Show layer info on OLED after connect or layer switch
void showLayerScreen() {
  if (currentLayer == 0) {
    updateOLED("Layer 0: Numbers", "1-9, 0", "K9=VolUp K11=MODE", "");
  } else {
    updateOLED("Layer 1: Shortcuts", "C+Z C+S C+A", "C+X C+Y Win+D", "A+F4 C+W C+T");
  }
}

// ============================================================
//  BLE Advertising
// ============================================================
void startAdvertising() {
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->reset();
  pAdv->setAppearance(HID_KEYBOARD);
  pAdv->addServiceUUID(hid->getHidService()->getUUID());
  NimBLEAdvertisementData scanRsp;
  scanRsp.setName(DEVICE_NAME);
  pAdv->setScanResponseData(scanRsp);
  pAdv->enableScanResponse(true);
  pAdv->setMinInterval(0x20);
  pAdv->setMaxInterval(0x40);
  bleState = BleState::ADVERTISING;
  if (pAdv->start()) {
    Serial.println("[BLE] Advertising started — waiting for host");
    updateOLED("BLE", "Advertising...", "Waiting for host");
  } else {
    Serial.println("[BLE] !! Advertising FAILED !!");
    updateOLED("BLE", "ADV FAILED", "Reset device");
  }
}

// ============================================================
//  Key Sending
// ============================================================
void sendKey(uint8_t modifier, uint8_t keycode) {
  if (bleState != BleState::CONNECTED || inputKbd == nullptr) {
    Serial.println("[KEY] Not connected — key ignored");
    return;
  }
  uint8_t down[8]    = {modifier, 0x00, keycode, 0, 0, 0, 0, 0};
  uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  inputKbd->setValue(down, sizeof(down));
  inputKbd->notify();
  delay(10);
  inputKbd->setValue(release, sizeof(release));
  inputKbd->notify();
  delay(10);
}

// Send a consumer/media key (Volume Up uses HID consumer report ID 2)
void sendVolumeUp() {
  if (bleState != BleState::CONNECTED || inputConsumer == nullptr) {
    Serial.println("[KEY] Not connected — volume key ignored");
    return;
  }
  // Consumer report: 2 bytes, usage ID 16-bit little-endian
  uint8_t press[2]   = {CONSUMER_VOL_UP, 0x00};
  uint8_t release[2] = {0x00, 0x00};
  inputConsumer->setValue(press, sizeof(press));
  inputConsumer->notify();
  delay(10);
  inputConsumer->setValue(release, sizeof(release));
  inputConsumer->notify();
  delay(10);
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("[BOOT] ESP32-C3 BLE Macropad v2 starting...");

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oledOk) {
    Serial.println("[OLED] Not found — continuing without display");
  } else {
    display.clearDisplay();
    display.display();
    updateOLED("Booting...", "", "");
    Serial.println("[OLED] OK");
  }

  // Matrix pins
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }
  Serial.println("[MATRIX] Pins configured");

  // BLE init
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  hid = new NimBLEHIDDevice(pServer);
  hid->setManufacturer(MANUFACTURER_NAME);
  hid->setPnp(0x01, 0x0000, 0x0000, 0x0100);
  hid->setHidInfo(0x00, 0x01);

  // HID Report Map: Keyboard (Report ID 1) + Consumer (Report ID 2)
  static const uint8_t reportMap[] = {
    // --- Keyboard (Report ID 1) ---
    0x05, 0x01,        // Usage Page: Generic Desktop
    0x09, 0x06,        // Usage: Keyboard
    0xA1, 0x01,        // Collection: Application
    0x85, 0x01,        //   Report ID: 1
    0x05, 0x07,        //   Usage Page: Keyboard/Keypad
    0x19, 0xE0,        //   Usage Minimum: Left Control
    0x29, 0xE7,        //   Usage Maximum: Right GUI
    0x15, 0x00,        //   Logical Minimum: 0
    0x25, 0x01,        //   Logical Maximum: 1
    0x75, 0x01,        //   Report Size: 1
    0x95, 0x08,        //   Report Count: 8
    0x81, 0x02,        //   Input: Data, Variable, Absolute (modifier byte)
    0x95, 0x01,        //   Report Count: 1
    0x75, 0x08,        //   Report Size: 8
    0x81, 0x01,        //   Input: Constant (reserved byte)
    0x95, 0x06,        //   Report Count: 6
    0x75, 0x08,        //   Report Size: 8
    0x15, 0x00,        //   Logical Minimum: 0
    0x25, 0x65,        //   Logical Maximum: 101
    0x05, 0x07,        //   Usage Page: Keyboard/Keypad
    0x19, 0x00,        //   Usage Minimum: 0
    0x29, 0x65,        //   Usage Maximum: 101
    0x81, 0x00,        //   Input: Data, Array, Absolute (key array)
    0xC0,              // End Collection

    // --- Consumer (Report ID 2) ---
    0x05, 0x0C,        // Usage Page: Consumer
    0x09, 0x01,        // Usage: Consumer Control
    0xA1, 0x01,        // Collection: Application
    0x85, 0x02,        //   Report ID: 2
    0x15, 0x00,        //   Logical Minimum: 0
    0x26, 0xFF, 0x03,  //   Logical Maximum: 1023
    0x19, 0x00,        //   Usage Minimum: 0
    0x2A, 0xFF, 0x03,  //   Usage Maximum: 1023
    0x75, 0x10,        //   Report Size: 16
    0x95, 0x01,        //   Report Count: 1
    0x81, 0x00,        //   Input: Data, Array, Absolute
    0xC0               // End Collection
  };
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));

  // Get characteristics
  inputKbd      = hid->getInputReport(1);   // Keyboard report
  inputConsumer = hid->getInputReport(2);   // Consumer report

  hid->startServices();
  Serial.println("[BLE] HID services started (Keyboard + Consumer)");

  startAdvertising();
  Serial.println("[BOOT] Ready — waiting for BLE host");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  // Re-advertise after disconnect
  if (needReAdv) {
    needReAdv = false;
    delay(ADV_RESTART_MS);
    startAdvertising();
  }

  // Periodic uptime log
  static uint32_t lastPrint = 0;
  if (bleState == BleState::CONNECTED && millis() - lastPrint > 10000) {
    lastPrint = millis();
    uint32_t upSec = (millis() - connectedAt) / 1000;
    Serial.printf("[BLE] Connected — uptime %lu s, clients: %d, layer: %d\n",
                  upSec, pServer->getConnectedCount(), currentLayer);
  }

  // Matrix scan
  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        const KeyDef& k  = keyMap[currentLayer][r][c];
        int            kn = r * COLS + c;

        Serial.printf("[KEY] K%d pressed -> %s  (layer: %d, state: %s)\n",
                      kn, k.label, currentLayer, stateName(bleState));

        // ---- Handle special actions first ----
        if (k.action == ACTION_LAYER_TOGGLE) {
          // Toggle layer regardless of BLE state
          currentLayer = (currentLayer == 0) ? 1 : 0;
          Serial.printf("[LAYER] Switched to layer %d\n", currentLayer);
          if (bleState == BleState::CONNECTED) {
            showLayerScreen();
          } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "Layer: %d", currentLayer);
            updateOLED("Layer Changed", buf, stateName(bleState));
          }
          delay(DEBOUNCE_MS);
          digitalWrite(rowPins[r], HIGH);
          return; // Skip rest of scan this cycle
        }

        if (k.action == ACTION_VOLUME_UP) {
          if (bleState == BleState::CONNECTED) {
            updateOLED("Sent:", "Volume Up", "", "");
            sendVolumeUp();
            delay(DEBOUNCE_MS);
            showLayerScreen();
          } else {
            updateOLED("Not Connected", stateName(bleState), "Key ignored", "");
            delay(DEBOUNCE_MS);
          }
          digitalWrite(rowPins[r], HIGH);
          return;
        }

        // ---- Normal key ----
        if (bleState == BleState::CONNECTED) {
          char buf[24];
          snprintf(buf, sizeof(buf), "K%d: %s", kn, k.label);
          updateOLED("Sent:", buf, "", "");
          sendKey(k.mod, k.keycode);
          delay(DEBOUNCE_MS);
          showLayerScreen();
        } else {
          char stateBuf[32];
          snprintf(stateBuf, sizeof(stateBuf), "State: %s", stateName(bleState));
          updateOLED("Not Connected", stateBuf, "Key ignored", "");
          delay(DEBOUNCE_MS);
        }
      }
    }
    digitalWrite(rowPins[r], HIGH);
  }
}

// ============================================================
//  Helpers
// ============================================================
const char* stateName(BleState s) {
  switch (s) {
    case BleState::INIT:         return "INIT";
    case BleState::ADVERTISING:  return "ADVERTISING";
    case BleState::PAIRING:      return "PAIRING";
    case BleState::CONNECTED:    return "CONNECTED";
    case BleState::DISCONNECTED: return "DISCONNECTED";
    default:                     return "UNKNOWN";
  }
}
