import schemaJson from "./schema.json";
import type { FadeState, LooperState, MidiState, RecordState, Schema, SceneInfo, SeasonInfo, SessionInfo, TelemetryMessage, Transport } from "./types";

// A stand-in engine for developing the UI in a plain browser (npm run dev). It uses
// the real schema (dumped by `tidefield_render --dump-schema`) and imitates the
// engine's behaviour closely enough to design against: terrain blending, glide,
// wander, fades, grains, voices, rain. Add ?demo to the URL for a populated terrain.

const schema = schemaJson as unknown as Schema;
const byId = new Map(schema.params.map((p) => [p.id, p]));
const P = (id: string) => byId.get(id)!.i;

type Listener = (payload: any) => void;

interface MockScene extends SceneInfo {
  values: Map<number, number>;
}

class MockEngine {
  listeners = new Map<string, Set<Listener>>();
  targets = new Float32Array(schema.params.map((p) => p.def));
  live = new Uint8Array(schema.params.length);
  sent = new Float32Array(schema.params.length).fill(NaN);
  sentLive = new Uint8Array(schema.params.length).fill(255);
  scenes: MockScene[] = [];
  fx: string[] = schema.fxSlots.map(() => "");
  samples = { clouds: ["chord", null, null, null] as (string | null)[], bloom: "glass" as string | null };
  midi: MidiState = {
    bindings: ["terrain.x", "terrain.y", "tide.rate", "terrain.wander", "harmony.gravity", "busA.level", "cloud1.density", "master.level"].map(
      (id, k) => ({ text: `CC ${21 + k} (any ch) -> ${byId.get(id)!.name}`, param: P(id) }),
    ),
    learning: false,
    learnParam: null,
    noteChannel: -1,
    notesToDrone: false,
    devices: [{ id: "mock", name: "Mock Controller", enabled: true, open: true }],
  };
  session: SessionInfo = { name: "Untitled", busy: false, device: "Browser mock", sampleRate: 48000, blockSize: 256, cpu: 0.11, xruns: 0 };

  seasons: SeasonInfo[] = [];
  seasonPhase = [0, 0, 0, 0, 0, 0, 0, 0];
  swell = 0;
  freeze = 0;
  inputFreeze = 0;
  looper = { state: 0 as LooperState, pos: 0, seconds: 0, passes: 0 };
  loopPhase = [0.1, 0.5, 0.8, 0.3, 0.65, 0.2, 0.9, 0.45];
  loopFlash = [0, 0, 0, 0, 0, 0, 0, 0];
  record: RecordState = { state: "idle", seconds: 0, stems: false, dropped: 0, folder: "~/Music/Tidefield", last: "" };
  recordTimer: ReturnType<typeof setInterval> | null = null;

  fadeGain = 0;
  fadeState: FadeState = 0;
  panic = false;
  cursor = [0.5, 0.5];
  pos = [0.5, 0.5];
  wander = [0, 0];
  weights: number[] = [];
  primed = false;
  time = 0;
  keyMorph = 1;
  lastKey = -1;
  grains: [number, number, number, number][][] = [[], [], [], []]; // pos, age, life, pan
  modes = Array.from({ length: 24 }, () => 0);
  voices: { note: number; level: number; age: number; held: boolean }[] = [];
  droneLevels = Array.from({ length: 6 }, () => 0);
  catchCount = 0;

  constructor(demo: boolean) {
    for (const [slot, type] of [[schema.busASlot, "tf.reverb"], [schema.busBSlot, "tf.delay"]] as const) {
      this.fx[slot] = type;
      const proc = schema.processors.find((p) => p.type === type)!;
      proc.controls.forEach((c, k) => (this.targets[schema.fxSlots[slot].first + k] = c.def));
    }
    if (demo) {
      const make = (name: string, x: number, y: number, vals: Record<string, number>) => {
        const values = new Map<number, number>();
        for (const p of schema.params) if (p.terrain) values.set(p.i, p.def);
        for (const [id, v] of Object.entries(vals)) values.set(P(id), v);
        this.scenes.push({ name, x, y, values });
      };
      make("Hollow", 0.18, 0.25, { "drone.cutoff": 280, "drone.density": 2, "cloud1.density": 4, "res.rain": 0.1, "tide.rate": 0.5 });
      make("Glass", 0.8, 0.22, { "drone.cutoff": 5200, "drone.density": 4, "cloud1.density": 30, "res.rain": 0.7, "res.structure": 0.9 });
      make("Fog", 0.52, 0.82, { "drone.cutoff": 900, "drone.density": 6, "cloud1.density": 60, "cloud1.spray": 0.8, "medium.noise": 0.8 });
      make("Ember", 0.2, 0.7, { "drone.cutoff": 1500, "drone.density": 3, "cloud1.density": 14, "res.rain": 0.35, "tide.rate": 1.6 });
      this.targets[P("terrain.wander")] = 0.35;
      this.targets[P("medium.type")] = 1;
      this.fadeGain = 1;
      this.fadeState = 2;
    }
    setInterval(() => this.tick(1 / 30), 1000 / 30);
    setInterval(() => this.emit("session", { ...this.session, cpu: 0.08 + 0.06 * Math.random() }), 1000);
  }

  emit(event: string, payload: unknown) {
    this.listeners.get(event)?.forEach((l) => l(payload));
  }

  status(message: string, warning = false) {
    this.emit("status", { message, warning });
  }

  setParam(i: number, v: number) {
    const spec = schema.params[i];
    if (!spec) return;
    if (spec.terrain && this.scenes.length > 0) this.live[i] = 1;
    this.targets[i] = Math.min(spec.max, Math.max(spec.min, v));
  }

  call(method: string, args: any[]): unknown {
    switch (method) {
      case "hello":
        this.primed = false;
        setTimeout(() => this.pushState(), 0);
        return { schema, version: "mock" };
      case "setParam":
        this.setParam(args[0], args[1]);
        return null;
      case "releaseParam":
        this.live[args[0]] = 0;
        return null;
      case "command":
        return this.command(args[0]);
      case "noteOn": {
        if (this.voices.length >= 8) this.voices.shift();
        this.voices.push({ note: args[0], level: 0, age: 0, held: true });
        return true;
      }
      case "noteOff":
        this.voices.forEach((v) => v.note === args[0] && (v.held = false));
        return true;
      case "scene.capture": {
        if (this.scenes.length >= schema.limits.scenes) return -1;
        const values = new Map<number, number>();
        for (const p of schema.params) if (p.terrain) values.set(p.i, this.targets[p.i]);
        this.scenes.push({ name: `Scene ${this.scenes.length + 1}`, x: args[0] ?? this.cursor[0], y: args[1] ?? this.cursor[1], values });
        this.live.fill(0);
        this.pushState();
        return this.scenes.length - 1;
      }
      case "scene.move":
        if (this.scenes[args[0]]) Object.assign(this.scenes[args[0]], { x: args[1], y: args[2] });
        break;
      case "scene.remove":
        this.scenes.splice(args[0], 1);
        break;
      case "scene.rename":
        if (this.scenes[args[0]]) this.scenes[args[0]].name = args[1];
        break;
      case "scene.commit": {
        const s = this.scenes[args[0]];
        if (s) this.live.forEach((l, i) => l && s.values.set(i, this.targets[i]));
        this.live.fill(0);
        break;
      }
      case "scene.replace": {
        const s = this.scenes[args[0]];
        if (s) for (const p of schema.params) if (p.terrain) s.values.set(p.i, this.targets[p.i]);
        this.live.fill(0);
        break;
      }
      case "scene.releaseLive":
        this.live.fill(0);
        break;
      case "fx.setType": {
        this.fx[args[0]] = args[1];
        const proc = schema.processors.find((p) => p.type === args[1]);
        const first = schema.fxSlots[args[0]].first;
        proc?.controls.forEach((c, k) => (this.targets[first + k] = c.def));
        break;
      }
      case "sample.load":
        if (args[0] === 4) this.samples.bloom = "one-shot";
        else this.samples.clouds[args[0]] = "imported";
        this.status("Loaded a sample (mock)");
        break;
      case "sample.peaks": {
        const name = args[0] === 4 ? this.samples.bloom : this.samples.clouds[args[0]];
        if (!name) return [];
        // A plausible outline: a swell with a little texture.
        return Array.from({ length: args[1] }, (_, k) => {
          const x = k / args[1];
          return Math.min(1, (args[0] === 4 ? Math.exp(-x * 4) : Math.sin(Math.PI * x) ** 0.6) * (0.75 + 0.25 * Math.sin(k * 0.37) * Math.sin(k * 0.11)));
        });
      }
      case "sample.clear":
        if (args[0] === 4) this.samples.bloom = null;
        else this.samples.clouds[args[0]] = null;
        break;
      case "session.new":
      case "session.open":
      case "session.save":
      case "session.saveAs":
        this.status(`${method} is not available in the browser mock`, true);
        break;
      case "midi.learnParam":
        this.midi = { ...this.midi, learning: true, learnParam: args[0] };
        setTimeout(() => {
          if (!this.midi.learning) return;
          this.midi = {
            ...this.midi,
            learning: false,
            learnParam: null,
            bindings: [...this.midi.bindings, { text: `CC 74 (ch 1) -> ${schema.params[args[0]].name}`, param: args[0] }],
          };
          this.status(`MIDI learned: CC 74 (ch 1) -> ${schema.params[args[0]].name}`);
          this.emit("midi", this.midi);
        }, 1500);
        break;
      case "midi.learnAction":
        this.midi = { ...this.midi, learning: true, learnParam: null };
        break;
      case "midi.cancelLearn":
        this.midi = { ...this.midi, learning: false, learnParam: null };
        break;
      case "midi.clearParam":
        this.midi = { ...this.midi, bindings: this.midi.bindings.filter((b) => b.param !== args[0]) };
        break;
      case "midi.remove":
        this.midi = { ...this.midi, bindings: this.midi.bindings.filter((_, k) => k !== args[0]) };
        break;
      case "midi.clearAll":
        this.midi = { ...this.midi, bindings: [] };
        break;
      case "midi.setNoteChannel":
        this.midi = { ...this.midi, noteChannel: args[0] };
        break;
      case "midi.setNotesToDrone":
        this.midi = { ...this.midi, notesToDrone: args[0] };
        break;
      case "record.toggle":
      case "record.start":
      case "record.stop": {
        const start = method === "record.start" || (method === "record.toggle" && this.record.state === "idle");
        if (start && this.record.state === "idle") {
          this.record = { ...this.record, state: "recording", seconds: 0 };
          this.recordTimer = setInterval(() => {
            this.record = { ...this.record, seconds: this.record.seconds + 1 };
            this.emit("record", this.record);
          }, 1000);
          this.status(this.record.stems ? "Recording master and stems" : "Recording");
        } else if (!start && this.record.state === "recording") {
          if (this.recordTimer) clearInterval(this.recordTimer);
          this.record = { ...this.record, state: "idle", last: "2026-10-07 18.04.12 Untitled" };
          this.status("Recording saved: " + this.record.last);
        }
        this.emit("record", this.record);
        return null;
      }
      case "season.set": {
        const [i, s] = args as [number, SeasonInfo];
        if (i === this.seasons.length && this.seasons.length < 8) this.seasons.push(s);
        else if (i < this.seasons.length) this.seasons[i] = s;
        this.emit("seasons", [...this.seasons]);
        return null;
      }
      case "season.remove":
        this.seasons.splice(args[0] as number, 1);
        this.emit("seasons", [...this.seasons]);
        return null;
      case "record.setStems":
        this.record = { ...this.record, stems: Boolean(args[0]) };
        this.emit("record", this.record);
        return null;
      case "record.chooseFolder":
      case "record.reveal":
        this.status("Not available in the browser preview");
        return null;
      case "audio.settings":
        this.status("Audio settings open in the app (mock)");
        break;
      default:
        return `unknown method: ${method}`;
    }
    this.pushState();
    return null;
  }

  command(name: string) {
    const fadeSecs = this.targets[P("master.fadeSeconds")];
    switch (name) {
      case "fadeIn":
        this.fadeState = 1;
        break;
      case "fadeOut":
        this.fadeState = 3;
        break;
      case "fadeToggle":
        this.fadeState = this.fadeState === 0 || this.fadeState === 3 ? 1 : 3;
        break;
      case "panic":
      case "panicToggle":
        this.panic = !this.panic;
        if (!this.panic) this.fadeState = 1;
        break;
      case "resume":
        this.panic = false;
        this.fadeState = 1;
        break;
      case "loopRecord": {
        const l = this.looper;
        if (l.state === 0 || l.state === 4) Object.assign(l, { state: 1, pos: 0, seconds: 0, passes: 0 });
        else if (l.state === 1) l.state = l.seconds < 0.25 ? 0 : 2;
        else l.state = l.state === 2 ? 3 : 2;
        break;
      }
      case "loopClear":
        if (this.looper.state === 1) this.looper.state = 0;
        else if (this.looper.state >= 2) this.looper.state = 4;
        break;
      case "catch":
        this.catchCount++;
        this.samples.clouds[Math.min(3, this.catchCount)] = `Catch ${this.catchCount}`;
        this.status(`Caught into Cloud ${Math.min(4, this.catchCount + 1)} (Catch ${this.catchCount})`);
        this.pushState();
        break;
      case "releaseLive":
        this.live.fill(0);
        break;
    }
    void fadeSecs;
    return true;
  }

  pushState() {
    this.emit("scenes", this.scenes.map(({ name, x, y }) => ({ name, x, y })));
    this.emit("fx", this.fx);
    this.emit("samples", this.samples);
    this.emit("midi", this.midi);
    this.emit("session", this.session);
    this.emit("record", this.record);
    this.emit("seasons", this.seasons);
  }

  tick(dt: number) {
    this.time += dt;
    const t = this.targets;
    const tide = t[P("tide.rate")];

    // Fades.
    const fadeRate = dt / Math.max(0.1, t[P("master.fadeSeconds")]);
    if (this.fadeState === 1) {
      this.fadeGain = Math.min(1, this.fadeGain + fadeRate);
      if (this.fadeGain >= 1) this.fadeState = 2;
    } else if (this.fadeState === 3) {
      this.fadeGain = Math.max(0, this.fadeGain - fadeRate);
      if (this.fadeGain <= 0) this.fadeState = 0;
    }
    const audible = this.panic ? 0 : this.fadeGain ** 3;

    // Terrain: glide, wander, weights.
    const glide = Math.max(0.05, t[P("terrain.glide")]);
    const k = 1 - Math.exp(-dt / glide);
    this.cursor[0] += (t[P("terrain.x")] - this.cursor[0]) * k;
    this.cursor[1] += (t[P("terrain.y")] - this.cursor[1]) * k;
    const wanderAmt = t[P("terrain.wander")];
    const rate = t[P("terrain.wanderRate")] * tide;
    for (let d = 0; d < 2; d++) this.wander[d] += -this.wander[d] * rate * 6 * dt + Math.sqrt(dt * rate * 6) * (Math.random() * 2 - 1) * 0.9;
    this.pos = [0, 1].map((d) => Math.min(1, Math.max(0, this.cursor[d] + this.wander[d] * 0.45 * wanderAmt)));
    const focus = t[P("terrain.focus")];
    const w = this.scenes.map((s) => Math.pow((this.pos[0] - s.x) ** 2 + (this.pos[1] - s.y) ** 2 + 1e-5, -0.5 * focus));
    const total = w.reduce((a, b) => a + b, 0) || 1;
    this.weights = w.map((x) => x / total);
    if (this.scenes.length) {
      for (const p of schema.params) {
        if (!p.terrain || this.live[p.i]) continue;
        let sum = 0;
        let wt = 0;
        this.scenes.forEach((s, k) => {
          const v = s.values.get(p.i);
          if (v === undefined) return;
          sum += (p.taper === "log" ? Math.log(v) : v) * this.weights[k];
          wt += this.weights[k];
        });
        if (wt > 0) t[p.i] = p.taper === "log" ? Math.exp(sum / wt) : sum / wt;
      }
    }

    // Key morph.
    const key = t[P("harmony.root")] * 16 + t[P("harmony.scale")];
    if (this.lastKey >= 0 && key !== this.lastKey) this.keyMorph = 0;
    this.lastKey = key;
    this.keyMorph = Math.min(1, this.keyMorph + (dt * tide) / Math.max(0.5, t[P("harmony.morph")]));

    // Drone voices.
    const density = t[P("drone.density")];
    const root = t[P("drone.root")];
    const intervals = [0, 12, 7, -12, 19, 24];
    this.droneLevels = this.droneLevels.map((l, v) => {
      const target = Math.max(0, Math.min(1, density - v)) * (0.75 + 0.25 * Math.sin(this.time * 0.3 * tide + v * 1.7));
      return l + (target - l) * Math.min(1, dt * 0.8);
    });

    // Clouds: grains drift around position (+ scan).
    const clouds = [0, 1, 2, 3].map((c) => {
      const first = schema.cloudFirstParam[c];
      const loaded = this.samples.clouds[c] !== null;
      const dens = t[first];
      const life = t[first + 1] / 1000;
      const position = t[first + 2];
      const spray = t[first + 3];
      const scan = t[first + 4];
      const g = this.grains[c];
      for (const gr of g) gr[1] += dt;
      this.grains[c] = g.filter((gr) => gr[1] < gr[2]);
      if (loaded && audible > 0.001) {
        let spawn = dens * dt;
        while (spawn > 0 && this.grains[c].length < 24) {
          if (Math.random() < spawn) {
            const p = (position + scan * this.time / 60 + spray * 0.5 * (Math.random() * 2 - 1) + 1) % 1;
            this.grains[c].push([p, 0, life * (0.8 + 0.4 * Math.random()), t[first + 10] * (Math.random() * 2 - 1)]);
          }
          spawn -= 1;
        }
      }
      return {
        loaded,
        count: Math.round(this.grains[c].length * 1.4),
        grains: this.grains[c].map(([p, age, l, pan]) => [p, Math.sin(Math.PI * Math.min(1, age / l)), pan] as [number, number, number]),
      };
    });

    // Resonator: rain strikes ring modes.
    const rain = t[P("res.rain")] * 8 * tide;
    const decay = t[P("res.decay")];
    this.modes = this.modes.map((m, k) => {
      let v = m * Math.exp((-dt * 6.9) / Math.max(0.1, decay / (1 + k * 0.05)));
      if (Math.random() < rain * dt * 0.06) v = Math.min(1, v + 0.1 + 0.2 * Math.random());
      return v;
    });

    // Bloom voices.
    const length = t[P("bloom.length")];
    this.voices.forEach((v) => {
      v.age += dt;
      const releasing = !v.held && v.age > length;
      v.level = releasing ? v.level * Math.exp(-dt / Math.max(0.1, t[P("bloom.release")] / 3)) : Math.min(1, v.level + dt * 2);
    });
    this.voices = this.voices.filter((v) => v.level > 0.002 || v.age < 0.2);

    // Performance layer.
    const towards = (v: number, target: number, seconds: number) => v + (target - v) * Math.min(1, (3 * dt) / Math.max(0.01, seconds));
    this.swell = towards(this.swell, t[P("swell.hold")], t[P("swell.hold")] > this.swell ? t[P("swell.attack")] : t[P("swell.release")] / tide);
    this.freeze = t[P("freeze.on")] > 0.5 ? Math.min(1, this.freeze + dt / 0.4) : Math.max(0, this.freeze - dt / 1.5);
    this.inputFreeze = t[P("input.freeze")] > 0.5 ? Math.min(1, this.inputFreeze + dt / 0.3) : Math.max(0, this.inputFreeze - dt / 2);
    const l = this.looper;
    if (l.state === 1) l.seconds = Math.min(60, l.seconds + dt);
    else if (l.state >= 2) {
      l.pos += dt / Math.max(0.25, l.seconds);
      if (l.pos >= 1) (l.pos -= 1), l.passes++;
      if (l.state === 4) l.state = 0;
    }
    const periods = [17, 19.7, 23.3, 26.3, 29.9, 31.7, 37.1, 41.3];
    const count = Math.round(t[P("loops.count")]);
    for (let k = 0; k < 8; k++) {
      this.loopFlash[k] *= Math.exp(-dt / 0.4);
      if (k >= count) continue;
      this.loopPhase[k] += (dt * t[P("loops.rate")] * tide) / periods[k];
      if (this.loopPhase[k] >= 1) {
        this.loopPhase[k] -= 1;
        if (t[P("loops.on")] > 0.5 && Math.random() < t[P("loops.density")]) this.loopFlash[k] = 1;
      }
    }
    const seasonValues = this.seasonPhase.map((ph, k) => {
      const s = this.seasons[k];
      if (!s) return 0;
      this.seasonPhase[k] = (ph + (dt * tide) / s.period) % 1;
      const p = (this.seasonPhase[k] + s.phase) % 1;
      return s.shape === 1 ? 1 - 4 * Math.abs(p - 0.5) : Math.sin(2 * Math.PI * p);
    });
    const reg = t[P("loops.register")];

    const loud = audible * (0.35 + 0.1 * Math.sin(this.time * 0.7) + this.voices.length * 0.04);
    const msg: TelemetryMessage = {
      t: Math.round(this.time * 48000),
      meter: [loud * (0.9 + 0.1 * Math.random()), loud * (0.9 + 0.1 * Math.random()), loud * 0.3, loud * 0.3],
      limiter: 1,
      fade: [this.fadeGain, this.fadeState],
      panic: this.panic,
      guard: 0,
      load: [0.18 + 0.04 * Math.sin(this.time * 0.3), 0],
      tide,
      key: [Math.round(t[P("harmony.root")]), Math.round(t[P("harmony.scale")]), this.keyMorph],
      medium: Math.round(t[P("medium.type")]),
      sustain: false,
      strips: schema.strips.map((_, k) => [loud * (k === 0 ? 0.8 : k < 5 ? 0.4 : 0.25), loud * (k === 0 ? 0.75 : 0.35)]),
      drone: this.droneLevels.map((l, v) => [l * audible, root + intervals[v]] as [number, number]),
      clouds,
      modes: this.modes.map((m, k) => [m * audible, t[P("res.root")] - 12 + k * 1.7] as [number, number]),
      bloom: {
        loaded: this.samples.bloom !== null,
        voices: Array.from({ length: 8 }, (_, k) => {
          const v = this.voices[k];
          return v ? ([true, v.note, v.level * audible] as [boolean, number, number]) : ([false, 0, 0] as [boolean, number, number]);
        }),
      },
      input: [0, false, this.inputFreeze],
      auto: t[P("master.auto")] > 0.5
        ? [-22 + 2 * Math.sin(this.time * 0.1), [-23, -16, -14][Math.round(t[P("master.autoTarget")])] + 22, 1.5, -1.2, -2.0, 1.05, 0.8, 1]
        : [-22, 0, 0, 0, 0, 1, 0, 0],
      perf: {
        swell: this.swell,
        hush: t[P("hush.hold")],
        slow: t[P("slow.hold")],
        freeze: this.freeze,
        seasons: seasonValues,
        loops: [
          [...this.loopPhase],
          this.loopPhase.map((_, k) => reg + ((k * 5) % 12)),
          [...this.loopFlash],
        ],
        looper: [l.state, l.state === 1 ? l.seconds / 60 : l.pos, l.seconds, l.passes],
        weather: [0.5 + 0.4 * Math.sin(this.time * 0.21), Math.max(0, Math.sin((this.time * Math.PI) / 9))],
      },
      terrain: { cursor: [this.cursor[0], this.cursor[1]], pos: [this.pos[0], this.pos[1]], n: this.scenes.length, version: 0, w: this.weights },
    };

    if (!this.primed) {
      this.sent.fill(NaN);
      this.sentLive.fill(255);
      this.primed = true;
      msg.full = true;
    }
    const p: [number, number][] = [];
    const live: [number, number][] = [];
    for (let i = 0; i < t.length; i++) {
      if (!(Math.abs(t[i] - this.sent[i]) < 1e-6)) {
        p.push([i, t[i]]);
        this.sent[i] = t[i];
      }
      if (this.live[i] !== this.sentLive[i]) {
        live.push([i, this.live[i]]);
        this.sentLive[i] = this.live[i];
      }
    }
    if (p.length) msg.p = p;
    if (live.length) msg.live = live;
    this.emit("telemetry", msg);
  }
}

export function createMockTransport(): Transport {
  const demo = typeof location !== "undefined" && new URLSearchParams(location.search).has("demo");
  const engine = new MockEngine(demo);
  return {
    isMock: true,
    call: (method, ...args) => Promise.resolve(engine.call(method, args)),
    on(event, listener) {
      if (!engine.listeners.has(event)) engine.listeners.set(event, new Set());
      engine.listeners.get(event)!.add(listener);
      return () => engine.listeners.get(event)!.delete(listener);
    },
  };
}
