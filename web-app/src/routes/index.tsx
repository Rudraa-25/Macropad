import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { toast } from "sonner";
import { ConnectScreen } from "@/components/ConnectScreen";
import { ConnectedLayout } from "@/components/ConnectedLayout";
import { KeyEditModal } from "@/components/KeyEditModal";
import { useDevice } from "@/hooks/useDevice";
import type { KeyDef, Keymap } from "@/types";

export const Route = createFileRoute("/")({
  head: () => ({
    meta: [
      { title: "Macropad Configurator — ESP32-C3 BLE Macropad" },
      {
        name: "description",
        content:
          "Visually configure your ESP32-C3 BLE macropad over local WiFi. Edit keys, modifiers, layers and actions, then save to the device.",
      },
      { property: "og:title", content: "Macropad Configurator" },
      {
        property: "og:description",
        content:
          "Configure your ESP32-C3 BLE macropad over local WiFi — edit keys, layers and actions.",
      },
    ],
  }),
  component: Index,
});

function cloneKeymap(km: Keymap): Keymap {
  return km.map((layer) => layer.map((k) => ({ ...k })));
}

function Index() {
  const device = useDevice();
  const [working, setWorking] = useState<Keymap | null>(null);
  const [activeLayer, setActiveLayer] = useState(0);
  const [editingIdx, setEditingIdx] = useState<number | null>(null);

  // The device hook's keymap is the last fetched/saved source of truth.
  const original = device.keymap;
  const prevConnected = useRef(false);

  // Seed the working copy whenever a fresh keymap arrives (connect / save).
  useEffect(() => {
    if (device.keymap) {
      setWorking(cloneKeymap(device.keymap));
      setActiveLayer((l) => (l < device.keymap!.length ? l : 0));
    } else {
      setWorking(null);
      setEditingIdx(null);
    }
  }, [device.keymap]);

  // Connection toasts.
  useEffect(() => {
    if (device.connected && !prevConnected.current) {
      toast.success(`Connected to ${device.status?.device ?? "device"}`);
    } else if (!device.connected && prevConnected.current) {
      toast("Disconnected");
    }
    prevConnected.current = device.connected;
  }, [device.connected, device.status]);

  // Surface device errors as toasts (but not on disconnect — handled by connection effect).
  useEffect(() => {
    if (device.error) {
      if (device.error === "Device disconnected.") return;
      toast.error(device.error);
    }
  }, [device.error]);

  const handleDisconnect = useCallback(() => {
    device.disconnect();
    setEditingIdx(null);
  }, [device]);

  // Per-layer set of dirty key indices for the active layer.
  const dirtyByLayer = useMemo(() => {
    if (!original || !working) return [] as Set<number>[];
    return working.map((layer, li) => {
      const dirty = new Set<number>();
      layer.forEach((k, ki) => {
        const o = original[li]?.[ki];
        if (!o || JSON.stringify(o) !== JSON.stringify(k)) dirty.add(k.i);
      });
      return dirty;
    });
  }, [original, working]);

  const dirtyCount = useMemo(
    () => dirtyByLayer.reduce((sum, s) => sum + s.size, 0),
    [dirtyByLayer],
  );

  const editingKey: KeyDef | null = useMemo(() => {
    if (editingIdx === null || !working) return null;
    return working[activeLayer]?.find((k) => k.i === editingIdx) ?? null;
  }, [editingIdx, working, activeLayer]);

  const applyKey = useCallback(
    (updated: KeyDef) => {
      setWorking((prev) => {
        if (!prev) return prev;
        const next = cloneKeymap(prev);
        const layer = next[updated.l];
        const i = layer.findIndex((k) => k.i === updated.i);
        if (i >= 0) layer[i] = { ...updated };
        return next;
      });
      setEditingIdx(null);
    },
    [],
  );

  const handleDiscard = useCallback(() => {
    if (original) setWorking(cloneKeymap(original));
    toast("Changes discarded");
  }, [original]);

  const handleSave = useCallback(async () => {
    if (!working) return;
    const t = toast.loading("Saving to device…");
    const ok = await device.saveKeymap(working);
    if (ok) toast.success("Keymap saved successfully", { id: t });
    else toast.error("Save failed — try again", { id: t });
  }, [device, working]);

  const isConnected = device.connected && working;

  return (
    <main className="min-h-screen bg-background">
      <div className="mx-auto max-w-lg space-y-4 px-4 pb-28 pt-5">
        {!isConnected ? (
          <ConnectScreen
            connecting={device.connecting}
            supported={device.supported}
            onConnect={device.connect}
          />
        ) : (
          working && (
            <ConnectedLayout
              status={device.status}
              keymap={working}
              activeLayer={activeLayer}
              onLayerChange={setActiveLayer}
              dirtyByLayer={dirtyByLayer}
              dirtyCount={dirtyCount}
              saving={device.saving}
              onKeyClick={setEditingIdx}
              onDisconnect={handleDisconnect}
              onDiscard={handleDiscard}
              onSave={handleSave}
            />
          )
        )}
      </div>

      <KeyEditModal
        keyDef={editingKey}
        onApply={applyKey}
        onClose={() => setEditingIdx(null)}
      />
    </main>
  );
}
