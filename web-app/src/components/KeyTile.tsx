import {
  ACTION_NORMAL,
  ACTION_SHORT,
  KEYCODE_LABELS,
  modifierLabel,
} from "@/constants";
import type { KeyDef } from "@/types";
import { cn } from "@/lib/utils";

interface KeyTileProps {
  keyDef: KeyDef;
  dirty: boolean;
  onClick: () => void;
}

export function KeyTile({ keyDef, dirty, onClick }: KeyTileProps) {
  const isSpecial = keyDef.a !== ACTION_NORMAL;
  const badge = isSpecial ? ACTION_SHORT[keyDef.a] : modifierLabel(keyDef.m);
  const fallback = KEYCODE_LABELS[keyDef.k] ?? "—";
  const display = keyDef.n?.trim() || (isSpecial ? badge : fallback);

  return (
    <button
      onClick={onClick}
      className={cn(
        "relative flex aspect-square flex-col items-center justify-center gap-1 rounded-2xl border p-1.5 text-center transition-all duration-150 active:scale-95",
        isSpecial
          ? "border-special/40 bg-special/10 hover:border-special/70"
          : "border-border bg-card/70 hover:border-primary/60",
        dirty &&
          "border-dirty ring-2 ring-dirty/70 ring-offset-2 ring-offset-background",
      )}
    >
      <span className="absolute left-2 top-1.5 font-mono text-[10px] font-semibold text-muted-foreground">
        K{keyDef.i}
      </span>
      {dirty && (
        <span className="absolute right-2 top-2 h-1.5 w-1.5 rounded-full bg-dirty" />
      )}
      <span
        className={cn(
          "mt-2 break-all font-mono text-sm font-bold leading-tight",
          isSpecial ? "text-special" : "text-foreground",
        )}
      >
        {display}
      </span>
      {badge && (
        <span
          className={cn(
            "rounded-md px-1.5 py-0.5 text-[9px] font-semibold uppercase tracking-wide",
            isSpecial
              ? "bg-special/20 text-special"
              : "bg-primary/15 text-primary",
          )}
        >
          {badge}
        </span>
      )}
    </button>
  );
}