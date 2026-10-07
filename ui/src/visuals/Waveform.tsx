import { useEffect, useRef, useState } from "react";
import { store, useSlice } from "../state/store";
import "./Waveform.css";

/** A sample's outline with the cloud's live grains drawn over it (or Bloom's voices).
 *  Peaks are requested once per loaded sample; grains come from telemetry. */
export function Waveform({ slot, positionParam }: { slot: number; positionParam?: number }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const { samples } = useSlice("samples");
  const name = slot === 4 ? samples.bloom : samples.clouds[slot];
  const [peaks, setPeaks] = useState<number[]>([]);

  useEffect(() => {
    let cancelled = false;
    if (!name) {
      setPeaks([]);
      return;
    }
    void store.call("sample.peaks", slot, 400).then((p) => !cancelled && setPeaks(Array.isArray(p) ? (p as number[]) : []));
    return () => {
      cancelled = true;
    };
  }, [name, slot]);

  useEffect(() => {
    const c = canvas.current!;
    const ctx = c.getContext("2d")!;
    let raf = 0;
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
      const mid = h / 2;
      if (peaks.length) {
        ctx.fillStyle = "rgba(151, 161, 175, 0.28)";
        const bw = w / peaks.length;
        peaks.forEach((p, k) => {
          const ph = Math.max(1, p * (h * 0.46));
          ctx.fillRect(k * bw, mid - ph, Math.max(1, bw - 0.6), ph * 2);
        });
      } else {
        ctx.fillStyle = "rgba(151, 161, 175, 0.5)";
        ctx.font = "12px Inter Variable, system-ui";
        ctx.textAlign = "center";
        ctx.fillText(slot === 4 ? "No one-shot loaded" : "Empty: load a sample or Catch into this cloud", w / 2, mid + 4);
      }

      const t = store.telemetry;
      if (positionParam !== undefined && peaks.length) {
        const x = store.params[positionParam] * w;
        ctx.fillStyle = "rgba(142, 201, 198, 0.5)";
        ctx.fillRect(x, 0, 1, h);
      }
      if (t && slot < 4) {
        for (const [pos, amp, pan] of t.clouds[slot]?.grains ?? []) {
          ctx.fillStyle = `rgba(142, 201, 198, ${(0.25 + amp * 0.7).toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(pos * w, mid - pan * h * 0.32, 2 + amp * 3, 0, Math.PI * 2);
          ctx.fill();
        }
      }
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [peaks, slot, positionParam]);

  return <canvas ref={canvas} className="waveform" />;
}
