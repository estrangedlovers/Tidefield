import { useEffect, useRef } from "react";
import { store } from "../state/store";
import "./Meter.css";

/** Peak meter driven straight from telemetry in an animation frame: no React renders.
 *  `source` picks the master or a mixer strip. Falls back slowly, like a VU. */
export function Meter({ strip, orientation = "horizontal" }: { strip?: number; orientation?: "horizontal" | "vertical" }) {
  const left = useRef<HTMLDivElement>(null);
  const right = useRef<HTMLDivElement>(null);

  useEffect(() => {
    let raf = 0;
    const held = [0, 0];
    const draw = () => {
      const t = store.telemetry;
      if (t) {
        const peaks = strip === undefined ? [t.meter[0], t.meter[1]] : t.strips[strip] ?? [0, 0];
        for (let c = 0; c < 2; c++) {
          const db = 20 * Math.log10(Math.max(1e-6, peaks[c]));
          const n = Math.max(0, Math.min(1, (db + 60) / 60));
          held[c] = Math.max(n, held[c] - 0.012);
          const el = c === 0 ? left.current : right.current;
          if (el) {
            el.style.transform = orientation === "horizontal" ? `scaleX(${held[c]})` : `scaleY(${held[c]})`;
            el.classList.toggle("hot", db > -3);
          }
        }
      }
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [strip, orientation]);

  return (
    <div className={`meter meter-${orientation}`}>
      <div className="meter-lane">
        <div ref={left} className="meter-bar" />
      </div>
      <div className="meter-lane">
        <div ref={right} className="meter-bar" />
      </div>
    </div>
  );
}
