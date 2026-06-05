// ===== BLE GATT identifiers =====
export const CONFIG_SERVICE = "12345678-1234-1234-1234-1234567890ab";
export const STATUS_CHAR = "12345678-1234-1234-1234-1234567890ac";
export const KEYMAP_CHAR = "12345678-1234-1234-1234-1234567890ad";
export const SAVE_CHAR = "12345678-1234-1234-1234-1234567890ae";
export const DEVICE_NAME = "ESP32-C3 Macropad";

// ===== HID keycodes =====
export const HID_KEYS: Record<string, number> = {
  None: 0,
  "0": 0x27,
  "1": 0x1e,
  "2": 0x1f,
  "3": 0x20,
  "4": 0x21,
  "5": 0x22,
  "6": 0x23,
  "7": 0x24,
  "8": 0x25,
  "9": 0x26,
  A: 0x04,
  B: 0x05,
  C: 0x06,
  D: 0x07,
  E: 0x08,
  F: 0x09,
  G: 0x0a,
  H: 0x0b,
  I: 0x0c,
  J: 0x0d,
  K: 0x0e,
  L: 0x0f,
  M: 0x10,
  N: 0x11,
  O: 0x12,
  P: 0x13,
  Q: 0x14,
  R: 0x15,
  S: 0x16,
  T: 0x17,
  U: 0x18,
  V: 0x19,
  W: 0x1a,
  X: 0x1b,
  Y: 0x1c,
  Z: 0x1d,
  F1: 0x3a,
  F2: 0x3b,
  F3: 0x3c,
  F4: 0x3d,
  F5: 0x3e,
  F6: 0x3f,
  F7: 0x40,
  F8: 0x41,
  F9: 0x42,
  F10: 0x43,
  F11: 0x44,
  F12: 0x45,
  ESC: 0x29,
  Tab: 0x2b,
  Del: 0x4c,
  Home: 0x4a,
  End: 0x4d,
  PgUp: 0x4b,
  PgDn: 0x4e,
  Up: 0x52,
  Down: 0x51,
  Left: 0x50,
  Right: 0x4f,
  Enter: 0x28,
  Space: 0x2c,
  Backspace: 0x2a,
};

export const MODIFIERS: Record<string, number> = {
  None: 0,
  Ctrl: 1,
  Shift: 2,
  Alt: 4,
  "Win/GUI": 8,
  "Ctrl+Shift": 3,
  "Ctrl+Alt": 5,
  "Ctrl+Shift+Alt": 7,
};

export const ACTIONS: Record<string, number> = {
  "Normal Key": 0,
  "Layer Toggle": 1,
  "Volume Up": 2,
  "Volume Down": 3,
  "Mute": 4,
};

// Action constants.
export const ACTION_NORMAL = 0;

// Reverse lookups -------------------------------------------------------
export const KEYCODE_LABELS: Record<number, string> = Object.entries(
  HID_KEYS,
).reduce(
  (acc, [label, value]) => {
    // Keep the first label that maps to a value (None wins for 0).
    if (!(value in acc)) acc[value] = label;
    return acc;
  },
  {} as Record<number, string>,
);

// Short badge text per action (excluding Normal Key).
export const ACTION_SHORT: Record<number, string> = {
  1: "Layer",
  2: "VolUp",
  3: "VolDn",
  4: "Mute",
};

// Individual modifier bits for building compact tile badges.
const MOD_BITS: { bit: number; short: string }[] = [
  { bit: 1, short: "Ctrl" },
  { bit: 2, short: "Shift" },
  { bit: 4, short: "Alt" },
  { bit: 8, short: "Win" },
];

export function modifierLabel(mod: number): string {
  if (!mod) return "";
  return MOD_BITS.filter((m) => mod & m.bit)
    .map((m) => m.short)
    .join("+");
}

export const LAYER_NAMES = ["Numbers", "Shortcuts"];

// Build a sensible default label for a given modifier + keycode combo.
export function autoLabel(mod: number, kc: number): string {
  if (!kc) return "";
  const key = KEYCODE_LABELS[kc] ?? "";
  if (!key || key === "None") return "";
  const m = modifierLabel(mod);
  const combined = m ? `${m}+${key}` : key;
  return combined.slice(0, 11);
}