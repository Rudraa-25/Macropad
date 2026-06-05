# ESP32-C3 Custom Macropad ⌨️

A fully open-source, DIY Bluetooth Macropad built with an ESP32-C3, featuring an OLED display and a dynamic **Web Bluetooth** configuration app. Configure your keymaps instantly from the web, completely wirelessly!

![Macropad Showcase](media/WhatsApp%20Image%202026-06-05%20at%206.21.26%20PM.jpeg)

## ✨ Features

- **True Wireless**: Acts as a BLE (Bluetooth Low Energy) HID Keyboard. No USB required for normal use.
- **Instant Web Configuration**: Includes a beautiful web application (`web-app/`) built with React and Web Bluetooth API. Re-map your keys, layers, and media controls straight from your browser without installing *any* drivers or software.
- **OLED Display**: Built-in SSD1306 OLED display for showing the current layer, sent keystrokes, and connection status.
- **Multi-Layer Support**: Toggle between different key layers instantly.
- **Zero-Latency Binary Protocol**: Uses a highly optimized 384-byte binary GATT protocol to instantly fetch and save your keymap layout.

---

## 📂 Repository Structure

```
├── firmware/       # ESP32-C3 Arduino firmware code (.ino)
├── hardware/       # KiCad schematics and PDF blueprints
├── web-app/        # React + Vite web configuration tool (Vercel ready)
└── media/          # Images and video showcases of the build
```

---

## 🛠️ Hardware Requirements

- **ESP32-C3** Microcontroller
- 12x Mechanical Switches (4 Rows x 3 Cols)
- SSD1306 OLED Display (I2C)
- Custom PCB or Handwired Matrix (See `hardware/output.pdf` for schematics)

### Media & Build Process
Here are some behind-the-scenes looks at the build:
![Schematic Process](media/WhatsApp%20Image%202026-06-05%20at%206.21.26%20PM%20(3).jpeg)

*(More images and videos can be found in the `media/` folder!)*

---

## 🚀 Getting Started

### 1. Flash the Firmware
1. Open `firmware/Macropad.ino` in the Arduino IDE.
2. Install the required libraries (`NimBLE-Arduino`, `Adafruit GFX`, `Adafruit SSD1306`).
3. Select your ESP32-C3 board and flash it.

### 2. Pair with your OS (Important!)
Because the device acts as a secure HID Keyboard, you **must pair it in your OS settings first** (Windows/Mac/Linux) before the Web App can communicate with it.

1. Power on the ESP32-C3 Macropad.
2. Go to your computer's Bluetooth Settings and click "Add Device".
3. Pair with **"ESP32-C3 Macropad"**.

### 3. Deploy the Web App
The `web-app` folder contains the frontend configurator. You can deploy it for free using **Vercel** with zero configuration:

[![Deploy with Vercel](https://vercel.com/button)](https://vercel.com/new/clone?repository-url=https://github.com/Rudraa-25/Macropad/tree/main/web-app)

Alternatively, to run it locally:
```bash
cd web-app
npm install
npm run dev
```

### 4. Configure Your Keys
1. Open your deployed web app in Google Chrome or Microsoft Edge (Safari/Firefox do not support Web Bluetooth).
2. Click **Connect Device**.
3. Re-map your keys and click Save!

---

## 🤝 Contributing
Feel free to open issues or submit pull requests. All hardware designs, firmware code, and web application code are completely open-source!
