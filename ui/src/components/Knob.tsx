import { useRef } from "react";
import { store, useParam, useSlice } from "../state/store";
import { formatParam, fromNorm, toNorm } from "../lib/format";
import { openMenu } from "./ContextMenu";
import "./Knob.css";

interface Props {
  param: number;
  label?: string;
  size?: "sm" | "md" | "lg";
  /** Overrides value text (FX controls format through their processor). */
  format?: (v: number) => string;
  disabled?: boolean;
}

const SIZES = { sm: 38, md: 52, lg: 76 };
const START = -135;
const SWEEP = 270;

function arc(cx: number, cy: number, r: number, from: number, to: number) {
  const a0 = ((from - 90) * Math.PI) / 180;
  const a1 = ((to - 90) * Math.PI) / 180;
  const large = to - from > 180 ? 1 : 0;
  return `M ${cx + r * Math.cos(a0)} ${cy + r * Math.sin(a0)} A ${r} ${r} 0 ${large} 1 ${cx + r * Math.cos(a1)} ${cy + r * Math.sin(a1)}`;
}

/** Rotary control bound to an engine parameter.
 *  Drag up/down (Shift for fine), wheel, double-click resets, Alt-click releases a
 *  live-layer hold, right-click for MIDI learn. Sand ring: held in the live layer.
 *  Coral: waiting for a MIDI controller to learn. Arrow: soft takeover direction. */
export function Knob({ param, label, size = "md", format, disabled }: Props) {
  const { value, live, pickup } = useParam(param);
  const { schema, midi } = useSlice("midi");
  const drag = useRef<{ y: number; n: number } | null>(null);
  const spec = schema?.params[param];
  if (!spec || !schema) return null;

  const n = toNorm(spec, value);
  const d = SIZES[size];
  const r = d / 2 - 4;
  const learning = midi.learning && midi.learnParam === param;
  const bipolar = spec.min < 0 && spec.max > 0 && spec.unit !== "dB";
  const zero = bipolar ? toNorm(spec, 0) : 0;
  const text = format ? format(value) : formatParam(spec, value, schema);
  const mapped = midi.bindings.some((b) => b.param === param);

  const setNorm = (x: number) => store.setParam(param, fromNorm(spec, x));

  const onPointerDown = (e: React.PointerEvent) => {
    if (disabled || e.button !== 0) return;
    if (e.altKey) {
      void store.call("releaseParam", param);
      return;
    }
    (e.target as Element).setPointerCapture(e.pointerId);
    drag.current = { y: e.clientY, n };
    store.touching.add(param);
  };
  const onPointerMove = (e: React.PointerEvent) => {
    if (!drag.current) return;
    const range = e.shiftKey ? 900 : 220;
    const next = drag.current.n + (drag.current.y - e.clientY) / range;
    drag.current = { y: e.clientY, n: Math.min(1, Math.max(0, next)) };
    setNorm(drag.current.n);
  };
  const end = () => {
    drag.current = null;
    store.touching.delete(param);
  };
  const onWheel = (e: React.WheelEvent) => {
    if (disabled) return;
    setNorm(n - Math.sign(e.deltaY) * (e.shiftKey ? 0.005 : 0.02));
  };
  const onContext = (e: React.MouseEvent) => {
    e.preventDefault();
    openMenu(
      e,
      [
        learning
          ? { label: "Cancel MIDI learn", onSelect: () => void store.call("midi.cancelLearn") }
          : { label: "MIDI learn (move a controller)", onSelect: () => void store.call("midi.learnParam", param), disabled: !spec.midi },
        ...(mapped ? [{ label: "Forget MIDI mapping", onSelect: () => void store.call("midi.clearParam", param) }] : []),
        "-",
        { label: "Release to terrain", onSelect: () => void store.call("releaseParam", param), disabled: !live },
        { label: `Reset to ${formatParam(spec, spec.def, schema)}`, onSelect: () => store.setParam(param, spec.def) },
      ],
      label ?? spec.name,
    );
  };

  const angle = START + SWEEP * n;
  const from = START + SWEEP * Math.min(zero, n);
  const to = START + SWEEP * Math.max(zero, n);
  const tone = learning ? "learn" : live ? "live" : "";

  return (
    <div className={`knob knob-${size} ${tone}${disabled ? " disabled" : ""}`} onContextMenu={onContext} title={spec.id}>
      <div className="knob-label">
        {label ?? spec.name}
        {pickup !== 0 && <span className="knob-pickup">{pickup > 0 ? "▲" : "▼"}</span>}
        {mapped && <span className="knob-midi" />}
      </div>
      <svg
        width={d}
        height={d}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={end}
        onPointerCancel={end}
        onDoubleClick={() => !disabled && store.setParam(param, spec.def)}
        onWheel={onWheel}
      >
        <path className="knob-track" d={arc(d / 2, d / 2, r, START, START + SWEEP)} />
        {to - from > 0.5 && <path className="knob-value" d={arc(d / 2, d / 2, r, from, to)} />}
        <circle className="knob-cap" cx={d / 2} cy={d / 2} r={r - 5} />
        <line
          className="knob-pointer"
          x1={d / 2 + (r - 12) * Math.cos(((angle - 90) * Math.PI) / 180) * 0.35}
          y1={d / 2 + (r - 12) * Math.sin(((angle - 90) * Math.PI) / 180) * 0.35}
          x2={d / 2 + (r - 7) * Math.cos(((angle - 90) * Math.PI) / 180)}
          y2={d / 2 + (r - 7) * Math.sin(((angle - 90) * Math.PI) / 180)}
        />
      </svg>
      <div className="knob-value-text">{text}</div>
    </div>
  );
}
