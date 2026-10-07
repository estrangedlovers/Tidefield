// Shapes of the messages exchanged with the engine (see src/io/UiProtocol.cpp).

export type Taper = "linear" | "log" | "db";

export interface ParamSpec {
  i: number;
  id: string;
  name: string;
  min: number;
  max: number;
  def: number;
  taper: Taper;
  unit: string;
  terrain: boolean;
  midi: boolean;
  perform: boolean;
  discrete: boolean;
}

export interface StripSpec {
  id: string;
  name: string;
  level: number;
  pan: number;
  width: number;
  sendA: number;
  sendB: number;
  fx: [number, number];
}

export interface FxSlotSpec {
  id: string;
  name: string;
  first: number;
}

export interface DisplayMap {
  curve: "linear" | "exp" | "power" | "choice" | "hidden";
  a: number;
  b: number;
  unit: string;
  decimals: number;
  choices: string[];
}

export interface ProcessorSpec {
  type: string;
  name: string;
  send: boolean;
  controls: { name: string; def: number; display: DisplayMap }[];
}

export interface Schema {
  params: ParamSpec[];
  strips: StripSpec[];
  fxSlots: FxSlotSpec[];
  busASlot: number;
  busBSlot: number;
  masterSlot: number;
  processors: ProcessorSpec[];
  cloudFirstParam: number[];
  scales: string[];
  noteNames: string[];
  mediumTypes: string[];
  bloomTransforms: string[];
  wanderStyles: string[];
  limits: { scenes: number; clouds: number; modes: number; bloomVoices: number; droneVoices: number; midiPorts: number };
}

/** FadeState in the engine: 0 silent, 1 fading in, 2 open, 3 fading out. */
export type FadeState = 0 | 1 | 2 | 3;

export interface TelemetryMessage {
  t: number;
  meter: [number, number, number, number];
  limiter: number;
  fade: [number, FadeState];
  panic: boolean;
  guard: number;
  /** Smoothed DSP load (0..1 of the real-time budget) and CPU guardrail level (0 = full quality). */
  load: [number, number];
  tide: number;
  key: [number, number, number];
  medium: number;
  sustain: boolean;
  strips: [number, number][];
  drone: [number, number][];
  clouds: { loaded: boolean; count: number; grains: [number, number, number][] }[];
  modes: [number, number][];
  bloom: { loaded: boolean; voices: [boolean, number, number][] };
  input: [number, boolean];
  terrain: { cursor: [number, number]; pos: [number, number]; n: number; version: number; w: number[] };
  full?: boolean;
  p?: [number, number][];
  live?: [number, number][];
  pickup?: [number, number][];
}

export interface SceneInfo {
  name: string;
  x: number;
  y: number;
}

export interface SamplesInfo {
  clouds: (string | null)[];
  bloom: string | null;
}

export interface MidiState {
  bindings: { text: string; param: number | null }[];
  learning: boolean;
  learnParam: number | null;
  noteChannel: number;
  notesToDrone: boolean;
  devices: { id: string; name: string; enabled: boolean; open: boolean }[];
}

export interface SessionInfo {
  name: string;
  busy: boolean;
  device: string;
  sampleRate: number;
  blockSize: number;
  cpu: number;
  xruns: number;
}

export interface RecordState {
  state: "idle" | "recording" | "finishing";
  seconds: number;
  stems: boolean;
  dropped: number;
  folder: string;
  last: string;
}

export interface StatusMessage {
  message: string;
  warning: boolean;
}

export interface MidiActivity {
  status: number;
  d1: number;
  d2: number;
  port: number;
}

/** How the UI talks to an engine: the real one through JUCE, or the browser mock. */
export interface Transport {
  call(method: string, ...args: unknown[]): Promise<unknown>;
  on(event: string, listener: (payload: any) => void): () => void;
  readonly isMock: boolean;
}
