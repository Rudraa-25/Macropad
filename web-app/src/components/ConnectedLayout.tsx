import { Bluetooth, Unplug } from "lucide-react";
import { Button } from "@/components/ui/button";
import { LayerTabs } from "./LayerTabs";
import { KeyGrid } from "./KeyGrid";
import { SaveBar } from "./SaveBar";
import type { DeviceStatus, Keymap } from "@/types";

interface ConnectedLayoutProps {
  status: DeviceStatus | null;
  keymap: Keymap;
  activeLayer: number;
  onLayerChange: (layer: number) => void;
  dirtyByLayer: Set<number>[];
  dirtyCount: number;
  saving: boolean;
  onKeyClick: (idx: number) => void;
  onDisconnect: () => void;
  onDiscard: () => void;
  onSave: () => void;
}

export function ConnectedLayout({
  status,
  keymap,
  activeLayer,
  onLayerChange,
  dirtyByLayer,
  dirtyCount,
  saving,
  onKeyClick,
  onDisconnect,
  onDiscard,
  onSave,
}: ConnectedLayoutProps) {
  return (
    <div className="space-y-4">
      <header className="flex items-center justify-between gap-3 rounded-2xl border border-border bg-card/60 p-3 backdrop-blur">
        <div className="flex items-center gap-2">
          <span className="flex h-9 w-9 items-center justify-center rounded-xl bg-primary/15 text-primary">
            <Bluetooth className="h-5 w-5" />
          </span>
          <div className="leading-tight">
            <h1 className="text-sm font-bold tracking-tight sm:text-base">
              Macropad Configurator
            </h1>
            {status && (
              <span className="flex items-center gap-1.5 text-xs font-medium text-emerald-400">
                <span className="h-2 w-2 rounded-full bg-emerald-400" />
                {status.device}
                <span className="font-mono text-muted-foreground">
                  {status.fw}
                </span>
              </span>
            )}
          </div>
        </div>
        <Button variant="secondary" size="sm" onClick={onDisconnect}>
          <Unplug className="h-4 w-4" /> Disconnect
        </Button>
      </header>

      <LayerTabs
        layers={keymap.length}
        active={activeLayer}
        onChange={onLayerChange}
      />

      <KeyGrid
        keys={keymap[activeLayer] ?? []}
        dirtyIdx={dirtyByLayer[activeLayer] ?? new Set()}
        onKeyClick={onKeyClick}
      />

      <SaveBar
        dirtyCount={dirtyCount}
        saving={saving}
        onDiscard={onDiscard}
        onSave={onSave}
      />
    </div>
  );
}