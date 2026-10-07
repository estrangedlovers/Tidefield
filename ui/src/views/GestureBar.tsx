import { useEffect } from "react";
import { store, useKey, useParam } from "../state/store";
import "./GestureBar.css";

const idx = (id: string) => store.index(id);
const LOOPER_TITLES = ["Loop", "Recording", "Looping", "Overdubbing", "Clearing"];
const LOOPER_HINTS = ["tap to record", "tap to close", "tap to overdub", "tap to stop", ""];

function useToggle(id: string) {
  const i = idx(id);
  const { value } = useParam(i);
  return [value > 0.5, () => store.setParam(i, value > 0.5 ? 0 : 1)] as const;
}

/** A hold gesture: pressed while the pointer (or the key) is down. */
function Hold({ id, title, sub, level, hint, tone = "accent" }: { id: string; title: string; sub: string; level: number; hint: string; tone?: string }) {
  const i = idx(id);
  const up = () => store.setParam(i, 0);
  return (
    <button
      className={`gesture hold tone-${tone}`}
      onPointerDown={(e) => {
        (e.target as HTMLElement).setPointerCapture(e.pointerId);
        store.setParam(i, 1);
      }}
      onPointerUp={up}
      onPointerCancel={up}
      title={hint}
    >
      <span className="gesture-fill" style={{ transform: `scaleY(${level})` }} />
      <span className="gesture-title">{title}</span>
      <span className="gesture-sub">{sub}</span>
    </button>
  );
}

/** Two macros on one pad: left-right darkens or brightens everything, down-up pulls
 *  it close and dry or pushes it far into the reverb. Double-click recentres. */
function ShapePad() {
  const ci = idx("perform.colour");
  const si = idx("perform.space");
  const c = useParam(ci).value;
  const s = useParam(si).value;
  const set = (e: React.PointerEvent<HTMLDivElement>) => {
    const r = e.currentTarget.getBoundingClientRect();
    const x = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
    const y = Math.min(1, Math.max(0, (e.clientY - r.top) / r.height));
    store.setParam(ci, x * 2 - 1);
    store.setParam(si, 1 - y * 2);
  };
  return (
    <div
      className="gesture shape-pad"
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        store.touching.add(ci).add(si);
        set(e);
      }}
      onPointerMove={(e) => e.buttons && set(e)}
      onPointerUp={() => (store.touching.delete(ci), store.touching.delete(si))}
      onDoubleClick={() => (store.setParam(ci, 0), store.setParam(si, 0))}
      title="Shape: left dark, right bright; down close and dry, up far and wet. Double-click to recentre."
    >
      <span className="pad-axis pad-x">colour</span>
      <span className="pad-axis pad-y">space</span>
      <span className="pad-cross" />
      <span className="pad-dot" style={{ left: `${(c + 1) * 50}%`, top: `${(1 - s) * 50}%` }} />
    </div>
  );
}

function Toggle({ id, title, sub, level, hint }: { id: string; title: string; sub: string; level: number; hint: string }) {
  const [on, toggle] = useToggle(id);
  return (
    <button className={`gesture toggle${on ? " on" : ""}`} onClick={toggle} title={hint}>
      <span className="gesture-glow" style={{ opacity: level }} />
      <span className="gesture-title">{title}</span>
      <span className="gesture-sub">{sub}</span>
    </button>
  );
}

function Looper() {
  const [state, pos, seconds, passes] = store.telemetry?.perf.looper ?? [0, 0, 0, 0];
  const r = 15;
  const c = 2 * Math.PI * r;
  const detail =
    state === 0 ? (store.value("loop.source") > 0.5 ? "from the mix" : "from the input") : state === 1 ? `${seconds.toFixed(1)} s` : `${seconds.toFixed(1)} s, pass ${passes}`;
  return (
    <div className={`gesture looper state-${state}`}>
      <button className="looper-main" onClick={() => store.command("loopRecord")} title="L: record, close the loop, overdub. Every pass wears the tape a little more.">
        <svg width="38" height="38" viewBox="0 0 38 38" className="looper-ring">
          <circle cx="19" cy="19" r={r} className="ring-track" />
          <circle cx="19" cy="19" r={r} className="ring-fill" strokeDasharray={`${c * pos} ${c}`} transform="rotate(-90 19 19)" />
          <circle cx="19" cy="19" r="5" className="ring-dot" />
        </svg>
        <span className="looper-text">
          <span className="gesture-title">{LOOPER_TITLES[state]}</span>
          <span className="gesture-sub">{detail}</span>
          <span className="gesture-sub hint">{LOOPER_HINTS[state]}</span>
        </span>
      </button>
      <div className="looper-side">
        {state !== 0 && (
          <button className="looper-clear" onClick={() => store.command("loopClear")} title="Shift+L: fade the loop out and clear it">
            Clear
          </button>
        )}
      </div>
    </div>
  );
}

function Loops() {
  const [on, toggle] = useToggle("loops.on");
  const [phases, , flashes] = store.telemetry?.perf.loops ?? [[], [], []];
  const count = Math.round(store.value("loops.count"));
  return (
    <button className={`gesture loops${on ? " on" : ""}`} onClick={toggle} title="E: incommensurate loops. Notes on long, never-aligning cycles, in the key.">
      <span className="gesture-title">Loops</span>
      <span className="loop-dots">
        {Array.from({ length: count }, (_, k) => (
          <span
            key={k}
            className="loop-dot"
            style={{ ["--p" as string]: (phases[k] ?? 0).toFixed(3), ["--f" as string]: (flashes[k] ?? 0).toFixed(3) }}
          />
        ))}
      </span>
    </button>
  );
}

/** Keyboard gestures: S, H, T (hold) swell, hush, slow; F freeze all, I hold input, L loop, Shift+L
 *  clear loop, E loops. Ignored while typing. */
function useGestureKeys() {
  useEffect(() => {
    const typing = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      return t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA" || e.metaKey || e.ctrlKey;
    };
    const flip = (id: string) => store.setParamById(id, store.value(id) > 0.5 ? 0 : 1);
    const down = (e: KeyboardEvent) => {
      if (typing(e) || e.repeat) return;
      const k = e.key.toLowerCase();
      if (k === "s") store.setParamById("swell.hold", 1);
      else if (k === "h") store.setParamById("hush.hold", 1);
      else if (k === "t") store.setParamById("slow.hold", 1);
      else if (k === "f") flip("freeze.on");
      else if (k === "i") flip("input.freeze");
      else if (k === "e") flip("loops.on");
      else if (k === "l") store.command(e.shiftKey ? "loopClear" : "loopRecord");
      else return;
      e.preventDefault();
    };
    const up = (e: KeyboardEvent) => {
      const k = e.key.toLowerCase();
      if (k === "s") store.setParamById("swell.hold", 0);
      else if (k === "h") store.setParamById("hush.hold", 0);
      else if (k === "t") store.setParamById("slow.hold", 0);
    };
    // A key held while the window loses focus never sends its keyup: let go of every
    // hold so a gesture cannot stay stuck on.
    const releaseAll = () => {
      for (const id of ["swell.hold", "hush.hold", "slow.hold"]) if (store.value(id) > 0.5) store.setParamById(id, 0);
    };
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    window.addEventListener("blur", releaseAll);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
      window.removeEventListener("blur", releaseAll);
    };
  }, []);
}

export function GestureBar() {
  useKey("telemetry");
  useGestureKeys();
  const perf = store.telemetry?.perf;
  return (
    <div className="gestures">
      <Hold id="swell.hold" title="Swell" sub="hold S" level={perf?.swell ?? 0} hint="Hold (or hold S): sends bloom and filters open. Release: it ebbs back over the Ebb time." />
      <Hold id="hush.hold" title="Hush" sub="hold H" level={perf?.hush ?? 0} tone="sand" hint="Hold (or hold H): every source sinks while the reverb and delay ring on. Release: it comes back." />
      <Hold id="slow.hold" title="Slow" sub="hold T" level={perf?.slow ?? 0} tone="sand" hint="Hold (or hold T): time slows to a quarter, every drift and cycle with it." />
      <ShapePad />
      <Toggle id="freeze.on" title="Freeze all" sub="the moment, F" level={perf?.freeze ?? 0} hint="F: hold the last two seconds as a granular cloud while the rest steps back" />
      <Looper />
      <Toggle id="input.freeze" title="Hold input" sub="spectral, I" level={store.telemetry?.input[2] ?? 0} hint="I: hold the live input's sound forever as a spectral pad" />
      <Loops />
    </div>
  );
}
