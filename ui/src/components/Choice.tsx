import { store, useParam, useSlice } from "../state/store";
import { choicesFor } from "../lib/format";
import { openMenu } from "./ContextMenu";
import "./Choice.css";

interface Props {
  param: number;
  label?: string;
  /** "tiles": large blocks for the performance view; "pills": compact segmented. */
  variant?: "tiles" | "pills";
  /** Optional per-choice captions under the name (tiles only). */
  captions?: string[];
  columns?: number;
}

/** Discrete parameter as a set of buttons. */
export function Choice({ param, label, variant = "pills", captions, columns }: Props) {
  const { value, live } = useParam(param);
  const { schema, midi } = useSlice("midi");
  const spec = schema?.params[param];
  if (!spec || !schema) return null;
  const choices = choicesFor(spec, schema) ?? Array.from({ length: spec.max - spec.min + 1 }, (_, k) => String(spec.min + k));
  const current = Math.round(value);
  const learning = midi.learning && midi.learnParam === param;

  return (
    <div
      className={`choice choice-${variant}${live ? " live" : ""}${learning ? " learn" : ""}`}
      onContextMenu={(e) => {
        e.preventDefault();
        openMenu(e, [
          { label: learning ? "Cancel MIDI learn" : "MIDI learn (move a controller)", onSelect: () => void store.call(learning ? "midi.cancelLearn" : "midi.learnParam", param) },
          { label: "Release to terrain", onSelect: () => void store.call("releaseParam", param), disabled: !live },
        ], label ?? spec.name);
      }}
    >
      {label !== "" && <div className="choice-label caps">{label ?? spec.name}</div>}
      <div className="choice-items" style={columns ? { gridTemplateColumns: `repeat(${columns}, 1fr)`, gridAutoFlow: "row" } : undefined}>
        {choices.map((c, k) => (
          <button
            key={c}
            className={`choice-item${k + spec.min === current ? " on" : ""}`}
            onClick={() => store.setParam(param, k + spec.min)}
          >
            <span>{c}</span>
            {captions?.[k] && <small>{captions[k]}</small>}
          </button>
        ))}
      </div>
    </div>
  );
}
