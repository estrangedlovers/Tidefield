import { useRef } from "react";
import { store, useParam, useSlice } from "../state/store";
import { formatParam, fromNorm, toNorm } from "../lib/format";
import { openMenu } from "./ContextMenu";
import "./Fader.css";

interface Props {
  param: number;
  label?: string;
  hint?: string;
}

/** A tall, wide performance fader: the large controls of the performance view.
 *  Drag anywhere on it (it does not jump to the pointer), Shift for fine, double-click
 *  to reset, right-click for MIDI learn. */
export function Fader({ param, label, hint }: Props) {
  const { value, live, pickup } = useParam(param);
  const { schema, midi } = useSlice("midi");
  const drag = useRef<{ y: number; n: number; h: number } | null>(null);
  const spec = schema?.params[param];
  if (!spec || !schema) return null;
  const n = toNorm(spec, value);
  const learning = midi.learning && midi.learnParam === param;

  return (
    <div
      className={`fader${live ? " live" : ""}${learning ? " learn" : ""}`}
      title={hint}
      onContextMenu={(e) => {
        e.preventDefault();
        openMenu(
          e,
          [
            { label: learning ? "Cancel MIDI learn" : "MIDI learn (move a controller)", onSelect: () => void store.call(learning ? "midi.cancelLearn" : "midi.learnParam", param) },
            { label: "Forget MIDI mapping", onSelect: () => void store.call("midi.clearParam", param), disabled: !midi.bindings.some((b) => b.param === param) },
            "-",
            { label: "Release to terrain", onSelect: () => void store.call("releaseParam", param), disabled: !live },
            { label: `Reset to ${formatParam(spec, spec.def, schema)}`, onSelect: () => store.setParam(param, spec.def) },
          ],
          label ?? spec.name,
        );
      }}
    >
      <div
        className="fader-body"
        onPointerDown={(e) => {
          if (e.button !== 0) return;
          (e.currentTarget as Element).setPointerCapture(e.pointerId);
          drag.current = { y: e.clientY, n, h: (e.currentTarget as HTMLElement).clientHeight };
          store.touching.add(param);
        }}
        onPointerMove={(e) => {
          if (!drag.current) return;
          const scale = e.shiftKey ? 4 : 1;
          const next = Math.min(1, Math.max(0, drag.current.n + (drag.current.y - e.clientY) / (drag.current.h * scale)));
          drag.current = { ...drag.current, y: e.clientY, n: next };
          store.setParam(param, fromNorm(spec, next));
        }}
        onPointerUp={() => {
          drag.current = null;
          store.touching.delete(param);
        }}
        onDoubleClick={() => store.setParam(param, spec.def)}
      >
        <div className="fader-fill" style={{ transform: `scaleY(${n})` }} />
        <div className="fader-line" style={{ bottom: `${n * 100}%` }} />
        <div className="fader-value">{formatParam(spec, value, schema)}</div>
        {pickup !== 0 && <div className="fader-pickup">{pickup > 0 ? "▲" : "▼"}</div>}
      </div>
      <div className="fader-label">{label ?? spec.name}</div>
    </div>
  );
}
