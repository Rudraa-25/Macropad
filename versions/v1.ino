/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║   ESP32-C3 BLE Macropad Firmware  —  NimBLE 2.x Fixed      ║
 * ║   NimBLE-Arduino 2.x  |  ESP32 Core 3.x  |  Arduino IDE    ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * FIX: NimBLESecurityCallbacks was removed in NimBLE-Arduino 2.x.
 *      Security methods (onPassKeyEntry, onConfirmPasskey, etc.)
 *      are now part of NimBLEServerCallbacks directly.
 *      NimBLEDevice::setSecurityCallbacks() no longer exists.
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════════════════════════
//  CONFIGURATION
// ═══════════════════════════════════════════════════════════════
#define DEVICE_NAME       "ESP32-C3 Macropad"
#define MANUFACTURER_NAME "DIY"

// OLED
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_ADDR  0x3C
#define OLED_SDA     8
#define OLED_SCL     9

// Matrix pins
#define ROWS 4
#define COLS 3
const int rowPins[ROWS] = {0, 1, 2, 3};
const int colPins[COLS] = {5, 6, 7};

// Timing
#define DEBOUNCE_MS        150
#define ADV_RESTART_MS     500
#define MTU_SIZE           100

// ═══════════════════════════════════════════════════════════════
//  BLE STATE MACHINE
// ═══════════════════════════════════════════════════════════════
enum class BleState {
  INIT,
  ADVERTISING,
  PAIRING,
  CONNECTED,
  DISCONNECTED
};

volatile BleState bleState    = BleState::INIT;
volatile bool     needReAdv   = false;
uint32_t          connectedAt = 0;

// ═══════════════════════════════════════════════════════════════
//  HID KEYCODES & MODIFIERS
// ═══════════════════════════════════════════════════════════════
#define MOD_NONE  0x00
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_GUI   0x08

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
#define KEY_C  0x06
#define KEY_V  0x19

// ═══════════════════════════════════════════════════════════════
//  KEY MAP
// ═══════════════════════════════════════════════════════════════
struct KeyDef {
  uint8_t     mod;
  uint8_t     keycode;
  const char* label;
};

// Physical layout:
//  [9] [1] [2]
//  [3] [4] [5]
//  [6] [7] [8]
//  [C] [0] [V]
const KeyDef keyMap[ROWS][COLS] = {
  { {MOD_NONE, KEY_9, "9"},      {MOD_NONE, KEY_1, "1"},      {MOD_NONE, KEY_2, "2"}      },
  { {MOD_NONE, KEY_3, "3"},      {MOD_NONE, KEY_4, "4"},      {MOD_NONE, KEY_5, "5"}      },
  { {MOD_NONE, KEY_6, "6"},      {MOD_NONE, KEY_7, "7"},      {MOD_NONE, KEY_8, "8"}      },
  { {MOD_CTRL, KEY_C, "Ctrl+C"}, {MOD_NONE, KEY_0, "0"},      {MOD_CTRL, KEY_V, "Ctrl+V"} },
};

// ═══════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════
NimBLEHIDDevice*      hid      = nullptr;
NimBLECharacteristic* inputKbd = nullptr;
NimBLEServer*         pServer  = nullptr;
Adafruit_SSD1306      display(SCREEN_W, SCREEN_H, &Wire, -1);
bool                  oledOk   = false;

// ═══════════════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════
void updateOLED(const char* l1, const char* l2, const char* l3, const char* l4 = "");
void startAdvertising();
void sendKey(uint8_t modifier, uint8_t keycode);
const char* stateName(BleState s);

// ═══════════════════════════════════════════════════════════════
//  BLE SERVER CALLBACKS
//  ── NimBLE 2.x: security methods live HERE, not in a separate
//     NimBLESecurityCallbacks class (that class no longer exists)
// ═══════════════════════════════════════════════════════════════
class ServerCallbacks : public NimBLEServerCallbacks {

  // ── Connection events ──────────────────────────────────────
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
      updateOLED("BLE Connected", connInfo.getAddress().toString().c_str(), "Ready", "");
    } else {
      Serial.println("[BLE] Auth failed — disconnecting");
      NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
    }
  }

  // ── Security / pairing events (moved here from NimBLESecurityCallbacks) ──
  //
  // onPassKeyEntry: host asks us to enter a passkey
  void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
    Serial.println("[BLE] PassKey entry requested — injecting 000000");
    NimBLEDevice::injectPassKey(connInfo, 000000);
  }

  // onConfirmPasskey: numeric comparison — host shows a number, we confirm
  bool onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pass_key) override {
    Serial.printf("[BLE] Confirm passkey: %06lu — auto-accepting\n", pass_key);
    char buf[12];
    snprintf(buf, sizeof(buf), "PIN: %06lu", pass_key);
    updateOLED("BLE Pairing", buf, "Auto-accept");
    return true;
  }

  // onSecurityRequest: host initiated security — return true to proceed
  bool onSecurityRequest() override {
    return true;
  }
};

// ═══════════════════════════════════════════════════════════════
//  OLED HELPER
// ═══════════════════════════════════════════════════════════════
void updateOLED(const char* l1, const char* l2, const char* l3, const char* l4) {
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(DEVICE_NAME);
  display.setTextColor(SSD1306_WHITE);

  if (bleState == BleState::CONNECTED) {
    display.fillCircle(SCREEN_W - 5, 5, 3, SSD1306_BLACK);
  }

  display.setCursor(0, 13); display.print(l1);
  display.setCursor(0, 25); display.print(l2);
  display.setCursor(0, 37); display.print(l3);
  display.setCursor(0, 49); display.print(l4);
  display.display();
}

// ═══════════════════════════════════════════════════════════════
//  START ADVERTISING
// ═══════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════
//  SEND HID KEY REPORT
// ═══════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n\n[BOOT] ESP32-C3 BLE Macropad starting...");

  // ── OLED ──────────────────────────────────────────────────────
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

  // ── MATRIX PINS ───────────────────────────────────────────────
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }
  Serial.println("[MATRIX] Pins configured");

  // ── NimBLE INIT ───────────────────────────────────────────────
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // ── SECURITY ──────────────────────────────────────────────────
  // bonding=true | MITM=true | SC=true
  // Using NO_INPUT_OUTPUT = "Just Works" pairing (no PIN required on host)
  // If pairing fails on your OS, try setSecurityAuth(true, false, false) first
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  // NOTE: No setSecurityCallbacks() call here — removed in NimBLE 2.x

  // ── SERVER ────────────────────────────────────────────────────
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());  // security methods included here

  // ── HID DEVICE ────────────────────────────────────────────────
  hid = new NimBLEHIDDevice(pServer);
  hid->setManufacturer(MANUFACTURER_NAME);
  hid->setPnp(0x01, 0x0000, 0x0000, 0x0100);
  hid->setHidInfo(0x00, 0x01);

  // ── HID REPORT DESCRIPTOR ─────────────────────────────────────
  static const uint8_t reportMap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
      // Modifier byte
      0x05, 0x07,     //   Usage Page (Key Codes)
      0x19, 0xE0,     //   Usage Minimum (Left Ctrl)
      0x29, 0xE7,     //   Usage Maximum (Right GUI)
      0x15, 0x00,     //   Logical Minimum (0)
      0x25, 0x01,     //   Logical Maximum (1)
      0x75, 0x01,     //   Report Size (1 bit)
      0x95, 0x08,     //   Report Count (8)
      0x81, 0x02,     //   Input (Data, Variable, Absolute)
      // Reserved byte
      0x95, 0x01,     //   Report Count (1)
      0x75, 0x08,     //   Report Size (8)
      0x81, 0x01,     //   Input (Constant)
      // Key array (6 keys)
      0x95, 0x06,     //   Report Count (6)
      0x75, 0x08,     //   Report Size (8)
      0x15, 0x00,     //   Logical Minimum (0)
      0x25, 0x65,     //   Logical Maximum (101)
      0x05, 0x07,     //   Usage Page (Key Codes)
      0x19, 0x00,     //   Usage Minimum (0)
      0x29, 0x65,     //   Usage Maximum (101)
      0x81, 0x00,     //   Input (Data, Array, Absolute)
    0xC0              // End Collection
  };
  hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));

  inputKbd = hid->getInputReport(1);
  hid->startServices();

  Serial.println("[BLE] HID services started");

  // ── START ADVERTISING ─────────────────────────────────────────
  startAdvertising();

  Serial.println("[BOOT] Ready — waiting for BLE host");
}

// ═══════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {

  // ── RE-ADVERTISE AFTER DISCONNECT ─────────────────────────────
  if (needReAdv) {
    needReAdv = false;
    delay(ADV_RESTART_MS);
    startAdvertising();
  }

  // ── CONNECTION UPTIME DEBUG ────────────────────────────────────
  static uint32_t lastPrint = 0;
  if (bleState == BleState::CONNECTED && millis() - lastPrint > 10000) {
    lastPrint = millis();
    uint32_t upSec = (millis() - connectedAt) / 1000;
    Serial.printf("[BLE] Connected — uptime %lu s, clients: %d\n",
                  upSec, pServer->getConnectedCount());
  }

  // ── KEY MATRIX SCAN ───────────────────────────────────────────
  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);

    for (int c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        const KeyDef& k  = keyMap[r][c];
        int            kn = r * COLS + c;

        Serial.printf("[KEY] K%d pressed -> %s  (state: %s)\n",
                      kn, k.label, stateName(bleState));

        if (bleState == BleState::CONNECTED) {
          char buf[24];
          snprintf(buf, sizeof(buf), "K%d: %s", kn, k.label);
          updateOLED("Sent:", buf, "", "");
          sendKey(k.mod, k.keycode);
          delay(DEBOUNCE_MS);
          updateOLED("BLE Connected", "Ready", "", "");
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

// ═══════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════
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
