// ============================================================
//  ESP32-C3 BLE Macropad — Firmware v6
//  Fixes in this version:
//    1. WiFi now uses ESP-IDF low-level API directly instead of
//       the Arduino WiFi wrapper — fixes 0.0.0.0 / AP not
//       appearing on ESP32-C3.
//    2. Config mode triggered by holding VolUp (K9) + MODE (K11)
//       together for 3 seconds — no reboot needed.
//       BLE is stopped cleanly before WiFi starts.
//    3. Exiting config mode restarts ESP32 cleanly back to BLE.
// ============================================================
//
//  HOW TO USE:
//    Normal boot  → BLE keyboard (WiFi off)
//    Hold K9+K11 for 3s → Config mode (BLE stops, WiFi AP on)
//      Connect phone to "MacropadConfig" / "macropad123"
//      Open browser → 192.168.4.1  PIN: 1234
//      Save changes → power cycle to return to BLE mode
//
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

// WiFi/web only included when needed (saves RAM in BLE mode)
#include <WiFi.h>
#include <WebServer.h>

// ============================================================
//  CONFIG
// ============================================================
#define DEVICE_NAME       "ESP32-C3 Macropad"
#define MANUFACTURER_NAME "DIY"
#define WIFI_SSID         "MacropadConfig"
#define WIFI_PASS         "macropad123"
#define WEB_PIN           "1234"
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

// ---- Config-mode key: hold this pin LOW at boot ----
#define CONFIG_KEY_ROW 0
#define CONFIG_KEY_COL 0   // K0 = row0, col0

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
//  Key definition
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
  // ---- LAYER 1: Custom Shortcuts ----
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
bool                  configMode    = false; // true = WiFi config, false = BLE keyboard
bool                  webAuthed     = false;

enum class BleState { INIT, ADVERTISING, PAIRING, CONNECTED, DISCONNECTED };
volatile BleState bleState  = BleState::INIT;
volatile bool     needReAdv = false;
uint32_t          connectedAt = 0;

// Config-mode hot-trigger: hold K9 (row3,col0) + K11 (row3,col2) for 3s in BLE mode
#define CONFIG_HOLD_ROW    3
#define CONFIG_HOLD_COL_A  0   // K9  = VolUp
#define CONFIG_HOLD_COL_B  2   // K11 = MODE
#define CONFIG_HOLD_MS  3000
uint32_t configHoldStart  = 0;
bool     configHoldActive = false;

// ============================================================
//  Forward declarations
// ============================================================
void updateOLED(const char* l1, const char* l2, const char* l3, const char* l4="");
void startAdvertising();
void sendKey(uint8_t mod, uint8_t kc);
void sendConsumer(uint8_t usage);
void showLayerScreen();
void saveKeyMap();
void loadKeyMap();
const char* stateName(BleState s);
void setupWebRoutes();
String buildMainPage();
String buildLoginPage();
String escHtml(const char* s);
bool  checkConfigBoot();

// ============================================================
//  BLE Callbacks
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
      Serial.println("[BLE] Authenticated + encrypted — HID ready");
      showLayerScreen();
    } else {
      // Stale bond keys — clear only the bond for this peer
      Serial.println("[BLE] Auth failed — removing peer bond");
      NimBLEDevice::deleteBond(ci.getAddress());
      NimBLEDevice::getServer()->disconnect(ci.getConnHandle());
    }
  }
};

// ============================================================
//  OLED
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
             currentLayer==0 ? "Hold K0=Config" : "Alt+F4 C+W C+T");
}

// ============================================================
//  Detect config-mode boot (K0 held at power-on)
// ============================================================
bool checkConfigBoot() {
  // Set row 0 low, read col 0
  pinMode(rowPins[CONFIG_KEY_ROW], OUTPUT);
  digitalWrite(rowPins[CONFIG_KEY_ROW], LOW);
  pinMode(colPins[CONFIG_KEY_COL], INPUT_PULLUP);
  delayMicroseconds(50);
  bool held = (digitalRead(colPins[CONFIG_KEY_COL]) == LOW);
  digitalWrite(rowPins[CONFIG_KEY_ROW], HIGH);
  return held;
}

// ============================================================
//  BLE advertising
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
    updateOLED("BLE Advertising", "Waiting for host", "Hold K0=Config", "");
  } else {
    Serial.println("[BLE] Advertising FAILED");
    updateOLED("BLE", "ADV FAILED", "Reset device", "");
  }
}

// ============================================================
//  Key sending
// ============================================================
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
//  NVS save / load
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
//  Web UI
// ============================================================
String escHtml(const char* s) {
  String o = s;
  o.replace("&","&amp;"); o.replace("<","&lt;");
  o.replace(">","&gt;");  o.replace("\"","&quot;");
  return o;
}

const char* JS_KEYCODES = R"JS(
const KC={
  "None":0,"0":0x27,"1":0x1E,"2":0x1F,"3":0x20,"4":0x21,"5":0x22,
  "6":0x23,"7":0x24,"8":0x25,"9":0x26,
  "A":0x04,"B":0x05,"C":0x06,"D":0x07,"E":0x08,"F":0x09,"G":0x0A,
  "H":0x0B,"I":0x0C,"J":0x0D,"K":0x0E,"L":0x0F,"M":0x10,"N":0x11,
  "O":0x12,"P":0x13,"Q":0x14,"R":0x15,"S":0x16,"T":0x17,"U":0x18,
  "V":0x19,"W":0x1A,"X":0x1B,"Y":0x1C,"Z":0x1D,
  "F1":0x3A,"F2":0x3B,"F3":0x3C,"F4":0x3D,"F5":0x3E,"F6":0x3F,
  "F7":0x40,"F8":0x41,"F9":0x42,"F10":0x43,"F11":0x44,"F12":0x45,
  "ESC":0x29,"TAB":0x2B,"DEL":0x4C,"HOME":0x4A,"END":0x4D,
  "PGUP":0x4B,"PGDN":0x4E,"UP":0x52,"DOWN":0x51,"LEFT":0x50,"RIGHT":0x4F,
  "ENTER":0x28,"SPACE":0x2C,"BKSP":0x2A
};
)JS";

String buildLoginPage() {
  return R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Macropad Login</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@300;600&display=swap');
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#0a0a0f;color:#e0e0ff;font-family:'Exo 2',sans-serif;
    display:flex;align-items:center;justify-content:center;min-height:100vh;
    background-image:radial-gradient(ellipse at 50% 0%,#1a0a3a 0%,#0a0a0f 70%)}
  .card{background:rgba(255,255,255,0.04);border:1px solid rgba(120,80,255,0.3);
    border-radius:16px;padding:40px 32px;width:90%;max-width:360px;
    box-shadow:0 0 40px rgba(100,60,255,0.15);text-align:center}
  .logo{font-family:'Share Tech Mono',monospace;font-size:11px;color:#7855ff;
    letter-spacing:4px;text-transform:uppercase;margin-bottom:8px}
  h1{font-size:22px;font-weight:600;margin-bottom:6px;color:#fff}
  p{font-size:13px;color:#888;margin-bottom:28px}
  input{width:100%;background:rgba(255,255,255,0.06);border:1px solid rgba(120,80,255,0.4);
    border-radius:10px;padding:14px 16px;color:#fff;font-size:20px;
    letter-spacing:8px;text-align:center;font-family:'Share Tech Mono',monospace;outline:none}
  input:focus{border-color:#7855ff;box-shadow:0 0 0 3px rgba(120,85,255,0.2)}
  button{width:100%;margin-top:16px;background:linear-gradient(135deg,#7855ff,#4a2dc7);
    border:none;border-radius:10px;padding:14px;color:#fff;font-size:15px;
    font-family:'Exo 2',sans-serif;font-weight:600;cursor:pointer;transition:opacity .2s}
  button:hover{opacity:.85}
  .err{color:#ff6b6b;font-size:13px;margin-top:12px;display:none}
  .icon{font-size:40px;margin-bottom:20px}
  .info{background:rgba(120,85,255,0.1);border:1px solid rgba(120,85,255,0.3);
    border-radius:8px;padding:10px;font-size:11px;color:#aaa;margin-bottom:20px;
    font-family:'Share Tech Mono',monospace;line-height:1.6}
</style></head><body>
<div class="card">
  <div class="icon">[KB]</div>
  <div class="logo">ESP32-C3 CONFIG</div>
  <h1>Macropad</h1>
  <div class="info">WiFi: MacropadConfig<br>IP: 192.168.4.1<br>BLE: OFF in this mode</div>
  <p>Enter PIN to continue</p>
  <input type="password" id="pin" maxlength="8" placeholder="****" inputmode="numeric" autofocus>
  <button onclick="doLogin()">Unlock</button>
  <div class="err" id="err">Incorrect PIN. Try again.</div>
</div>
<script>
document.getElementById('pin').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin()});
function doLogin(){
  const pin=document.getElementById('pin').value;
  fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'pin='+encodeURIComponent(pin)})
  .then(r=>r.json()).then(d=>{
    if(d.ok) location.href='/';
    else{document.getElementById('err').style.display='block';document.getElementById('pin').value='';}
  });
}
</script></body></html>)HTML";
}

String buildMainPage() {
  String kmJson = "[";
  for (int layer = 0; layer < NUM_LAYERS; layer++) {
    kmJson += "[";
    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        KeyDef& k = keyMap[layer][r][c];
        int idx = r * COLS + c;
        kmJson += "{\"idx\":" + String(idx) + ",\"layer\":" + String(layer) +
                  ",\"mod\":" + String(k.mod) + ",\"kc\":" + String(k.keycode) +
                  ",\"action\":" + String(k.action) +
                  ",\"label\":\"" + escHtml(k.label) + "\"}";
        if (!(r==ROWS-1 && c==COLS-1)) kmJson += ",";
      }
    }
    kmJson += "]";
    if (layer < NUM_LAYERS-1) kmJson += ",";
  }
  kmJson += "]";

  String html = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Macropad Config</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@300;400;600;700&display=swap');
  *{box-sizing:border-box;margin:0;padding:0}
  :root{--accent:#7855ff;--accent2:#ff5580;--bg:#0a0a0f;--surface:rgba(255,255,255,0.05);
    --border:rgba(120,80,255,0.25);--text:#e0e0ff;--muted:#667}
  body{background:var(--bg);color:var(--text);font-family:'Exo 2',sans-serif;min-height:100vh;
    background-image:radial-gradient(ellipse at 20% 0%,#1a0530 0%,var(--bg) 60%)}
  header{padding:16px 20px;border-bottom:1px solid var(--border);
    display:flex;align-items:center;justify-content:space-between;
    background:rgba(10,10,15,0.8);backdrop-filter:blur(12px);position:sticky;top:0;z-index:10}
  .logo{font-family:'Share Tech Mono',monospace;font-size:10px;color:var(--accent);letter-spacing:3px}
  h1{font-size:17px;font-weight:700;color:#fff}
  .badge{background:rgba(120,85,255,0.15);border:1px solid var(--border);border-radius:20px;
    padding:4px 12px;font-size:11px;color:var(--accent);font-family:'Share Tech Mono',monospace}
  main{padding:20px;max-width:600px;margin:0 auto}
  .section-title{font-size:10px;letter-spacing:3px;text-transform:uppercase;color:var(--muted);
    margin:24px 0 12px;font-family:'Share Tech Mono',monospace}
  .info-bar{background:rgba(120,85,255,0.08);border:1px solid rgba(120,85,255,0.2);
    border-radius:10px;padding:12px 16px;font-size:12px;color:#aaa;margin-bottom:20px;
    font-family:'Share Tech Mono',monospace;line-height:1.7}
  .tabs{display:flex;gap:8px;margin-bottom:20px}
  .tab{flex:1;padding:10px;border-radius:10px;border:1px solid var(--border);
    background:var(--surface);color:var(--muted);font-size:13px;font-family:'Exo 2',sans-serif;
    cursor:pointer;transition:all .2s;text-align:center;font-weight:600}
  .tab.active{background:rgba(120,85,255,0.2);border-color:var(--accent);color:#fff}
  .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:24px}
  .key{background:var(--surface);border:1px solid var(--border);border-radius:12px;
    padding:14px 8px;text-align:center;cursor:pointer;transition:all .2s;position:relative;
    min-height:64px;display:flex;flex-direction:column;align-items:center;justify-content:center}
  .key:hover{border-color:var(--accent);background:rgba(120,85,255,0.1);
    transform:translateY(-1px);box-shadow:0 4px 20px rgba(120,85,255,0.2)}
  .key.special{border-color:rgba(255,85,128,0.4);background:rgba(255,85,128,0.06)}
  .key.special:hover{border-color:var(--accent2);background:rgba(255,85,128,0.12)}
  .key-num{font-size:9px;color:var(--muted);font-family:'Share Tech Mono',monospace;
    position:absolute;top:5px;left:7px}
  .key-label{font-family:'Share Tech Mono',monospace;font-size:13px;color:#fff;line-height:1.3;word-break:break-all}
  .key-sub{font-size:9px;color:var(--muted);margin-top:3px}
  .overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.7);
    backdrop-filter:blur(6px);z-index:100;align-items:flex-end;justify-content:center}
  .overlay.show{display:flex}
  .modal{background:#13111f;border:1px solid var(--border);border-radius:20px 20px 0 0;
    padding:28px 24px;width:100%;max-width:500px;animation:slideUp .25s ease}
  @keyframes slideUp{from{transform:translateY(100%)}to{transform:translateY(0)}}
  .modal-title{font-size:16px;font-weight:700;margin-bottom:4px;color:#fff}
  .modal-sub{font-size:12px;color:var(--muted);margin-bottom:20px;font-family:'Share Tech Mono',monospace}
  label{display:block;font-size:11px;letter-spacing:2px;text-transform:uppercase;
    color:var(--muted);margin-bottom:6px;font-family:'Share Tech Mono',monospace}
  select,input[type=text]{width:100%;background:rgba(255,255,255,0.06);
    border:1px solid var(--border);border-radius:10px;padding:11px 14px;
    color:#fff;font-family:'Share Tech Mono',monospace;font-size:14px;outline:none;
    margin-bottom:16px;appearance:none}
  select:focus,input[type=text]:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(120,85,255,0.2)}
  select option{background:#13111f;color:#fff}
  .row2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  .btn-row{display:flex;gap:10px;margin-top:4px}
  .btn{flex:1;padding:13px;border-radius:10px;border:none;font-family:'Exo 2',sans-serif;
    font-size:14px;font-weight:600;cursor:pointer;transition:all .2s}
  .btn-primary{background:linear-gradient(135deg,var(--accent),#4a2dc7);color:#fff}
  .btn-primary:hover{opacity:.85}
  .btn-cancel{background:rgba(255,255,255,0.07);color:var(--muted)}
  .btn-cancel:hover{background:rgba(255,255,255,0.12);color:#fff}
  .save-bar{position:fixed;bottom:0;left:0;right:0;padding:16px 20px;
    background:rgba(10,10,15,0.95);border-top:1px solid var(--border);
    display:flex;gap:10px;justify-content:center;
    transform:translateY(100%);transition:transform .3s;z-index:50}
  .save-bar.show{transform:translateY(0)}
  .toast{position:fixed;top:80px;left:50%;transform:translateX(-50%) translateY(-20px);
    background:rgba(120,85,255,0.9);color:#fff;padding:10px 24px;border-radius:30px;
    font-size:13px;opacity:0;transition:all .3s;pointer-events:none;z-index:200;white-space:nowrap}
  .toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
</style></head><body>
<header>
  <div><div class="logo">ESP32-C3 CONFIG MODE</div><h1>Macropad</h1></div>
  <div class="badge">WiFi AP</div>
</header>
<main>
  <div class="info-bar">
    [!] Config Mode - BLE keyboard is OFF<br>
    To return to keyboard mode: power cycle without holding K0
  </div>
  <div class="section-title">Key Layout - Click to Edit</div>
  <div class="tabs">
    <div class="tab active" onclick="switchLayer(0)" id="tab0">Layer 0 - Numbers</div>
    <div class="tab" onclick="switchLayer(1)" id="tab1">Layer 1 - Shortcuts</div>
  </div>
  <div class="grid" id="keyGrid"></div>
</main>
<div class="save-bar" id="saveBar">
  <button class="btn btn-cancel" onclick="discardChanges()">Discard</button>
  <button class="btn btn-primary" onclick="saveChanges()">Save to Flash [save]</button>
</div>
<div class="overlay" id="overlay" onclick="closeModal(event)">
  <div class="modal">
    <div class="modal-title" id="modalTitle">Edit Key</div>
    <div class="modal-sub" id="modalSub"></div>
    <label>Action Type</label>
    <select id="editAction" onchange="onActionChange()">
      <option value="0">Normal Key</option>
      <option value="1">Layer Toggle</option>
      <option value="2">Volume Up</option>
      <option value="3">Volume Down</option>
      <option value="4">Mute</option>
    </select>
    <div id="keyFields">
      <div class="row2">
        <div>
          <label>Modifier</label>
          <select id="editMod">
            <option value="0">None</option>
            <option value="1">Ctrl</option>
            <option value="2">Shift</option>
            <option value="4">Alt</option>
            <option value="8">Win/GUI</option>
            <option value="3">Ctrl+Shift</option>
            <option value="5">Ctrl+Alt</option>
            <option value="7">Ctrl+Shift+Alt</option>
          </select>
        </div>
        <div>
          <label>Key</label>
          <select id="editKc"></select>
        </div>
      </div>
    </div>
    <label>Display Label</label>
    <input type="text" id="editLabel" maxlength="11" placeholder="e.g. Ctrl+Z">
    <div class="btn-row">
      <button class="btn btn-cancel" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="applyEdit()">Apply</button>
    </div>
  </div>
</div>
<div class="toast" id="toast"></div>
<script>
)HTML";

  html += "const keymap="; html += kmJson; html += ";\n";
  html += JS_KEYCODES;
  html += R"JS(
let currentLayer=0,editingIdx=-1,dirty=false;
let km=JSON.parse(JSON.stringify(keymap));
function buildKcSelect(){
  const s=document.getElementById('editKc'); s.innerHTML='';
  Object.keys(KC).forEach(n=>{const o=document.createElement('option');o.value=KC[n];o.textContent=n;s.appendChild(o);});
}
buildKcSelect();
function onActionChange(){
  document.getElementById('keyFields').style.display=parseInt(document.getElementById('editAction').value)===0?'block':'none';
}
function renderGrid(){
  const g=document.getElementById('keyGrid'); g.innerHTML='';
  km[currentLayer].forEach((key,i)=>{
    const d=document.createElement('div');
    d.className='key'+(key.action!==0?' special':'');
    const acts=['','[Layer]','[VolUp]','[VolDn]','[Mute]'];
    const mods=['','Ctrl','Shift','Ctrl+Sh','Alt','Ctrl+Alt','','C+S+A','Win'];
    const sub=key.action===0?(mods[key.mod]||''):(acts[key.action]||'');
    d.innerHTML=`<span class="key-num">K${i}</span><span class="key-label">${key.label}</span><span class="key-sub">${sub}</span>`;
    d.onclick=()=>openModal(i); g.appendChild(d);
  });
}
function switchLayer(l){
  currentLayer=l;
  document.getElementById('tab0').className='tab'+(l===0?' active':'');
  document.getElementById('tab1').className='tab'+(l===1?' active':'');
  renderGrid();
}
function openModal(idx){
  editingIdx=idx;
  const key=km[currentLayer][idx];
  document.getElementById('modalTitle').textContent='Edit K'+idx;
  document.getElementById('modalSub').textContent='K'+idx+' - Layer '+currentLayer;
  document.getElementById('editAction').value=key.action;
  document.getElementById('editMod').value=key.mod;
  const s=document.getElementById('editKc');
  for(let i=0;i<s.options.length;i++){if(parseInt(s.options[i].value)===key.kc){s.selectedIndex=i;break;}}
  document.getElementById('editLabel').value=key.label;
  onActionChange();
  document.getElementById('overlay').classList.add('show');
}
function closeModal(e){if(e&&e.target!==document.getElementById('overlay'))return;document.getElementById('overlay').classList.remove('show');}
function applyEdit(){
  const action=parseInt(document.getElementById('editAction').value);
  const mod=parseInt(document.getElementById('editMod').value);
  const kc=parseInt(document.getElementById('editKc').value);
  const label=document.getElementById('editLabel').value.trim()||'K'+editingIdx;
  km[currentLayer][editingIdx]={...km[currentLayer][editingIdx],action,mod,kc,label};
  document.getElementById('overlay').classList.remove('show');
  renderGrid(); setDirty(true);
}
function setDirty(v){dirty=v;document.getElementById('saveBar').className='save-bar'+(v?' show':'');}
function discardChanges(){km=JSON.parse(JSON.stringify(keymap));renderGrid();setDirty(false);showToast('Discarded');}
function saveChanges(){
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({keymap:km})})
  .then(r=>r.json()).then(d=>{if(d.ok){showToast('Saved OK');setDirty(false);}else showToast('Save failed');})
  .catch(()=>showToast('Save failed ✗'));
}
function showToast(msg){const t=document.getElementById('toast');t.textContent=msg;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2500);}
renderGrid();
)JS";
  html += "</script></body></html>";
  return html;
}

// ============================================================
//  Web routes
// ============================================================
void setupWebRoutes() {
  webServer.on("/login", HTTP_GET, [](){
    if (webAuthed) { webServer.sendHeader("Location","/"); webServer.send(302); return; }
    webServer.send(200, "text/html; charset=utf-8", buildLoginPage());
  });
  webServer.on("/login", HTTP_POST, [](){
    String pin = webServer.arg("pin");
    webAuthed = (pin == WEB_PIN);
    webServer.send(200, "application/json", webAuthed ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  webServer.on("/", HTTP_GET, [](){
    if (!webAuthed) { webServer.sendHeader("Location","/login"); webServer.send(302); return; }
    webServer.send(200, "text/html; charset=utf-8", buildMainPage());
  });
  webServer.on("/save", HTTP_POST, [](){
    if (!webAuthed) { webServer.send(403,"application/json","{\"err\":\"unauth\"}"); return; }
    String body = webServer.arg("plain");
    bool ok = false; int pos = 0, found = 0;
    while (pos < (int)body.length() && found < NUM_LAYERS*NUM_KEYS) {
      int i = body.indexOf("\"idx\":", pos); if (i<0) break;
      int idx   = body.substring(i+6).toInt();
      int li    = body.indexOf("\"layer\":", i); int layer = body.substring(li+8).toInt();
      int mi    = body.indexOf("\"mod\":", i);   uint8_t mod = body.substring(mi+6).toInt();
      int ki    = body.indexOf("\"kc\":", i);    uint8_t kc  = body.substring(ki+5).toInt();
      int ai    = body.indexOf("\"action\":", i);uint8_t act = body.substring(ai+9).toInt();
      int lbi   = body.indexOf("\"label\":\"", i);
      int lbe   = body.indexOf("\"", lbi+9);
      String lbl = body.substring(lbi+9, lbe);
      if (layer>=0&&layer<NUM_LAYERS&&idx>=0&&idx<NUM_KEYS) {
        int r=idx/COLS, c=idx%COLS;
        keyMap[layer][r][c].mod=mod; keyMap[layer][r][c].keycode=kc; keyMap[layer][r][c].action=act;
        strncpy(keyMap[layer][r][c].label, lbl.c_str(), 11); keyMap[layer][r][c].label[11]='\0';
        found++; ok=true;
      }
      pos = lbe+1;
    }
    if (ok) saveKeyMap();
    webServer.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  webServer.onNotFound([](){
    webServer.sendHeader("Location","/"); webServer.send(302);
  });
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[BOOT] ESP32-C3 Macropad v6");

  // OLED first
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) { display.clearDisplay(); display.display(); }

  // Matrix pins
  for (int r = 0; r < ROWS; r++) { pinMode(rowPins[r], OUTPUT); digitalWrite(rowPins[r], HIGH); }
  for (int c = 0; c < COLS; c++) pinMode(colPins[c], INPUT_PULLUP);
  delay(50); // settle

  // Check if config boot
  configMode = checkConfigBoot();

  loadKeyMap();

  if (configMode) {
    // ---- CONFIG MODE: WiFi AP only, BLE off ----
    Serial.println("[BOOT] CONFIG MODE — WiFi AP starting");
    updateOLED("CONFIG MODE", "Starting WiFi...", "", "");

    webAuthed = false;  // always start unauthenticated

    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    delay(300);
    WiFi.softAP(WIFI_SSID, WIFI_PASS, 1, 0, 4);
    delay(1000);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WiFi] AP up  SSID:%s  IP:%s\n", WIFI_SSID, ip.toString().c_str());

    setupWebRoutes();
    webServer.begin();

    updateOLED("CONFIG MODE", "WiFi: MacropadCfg", ip.toString().c_str(), "PIN: 1234");
    Serial.println("[WEB] Server ready");

  } else {
    // ---- NORMAL MODE: BLE keyboard, WiFi off ----
    Serial.println("[BOOT] KEYBOARD MODE — BLE starting");
    updateOLED("BLE Mode", "Starting...", "Hold K0=Config", "");

    // Make sure WiFi is fully off before BLE
    WiFi.mode(WIFI_OFF);
    delay(100);

    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    // Bond only, no MITM — correct for Just Works HID
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    hid = new NimBLEHIDDevice(pServer);
    hid->setManufacturer(MANUFACTURER_NAME);
    hid->setPnp(0x01, 0x0000, 0x0000, 0x0100);
    hid->setHidInfo(0x00, 0x01);

    static const uint8_t reportMap[] = {
      // Keyboard report ID 1
      0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
      0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00,
      0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
      0x95,0x01, 0x75,0x08, 0x81,0x01,
      0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65,
      0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00, 0xC0,
      // Consumer report ID 2
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
    return; // No key scanning in config mode
  }

  // BLE mode
  if (needReAdv) {
    needReAdv = false;
    delay(ADV_RESTART_MS);
    startAdvertising();
  }

  // --- K9 + K11 hold for 3 s → enter config mode without reboot ---
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
        updateOLED("Hold K9+K11...", "Release to cancel", "Config in 3s", "");
      } else if (millis() - configHoldStart >= CONFIG_HOLD_MS) {
        // Trigger config mode: stop BLE, start WiFi AP
        Serial.println("[CFG] Hot-trigger: stopping BLE, starting WiFi AP");
        NimBLEDevice::getAdvertising()->stop();
        NimBLEDevice::deinit(true);
        delay(200);

        configMode   = true;
        webAuthed    = false;  // reset auth for new session
        configHoldActive = false;

        WiFi.mode(WIFI_OFF);
        delay(200);
        WiFi.mode(WIFI_AP);
        delay(300);
        WiFi.softAP(WIFI_SSID, WIFI_PASS, 1, 0, 4);
        delay(1000);

        IPAddress ip = WiFi.softAPIP();
        Serial.printf("[WiFi] AP up  SSID:%s  IP:%s\n", WIFI_SSID, ip.toString().c_str());

        setupWebRoutes();
        webServer.begin();
        updateOLED("CONFIG MODE", "WiFi: MacropadCfg", ip.toString().c_str(), "PIN: 1234");
        Serial.println("[WEB] Server ready");
        return; // jump back to top of loop — now configMode==true
      }
    } else {
      if (configHoldActive) {
        configHoldActive = false;
        showLayerScreen(); // restore normal screen on release
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

        // Layer toggle — always works regardless of BLE state
        if (k.action == ACTION_LAYER_TOGGLE) {
          currentLayer = (currentLayer == 0) ? 1 : 0;
          Serial.printf("[LAYER] -> %d\n", currentLayer);
          showLayerScreen();
          delay(DEBOUNCE_MS);
          digitalWrite(rowPins[r], HIGH);
          return;
        }

        // All other keys — only act when fully CONNECTED
        if (bleState != BleState::CONNECTED) {
          char buf[24];
          snprintf(buf, sizeof(buf), "State: %s", stateName(bleState));
          updateOLED("Not Ready", buf, "Key ignored", "");
          delay(DEBOUNCE_MS);
          continue;
        }

        if (k.action == ACTION_VOLUME_UP) {
          updateOLED("Sent:", "Volume Up", "", "");
          sendConsumer(CONSUMER_VOL_UP);
          delay(DEBOUNCE_MS); showLayerScreen();
          digitalWrite(rowPins[r], HIGH); return;
        }
        if (k.action == ACTION_VOLUME_DOWN) {
          updateOLED("Sent:", "Volume Down", "", "");
          sendConsumer(CONSUMER_VOL_DOWN);
          delay(DEBOUNCE_MS); showLayerScreen();
          digitalWrite(rowPins[r], HIGH); return;
        }
        if (k.action == ACTION_MUTE) {
          updateOLED("Sent:", "Mute", "", "");
          sendConsumer(CONSUMER_MUTE);
          delay(DEBOUNCE_MS); showLayerScreen();
          digitalWrite(rowPins[r], HIGH); return;
        }

        // Normal key
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

// ============================================================
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
