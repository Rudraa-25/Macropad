// ============================================================
//  ESP32-C3 BLE Macropad -- Firmware v7
//
//  CHANGES FROM v6:
//    - Removed PIN/login wall from web server entirely
//    - ESP serves a pure REST JSON API (no HTML page)
//    - External Lovable web app talks to the ESP over WiFi
//    - CORS headers on every response so browser can fetch
//    - WiFi AP is open (no password) for easy phone connection
//
//  HOW TO USE:
//    Normal boot   -> BLE keyboard (WiFi off)
//    Hold K9+K11 for 3s -> Config mode (BLE stops, WiFi AP on)
//      Connect phone/laptop to "MacropadConfig" (no password)
//      Open the Lovable web app -> it auto-connects to 192.168.4.1
//      Edit keys -> press Save -> power cycle back to BLE
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include <WiFi.h>
#include <WebServer.h>

// ============================================================
//  CONFIG
// ============================================================
#define DEVICE_NAME       "ESP32-C3 Macropad"
#define MANUFACTURER_NAME "DIY"
#define WIFI_SSID         "MacropadConfig"   // open AP, no password
#define WEB_PORT          80

// ============================================================
//  OLED
// ============================================================
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_ADDR  0x3C
#define OLED_SDA     8
#define OLED_SCL     9

// ============================================================
//  MATRIX
// ============================================================
#define ROWS 4
#define COLS 3
const int rowPins[ROWS] = {0, 1, 2, 3};
const int colPins[COLS] = {5, 6, 7};

#define DEBOUNCE_MS    150
#define ADV_RESTART_MS 500
#define MTU_SIZE       100
#define NUM_KEYS       (ROWS * COLS)
#define NUM_LAYERS     2

// Boot config key: hold K0 at power-on
#define CONFIG_KEY_ROW 0
#define CONFIG_KEY_COL 0

// Hot-trigger: hold K9 + K11 for 3 s while running
#define CONFIG_HOLD_ROW    3
#define CONFIG_HOLD_COL_A  0   // K9  = VolUp
#define CONFIG_HOLD_COL_B  2   // K11 = MODE
#define CONFIG_HOLD_MS  3000

// ============================================================
//  HID constants
// ============================================================
#define MOD_NONE  0x00
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_GUI   0x08

#define KEY_NONE  0x00
#define KEY_0     0x27
#define KEY_1     0x1E
#define KEY_2     0x1F
#define KEY_3     0x20
#define KEY_4     0x21
#define KEY_5     0x22
#define KEY_6     0x23
#define KEY_7     0x24
#define KEY_8     0x25
#define KEY_9     0x26
#define KEY_A     0x04
#define KEY_B     0x05
#define KEY_C     0x06
#define KEY_D     0x07
#define KEY_E     0x08
#define KEY_F     0x09
#define KEY_G     0x0A
#define KEY_H     0x0B
#define KEY_I     0x0C
#define KEY_J     0x0D
#define KEY_K     0x0E
#define KEY_L     0x0F
#define KEY_M     0x10
#define KEY_N     0x11
#define KEY_O     0x12
#define KEY_P     0x13
#define KEY_Q     0x14
#define KEY_R     0x15
#define KEY_S     0x16
#define KEY_T     0x17
#define KEY_U     0x18
#define KEY_V     0x19
#define KEY_W     0x1A
#define KEY_X     0x1B
#define KEY_Y     0x1C
#define KEY_Z     0x1D
#define KEY_F1    0x3A
#define KEY_F2    0x3B
#define KEY_F3    0x3C
#define KEY_F4    0x3D
#define KEY_F5    0x3E
#define KEY_F6    0x3F
#define KEY_F7    0x40
#define KEY_F8    0x41
#define KEY_F9    0x42
#define KEY_F10   0x43
#define KEY_F11   0x44
#define KEY_F12   0x45
#define KEY_ESC   0x29
#define KEY_TAB   0x2B
#define KEY_DEL   0x4C
#define KEY_HOME  0x4A
#define KEY_END   0x4D
#define KEY_PGUP  0x4B
#define KEY_PGDN  0x4E
#define KEY_UP    0x52
#define KEY_DOWN  0x51
#define KEY_LEFT  0x50
#define KEY_RIGHT 0x4F
#define KEY_ENTER 0x28
#define KEY_SPACE 0x2C
#define KEY_BKSP  0x2A

#define CONSUMER_VOL_UP   0xE9
#define CONSUMER_VOL_DOWN 0xEA
#define CONSUMER_MUTE     0xE2

#define ACTION_NONE         0
#define ACTION_LAYER_TOGGLE 1
#define ACTION_VOLUME_UP    2
#define ACTION_VOLUME_DOWN  3
#define ACTION_MUTE         4

// ============================================================
//  Data
// ============================================================
struct KeyDef {
  uint8_t mod;
  uint8_t keycode;
  uint8_t action;
  char    label[12];
};

KeyDef keyMap[NUM_LAYERS][ROWS][COLS] = {
  // ---- LAYER 0: Numbers ----
  {
    { {MOD_NONE,KEY_1,ACTION_NONE,"1"},    {MOD_NONE,KEY_2,ACTION_NONE,"2"},    {MOD_NONE,KEY_3,ACTION_NONE,"3"}    },
    { {MOD_NONE,KEY_4,ACTION_NONE,"4"},    {MOD_NONE,KEY_5,ACTION_NONE,"5"},    {MOD_NONE,KEY_6,ACTION_NONE,"6"}    },
    { {MOD_NONE,KEY_7,ACTION_NONE,"7"},    {MOD_NONE,KEY_8,ACTION_NONE,"8"},    {MOD_NONE,KEY_9,ACTION_NONE,"9"}    },
    { {MOD_NONE,KEY_NONE,ACTION_VOLUME_UP,"VolUp"}, {MOD_NONE,KEY_0,ACTION_NONE,"0"}, {MOD_NONE,KEY_NONE,ACTION_LAYER_TOGGLE,"MODE"} },
  },
  // ---- LAYER 1: Shortcuts ----
  {
    { {MOD_CTRL,KEY_Z,ACTION_NONE,"Ctrl+Z"}, {MOD_CTRL,KEY_S,ACTION_NONE,"Ctrl+S"}, {MOD_CTRL,KEY_A,ACTION_NONE,"Ctrl+A"} },
    { {MOD_CTRL,KEY_X,ACTION_NONE,"Ctrl+X"}, {MOD_CTRL,KEY_Y,ACTION_NONE,"Ctrl+Y"}, {MOD_GUI, KEY_D,ACTION_NONE,"Win+D"}  },
    { {MOD_ALT, KEY_F4,ACTION_NONE,"Alt+F4"},{MOD_CTRL,KEY_W,ACTION_NONE,"Ctrl+W"}, {MOD_CTRL,KEY_T,ACTION_NONE,"Ctrl+T"} },
    { {MOD_NONE,KEY_NONE,ACTION_VOLUME_DOWN,"VolDn"}, {MOD_NONE,KEY_0,ACTION_NONE,"0"}, {MOD_NONE,KEY_NONE,ACTION_LAYER_TOGGLE,"MODE"} },
  }
};

// ============================================================
//  Globals
// ============================================================
Preferences           prefs;
WebServer             webServer(WEB_PORT);
NimBLEHIDDevice*      hid           = nullptr;
NimBLECharacteristic* inputKbd      = nullptr;
NimBLECharacteristic* inputConsumer = nullptr;
NimBLEServer*         pServer       = nullptr;
Adafruit_SSD1306      display(SCREEN_W, SCREEN_H, &Wire, -1);
bool                  oledOk        = false;
int                   currentLayer  = 0;
bool                  configMode    = false;

enum class BleState { INIT, ADVERTISING, PAIRING, CONNECTED, DISCONNECTED };
volatile BleState bleState  = BleState::INIT;
volatile bool     needReAdv = false;
uint32_t          connectedAt = 0;

uint32_t configHoldStart  = 0;
bool     configHoldActive = false;

// ============================================================
//  Forward declarations
// ============================================================
void updateOLED(const char* l1, const char* l2, const char* l3, const char* l4 = "");
void startAdvertising();
void sendKey(uint8_t mod, uint8_t kc);
void sendConsumer(uint8_t usage);
void showLayerScreen();
void saveKeyMap();
void loadKeyMap();
const char* stateName(BleState s);
void setupWebRoutes();
bool checkConfigBoot();
void addCORSHeaders();
String buildKeymapJson();

// ============================================================
//  BLE callbacks
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pSrv, NimBLEConnInfo& ci) override {
    bleState = BleState::PAIRING;
    Serial.printf("[BLE] Host connected: %s\n", ci.getAddress().toString().c_str());
    updateOLED("BLE", "Pairing...", ci.getAddress().toString().c_str());
    pSrv->setDataLen(ci.getConnHandle(), MTU_SIZE);
    NimBLEDevice::setMTU(MTU_SIZE);
  }
  void onDisconnect(NimBLEServer* pSrv, NimBLEConnInfo& ci, int reason) override {
    bleState  = BleState::DISCONNECTED;
    needReAdv = true;
    Serial.printf("[BLE] Disconnected: 0x%02X (%d)\n", reason, reason);
    updateOLED("BLE", "Disconnected", "Re-advertising...");
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    if (ci.isEncrypted()) {
      bleState    = BleState::CONNECTED;
      connectedAt = millis();
      Serial.println("[BLE] Authenticated + encrypted -- HID ready");
      showLayerScreen();
    } else {
      Serial.println("[BLE] Auth failed -- removing peer bond");
      NimBLEDevice::deleteBond(ci.getAddress());
      NimBLEDevice::getServer()->disconnect(ci.getConnHandle());
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
  display.fillRect(0, 0, SCREEN_W, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(configMode ? "CONFIG MODE" : DEVICE_NAME);
  display.setTextColor(SSD1306_WHITE);
  if (!configMode && bleState == BleState::CONNECTED)
    display.fillCircle(SCREEN_W-5, 5, 3, SSD1306_BLACK);
  display.setCursor(0, 13); display.print(l1);
  display.setCursor(0, 25); display.print(l2);
  display.setCursor(0, 37); display.print(l3);
  display.setCursor(0, 49); display.print(l4);
  display.display();
}

void showLayerScreen() {
  char top[32];
  snprintf(top, sizeof(top), "Layer %d: %s", currentLayer, currentLayer==0 ? "Numbers" : "Shortcuts");
  updateOLED(top,
             currentLayer==0 ? "1-9  0  VolUp" : "C+Z C+S C+A",
             currentLayer==0 ? "K11=MODE" : "C+X C+Y Win+D",
             currentLayer==0 ? "Hold K9+K11=Cfg" : "Alt+F4 C+W C+T");
}

// ============================================================
//  Config boot check
// ============================================================
bool checkConfigBoot() {
  pinMode(rowPins[CONFIG_KEY_ROW], OUTPUT);
  digitalWrite(rowPins[CONFIG_KEY_ROW], LOW);
  pinMode(colPins[CONFIG_KEY_COL], INPUT_PULLUP);
  delayMicroseconds(50);
  bool held = (digitalRead(colPins[CONFIG_KEY_COL]) == LOW);
  digitalWrite(rowPins[CONFIG_KEY_ROW], HIGH);
  return held;
}

// ============================================================
//  BLE helpers
// ============================================================
void startAdvertising() {
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->reset();
  pAdv->setAppearance(HID_KEYBOARD);
  pAdv->addServiceUUID(hid->getHidService()->getUUID());
  NimBLEAdvertisementData sr;
  sr.setName(DEVICE_NAME);
  pAdv->setScanResponseData(sr);
  pAdv->enableScanResponse(true);
  pAdv->setMinInterval(0x20);
  pAdv->setMaxInterval(0x40);
  bleState = BleState::ADVERTISING;
  if (pAdv->start()) {
    Serial.println("[BLE] Advertising started");
    updateOLED("BLE Advertising", "Waiting for host", "Hold K9+K11=Cfg", "");
  } else {
    Serial.println("[BLE] Advertising FAILED");
    updateOLED("BLE", "ADV FAILED", "Reset device", "");
  }
}

void sendKey(uint8_t mod, uint8_t kc) {
  if (bleState != BleState::CONNECTED || !inputKbd) return;
  uint8_t d[8] = {mod, 0, kc, 0, 0, 0, 0, 0};
  uint8_t r[8] = {0};
  inputKbd->setValue(d, 8); inputKbd->notify(); delay(12);
  inputKbd->setValue(r, 8); inputKbd->notify(); delay(12);
}

void sendConsumer(uint8_t usage) {
  if (bleState != BleState::CONNECTED || !inputConsumer) return;
  uint8_t p[2] = {usage, 0};
  uint8_t r[2] = {0, 0};
  inputConsumer->setValue(p, 2); inputConsumer->notify(); delay(12);
  inputConsumer->setValue(r, 2); inputConsumer->notify(); delay(12);
}

// ============================================================
//  NVS save/load
// ============================================================
void saveKeyMap() {
  prefs.begin("keymap", false);
  for (int layer = 0; layer < NUM_LAYERS; layer++) {
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int idx = layer * NUM_KEYS + r * COLS + c;
        char km[8], kk[8], ka[8], kl[8];
        snprintf(km,8,"m%d",idx); snprintf(kk,8,"k%d",idx);
        snprintf(ka,8,"a%d",idx); snprintf(kl,8,"l%d",idx);
        prefs.putUChar(km, keyMap[layer][r][c].mod);
        prefs.putUChar(kk, keyMap[layer][r][c].keycode);
        prefs.putUChar(ka, keyMap[layer][r][c].action);
        prefs.putString(kl, keyMap[layer][r][c].label);
      }
    }
  }
  prefs.end();
  Serial.println("[NVS] Saved");
}

void loadKeyMap() {
  prefs.begin("keymap", true);
  if (!prefs.isKey("m0")) { prefs.end(); Serial.println("[NVS] Using defaults"); return; }
  for (int layer = 0; layer < NUM_LAYERS; layer++) {
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int idx = layer * NUM_KEYS + r * COLS + c;
        char km[8], kk[8], ka[8], kl[8];
        snprintf(km,8,"m%d",idx); snprintf(kk,8,"k%d",idx);
        snprintf(ka,8,"a%d",idx); snprintf(kl,8,"l%d",idx);
        keyMap[layer][r][c].mod     = prefs.getUChar(km, keyMap[layer][r][c].mod);
        keyMap[layer][r][c].keycode = prefs.getUChar(kk, keyMap[layer][r][c].keycode);
        keyMap[layer][r][c].action  = prefs.getUChar(ka, keyMap[layer][r][c].action);
        String lbl = prefs.getString(kl, keyMap[layer][r][c].label);
        strncpy(keyMap[layer][r][c].label, lbl.c_str(), 11);
        keyMap[layer][r][c].label[11] = '\0';
      }
    }
  }
  prefs.end();
  Serial.println("[NVS] Loaded");
}

// ============================================================
//  REST API helpers
// ============================================================

// Add CORS headers to every response so the Lovable app
// (running on a different origin) can talk to the ESP freely.
void addCORSHeaders() {
  webServer.sendHeader("Access-Control-Allow-Origin",  "*");
  webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// Build the full keymap as a JSON string.
// Shape returned:
// {
//   "layers": 2,
//   "keys_per_layer": 12,
//   "keymap": [
//     [ { "idx":0,"layer":0,"mod":0,"kc":30,"action":0,"label":"1" }, ... ],
//     [ ... ]
//   ]
// }
String buildKeymapJson() {
  String s = "{\"layers\":";
  s += NUM_LAYERS;
  s += ",\"keys_per_layer\":";
  s += NUM_KEYS;
  s += ",\"keymap\":[";
  for (int layer = 0; layer < NUM_LAYERS; layer++) {
    s += "[";
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        KeyDef& k = keyMap[layer][r][c];
        int idx = r * COLS + c;
        s += "{\"idx\":";  s += idx;
        s += ",\"layer\":"; s += layer;
        s += ",\"mod\":";   s += k.mod;
        s += ",\"kc\":";    s += k.keycode;
        s += ",\"action\":";s += k.action;
        s += ",\"label\":\"";
        // Simple JSON escape for label
        for (int i = 0; k.label[i] && i < 11; i++) {
          char ch = k.label[i];
          if (ch == '"')  s += "\\\"";
          else if (ch == '\\') s += "\\\\";
          else s += ch;
        }
        s += "\"}";
        if (!(r == ROWS-1 && c == COLS-1)) s += ",";
      }
    }
    s += "]";
    if (layer < NUM_LAYERS-1) s += ",";
  }
  s += "]}";
  return s;
}

// ============================================================
//  Web routes  (pure REST API, no HTML served)
// ============================================================
void setupWebRoutes() {

  // --- OPTIONS pre-flight (browser sends this before POST) ---
  webServer.on("/keymap",  HTTP_OPTIONS, [](){
    addCORSHeaders();
    webServer.send(204);
  });
  webServer.on("/save",    HTTP_OPTIONS, [](){
    addCORSHeaders();
    webServer.send(204);
  });
  webServer.on("/reboot",  HTTP_OPTIONS, [](){
    addCORSHeaders();
    webServer.send(204);
  });
  webServer.on("/status",  HTTP_OPTIONS, [](){
    addCORSHeaders();
    webServer.send(204);
  });

  // GET /status  -- device info, lets the app detect the ESP
  webServer.on("/status", HTTP_GET, [](){
    addCORSHeaders();
    String j = "{\"device\":\"ESP32-C3 Macropad\",\"fw\":\"v7\",";
    j += "\"layers\":"; j += NUM_LAYERS; j += ",";
    j += "\"keys\":";   j += NUM_KEYS;   j += ",";
    j += "\"mode\":\"config\"}";
    webServer.send(200, "application/json", j);
  });

  // GET /keymap  -- full keymap JSON
  webServer.on("/keymap", HTTP_GET, [](){
    addCORSHeaders();
    webServer.send(200, "application/json", buildKeymapJson());
  });

  // POST /save  -- receive updated keymap JSON from Lovable app
  // Expected body (same shape as GET /keymap .keymap array):
  // { "keymap": [ [ {idx,layer,mod,kc,action,label}, ... ], [...] ] }
  webServer.on("/save", HTTP_POST, [](){
    addCORSHeaders();
    String body = webServer.arg("plain");
    bool ok = false;
    int found = 0;
    int pos = 0;
    while (pos < (int)body.length() && found < NUM_LAYERS * NUM_KEYS) {
      int i = body.indexOf("\"idx\":", pos);   if (i < 0) break;
      int li = body.indexOf("\"layer\":", i);  if (li < 0) break;
      int mi = body.indexOf("\"mod\":", i);    if (mi < 0) break;
      int ki = body.indexOf("\"kc\":", i);     if (ki < 0) break;
      int ai = body.indexOf("\"action\":", i); if (ai < 0) break;
      int lbi= body.indexOf("\"label\":\"", i);if (lbi < 0) break;

      int idx   = body.substring(i+6).toInt();
      int layer = body.substring(li+8).toInt();
      uint8_t mod = (uint8_t)body.substring(mi+6).toInt();
      uint8_t kc  = (uint8_t)body.substring(ki+5).toInt();
      uint8_t act = (uint8_t)body.substring(ai+9).toInt();

      int lbe = body.indexOf("\"", lbi+9);
      String lbl = (lbe > lbi+9) ? body.substring(lbi+9, lbe) : "";

      if (layer >= 0 && layer < NUM_LAYERS && idx >= 0 && idx < NUM_KEYS) {
        int r = idx / COLS, c = idx % COLS;
        keyMap[layer][r][c].mod     = mod;
        keyMap[layer][r][c].keycode = kc;
        keyMap[layer][r][c].action  = act;
        strncpy(keyMap[layer][r][c].label, lbl.c_str(), 11);
        keyMap[layer][r][c].label[11] = '\0';
        found++;
        ok = true;
      }
      pos = (lbe > 0) ? lbe + 1 : pos + 1;
    }
    if (ok) saveKeyMap();
    webServer.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"parse error\"}");
  });

  // POST /reboot  -- gracefully restart ESP back to BLE mode
  webServer.on("/reboot", HTTP_POST, [](){
    addCORSHeaders();
    webServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"rebooting\"}");
    delay(300);
    ESP.restart();
  });

  // Catch-all
  webServer.onNotFound([](){
    addCORSHeaders();
    webServer.send(404, "application/json", "{\"err\":\"not found\"}");
  });
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[BOOT] ESP32-C3 Macropad v7");

  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) { display.clearDisplay(); display.display(); }

  for (int r = 0; r < ROWS; r++) { pinMode(rowPins[r], OUTPUT); digitalWrite(rowPins[r], HIGH); }
  for (int c = 0; c < COLS; c++) pinMode(colPins[c], INPUT_PULLUP);
  delay(50);

  configMode = checkConfigBoot();
  loadKeyMap();

  if (configMode) {
    Serial.println("[BOOT] CONFIG MODE");
    updateOLED("CONFIG MODE", "Starting WiFi...", "", "");

    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    delay(300);
    // Open AP -- no password
    WiFi.softAP(WIFI_SSID);
    delay(1000);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WiFi] AP up  SSID:%s  IP:%s\n", WIFI_SSID, ip.toString().c_str());

    setupWebRoutes();
    webServer.begin();

    updateOLED("CONFIG MODE", "WiFi: MacropadConfig", ip.toString().c_str(), "No password");
    Serial.println("[API] REST server ready");

  } else {
    Serial.println("[BOOT] KEYBOARD MODE");
    updateOLED("BLE Mode", "Starting...", "Hold K9+K11=Cfg", "");

    WiFi.mode(WIFI_OFF);
    delay(100);

    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    hid = new NimBLEHIDDevice(pServer);
    hid->setManufacturer(MANUFACTURER_NAME);
    hid->setPnp(0x01, 0x0000, 0x0000, 0x0100);
    hid->setHidInfo(0x00, 0x01);

    static const uint8_t reportMap[] = {
      0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
      0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00,
      0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
      0x95,0x01, 0x75,0x08, 0x81,0x01,
      0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65,
      0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00, 0xC0,
      0x05,0x0C, 0x09,0x01, 0xA1,0x01, 0x85,0x02,
      0x15,0x00, 0x26,0xFF,0x03, 0x19,0x00, 0x2A,0xFF,0x03,
      0x75,0x10, 0x95,0x01, 0x81,0x00, 0xC0
    };
    hid->setReportMap((uint8_t*)reportMap, sizeof(reportMap));
    inputKbd      = hid->getInputReport(1);
    inputConsumer = hid->getInputReport(2);
    hid->startServices();

    startAdvertising();
    Serial.println("[BOOT] BLE ready");
  }
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  if (configMode) {
    webServer.handleClient();
    return;
  }

  if (needReAdv) {
    needReAdv = false;
    delay(ADV_RESTART_MS);
    startAdvertising();
  }

  // K9 + K11 hold for 3 s -- hot enter config mode
  {
    digitalWrite(rowPins[CONFIG_HOLD_ROW], LOW);
    delayMicroseconds(10);
    bool k9  = (digitalRead(colPins[CONFIG_HOLD_COL_A]) == LOW);
    bool k11 = (digitalRead(colPins[CONFIG_HOLD_COL_B]) == LOW);
    digitalWrite(rowPins[CONFIG_HOLD_ROW], HIGH);

    if (k9 && k11) {
      if (!configHoldActive) {
        configHoldActive = true;
        configHoldStart  = millis();
        updateOLED("Hold K9+K11...", "Release=cancel", "Config in 3s", "");
      } else if (millis() - configHoldStart >= CONFIG_HOLD_MS) {
        Serial.println("[CFG] Hot-trigger: BLE off, WiFi AP on");
        NimBLEDevice::getAdvertising()->stop();
        NimBLEDevice::deinit(true);
        delay(200);

        configMode       = true;
        configHoldActive = false;

        WiFi.mode(WIFI_OFF);
        delay(200);
        WiFi.mode(WIFI_AP);
        delay(300);
        WiFi.softAP(WIFI_SSID);
        delay(1000);

        IPAddress ip = WiFi.softAPIP();
        Serial.printf("[WiFi] AP up  SSID:%s  IP:%s\n", WIFI_SSID, ip.toString().c_str());

        setupWebRoutes();
        webServer.begin();
        updateOLED("CONFIG MODE", "WiFi: MacropadConfig", ip.toString().c_str(), "No password");
        Serial.println("[API] REST server ready");
        return;
      }
    } else {
      if (configHoldActive) {
        configHoldActive = false;
        showLayerScreen();
      }
    }
  }

  // Matrix scan
  for (int r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);
    for (int c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        const KeyDef& k = keyMap[currentLayer][r][c];
        int kn = r * COLS + c;
        Serial.printf("[KEY] K%d -> %s (L%d state:%s)\n", kn, k.label, currentLayer, stateName(bleState));

        if (k.action == ACTION_LAYER_TOGGLE) {
          currentLayer = (currentLayer == 0) ? 1 : 0;
          Serial.printf("[LAYER] -> %d\n", currentLayer);
          showLayerScreen();
          delay(DEBOUNCE_MS);
          digitalWrite(rowPins[r], HIGH);
          return;
        }

        if (bleState != BleState::CONNECTED) {
          char buf[24];
          snprintf(buf, sizeof(buf), "State: %s", stateName(bleState));
          updateOLED("Not Ready", buf, "Key ignored", "");
          delay(DEBOUNCE_MS);
          continue;
        }

        if (k.action == ACTION_VOLUME_UP)   { updateOLED("Sent:","Volume Up","","");   sendConsumer(CONSUMER_VOL_UP);   delay(DEBOUNCE_MS); showLayerScreen(); digitalWrite(rowPins[r],HIGH); return; }
        if (k.action == ACTION_VOLUME_DOWN) { updateOLED("Sent:","Volume Down","",""); sendConsumer(CONSUMER_VOL_DOWN); delay(DEBOUNCE_MS); showLayerScreen(); digitalWrite(rowPins[r],HIGH); return; }
        if (k.action == ACTION_MUTE)        { updateOLED("Sent:","Mute","","");        sendConsumer(CONSUMER_MUTE);     delay(DEBOUNCE_MS); showLayerScreen(); digitalWrite(rowPins[r],HIGH); return; }

        char buf[24];
        snprintf(buf, sizeof(buf), "K%d: %s", kn, k.label);
        updateOLED("Sent:", buf, "", "");
        sendKey(k.mod, k.keycode);
        delay(DEBOUNCE_MS);
        showLayerScreen();
      }
    }
    digitalWrite(rowPins[r], HIGH);
  }
}

const char* stateName(BleState s) {
  switch(s) {
    case BleState::INIT:         return "INIT";
    case BleState::ADVERTISING:  return "ADV";
    case BleState::PAIRING:      return "PAIRING";
    case BleState::CONNECTED:    return "CONNECTED";
    case BleState::DISCONNECTED: return "DISCONNECTED";
    default:                     return "UNKNOWN";
  }
}
