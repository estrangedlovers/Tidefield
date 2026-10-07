import { useEffect, useRef, useState } from "react";
import { store } from "../state/store";
import "./Keyboard.css";

const BLACK = new Set([1, 3, 6, 8, 10]);

/** Bloom keyboard. Mouse down plays, gliding across keys plays the keys you pass,
 *  and keys glow with the voices that are actually sounding (from telemetry). */
export function Keyboard({ low = 48, octaves = 3 }: { low?: number; octaves?: number }) {
  const [down, setDown] = useState<number | null>(null);
  const keysRef = useRef<Map<number, HTMLDivElement>>(new Map());
  const notes = Array.from({ length: octaves * 12 + 1 }, (_, k) => low + k);
  const whites = notes.filter((n) => !BLACK.has(n % 12));

  useEffect(() => {
    let raf = 0;
    const draw = () => {
      const voices = store.telemetry?.bloom.voices ?? [];
      const glow = new Map<number, number>();
      for (const [active, note, level] of voices)
        if (active) glow.set(Math.round(note), Math.max(glow.get(Math.round(note)) ?? 0, level));
      keysRef.current.forEach((el, n) => {
        const g = Math.min(1, (glow.get(n) ?? 0) * 3);
        el.style.setProperty("--glow", g.toFixed(3));
      });
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, []);

  const press = (n: number, e: React.PointerEvent) => {
    const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const velocity = Math.min(1, Math.max(0.25, (e.clientY - rect.top) / rect.height + 0.2));
    void store.call("noteOn", n, velocity);
    setDown(n);
  };
  const release = () => {
    if (down !== null) void store.call("noteOff", down);
    setDown(null);
  };

  const keyProps = (n: number) => ({
    ref: (el: HTMLDivElement | null) => {
      if (el) keysRef.current.set(n, el);
      else keysRef.current.delete(n);
    },
    className: `kb-key ${BLACK.has(n % 12) ? "black" : "white"}${down === n ? " down" : ""}`,
    onPointerDown: (e: React.PointerEvent) => {
      (e.currentTarget as Element).releasePointerCapture?.(e.pointerId);
      press(n, e);
    },
    onPointerEnter: (e: React.PointerEvent) => {
      if (down !== null && e.buttons === 1 && down !== n) {
        void store.call("noteOff", down);
        press(n, e);
      }
    },
  });

  return (
    <div className="kb" onPointerUp={release} onPointerLeave={release}>
      <div className="kb-whites">
        {whites.map((n) => (
          <div key={n} {...keyProps(n)}>
            {n % 12 === 0 && <span className="kb-c">C{Math.floor(n / 12) - 1}</span>}
          </div>
        ))}
      </div>
      <div className="kb-blacks">
        {notes.map((n) => {
          if (!BLACK.has(n % 12)) return null;
          const whiteIndex = whites.filter((w) => w < n).length;
          return <div key={n} {...keyProps(n)} style={{ left: `calc(${(whiteIndex / whites.length) * 100}% - 1.15%)` }} />;
        })}
      </div>
    </div>
  );
}
