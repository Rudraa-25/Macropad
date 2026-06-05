import { LAYER_NAMES } from "@/constants";
import { cn } from "@/lib/utils";

interface LayerTabsProps {
  layers: number;
  active: number;
  onChange: (layer: number) => void;
}

export function LayerTabs({ layers, active, onChange }: LayerTabsProps) {
  return (
    <div className="grid grid-cols-2 gap-1 rounded-2xl border border-border bg-card/60 p-1">
      {Array.from({ length: layers }).map((_, i) => (
        <button
          key={i}
          onClick={() => onChange(i)}
          className={cn(
            "rounded-xl px-3 py-2.5 text-sm font-semibold transition-colors",
            active === i
              ? "bg-primary text-primary-foreground shadow-lg shadow-primary/20"
              : "text-muted-foreground hover:bg-accent/40 hover:text-foreground",
          )}
        >
          <span className="hidden sm:inline">Layer {i} · </span>
          <span className="sm:hidden">L{i} · </span>
          {LAYER_NAMES[i] ?? ""}
        </button>
      ))}
    </div>
  );
}