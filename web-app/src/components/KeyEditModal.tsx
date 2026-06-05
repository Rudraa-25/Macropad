import { useEffect, useState } from "react";
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetTitle,
} from "@/components/ui/sheet";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { ACTION_NORMAL, ACTIONS, HID_KEYS, MODIFIERS, autoLabel } from "@/constants";
import type { KeyDef } from "@/types";

interface KeyEditModalProps {
  keyDef: KeyDef | null;
  onApply: (updated: KeyDef) => void;
  onClose: () => void;
}

const ACTION_ENTRIES = Object.entries(ACTIONS); // [label, value]
const MOD_ENTRIES = Object.entries(MODIFIERS);
const KEY_ENTRIES = Object.entries(HID_KEYS);

export function KeyEditModal({ keyDef, onApply, onClose }: KeyEditModalProps) {
  const [draft, setDraft] = useState<KeyDef | null>(keyDef);
  // Tracks whether the label was manually edited so we stop auto-populating it.
  const [labelTouched, setLabelTouched] = useState(false);

  useEffect(() => {
    setDraft(keyDef);
    setLabelTouched(false);
  }, [keyDef]);

  if (!draft) return null;

  const update = (patch: Partial<KeyDef>) =>
    setDraft((d) => (d ? { ...d, ...patch } : d));

  const onActionChange = (value: string) => {
    const a = Number(value);
    if (a === ACTION_NORMAL) {
      update({ a });
    } else {
      const label = ACTION_ENTRIES.find(([, v]) => v === a)?.[0] ?? "";
      update({ a, m: 0, k: 0, n: label.slice(0, 11) });
    }
  };

  const onKeyChange = (value: string) => {
    const k = Number(value);
    const next: Partial<KeyDef> = { k };
    if (!labelTouched) next.n = autoLabel(draft.m, k);
    update(next);
  };

  const onModChange = (value: string) => {
    const m = Number(value);
    const next: Partial<KeyDef> = { m };
    if (!labelTouched) next.n = autoLabel(m, draft.k);
    update(next);
  };

  const isNormal = draft.a === ACTION_NORMAL;

  return (
    <Sheet open={!!keyDef} onOpenChange={(o) => !o && onClose()}>
      <SheetContent
        side="bottom"
        className="mx-auto max-w-lg rounded-t-3xl border-border"
      >
        <SheetHeader className="text-left">
          <SheetTitle className="font-display">
            Edit K{draft.i}
            <span className="ml-1 text-muted-foreground">· Layer {draft.l}</span>
          </SheetTitle>
          <SheetDescription>
            Configure what this key does. Changes apply locally until you save.
          </SheetDescription>
        </SheetHeader>

        <div className="space-y-4 px-4 py-2">
          <div className="space-y-1.5">
            <Label>Action Type</Label>
            <Select value={String(draft.a)} onValueChange={onActionChange}>
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {ACTION_ENTRIES.map(([label, value]) => (
                  <SelectItem key={value} value={String(value)}>
                    {label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          {isNormal && (
            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1.5">
                <Label>Modifier</Label>
                <Select value={String(draft.m)} onValueChange={onModChange}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {MOD_ENTRIES.map(([label, value]) => (
                      <SelectItem key={label} value={String(value)}>
                        {label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1.5">
                <Label>Key</Label>
                <Select value={String(draft.k)} onValueChange={onKeyChange}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent className="max-h-64">
                    {KEY_ENTRIES.map(([label, value]) => (
                      <SelectItem key={label} value={String(value)}>
                        {label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            </div>
          )}

          <div className="space-y-1.5">
            <Label>Label</Label>
            <Input
              value={draft.n}
              maxLength={11}
              onChange={(e) => {
                setLabelTouched(true);
                update({ n: e.target.value.slice(0, 11) });
              }}
              placeholder="Display label"
              className="font-mono"
            />
            <p className="text-right text-[10px] text-muted-foreground">
              {draft.n.length}/11
            </p>
          </div>
        </div>

        <SheetFooter className="flex-row gap-2">
          <Button variant="ghost" className="flex-1" onClick={onClose}>
            Cancel
          </Button>
          <Button className="flex-1" onClick={() => onApply(draft)}>
            Apply
          </Button>
        </SheetFooter>
      </SheetContent>
    </Sheet>
  );
}