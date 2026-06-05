import { KeyTile } from "./KeyTile";
import type { KeyDef } from "@/types";

interface KeyGridProps {
  keys: KeyDef[];
  dirtyIdx: Set<number>;
  onKeyClick: (idx: number) => void;
}

export function KeyGrid({ keys, dirtyIdx, onKeyClick }: KeyGridProps) {
  return (
    <div className="rounded-3xl border border-border bg-card/40 p-3">
      <div className="grid grid-cols-3 gap-2.5">
        {keys.map((k) => (
          <KeyTile
            key={k.i}
            keyDef={k}
            dirty={dirtyIdx.has(k.i)}
            onClick={() => onKeyClick(k.i)}
          />
        ))}
      </div>
    </div>
  );
}