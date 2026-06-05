// ============================================================
//  ESP32-C3 BLE Macropad  — Firmware v3
//  Features:
//    • BLE HID Keyboard + Consumer (Volume)
//    • WiFi Access Point + Web Configurator at 192.168.4.1
//    • PIN-protected web UI
//    • 2 Layers — saved to flash (NVS Preferences)
//    • OLED status display
//    • Layer 0: Numbers  |  Layer 1: Custom Shortcuts
//    • K9  L0=VolUp  L1=VolDown
//    • K11 = Layer Toggle (both layers)
// ============================================================

// ---- Libraries ----
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
//  CONFIG — change these to your preference
// ============================================================
#define DEVICE_NAME       "ESP32-C3 Macropad"
#define MANUFACTURER_NAME "DIY"
#define WIFI_SSID         "MacropadConfig"
#define WIFI_PASS         "macropad123"      // WiFi AP password (min 8 chars)
#define WEB_PIN           "1234"             // PIN to access the web UI
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

#define DEBOUNCE_MS      150
#define ADV_RESTART_MS   500
#define MTU_SIZE         100
#define NUM_KEYS         (ROWS * COLS)   // 12
#define NUM_LAYERS       2

// ============================================================
//  HID constants
// ============================================================
#define MOD_NONE  0x00
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_GUI   0x08

// Keyboard keycodes
#define KEY_NONE 0x00
#define KEY_0    0x27
#define KEY_1    0x1E
#define KEY_2    0x1F
#define KEY_3    0x20
#define KEY_4    0x21
#define KEY_5    0x22
#define KEY_6    0x23
#define KEY_7    0x24
#define KEY_8    0x25
#define KEY_9    0x26
#define KEY_A    0x04
#define KEY_B    0x05
#define KEY_C    0x06
#define KEY_D    0x07
#define KEY_E    0x08
#define KEY_F    0x09
#define KEY_G    0x0A
#define KEY_H    0x0B
#define KEY_I    0x0C
#define KEY_J    0x0D
#define KEY_K    0x0E
#define KEY_L    0x0F
#define KEY_M    0x10
#define KEY_N    0x11
#define KEY_O    0x12
#define KEY_P    0x13
#define KEY_Q    0x14
#define KEY_R    0x15
#define KEY_S    0x16
#define KEY_T    0x17
#define KEY_U    0x18
#define KEY_V    0x19
#define KEY_W    0x1A
#define KEY_X    0x1B
#define KEY_Y    0x1C
#define KEY_Z    0x1D
#define KEY_F1   0x3A
#define KEY_F2   0x3B
#define KEY_F3   0x3C
#define KEY_F4   0x3D
#define KEY_F5   0x3E
#define KEY_F6   0x3F
#define KEY_F7   0x40
#define KEY_F8   0x41
#define KEY_F9   0x42
#define KEY_F10  0x43
#define KEY_F11  0x44
#define KEY_F12  0x45
#define KEY_ESC  0x29
#define KEY_TAB  0x2B
#define KEY_DEL  0x4C
#define KEY_HOME 0x4A
#define KEY_END  0x4D
#define KEY_PGUP 0x4B
#define KEY_PGDN 0x4E
#define KEY_UP   0x52
#define KEY_DOWN 0x51
#define KEY_LEFT 0x50
#define KEY_RIGHT 0x4F
#define KEY_ENTER 0x28
#define KEY_SPACE 0x2C
#define KEY_BKSP  0x2A

// Consumer usage IDs
#define CONSUMER_VOL_UP   0xE9
#define CONSUMER_VOL_DOWN 0xEA
#define CONSUMER_MUTE     0xE2
#define CONSUMER_PLAY     0xCD
#define CONSUMER_NEXT     0xB5
#define CONSUMER_PREV     0xB6

// Special actions
#define ACTION_NONE         0
#define ACTION_LAYER_TOGGLE 1
#define ACTION_VOLUME_UP    2
#define ACTION_VOLUME_DOWN  3
#define ACTION_MUTE         4

// ============================================================
//  Key Definition
// ============================================================
struct KeyDef {
  uint8_t mod;
  uint8_t keycode;
  uint8_t action;
  char    label[12];
};

// ============================================================
//  DEFAULT key maps — loaded into RAM, overwritten by NVS
// ============================================================
KeyDef keyMap[NUM_LAYERS][ROWS][COLS] = {
  // ---- LAYER 0: Numbers ----
  {
    { {MOD_NONE,KEY_1,ACTION_NONE,"1"},       {MOD_NONE,KEY_2,ACTION_NONE,"2"},       {MOD_NONE,KEY_3,ACTION_NONE,"3"}       },
    { {MOD_NONE,KEY_4,ACTION_NONE,"4"},       {MOD_NONE,KEY_5,ACTION_NONE,"5"},       {MOD_NONE,KEY_6,ACTION_NONE,"6"}       },
    { {MOD_NONE,KEY_7,ACTION_NONE,"7"},       {MOD_NONE,KEY_8,ACTION_NONE,"8"},       {MOD_NONE,KEY_9,ACTION_NONE,"9"}       },
    { {MOD_NONE,KEY_NONE,ACTION_VOLUME_UP,"VolUp"}, {MOD_NONE,KEY_0,ACTION_NONE,"0"}, {MOD_NONE,KEY_NONE,ACTION_LAYER_TOGGLE,"MODE"} },
  },
  // ---- LAYER 1: Custom Shortcuts ----
  {
    { {MOD_CTRL,KEY_Z,ACTION_NONE,"Ctrl+Z"},  {MOD_CTRL,KEY_S,ACTION_NONE,"Ctrl+S"},  {MOD_CTRL,KEY_A,ACTION_NONE,"Ctrl+A"}  },
    { {MOD_CTRL,KEY_X,ACTION_NONE,"Ctrl+X"},  {MOD_CTRL,KEY_Y,ACTION_NONE,"Ctrl+Y"},  {MOD_GUI, KEY_D,ACTION_NONE,"Win+D"}   },
    { {MOD_ALT, KEY_F4,ACTION_NONE,"Alt+F4"}, {MOD_CTRL,KEY_W,ACTION_NONE,"Ctrl+W"},  {MOD_CTRL,KEY_T,ACTION_NONE,"Ctrl+T"}  },
    { {MOD_NONE,KEY_NONE,ACTION_VOLUME_DOWN,"VolDn"}, {MOD_NONE,KEY_0,ACTION_NONE,"0"}, {MOD_NONE,KEY_NONE,ACTION_LAYER_TOGGLE,"MODE"} },
  }
};

// ============================================================
//  Global objects
// ============================================================
Preferences               prefs;
WebServer                 webServer(WEB_PORT);
NimBLEHIDDevice*          hid           = nullptr;
NimBLECharacteristic*     inputKbd      = nullptr;
NimBLECharacteristic*     inputConsumer = nullptr;
NimBLEServer*             pServer       = nullptr;
Adafruit_SSD1306          display(SCREEN_W, SCREEN_H, &Wire, -1);
bool                      oledOk        = false;
int                       currentLayer  = 0;
bool                      webAuthed     = false; // simple session flag (single client)

// ============================================================
//  BLE State
// ============================================================
enum class BleState { INIT, ADVERTISING, PAIRING, CONNECTED, DISCONNECTED };
volatile BleState bleState  = BleState::INIT;
volatile bool     needReAdv = false;
uint32_t          connectedAt = 0;

// ============================================================
//  Forward declarations
// ============================================================
void updateOLED(const char* l1,const char* l2,const char* l3,const char* l4="");
void startAdvertising();
void sendKey(uint8_t mod,uint8_t kc);
void sendConsumer(uint8_t usage);
void showLayerScreen();
void saveKeyMap();
void loadKeyMap();
const char* stateName(BleState s);
void setupWebRoutes();
String buildMainPage();
String buildLoginPage();
String keyLabel(uint8_t mod,uint8_t kc,uint8_t action);

// ============================================================
//  BLE Server Callbacks
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pSrv,NimBLEConnInfo& ci) override {
    bleState = BleState::PAIRING;
    Serial.printf("[BLE] Connected: %s\n",ci.getAddress().toString().c_str());
    updateOLED("BLE","Pairing...",ci.getAddress().toString().c_str());
    pSrv->setDataLen(ci.getConnHandle(),MTU_SIZE);
    NimBLEDevice::setMTU(MTU_SIZE);
  }
  void onDisconnect(NimBLEServer* pSrv,NimBLEConnInfo& ci,int reason) override {
    bleState  = BleState::DISCONNECTED;
    needReAdv = true;
    Serial.printf("[BLE] Disconnected: 0x%02X\n",reason);
    updateOLED("BLE","Disconnected","Re-advertising...");
  }
  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    if(ci.isEncrypted()){
      bleState    = BleState::CONNECTED;
      connectedAt = millis();
      Serial.println("[BLE] Ready");
      showLayerScreen();
    } else {
      NimBLEDevice::getServer()->disconnect(ci.getConnHandle());
    }
  }
};

// ============================================================
//  OLED
// ============================================================
void updateOLED(const char* l1,const char* l2,const char* l3,const char* l4){
  if(!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0,0,SCREEN_W,10,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2,1);
  display.print(DEVICE_NAME);
  display.setTextColor(SSD1306_WHITE);
  if(bleState==BleState::CONNECTED) display.fillCircle(SCREEN_W-5,5,3,SSD1306_BLACK);
  display.setCursor(0,13); display.print(l1);
  display.setCursor(0,25); display.print(l2);
  display.setCursor(0,37); display.print(l3);
  display.setCursor(0,49); display.print(l4);
  display.display();
}

void showLayerScreen(){
  char top[32]; snprintf(top,sizeof(top),"Layer %d %s",currentLayer,currentLayer==0?"Numbers":"Shortcuts");
  if(currentLayer==0) updateOLED(top,"1-9, 0","K9=VolUp K11=MODE","");
  else                updateOLED(top,"C+Z C+S C+A","C+X C+Y Win+D","A+F4 C+W C+T");
}

// ============================================================
//  BLE Advertising
// ============================================================
void startAdvertising(){
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->reset();
  pAdv->setAppearance(HID_KEYBOARD);
  pAdv->addServiceUUID(hid->getHidService()->getUUID());
  NimBLEAdvertisementData sr; sr.setName(DEVICE_NAME);
  pAdv->setScanResponseData(sr);
  pAdv->enableScanResponse(true);
  pAdv->setMinInterval(0x20); pAdv->setMaxInterval(0x40);
  bleState = BleState::ADVERTISING;
  if(pAdv->start()) updateOLED("BLE","Advertising...","Waiting for host");
  else              updateOLED("BLE","ADV FAILED","Reset device");
}

// ============================================================
//  Key sending
// ============================================================
void sendKey(uint8_t mod,uint8_t kc){
  if(bleState!=BleState::CONNECTED||!inputKbd) return;
  uint8_t d[8]={mod,0,kc,0,0,0,0,0}, r[8]={0};
  inputKbd->setValue(d,8); inputKbd->notify(); delay(10);
  inputKbd->setValue(r,8); inputKbd->notify(); delay(10);
}

void sendConsumer(uint8_t usage){
  if(bleState!=BleState::CONNECTED||!inputConsumer) return;
  uint8_t p[2]={usage,0}, r[2]={0,0};
  inputConsumer->setValue(p,2); inputConsumer->notify(); delay(10);
  inputConsumer->setValue(r,2); inputConsumer->notify(); delay(10);
}

// ============================================================
//  NVS save / load
// ============================================================
void saveKeyMap(){
  prefs.begin("keymap",false);
  for(int layer=0;layer<NUM_LAYERS;layer++){
    for(int r=0;r<ROWS;r++){
      for(int c=0;c<COLS;c++){
        int idx=layer*NUM_KEYS+r*COLS+c;
        char kmod[16],kkc[16],kact[16],klbl[16];
        snprintf(kmod,16,"m%d",idx); snprintf(kkc,16,"k%d",idx);
        snprintf(kact,16,"a%d",idx); snprintf(klbl,16,"l%d",idx);
        prefs.putUChar(kmod,keyMap[layer][r][c].mod);
        prefs.putUChar(kkc, keyMap[layer][r][c].keycode);
        prefs.putUChar(kact,keyMap[layer][r][c].action);
        prefs.putString(klbl,keyMap[layer][r][c].label);
      }
    }
  }
  prefs.end();
  Serial.println("[NVS] Keymap saved");
}

void loadKeyMap(){
  prefs.begin("keymap",true);
  if(!prefs.isKey("m0")){ prefs.end(); Serial.println("[NVS] No saved keymap — using defaults"); return; }
  for(int layer=0;layer<NUM_LAYERS;layer++){
    for(int r=0;r<ROWS;r++){
      for(int c=0;c<COLS;c++){
        int idx=layer*NUM_KEYS+r*COLS+c;
        char kmod[16],kkc[16],kact[16],klbl[16];
        snprintf(kmod,16,"m%d",idx); snprintf(kkc,16,"k%d",idx);
        snprintf(kact,16,"a%d",idx); snprintf(klbl,16,"l%d",idx);
        keyMap[layer][r][c].mod     = prefs.getUChar(kmod,keyMap[layer][r][c].mod);
        keyMap[layer][r][c].keycode = prefs.getUChar(kkc, keyMap[layer][r][c].keycode);
        keyMap[layer][r][c].action  = prefs.getUChar(kact,keyMap[layer][r][c].action);
        String lbl = prefs.getString(klbl,keyMap[layer][r][c].label);
        strncpy(keyMap[layer][r][c].label,lbl.c_str(),11);
        keyMap[layer][r][c].label[11]='\0';
      }
    }
  }
  prefs.end();
  Serial.println("[NVS] Keymap loaded");
}

// ============================================================
//  Web UI HTML
// ============================================================
String escHtml(const char* s){
  String o=s;
  o.replace("&","&amp;"); o.replace("<","&lt;"); o.replace(">","&gt;");
  o.replace("\"","&quot;");
  return o;
}

// Returns a JS keycodes map and modifier constants used in the web UI
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
const ACTIONS={"Normal":0,"LayerToggle":1,"VolumeUp":2,"VolumeDown":3,"Mute":4};
const MODS={"None":0,"Ctrl":1,"Shift":2,"Alt":4,"Win":8,"Ctrl+Shift":3,"Ctrl+Alt":5,"Ctrl+Shift+Alt":7};
)JS";

String buildLoginPage(){
  return R"HTML(<!DOCTYPE html><html><head>
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
  .logo{font-family:'Share Tech Mono',monospace;font-size:13px;color:#7855ff;
    letter-spacing:4px;text-transform:uppercase;margin-bottom:8px}
  h1{font-size:22px;font-weight:600;margin-bottom:6px;color:#fff}
  p{font-size:13px;color:#888;margin-bottom:28px}
  input{width:100%;background:rgba(255,255,255,0.06);border:1px solid rgba(120,80,255,0.4);
    border-radius:10px;padding:14px 16px;color:#fff;font-size:20px;
    letter-spacing:8px;text-align:center;font-family:'Share Tech Mono',monospace;outline:none}
  input:focus{border-color:#7855ff;box-shadow:0 0 0 3px rgba(120,85,255,0.2)}
  button{width:100%;margin-top:16px;background:linear-gradient(135deg,#7855ff,#4a2dc7);
    border:none;border-radius:10px;padding:14px;color:#fff;font-size:15px;
    font-family:'Exo 2',sans-serif;font-weight:600;cursor:pointer;letter-spacing:1px;
    transition:opacity .2s}
  button:hover{opacity:.85}
  .err{color:#ff6b6b;font-size:13px;margin-top:12px;display:none}
  .key-icon{font-size:40px;margin-bottom:20px;filter:drop-shadow(0 0 12px #7855ff)}
</style></head><body>
<div class="card">
  <div class="key-icon">⌨️</div>
  <div class="logo">ESP32-C3</div>
  <h1>Macropad Config</h1>
  <p>Enter your PIN to continue</p>
  <input type="password" id="pin" maxlength="8" placeholder="••••" inputmode="numeric" autofocus>
  <button onclick="doLogin()">Unlock →</button>
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
    else{ document.getElementById('err').style.display='block';
          document.getElementById('pin').value=''; }
  });
}
</script></body></html>)HTML";
}

String buildMainPage(){
  // Build JSON of current keymap for JS
  String kmJson="[";
  for(int layer=0;layer<NUM_LAYERS;layer++){
    kmJson+="[";
    for(int r=0;r<ROWS;r++){
      for(int c=0;c<COLS;c++){
        KeyDef& k=keyMap[layer][r][c];
        int idx=r*COLS+c;
        kmJson+="{\"idx\":"+String(idx)+",\"layer\":"+String(layer)+
                ",\"mod\":"+String(k.mod)+",\"kc\":"+String(k.keycode)+
                ",\"action\":"+String(k.action)+
                ",\"label\":\""+escHtml(k.label)+"\"}";
        if(!(r==ROWS-1&&c==COLS-1)) kmJson+=",";
      }
    }
    kmJson+="]";
    if(layer<NUM_LAYERS-1) kmJson+=",";
  }
  kmJson+="]";

  String html = R"HTML(<!DOCTYPE html><html><head>
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
  .logo{font-family:'Share Tech Mono',monospace;font-size:11px;color:var(--accent);
    letter-spacing:3px;text-transform:uppercase}
  h1{font-size:17px;font-weight:700;color:#fff}
  .badge{background:rgba(120,85,255,0.15);border:1px solid var(--border);
    border-radius:20px;padding:4px 12px;font-size:11px;color:var(--accent);
    font-family:'Share Tech Mono',monospace}
  main{padding:20px;max-width:600px;margin:0 auto}
  .section-title{font-size:11px;letter-spacing:3px;text-transform:uppercase;
    color:var(--muted);margin:24px 0 12px;font-family:'Share Tech Mono',monospace}
  /* Layer tabs */
  .tabs{display:flex;gap:8px;margin-bottom:20px}
  .tab{flex:1;padding:10px;border-radius:10px;border:1px solid var(--border);
    background:var(--surface);color:var(--muted);font-size:13px;font-family:'Exo 2',sans-serif;
    cursor:pointer;transition:all .2s;text-align:center;font-weight:600}
  .tab.active{background:rgba(120,85,255,0.2);border-color:var(--accent);color:#fff}
  /* Key grid */
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
  .key-label{font-family:'Share Tech Mono',monospace;font-size:13px;color:#fff;
    line-height:1.3;word-break:break-all}
  .key-sub{font-size:9px;color:var(--muted);margin-top:3px}
  /* Modal */
  .overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.7);
    backdrop-filter:blur(6px);z-index:100;align-items:flex-end;justify-content:center}
  .overlay.show{display:flex}
  .modal{background:#13111f;border:1px solid var(--border);border-radius:20px 20px 0 0;
    padding:28px 24px;width:100%;max-width:500px;
    animation:slideUp .25s ease}
  @keyframes slideUp{from{transform:translateY(100%)}to{transform:translateY(0)}}
  .modal-title{font-size:16px;font-weight:700;margin-bottom:4px;color:#fff}
  .modal-sub{font-size:12px;color:var(--muted);margin-bottom:20px;
    font-family:'Share Tech Mono',monospace}
  label{display:block;font-size:11px;letter-spacing:2px;text-transform:uppercase;
    color:var(--muted);margin-bottom:6px;font-family:'Share Tech Mono',monospace}
  select,input[type=text]{width:100%;background:rgba(255,255,255,0.06);
    border:1px solid var(--border);border-radius:10px;padding:11px 14px;
    color:#fff;font-family:'Share Tech Mono',monospace;font-size:14px;outline:none;
    margin-bottom:16px;appearance:none}
  select:focus,input[type=text]:focus{border-color:var(--accent);
    box-shadow:0 0 0 3px rgba(120,85,255,0.2)}
  select option{background:#13111f;color:#fff}
  .row2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  .btn-row{display:flex;gap:10px;margin-top:4px}
  .btn{flex:1;padding:13px;border-radius:10px;border:none;font-family:'Exo 2',sans-serif;
    font-size:14px;font-weight:600;cursor:pointer;transition:all .2s}
  .btn-primary{background:linear-gradient(135deg,var(--accent),#4a2dc7);color:#fff}
  .btn-primary:hover{opacity:.85}
  .btn-cancel{background:rgba(255,255,255,0.07);color:var(--muted)}
  .btn-cancel:hover{background:rgba(255,255,255,0.12);color:#fff}
  /* Save bar */
  .save-bar{position:fixed;bottom:0;left:0;right:0;padding:16px 20px;
    background:rgba(10,10,15,0.95);border-top:1px solid var(--border);
    display:flex;gap:10px;justify-content:center;max-width:600px;margin:0 auto;
    transform:translateY(100%);transition:transform .3s;z-index:50}
  .save-bar.show{transform:translateY(0)}
  .toast{position:fixed;top:80px;left:50%;transform:translateX(-50%) translateY(-20px);
    background:rgba(120,85,255,0.9);color:#fff;padding:10px 24px;border-radius:30px;
    font-size:13px;opacity:0;transition:all .3s;pointer-events:none;z-index:200;white-space:nowrap}
  .toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
  .status-row{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:20px}
  .stat{background:var(--surface);border:1px solid var(--border);border-radius:10px;
    padding:10px 14px;flex:1;min-width:100px}
  .stat-label{font-size:9px;text-transform:uppercase;letter-spacing:2px;color:var(--muted);
    font-family:'Share Tech Mono',monospace}
  .stat-val{font-size:15px;font-weight:700;margin-top:2px;color:#fff}
  .stat-val.on{color:#4fffb0}
  .stat-val.off{color:#ff6b6b}
</style></head><body>
<header>
  <div><div class="logo">ESP32-C3</div><h1>Macropad Config</h1></div>
  <div class="badge" id="bleBadge">BLE ···</div>
</header>
<main>
  <div class="section-title">Device Status</div>
  <div class="status-row">
    <div class="stat"><div class="stat-label">BLE</div><div class="stat-val" id="bleVal">-</div></div>
    <div class="stat"><div class="stat-label">Layer</div><div class="stat-val" id="layerVal">-</div></div>
    <div class="stat"><div class="stat-label">WiFi</div><div class="stat-val on">AP Active</div></div>
  </div>

  <div class="section-title">Key Layout</div>
  <div class="tabs">
    <div class="tab active" onclick="switchLayer(0)" id="tab0">Layer 0 — Numbers</div>
    <div class="tab" onclick="switchLayer(1)" id="tab1">Layer 1 — Shortcuts</div>
  </div>
  <div class="grid" id="keyGrid"></div>
</main>

<!-- Save bar -->
<div class="save-bar" id="saveBar">
  <button class="btn btn-cancel" onclick="discardChanges()">Discard</button>
  <button class="btn btn-primary" onclick="saveChanges()">Save to Device 💾</button>
</div>

<!-- Edit modal -->
<div class="overlay" id="overlay" onclick="closeModal(event)">
  <div class="modal" id="modal">
    <div class="modal-title" id="modalTitle">Edit Key</div>
    <div class="modal-sub" id="modalSub">K0 · Layer 0</div>
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
            <option value="8">Win / GUI</option>
            <option value="3">Ctrl + Shift</option>
            <option value="5">Ctrl + Alt</option>
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

  html += "const keymap = "; html += kmJson; html += ";\n";
  html += JS_KEYCODES;
  html += R"JS(

let currentLayer = 0;
let editingIdx   = -1;
let dirty        = false;
let km           = JSON.parse(JSON.stringify(keymap)); // deep copy

// --- Build key select options ---
function buildKcSelect(){
  const sel=document.getElementById('editKc');
  sel.innerHTML='';
  Object.keys(KC).forEach(name=>{
    const o=document.createElement('option');
    o.value=KC[name]; o.textContent=name;
    sel.appendChild(o);
  });
}
buildKcSelect();

function onActionChange(){
  const v=parseInt(document.getElementById('editAction').value);
  document.getElementById('keyFields').style.display=(v===0)?'block':'none';
}

// --- Render grid ---
function renderGrid(){
  const grid=document.getElementById('keyGrid');
  grid.innerHTML='';
  const layer=km[currentLayer];
  layer.forEach((key,i)=>{
    const div=document.createElement('div');
    const special=(key.action!==0);
    div.className='key'+(special?' special':'');
    let sub='';
    if(key.action===0){
      const mods=['','Ctrl','Shift','Ctrl+Shift','Alt','Ctrl+Alt','','Ctrl+Shift+Alt','Win'];
      sub=mods[key.mod]||'';
    } else {
      const acts=['','[Layer Toggle]','[Vol Up]','[Vol Down]','[Mute]'];
      sub=acts[key.action]||'';
    }
    div.innerHTML=`<span class="key-num">K${i}</span>
      <span class="key-label">${key.label}</span>
      <span class="key-sub">${sub}</span>`;
    div.onclick=()=>openModal(i);
    grid.appendChild(div);
  });
}

function switchLayer(l){
  currentLayer=l;
  document.getElementById('tab0').className='tab'+(l===0?' active':'');
  document.getElementById('tab1').className='tab'+(l===1?' active':'');
  renderGrid();
}

// --- Modal ---
function openModal(idx){
  editingIdx=idx;
  const key=km[currentLayer][idx];
  document.getElementById('modalTitle').textContent='Edit K'+idx;
  document.getElementById('modalSub').textContent='K'+idx+' · Layer '+currentLayer;
  document.getElementById('editAction').value=key.action;
  document.getElementById('editMod').value=key.mod;
  // Set keycode select
  const sel=document.getElementById('editKc');
  for(let i=0;i<sel.options.length;i++){
    if(parseInt(sel.options[i].value)===key.kc){sel.selectedIndex=i;break;}
  }
  document.getElementById('editLabel').value=key.label;
  onActionChange();
  document.getElementById('overlay').classList.add('show');
}

function closeModal(e){
  if(e&&e.target!==document.getElementById('overlay')) return;
  document.getElementById('overlay').classList.remove('show');
}

function applyEdit(){
  const action=parseInt(document.getElementById('editAction').value);
  const mod   =parseInt(document.getElementById('editMod').value);
  const kc    =parseInt(document.getElementById('editKc').value);
  const label =document.getElementById('editLabel').value.trim()||'K'+editingIdx;
  km[currentLayer][editingIdx]={...km[currentLayer][editingIdx],action,mod,kc,label};
  document.getElementById('overlay').classList.remove('show');
  renderGrid();
  setDirty(true);
}

function setDirty(v){
  dirty=v;
  document.getElementById('saveBar').className='save-bar'+(v?' show':'');
}

function discardChanges(){
  km=JSON.parse(JSON.stringify(keymap));
  renderGrid();
  setDirty(false);
  showToast('Changes discarded');
}

function saveChanges(){
  fetch('/save',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({keymap:km})
  }).then(r=>r.json()).then(d=>{
    if(d.ok){ showToast('Saved to device! ✓'); setDirty(false); }
    else     showToast('Save failed ✗');
  }).catch(()=>showToast('Save failed ✗'));
}

function showToast(msg){
  const t=document.getElementById('toast');
  t.textContent=msg; t.classList.add('show');
  setTimeout(()=>t.classList.remove('show'),2500);
}

// --- Status polling ---
function pollStatus(){
  fetch('/status').then(r=>r.json()).then(d=>{
    const v=document.getElementById('bleVal');
    const badge=document.getElementById('bleBadge');
    v.textContent=d.ble;
    v.className='stat-val '+(d.ble==='CONNECTED'?'on':'off');
    badge.textContent='BLE: '+d.ble;
    document.getElementById('layerVal').textContent='Layer '+d.layer;
  }).catch(()=>{});
}
setInterval(pollStatus,3000);
pollStatus();

// --- Init ---
renderGrid();
</script></body></html>)JS";

  return html;
}

// ============================================================
//  Web server routes
// ============================================================
void setupWebRoutes(){
  // Login page GET
  webServer.on("/login", HTTP_GET, [](){
    if(webAuthed){ webServer.sendHeader("Location","/"); webServer.send(302); return; }
    webServer.send(200,"text/html",buildLoginPage());
  });

  // Login POST
  webServer.on("/login", HTTP_POST, [](){
    String pin = webServer.arg("pin");
    if(pin == WEB_PIN){
      webAuthed = true;
      webServer.send(200,"application/json","{\"ok\":true}");
    } else {
      webServer.send(200,"application/json","{\"ok\":false}");
    }
  });

  // Main page
  webServer.on("/", HTTP_GET, [](){
    if(!webAuthed){ webServer.sendHeader("Location","/login"); webServer.send(302); return; }
    webServer.send(200,"text/html",buildMainPage());
  });

  // Status JSON
  webServer.on("/status", HTTP_GET, [](){
    if(!webAuthed){ webServer.send(403,"application/json","{\"err\":\"unauth\"}"); return; }
    String j="{\"ble\":\"";
    j+=stateName(bleState);
    j+="\",\"layer\":"+String(currentLayer)+"}";
    webServer.send(200,"application/json",j);
  });

  // Save keymap
  webServer.on("/save", HTTP_POST, [](){
    if(!webAuthed){ webServer.send(403,"application/json","{\"err\":\"unauth\"}"); return; }
    String body = webServer.arg("plain");
    // Parse JSON manually (avoid ArduinoJson dependency)
    // Format: {"keymap":[[{idx,layer,mod,kc,action,label},...],[...]]}
    // We'll use a lightweight approach: find each key object
    bool ok = false;
    int layerIdx=0, keyIdx=0;
    // We search for "mod":N,"kc":N,"action":N,"label":"..."
    // Simple sequential parser
    int pos=0;
    int foundKeys=0;
    while(pos<(int)body.length() && foundKeys<NUM_LAYERS*NUM_KEYS){
      // Find next "idx":
      int i=body.indexOf("\"idx\":",pos);
      if(i<0) break;
      int idx=body.substring(i+6).toInt();
      int li=body.indexOf("\"layer\":",i);
      int layer=body.substring(li+8).toInt();
      int mi=body.indexOf("\"mod\":",i);
      uint8_t mod=(uint8_t)body.substring(mi+6).toInt();
      int ki=body.indexOf("\"kc\":",i);
      uint8_t kc=(uint8_t)body.substring(ki+5).toInt();
      int ai=body.indexOf("\"action\":",i);
      uint8_t action=(uint8_t)body.substring(ai+9).toInt();
      int lbi=body.indexOf("\"label\":\"",i);
      int lbe=body.indexOf("\"",lbi+9);
      String label=body.substring(lbi+9,lbe);
      if(layer>=0&&layer<NUM_LAYERS&&idx>=0&&idx<NUM_KEYS){
        int r=idx/COLS, c=idx%COLS;
        keyMap[layer][r][c].mod=mod;
        keyMap[layer][r][c].keycode=kc;
        keyMap[layer][r][c].action=action;
        strncpy(keyMap[layer][r][c].label,label.c_str(),11);
        keyMap[layer][r][c].label[11]='\0';
        foundKeys++;
        ok=true;
      }
      pos=lbe+1;
    }
    if(ok) saveKeyMap();
    webServer.send(200,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");
  });

  // Logout
  webServer.on("/logout", HTTP_GET, [](){
    webAuthed=false;
    webServer.sendHeader("Location","/login");
    webServer.send(302);
  });

  webServer.onNotFound([](){
    webServer.sendHeader("Location","/");
    webServer.send(302);
  });
}

// ============================================================
//  setup()
// ============================================================
void setup(){
  Serial.begin(115200);
  delay(800);
  Serial.println("[BOOT] ESP32-C3 BLE Macropad v3");

  // OLED
  Wire.begin(OLED_SDA,OLED_SCL);
  oledOk=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR);
  if(oledOk){ display.clearDisplay(); display.display(); updateOLED("Booting v3...","",""); }
  else Serial.println("[OLED] Not found");

  // Matrix
  for(int r=0;r<ROWS;r++){ pinMode(rowPins[r],OUTPUT); digitalWrite(rowPins[r],HIGH); }
  for(int c=0;c<COLS;c++) pinMode(colPins[c],INPUT_PULLUP);
  Serial.println("[MATRIX] Ready");

  // Load saved keymap
  loadKeyMap();

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] AP started: %s  IP: %s\n", WIFI_SSID,
                WiFi.softAPIP().toString().c_str());
  updateOLED("WiFi AP Ready", WiFi.softAPIP().toString().c_str(), "Connect to config");
  delay(1000);

  // Web server
  setupWebRoutes();
  webServer.begin();
  Serial.println("[WEB] Server started on port 80");

  // BLE
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true,true,true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  pServer=NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  hid=new NimBLEHIDDevice(pServer);
  hid->setManufacturer(MANUFACTURER_NAME);
  hid->setPnp(0x01,0x0000,0x0000,0x0100);
  hid->setHidInfo(0x00,0x01);

  static const uint8_t reportMap[]={
    0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,
    0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,
    0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xC0,
    0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x02,
    0x15,0x00,0x26,0xFF,0x03,0x19,0x00,0x2A,0xFF,0x03,
    0x75,0x10,0x95,0x01,0x81,0x00,0xC0
  };
  hid->setReportMap((uint8_t*)reportMap,sizeof(reportMap));
  inputKbd      = hid->getInputReport(1);
  inputConsumer = hid->getInputReport(2);
  hid->startServices();

  startAdvertising();
  Serial.println("[BOOT] Ready — BLE advertising, WiFi AP up");
  updateOLED("BLE Advertising", "WiFi: "+String(WIFI_SSID), "192.168.4.1");
}

// ============================================================
//  loop()
// ============================================================
void loop(){
  webServer.handleClient();

  if(needReAdv){ needReAdv=false; delay(ADV_RESTART_MS); startAdvertising(); }

  // Matrix scan
  for(int r=0;r<ROWS;r++){
    digitalWrite(rowPins[r],LOW);
    delayMicroseconds(10);
    for(int c=0;c<COLS;c++){
      if(digitalRead(colPins[c])==LOW){
        const KeyDef& k=keyMap[currentLayer][r][c];
        int kn=r*COLS+c;
        Serial.printf("[KEY] K%d -> %s (L%d %s)\n",kn,k.label,currentLayer,stateName(bleState));

        // Layer toggle — always works
        if(k.action==ACTION_LAYER_TOGGLE){
          currentLayer=(currentLayer==0)?1:0;
          Serial.printf("[LAYER] -> %d\n",currentLayer);
          showLayerScreen();
          delay(DEBOUNCE_MS);
          digitalWrite(rowPins[r],HIGH);
          return;
        }

        // Volume actions
        if(k.action==ACTION_VOLUME_UP){
          if(bleState==BleState::CONNECTED){
            updateOLED("Sent:","Volume Up","","");
            sendConsumer(CONSUMER_VOL_UP);
            delay(DEBOUNCE_MS); showLayerScreen();
          } else { updateOLED("Not Connected",stateName(bleState),"Key ignored",""); delay(DEBOUNCE_MS); }
          digitalWrite(rowPins[r],HIGH); return;
        }
        if(k.action==ACTION_VOLUME_DOWN){
          if(bleState==BleState::CONNECTED){
            updateOLED("Sent:","Volume Down","","");
            sendConsumer(CONSUMER_VOL_DOWN);
            delay(DEBOUNCE_MS); showLayerScreen();
          } else { updateOLED("Not Connected",stateName(bleState),"Key ignored",""); delay(DEBOUNCE_MS); }
          digitalWrite(rowPins[r],HIGH); return;
        }
        if(k.action==ACTION_MUTE){
          if(bleState==BleState::CONNECTED){
            updateOLED("Sent:","Mute","","");
            sendConsumer(CONSUMER_MUTE);
            delay(DEBOUNCE_MS); showLayerScreen();
          } else { updateOLED("Not Connected",stateName(bleState),"Key ignored",""); delay(DEBOUNCE_MS); }
          digitalWrite(rowPins[r],HIGH); return;
        }

        // Normal key
        if(bleState==BleState::CONNECTED){
          char buf[24]; snprintf(buf,24,"K%d: %s",kn,k.label);
          updateOLED("Sent:",buf,"","");
          sendKey(k.mod,k.keycode);
          delay(DEBOUNCE_MS); showLayerScreen();
        } else {
          updateOLED("Not Connected",stateName(bleState),"Key ignored","");
          delay(DEBOUNCE_MS);
        }
      }
    }
    digitalWrite(rowPins[r],HIGH);
  }
}

// ============================================================
const char* stateName(BleState s){
  switch(s){
    case BleState::INIT:         return "INIT";
    case BleState::ADVERTISING:  return "ADV";
    case BleState::PAIRING:      return "PAIRING";
    case BleState::CONNECTED:    return "CONNECTED";
    case BleState::DISCONNECTED: return "DISCONNECTED";
    default:                     return "UNKNOWN";
  }
}
