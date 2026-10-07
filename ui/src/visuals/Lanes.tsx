import { useEffect, useRef } from "react";
import { store } from "../state/store";

/** Pitch lane: notes as horizontal position, level as brightness/height. Used for the
 *  drone voices and the resonator's modes. */
export function PitchLane({ source }: { source: "drone" | "modes" }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const c = canvas.current!;
    const ctx = c.getContext("2d")!;
    let raf = 0;
    const shown: number[] = [];
    const draw = () => {
      const dpr = window.devicePixelRatio || 1;
      const w = c.clientWidth;
      const h = c.clientHeight;
      if (c.width !== Math.round(w * dpr)) {
        c.width = Math.round(w * dpr);
        c.height = Math.round(h * dpr);
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);
      const lo = 24;
      const hi = 108;
      ctx.fillStyle = "rgba(255,255,255,0.05)";
      for (let n = lo; n <= hi; n += 12) ctx.fillRect(((n - lo) / (hi - lo)) * w, 0, 1, h);
      ctx.fillStyle = "rgba(151,161,175,0.6)";
      ctx.font = "10px Inter Variable, system-ui";
      for (let n = lo; n <= hi; n += 12) ctx.fillText(`C${n / 12 - 1}`, ((n - lo) / (hi - lo)) * w + 3, h - 4);

      const items = source === "drone" ? store.telemetry?.drone ?? [] : store.telemetry?.modes ?? [];
      items.forEach(([level, note], k) => {
        shown[k] = (shown[k] ?? 0) + (level - (shown[k] ?? 0)) * 0.15;
        const x = ((note - lo) / (hi - lo)) * w;
        const l = Math.min(1, shown[k] * (source === "drone" ? 1.2 : 4));
        const bar = (h - 18) * l;
        ctx.fillStyle = `rgba(142, 201, 198, ${(0.15 + 0.75 * l).toFixed(3)})`;
        ctx.fillRect(x - 1.5, h - 16 - bar, 3, bar);
        ctx.beginPath();
        ctx.arc(x, h - 16 - bar, 2.5 + 2 * l, 0, Math.PI * 2);
        ctx.fill();
      });
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [source]);
  return <canvas ref={canvas} style={{ display: "block", width: "100%", height: 110, borderRadius: 10, background: "var(--surface)", border: "1px solid var(--line)" }} />;
}
