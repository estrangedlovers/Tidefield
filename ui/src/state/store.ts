import { useSyncExternalStore } from "react";
import type {
  MidiActivity,
  MidiState,
  RecordState,
  SeasonInfo,
  Schema,
  SceneInfo,
  SamplesInfo,
  SessionInfo,
  StatusMessage,
  TelemetryMessage,
  Transport,
} from "../bridge/types";

// One external store for the whole UI. Parameters are kept in typed arrays and each
// has its own subscription key ("p:12"), so a 30 Hz telemetry stream only re-renders
// the controls whose values actually moved. Visuals read `telemetry` directly from
// requestAnimationFrame loops and never re-render through React.

export interface Toast extends StatusMessage {
  id: number;
  at: number;
}

type Key = string;

class Store {
  transport: Transport | null = null;
  schema: Schema | null = null;
  engineVersion = "";
  params = new Float32Array(0);
  live = new Uint8Array(0);
  pickup = new Int8Array(0);
  /** Parameters the user is dragging: incoming telemetry must not fight the hand. */
  touching = new Set<number>();
  telemetry: TelemetryMessage | null = null;
  scenes: SceneInfo[] = [];
  fx: string[] = [];
  samples: SamplesInfo = { clouds: [], bloom: null };
  midi: MidiState = { bindings: [], learning: false, learnParam: null, noteChannel: -1, notesToDrone: false, devices: [] };
  session: SessionInfo = { name: "Untitled", busy: false, device: "", sampleRate: 0, blockSize: 0, cpu: 0, xruns: 0 };
  seasons: SeasonInfo[] = [];
  record: RecordState = { state: "idle", seconds: 0, stems: false, dropped: 0, folder: "", last: "" };
  toasts: Toast[] = [];
  lastMidi: MidiActivity | null = null;
  view: "perform" | "edit" = "perform";
  editPage = "drone";

  private listeners = new Map<Key, Set<() => void>>();
  private versions = new Map<Key, number>();
  private toastId = 0;

  subscribe(key: Key, fn: () => void) {
    if (!this.listeners.has(key)) this.listeners.set(key, new Set());
    this.listeners.get(key)!.add(fn);
    return () => this.listeners.get(key)!.delete(fn);
  }

  versionOf(key: Key) {
    return this.versions.get(key) ?? 0;
  }

  notify(key: Key) {
    this.versions.set(key, (this.versions.get(key) ?? 0) + 1);
    this.listeners.get(key)?.forEach((fn) => fn());
  }

  async connect(transport: Transport) {
    this.transport = transport;
    transport.on("telemetry", (m: TelemetryMessage) => this.applyTelemetry(m));
    transport.on("scenes", (s: SceneInfo[]) => this.set("scenes", s));
    transport.on("fx", (f: string[]) => this.set("fx", f));
    transport.on("samples", (s: SamplesInfo) => this.set("samples", s));
    transport.on("midi", (m: MidiState) => this.set("midi", m));
    transport.on("session", (s: SessionInfo) => this.set("session", s));
    transport.on("record", (r: RecordState) => this.set("record", r));
    transport.on("seasons", (s: SeasonInfo[]) => this.set("seasons", s));
    transport.on("status", (s: StatusMessage) => this.toast(s.message, s.warning));
    transport.on("midiActivity", (m: MidiActivity) => {
      this.lastMidi = m;
      this.notify("midiActivity");
    });
    const hello = (await transport.call("hello")) as { schema: Schema; version: string };
    this.schema = hello.schema;
    this.engineVersion = hello.version;
    const n = hello.schema.params.length;
    this.params = new Float32Array(hello.schema.params.map((p) => p.def));
    this.live = new Uint8Array(n);
    this.pickup = new Int8Array(n);
    this.notify("schema");
  }

  private set<K extends "scenes" | "fx" | "samples" | "midi" | "session" | "record" | "seasons">(key: K, value: Store[K]) {
    (this as any)[key] = value;
    this.notify(key);
  }

  applyTelemetry(m: TelemetryMessage) {
    this.telemetry = m;
    if (m.p)
      for (const [i, v] of m.p) {
        if (this.touching.has(i)) continue;
        this.params[i] = v;
        this.notify(`p:${i}`);
      }
    if (m.live)
      for (const [i, v] of m.live) {
        this.live[i] = v;
        this.notify(`p:${i}`);
      }
    if (m.pickup)
      for (const [i, v] of m.pickup) {
        this.pickup[i] = v;
        this.notify(`p:${i}`);
      }
    this.notify("telemetry");
  }

  // --- Actions -----------------------------------------------------------------------
  call(method: string, ...args: unknown[]) {
    return this.transport?.call(method, ...args) ?? Promise.resolve(null);
  }

  setParam(i: number, value: number) {
    const spec = this.schema?.params[i];
    if (!spec) return;
    const v = Math.min(spec.max, Math.max(spec.min, value));
    this.params[i] = v;
    if (spec.terrain && this.scenes.length > 0) this.live[i] = 1;
    this.notify(`p:${i}`);
    void this.call("setParam", i, v);
  }

  setParamById(id: string, value: number) {
    const i = this.index(id);
    if (i >= 0) this.setParam(i, value);
  }

  index(id: string) {
    return this.schema?.params.findIndex((p) => p.id === id) ?? -1;
  }

  value(id: string) {
    const i = this.index(id);
    return i >= 0 ? this.params[i] : 0;
  }

  command(name: string) {
    void this.call("command", name);
  }

  toast(message: string, warning = false) {
    const t = { id: this.toastId++, message, warning, at: performance.now() };
    this.toasts = [...this.toasts.slice(-3), t];
    this.notify("toasts");
    setTimeout(() => {
      this.toasts = this.toasts.filter((x) => x.id !== t.id);
      this.notify("toasts");
    }, warning ? 6000 : 3500);
  }

  setView(view: "perform" | "edit", page?: string) {
    this.view = view;
    if (page) this.editPage = page;
    this.notify("view");
  }
}

export const store = new Store();

/** Re-renders when `key` is notified. */
export function useKey(key: Key) {
  useSyncExternalStore(
    (fn) => store.subscribe(key, fn),
    () => store.versionOf(key),
  );
}

export function useParam(i: number): { value: number; live: boolean; pickup: number } {
  useKey(`p:${i}`);
  return { value: store.params[i] ?? 0, live: store.live[i] === 1, pickup: store.pickup[i] ?? 0 };
}

export function useParamId(id: string) {
  useKey("schema");
  return useParam(store.index(id));
}

export function useSlice<
  K extends "scenes" | "fx" | "samples" | "midi" | "session" | "record" | "seasons" | "toasts" | "view" | "schema" | "midiActivity",
>(key: K) {
  useKey(key);
  return store;
}
