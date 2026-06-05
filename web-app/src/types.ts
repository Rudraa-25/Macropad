export interface KeyDef {
  i: number; // index 0-11
  l: number; // layer 0 or 1
  m: number; // modifier bitmask: 0=none 1=Ctrl 2=Shift 4=Alt 8=Win
  k: number; // HID keycode (0 = none)
  a: number; // action: 0=key 1=layer-toggle 2=vol-up 3=vol-down 4=mute
  n: string; // display label, max 11 chars
}

export interface DeviceStatus {
  device: string;
  fw: string;
  layers: number;
  keys: number;
}

export interface KeymapResponse {
  layers: number;
  keys: number;
  keymap: KeyDef[][];
}

export type Keymap = KeyDef[][];