import { useEffect } from "react";
import { store, useKey, useParam } from "../state/store";
import { Knob } from "../components/Knob";
import "./GestureBar.css";

const idx = (id: string) => store.index(id);
const LOOPER_TITLES = ["Loop", "Recording", "Looping", "Overdubbing", "Clearing"];
const LOOPER_HINTS = ["tap to record", "tap to close", "tap to overdub", "tap to stop", ""];

function useToggle(id: string) {
  const i = idx(id);
  const { value } = useParam(i);
  return [value > 0.5, () => store.setParam(i, value > 0.5 ? 0 : 1)] as const;
}

/** Hold to swell: every send blooms and the filters open; release ebbs back. */
function Swell() {
  const i = idx("swell.hold");
  const level = store.telemetry?.perf.swell ?? 0;
  const down = () => store.setParam(i, 1);
  const up = () => store.setParam(i, 0);
  return (
    <button
      className="gesture swell"
      onPointerDown={(e) => {
        (e.target as HTMLElement).setPointerCapture(e.pointerId);
        down();
      }}
      onPointerUp={up}
      onPointerCancel={up}
      title="Hold (or hold S): sends bloom and filters open. Release: it ebbs back over the Ebb time."
    >
      <span className="gesture-fill" style={{ transform: `scaleY(${level})` }} />
      <span className="gesture-title">Swell</span>
      <span className="gesture-sub">hold</span>
    </button>
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

function Weather() {
  return (
    <div className="gesture weather" title="A procedural weather bed: wind with gusts, rain, surf">
      <Knob param={idx("weather.wind")} label="Wind" size="sm" />
      <Knob param={idx("weather.rain")} label="Rain" size="sm" />
      <Knob param={idx("weather.surf")} label="Surf" size="sm" />
    </div>
  );
}

/** Keyboard gestures: S (hold) swell, F freeze all, I hold input, L loop, Shift+L
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
      else if (k === "f") flip("freeze.on");
      else if (k === "i") flip("input.freeze");
      else if (k === "e") flip("loops.on");
      else if (k === "l") store.command(e.shiftKey ? "loopClear" : "loopRecord");
      else return;
      e.preventDefault();
    };
    const up = (e: KeyboardEvent) => {
      if (e.key.toLowerCase() === "s") store.setParamById("swell.hold", 0);
    };
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", up);
    return () => {
      window.removeEventListener("keydown", down);
      window.removeEventListener("keyup", up);
    };
  }, []);
}

export function GestureBar() {
  useKey("telemetry");
  useGestureKeys();
  const perf = store.telemetry?.perf;
  return (
    <div className="gestures">
      <Swell />
      <Toggle id="freeze.on" title="Freeze all" sub="hold this moment" level={perf?.freeze ?? 0} hint="F: hold the last two seconds as a granular cloud while the rest steps back" />
      <Looper />
      <Toggle id="input.freeze" title="Hold input" sub="spectral" level={store.telemetry?.input[2] ?? 0} hint="I: hold the live input's sound forever as a spectral pad" />
      <Loops />
      <Weather />
    </div>
  );
}
