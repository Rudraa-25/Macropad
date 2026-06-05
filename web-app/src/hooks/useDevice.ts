import { useCallback, useEffect, useRef, useState } from "react";
import {
  CONFIG_SERVICE,
  DEVICE_NAME,
  KEYMAP_CHAR,
  SAVE_CHAR,
  STATUS_CHAR,
} from "@/constants";
import type { DeviceStatus, Keymap, KeymapResponse } from "@/types";

const CHUNK = 490;

function decode(value: DataView): string {
  return new TextDecoder().decode(value);
}

async function chunkedWrite(
  char: BluetoothRemoteGATTCharacteristic,
  data: Uint8Array,
): Promise<void> {
  if (data.length <= CHUNK) {
    await char.writeValue(data);
    return;
  }
  for (let offset = 0; offset < data.length; offset += CHUNK) {
    const chunk = data.slice(offset, offset + CHUNK);
    await char.writeValue(chunk);
    await new Promise((r) => setTimeout(r, 50));
  }
}

function friendlyError(err: unknown): string {
  if (err instanceof Error) {
    if (err.name === "NotFoundError")
      return "No device selected. Make sure the macropad is powered on and in range.";
    if (err.name === "SecurityError")
      return "Bluetooth access was blocked by the browser.";
    if (err.name === "NetworkError")
      return "Connection to the device failed. Try again.";
    return err.message;
  }
  return "Something went wrong while talking to the device.";
}

const BLE_FILTERS: BluetoothLEScanFilter[] = [
  { name: DEVICE_NAME },
  { namePrefix: "ESP32-C3" },
  { services: [CONFIG_SERVICE] },
];

export interface UseDeviceReturn {
  connected: boolean;
  status: DeviceStatus | null;
  keymap: Keymap | null;
  connecting: boolean;
  saving: boolean;
  error: string | null;
  supported: boolean;
  connect: () => Promise<void>;
  disconnect: () => void;
  saveKeymap: (updated: Keymap) => Promise<boolean>;
}

export function useDevice(): UseDeviceReturn {
  const [connected, setConnected] = useState(false);
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [keymap, setKeymap] = useState<Keymap | null>(null);
  const [connecting, setConnecting] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [supported, setSupported] = useState(true);

  const deviceRef = useRef<BluetoothDevice | null>(null);
  const serverRef = useRef<BluetoothRemoteGATTServer | null>(null);
  const saveCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);

  useEffect(() => {
    setSupported(
      typeof navigator !== "undefined" && "bluetooth" in navigator,
    );
  }, []);

  const cleanup = useCallback(() => {
    setConnected(false);
    setStatus(null);
    setKeymap(null);
    serverRef.current = null;
    saveCharRef.current = null;
  }, []);

  const onDisconnected = useCallback(() => {
    cleanup();
  }, [cleanup]);

  const connect = useCallback(async () => {
    if (typeof navigator === "undefined" || !("bluetooth" in navigator)) {
      setSupported(false);
      setError(
        "Web Bluetooth is not supported in this browser. Please use Chrome or Edge.",
      );
      return;
    }
    setConnecting(true);
    setError(null);
    try {
      const device = await navigator.bluetooth.requestDevice({
        filters: BLE_FILTERS,
        optionalServices: [CONFIG_SERVICE],
      });
      deviceRef.current = device;
      device.addEventListener("gattserverdisconnected", onDisconnected);

      const connectAndFetch = async () => {
        console.log("[BLE] Connecting to GATT server...");
        const server = await device.gatt!.connect();
        serverRef.current = server;
        console.log("[BLE] Connected. Getting primary service...", CONFIG_SERVICE);
        const service = await server.getPrimaryService(CONFIG_SERVICE);

        console.log("[BLE] Got service. Getting STATUS char...");
        const statusChar = await service.getCharacteristic(STATUS_CHAR);
        console.log("[BLE] Reading STATUS char...");
        const statusVal = await statusChar.readValue();
        const statusStr = decode(statusVal);
        console.log("[BLE] STATUS string:", statusStr);
        const statusData = JSON.parse(statusStr) as DeviceStatus;

        console.log("[BLE] Getting KEYMAP char...");
        const keymapChar = await service.getCharacteristic(KEYMAP_CHAR);
        console.log("[BLE] Reading KEYMAP char...");
        const keymapVal = await keymapChar.readValue();
        console.log("[BLE] KEYMAP binary length:", keymapVal.byteLength);

        // Parse 384-byte binary keymap
        const parsedKeymap: Keymap = [[], []];
        let offset = 0;
        for (let l = 0; l < 2; l++) {
          for (let i = 0; i < 12; i++) {
            const mod = keymapVal.getUint8(offset);
            const keycode = keymapVal.getUint8(offset + 1);
            const action = keymapVal.getUint8(offset + 2);
            
            // Extract up to 12 bytes for the label, stopping at null terminator
            const labelBytes = new Uint8Array(keymapVal.buffer, keymapVal.byteOffset + offset + 4, 12);
            let nullIdx = labelBytes.indexOf(0);
            if (nullIdx === -1) nullIdx = 12;
            const label = new TextDecoder().decode(labelBytes.slice(0, nullIdx));

            parsedKeymap[l].push({ i, l, m: mod, k: keycode, a: action, n: label });
            offset += 16;
          }
        }

        console.log("[BLE] Getting SAVE char...");
        saveCharRef.current = await service.getCharacteristic(SAVE_CHAR);

        console.log("[BLE] All chars fetched successfully.");
        return { statusData, parsedKeymap };
      };

      // Wrap GATT operations in a 10-second timeout to prevent infinite hanging
      const { statusData, parsedKeymap: finalKeymap } = await Promise.race([
        connectAndFetch(),
        new Promise<never>((_, reject) =>
          setTimeout(
            () => reject(new Error("Connection timed out. The device didn't respond.")),
            10000
          )
        ),
      ]);

      setStatus(statusData);
      setKeymap(finalKeymap);
      setConnected(true);
    } catch (err) {
      deviceRef.current?.gatt?.disconnect();
      cleanup();
      setError(friendlyError(err));
    } finally {
      setConnecting(false);
    }
  }, [cleanup, onDisconnected]);

  const disconnect = useCallback(() => {
    const device = deviceRef.current;
    if (device) {
      device.removeEventListener("gattserverdisconnected", onDisconnected);
      if (device.gatt?.connected) device.gatt.disconnect();
    }
    deviceRef.current = null;
    cleanup();
    setError(null);
  }, [cleanup, onDisconnected]);

  const saveKeymap = useCallback(
    async (updated: Keymap): Promise<boolean> => {
      const char = saveCharRef.current;
      if (!char) {
        setError("Not connected to a device.");
        return false;
      }
      setSaving(true);
      setError(null);
      try {
        const buffer = new ArrayBuffer(384);
        const view = new DataView(buffer);
        const uint8 = new Uint8Array(buffer);
        let offset = 0;

        for (let l = 0; l < 2; l++) {
          for (let i = 0; i < 12; i++) {
            const key = updated[l][i];
            view.setUint8(offset, key.m);
            view.setUint8(offset + 1, key.k);
            view.setUint8(offset + 2, key.a);
            view.setUint8(offset + 3, 0); // reserved

            const labelBytes = new TextEncoder().encode(key.n);
            for (let j = 0; j < 12; j++) {
              uint8[offset + 4 + j] = j < labelBytes.length ? labelBytes[j] : 0;
            }
            offset += 16;
          }
        }

        await chunkedWrite(char, uint8);

        await new Promise((r) => setTimeout(r, 100));
        const ackVal = await char.readValue();
        const ack = JSON.parse(decode(ackVal)) as { ok: boolean; err?: string };
        if (!ack.ok) throw new Error(ack.err || "Device rejected the keymap.");

        setKeymap(updated);
        return true;
      } catch (err) {
        setError(friendlyError(err));
        return false;
      } finally {
        setSaving(false);
      }
    },
    [],
  );

  return {
    connected,
    status,
    keymap,
    connecting,
    saving,
    error,
    supported,
    connect,
    disconnect,
    saveKeymap,
  };
}