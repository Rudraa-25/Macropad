import { AlertTriangle } from "lucide-react";

export function BrowserWarning() {
  return (
    <div className="flex items-start gap-3 rounded-2xl border border-destructive/40 bg-destructive/10 p-4 text-sm">
      <AlertTriangle className="mt-0.5 h-5 w-5 shrink-0 text-destructive" />
      <p className="text-foreground">
        Web Bluetooth is not supported in this browser. Please use{" "}
        <span className="font-semibold">Chrome</span> or{" "}
        <span className="font-semibold">Edge</span> on desktop or Android.
      </p>
    </div>
  );
}