import { Bluetooth, Loader2, Power, Radio, MousePointerClick } from "lucide-react";
import { Button } from "@/components/ui/button";
import { BrowserWarning } from "./BrowserWarning";
import { cn } from "@/lib/utils";

interface ConnectScreenProps {
  connecting: boolean;
  supported: boolean;
  onConnect: () => void;
}

const STEPS = [
  {
    icon: Power,
    title: "Power on the macropad",
    body: "Make sure the device is switched on and within Bluetooth range.",
  },
  {
    icon: MousePointerClick,
    title: "Click Connect Device",
    body: "Press the button below to open the browser's Bluetooth picker.",
  },
  {
    icon: Radio,
    title: "Select your device",
    body: 'In the dialog, choose "ESP32-C3 Macropad" to start configuring.',
  },
];

export function ConnectScreen({
  connecting,
  supported,
  onConnect,
}: ConnectScreenProps) {
  return (
    <div className="flex min-h-[80vh] flex-col items-center justify-center">
      <div className="w-full max-w-md space-y-5 rounded-3xl border border-border bg-card/40 p-7 text-center">
        <div className="flex flex-col items-center gap-3">
          <span
            className={cn(
              "flex h-16 w-16 items-center justify-center rounded-2xl bg-primary/15 text-primary",
              connecting && "animate-pulse",
            )}
          >
            <Bluetooth className="h-8 w-8" />
          </span>
          <div>
            <h1 className="font-display text-2xl font-bold tracking-tight">
              Macropad Configurator
            </h1>
            <p className="mt-1 text-sm text-muted-foreground">
              Connect over Bluetooth to remap your keys.
            </p>
          </div>
        </div>

        {!supported && <BrowserWarning />}

        <ol className="space-y-3 text-left">
          {STEPS.map((step, i) => (
            <li key={i} className="flex gap-3">
              <span className="flex h-9 w-9 shrink-0 items-center justify-center rounded-xl bg-primary/15 text-primary">
                <step.icon className="h-4 w-4" />
              </span>
              <div>
                <p className="text-sm font-semibold">{step.title}</p>
                <p className="mt-0.5 text-sm text-muted-foreground">
                  {step.body}
                </p>
              </div>
            </li>
          ))}
        </ol>

        <Button
          size="lg"
          className={cn("w-full", connecting && "animate-pulse")}
          disabled={connecting || !supported}
          onClick={onConnect}
        >
          {connecting ? (
            <Loader2 className="h-5 w-5 animate-spin" />
          ) : (
            <Bluetooth className="h-5 w-5" />
          )}
          {connecting ? "Connecting…" : "Connect Device"}
        </Button>

        <p className="text-xs text-muted-foreground">
          Requires Chrome or Edge on desktop/Android. iOS is not supported.
        </p>
      </div>
    </div>
  );
}