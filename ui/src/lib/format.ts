import type { DisplayMap, ParamSpec, Schema } from "../bridge/types";

// Value mapping and formatting, mirroring the engine: ParamSpec::toNormalised /
// fromNormalised (only "log" tapers are non-linear) and dsp::DisplayMap::format.

export function toNorm(spec: ParamSpec, v: number): number {
  const c = Math.min(spec.max, Math.max(spec.min, v));
  if (spec.taper === "log" && spec.min > 0) return Math.log(c / spec.min) / Math.log(spec.max / spec.min);
  return (c - spec.min) / (spec.max - spec.min);
}

export function fromNorm(spec: ParamSpec, n: number): number {
  const x = Math.min(1, Math.max(0, n));
  if (spec.taper === "log" && spec.min > 0) return spec.min * Math.pow(spec.max / spec.min, x);
  const v = spec.min + x * (spec.max - spec.min);
  return spec.discrete ? Math.round(v) : v;
}

export function noteName(midi: number, names: string[]): string {
  const n = Math.round(midi);
  return `${names[((n % 12) + 12) % 12]}${Math.floor(n / 12) - 1}`;
}

/** Choice lists for discrete parameters, by id. */
export function choicesFor(spec: ParamSpec, schema: Schema): string[] | null {
  switch (spec.id) {
    case "harmony.root":
      return schema.noteNames;
    case "harmony.scale":
      return schema.scales;
    case "medium.type":
      return schema.mediumTypes;
    case "bloom.transform":
      return schema.bloomTransforms;
    case "terrain.wanderStyle":
      return schema.wanderStyles;
    case "catch.source":
      return ["Output", "Live input"];
    case "catch.target":
      return ["Auto", "Cloud 1", "Cloud 2", "Cloud 3", "Cloud 4"];
    case "input.channel":
      return ["Input 1", "Input 2", "1 + 2"];
    case "input.armed":
    case "input.freeze":
    case "freeze.on":
    case "loops.on":
    case "swell.hold":
      return ["Off", "On"];
    case "master.auto":
      return ["Off", "On"];
    case "master.autoTarget":
      return ["Quiet (-23)", "Streaming (-16)", "Loud (-14)"];
    case "loop.source":
      return ["Live input", "The mix"];
    case "loops.target":
      return ["Bloom", "Resonator", "Both"];
  }
  return null;
}

function trim(n: number, decimals: number) {
  return n.toFixed(decimals);
}

export function formatParam(spec: ParamSpec, v: number, schema: Schema): string {
  const choices = choicesFor(spec, schema);
  if (choices) return choices[Math.max(0, Math.min(choices.length - 1, Math.round(v)))] ?? "";
  if (spec.id.endsWith(".root") || spec.id === "loops.register") return noteName(v, schema.noteNames);
  if (spec.discrete) return String(Math.round(v));

  switch (spec.unit) {
    case "Hz":
      if (v >= 1000) return `${trim(v / 1000, v >= 10000 ? 1 : 2)} kHz`;
      if (v < 1) return `${trim(v, v < 0.1 ? 3 : 2)} Hz`;
      return v < 10 ? `${trim(v, 1)} Hz` : `${Math.round(v)} Hz`;
    case "s":
      return v < 1 ? `${Math.round(v * 1000)} ms` : `${trim(v, v < 10 ? 2 : 1)} s`;
    case "ms":
      return v >= 1000 ? `${trim(v / 1000, 2)} s` : `${Math.round(v)} ms`;
    case "dB":
      return v <= spec.min + 0.05 && spec.min <= -59 ? "Off" : `${v > 0 ? "+" : ""}${trim(v, 1)} dB`;
    case "oct":
      return `${trim(v, 1)} oct`;
    case "st":
      return `${v > 0 ? "+" : ""}${trim(v, 1)} st`;
    case "ct":
      return `${Math.round(v)} ct`;
    case "x":
      return `${trim(v, 2)}x`;
    case "/s":
      return `${trim(v, v < 10 ? 1 : 0)} /s`;
  }
  if (spec.min === -1 && spec.max === 1) {
    if (spec.id.endsWith(".pan")) return Math.abs(v) < 0.02 ? "C" : `${v < 0 ? "L" : "R"} ${Math.round(Math.abs(v) * 100)}`;
    return `${v > 0 ? "+" : ""}${Math.round(v * 100)}%`;
  }
  if (spec.min === 0 && (spec.max === 1 || spec.max === 2)) return `${Math.round(v * 100)}%`;
  return trim(v, Math.abs(spec.max - spec.min) > 50 ? 0 : 2);
}

/** Mirrors dsp::DisplayMap. */
export function displayValue(d: DisplayMap, v: number): number {
  switch (d.curve) {
    case "linear":
      return d.a + d.b * v;
    case "exp":
      return d.a * Math.pow(d.b, v);
    case "power":
      return d.a * Math.pow(v, d.b);
    default:
      return v;
  }
}

export function formatDisplay(d: DisplayMap, v: number): string {
  if (d.curve === "hidden") return "-";
  if (d.curve === "choice" && d.choices.length) {
    const i = Math.max(0, Math.min(d.choices.length - 1, Math.floor(v * d.choices.length)));
    return d.choices[i];
  }
  const x = displayValue(d, v);
  if (d.unit === "ms" && x >= 1000) return `${(x * 0.001).toFixed(2)} s`;
  const space = d.unit === "" || d.unit === "%" ? "" : " ";
  return `${x.toFixed(d.decimals)}${space}${d.unit}`;
}
