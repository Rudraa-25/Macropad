import { Loader2, RotateCcw, Save } from "lucide-react";
import { Button } from "@/components/ui/button";

interface SaveBarProps {
  dirtyCount: number;
  saving: boolean;
  onDiscard: () => void;
  onSave: () => void;
}

export function SaveBar({ dirtyCount, saving, onDiscard, onSave }: SaveBarProps) {
  if (dirtyCount === 0) return null;
  return (
    <div className="fixed inset-x-0 bottom-0 z-40 animate-in slide-in-from-bottom-4 border-t border-dirty/40 bg-card/95 px-4 py-3 backdrop-blur duration-200">
      <div className="mx-auto flex max-w-lg items-center justify-between gap-3">
        <span className="text-sm font-medium text-muted-foreground">
          <span className="font-semibold text-dirty">{dirtyCount}</span> unsaved
          {dirtyCount === 1 ? " change" : " changes"}
        </span>
        <div className="flex gap-2">
          <Button variant="secondary" onClick={onDiscard} disabled={saving}>
            <RotateCcw className="h-4 w-4" /> Discard All
          </Button>
          <Button onClick={onSave} disabled={saving}>
            {saving ? (
              <Loader2 className="h-4 w-4 animate-spin" />
            ) : (
              <Save className="h-4 w-4" />
            )}
            Save to Device
          </Button>
        </div>
      </div>
    </div>
  );
}