import type { ReactNode } from "react";
import { store, useKey, useParam, useSlice } from "../state/store";
import { Knob } from "../components/Knob";
import { Choice } from "../components/Choice";
import { Section } from "../components/Section";
import { Button } from "../components/Button";
import { Meter } from "../components/Meter";
import { Keyboard } from "../components/Keyboard";
import { Waveform } from "../visuals/Waveform";
import { PitchLane } from "../visuals/Lanes";
import { formatDisplay } from "../lib/format";
import type { StripSpec } from "../bridge/types";
import "./EditView.css";

const idx = (id: string) => store.index(id);
const knobs = (ids: string[]) => ids.map((id) => <Knob key={id} param={idx(id)} />);

const PAGES: { group: string; items: { id: string; label: string }[] }[] = [
  { group: "Global", items: [{ id: "global", label: "Master, terrain, key" }] },
  {
    group: "Sources",
    items: [
      { id: "drone", label: "Drone" },
      { id: "cloud0", label: "Cloud 1" },
      { id: "cloud1", label: "Cloud 2" },
      { id: "cloud2", label: "Cloud 3" },
      { id: "cloud3", label: "Cloud 4" },
      { id: "res", label: "Resonator" },
      { id: "bloom", label: "Bloom" },
      { id: "input", label: "Live input" },
    ],
  },
  {
    group: "Performance",
    items: [
      { id: "gestures", label: "Gestures" },
      { id: "looper", label: "Looper" },
      { id: "loops", label: "Loops" },
      { id: "weather", label: "Weather" },
      { id: "seasons", label: "Seasons" },
    ],
  },
  { group: "Mix", items: [{ id: "mixer", label: "Mixer" }, { id: "fx", label: "Effects" }] },
  { group: "Setup", items: [{ id: "scenes", label: "Scenes" }, { id: "midi", label: "MIDI" }] },
];

function StripSection({ stripId }: { stripId: string }) {
  const strip = store.schema!.strips.find((s) => s.id === stripId)!;
  return (
    <Section title="Mix" actions={<Meter strip={store.schema!.strips.indexOf(strip)} />}>
      <Knob param={strip.level} />
      <Knob param={strip.pan} />
      <Knob param={strip.width} />
      <Knob param={strip.sendA} />
      <Knob param={strip.sendB} />
    </Section>
  );
}

function Page({ title, subtitle, children }: { title: string; subtitle?: string; children: ReactNode }) {
  return (
    <div className="edit-page fade-in">
      <header className="edit-page-head">
        <h1>{title}</h1>
        {subtitle && <p>{subtitle}</p>}
      </header>
      {children}
    </div>
  );
}

function GlobalPage() {
  return (
    <Page title="Master, terrain and key" subtitle="The controls that shape everything at once.">
      <div className="grid-2">
        <Section title="Master">{knobs(["master.level", "master.fadeSeconds", "master.ceiling", "busA.level", "busB.level"])}</Section>
        <Section title="Terrain">{knobs(["terrain.glide", "terrain.focus", "terrain.wander", "terrain.wanderRate"])}</Section>
        <Section title="Tide and key">
          {knobs(["tide.rate", "harmony.gravity", "harmony.morph"])}
          <div className="wide">
            <Choice param={idx("harmony.root")} label="Key" columns={6} />
          </div>
          <div className="wide">
            <Choice param={idx("harmony.scale")} label="Scale" columns={3} />
          </div>
        </Section>
        <Section title="Recording type">
          <div className="wide">
            <Choice param={idx("medium.type")} label="" />
          </div>
          {knobs(["medium.age", "medium.noise", "medium.wobble", "medium.drive", "medium.mix"])}
        </Section>
        <AutoMasterSection />
        <Section title="Catch">
          {knobs(["catch.seconds"])}
          <div className="wide">
            <Choice param={idx("catch.source")} label="From" />
          </div>
          <div className="wide">
            <Choice param={idx("catch.target")} label="Into" />
          </div>
        </Section>
        <Section title="Wander style">
          <div className="wide">
            <Choice param={idx("terrain.wanderStyle")} label="" />
          </div>
          <p className="note">Drift roams around the cursor. Orbit circles it. Tide pool settles into nearby scenes, lingers, then drifts on.</p>
        </Section>
      </div>
    </Page>
  );
}

function AutoMasterSection() {
  useKey("telemetry");
  const a = store.telemetry?.auto ?? [];
  const on = (a[7] ?? 0) > 0;
  const bar = (label: string, db: number) => (
    <div className="am-band" key={label}>
      <span>{label}</span>
      <div className="am-track">
        <div className="am-fill" style={{ left: db < 0 ? `${50 + (db / 12) * 100}%` : "50%", width: `${(Math.abs(db) / 12) * 100}%` }} />
      </div>
      <span className="am-num">
        {db >= 0 ? "+" : ""}
        {db.toFixed(1)} dB
      </span>
    </div>
  );
  return (
    <Section title="Auto master">
      <div className="wide">
        <Choice param={idx("master.auto")} label="" />
      </div>
      <div className="wide">
        <Choice param={idx("master.autoTarget")} label="Loudness target" />
      </div>
      {knobs(["master.autoAmount"])}
      <div className="wide am-readout">
        <div className="am-row">
          <span className="am-pair">
            In <span className="am-num">{on ? `${(a[0] ?? -70).toFixed(1)} LUFS` : "-"}</span>
          </span>
          <span className="am-pair">
            Make-up <span className="am-num">{on ? `${(a[1] ?? 0).toFixed(1)} dB` : "-"}</span>
          </span>
          <span className="am-pair">
            Glue <span className="am-num">{on ? `${(a[6] ?? 0).toFixed(1)} dB` : "-"}</span>
          </span>
        </div>
        {bar("Lows", on ? (a[2] ?? 0) : 0)}
        {bar("Mud", on ? (a[3] ?? 0) : 0)}
        {bar("Air", on ? (a[4] ?? 0) : 0)}
      </div>
      <p className="note">Listens to the mix and eases its tone, glue, width and level toward a finished sound, over seconds, never chasing notes. It never lifts silence.</p>
    </Section>
  );
}

function DronePage() {
  return (
    <Page title="Drone" subtitle="Six breathing voices around a root. They fade, drift and re-voice on their own.">
      <PitchLane source="drone" />
      <div className="grid-2">
        <Section title="Tone">{knobs(["drone.root", "drone.cutoff", "drone.resonance", "drone.detune", "drone.shape", "drone.noise", "drone.gravity"])}</Section>
        <Section title="Motion">{knobs(["drone.density", "drone.evolve", "drone.driftDepth", "drone.driftRate", "drone.spread"])}</Section>
      </div>
      <StripSection stripId="drone" />
    </Page>
  );
}

function SampleHeader({ slot }: { slot: number }) {
  const { samples } = useSlice("samples");
  const name = slot === 4 ? samples.bloom : samples.clouds[slot];
  return (
    <div className="sample-head">
      <div>
        <span className="caps">Sample</span>
        <div className="sample-name">{name ?? "Empty"}</div>
      </div>
      <div className="sample-actions">
        <Button size="sm" onClick={() => void store.call("sample.load", slot)}>
          Load...
        </Button>
        {name && (
          <Button size="sm" tone="ghost" onClick={() => void store.call("sample.clear", slot)}>
            Clear
          </Button>
        )}
      </div>
    </div>
  );
}

function CloudPage({ cloud }: { cloud: number }) {
  const first = store.schema!.cloudFirstParam[cloud];
  const p = (o: number) => <Knob key={o} param={first + o} />;
  return (
    <Page title={`Cloud ${cloud + 1}`} subtitle="Grains read from a sample, scattered, pitched and scanned. Catch fills clouds from the output.">
      <SampleHeader slot={cloud} />
      <Waveform slot={cloud} positionParam={first + 2} />
      <div className="grid-3">
        <Section title="Grains">{[0, 1, 9, 10].map(p)}</Section>
        <Section title="Where">{[2, 3, 4, 8].map(p)}</Section>
        <Section title="Pitch">{[5, 6, 7, 11].map(p)}</Section>
      </div>
      <StripSection stripId={`cloud${cloud + 1}`} />
    </Page>
  );
}

function ResonatorPage() {
  return (
    <Page title="Resonator" subtitle="Tuned modes that ring when struck: by their own rain, or by any other source.">
      <PitchLane source="modes" />
      <div className="grid-3">
        <Section title="Tuning">{knobs(["res.root", "res.modes", "res.structure", "res.gravity"])}</Section>
        <Section title="Body">{knobs(["res.decay", "res.brightness", "res.spread"])}</Section>
        <Section title="Excitation">{knobs(["res.rain", "res.rainColour", "res.exciteInput", "res.exciteDrone", "res.exciteClouds", "res.exciteBloom"])}</Section>
      </div>
      <StripSection stripId="res" />
    </Page>
  );
}

function BloomPage() {
  return (
    <Page title="Bloom" subtitle="A keyboard that turns ordinary one-shots into ambient material.">
      <SampleHeader slot={4} />
      <Waveform slot={4} />
      <Section title="Transform">
        <div className="wide">
          <Choice param={idx("bloom.transform")} label="" />
        </div>
      </Section>
      <div className="grid-3">
        <Section title="Shape">{knobs(["bloom.amount", "bloom.length", "bloom.attack", "bloom.release"])}</Section>
        <Section title="Pitch">{knobs(["bloom.root", "bloom.pitch", "bloom.gravity", "bloom.random"])}</Section>
        <Section title="Colour">{knobs(["bloom.tone", "bloom.spread", "bloom.position"])}</Section>
      </div>
      <div className="mini-keys">
        <Keyboard low={48} octaves={3} />
      </div>
      <StripSection stripId="bloom" />
    </Page>
  );
}

function InputPage() {
  useKey("telemetry");
  const level = store.telemetry?.input[0] ?? 0;
  const gate = store.telemetry?.input[1] ?? false;
  return (
    <Page title="Live input" subtitle="Guitar, cello, voice: through the interface into the mix, the resonator and Catch.">
      <div className="grid-2">
        <Section title="Monitor">
          <div className="wide">
            <Choice param={idx("input.armed")} label="Heard in the mix" />
          </div>
          <div className="wide">
            <Choice param={idx("input.channel")} label="Channel" />
          </div>
          <div className="input-level">
            <div className="input-level-bar" style={{ transform: `scaleX(${Math.min(1, level * 2)})` }} />
            <span className={gate ? "gate open" : "gate"}>{gate ? "Gate open" : "Gate closed"}</span>
          </div>
        </Section>
        <Section title="Conditioning">{knobs(["input.gain", "input.highPass", "input.gate"])}</Section>
        <Section title="Spectral hold">
          <div className="wide">
            <Choice param={idx("input.freeze")} label="Hold the input (I)" />
          </div>
          {knobs(["input.freezeLevel", "input.freezeDrift"])}
          <p className="note">Freezes the sound of the input as a wide spectral pad that sustains on its own, heard even when the monitor is off.</p>
        </Section>
      </div>
      <p className="note">The input always feeds the resonator (From Input) and can be caught (Catch, From: Live input), even while it is not heard.</p>
      <StripSection stripId="input" />
    </Page>
  );
}

function GesturesPage() {
  return (
    <Page title="Gestures" subtitle="Moves for a whole room at once: hold to swell, freeze the moment, hold the input.">
      <div className="grid-3">
        <Section title="Swell (hold S)">
          {knobs(["swell.depth", "swell.attack", "swell.release"])}
          <p className="note">While held, every send blooms, the drone, resonator and Bloom open up and the clouds thicken. The ebb follows Tide.</p>
        </Section>
        <Section title="Freeze all (F)">
          {knobs(["freeze.duck", "freeze.texture"])}
          <p className="note">Holds the last two seconds of the mix as a granular cloud. Duck sets how far everything else steps back underneath.</p>
        </Section>
        <Section title="Hold input (I)">
          {knobs(["input.freezeLevel", "input.freezeDrift"])}
        </Section>
      </div>
      <StripSection stripId="freeze" />
    </Page>
  );
}

const LOOPER_STATES = ["Empty", "Recording", "Playing", "Overdubbing", "Clearing"];

function LooperPage() {
  useKey("telemetry");
  const [state, pos, seconds, passes] = store.telemetry?.perf.looper ?? [0, 0, 0, 0];
  return (
    <Page title="Looper" subtitle="A disintegrating tape loop. Every pass rewrites the tape a little worse: darker, quieter, flaking.">
      <div className="looper-status">
        <div className="looper-bar" style={{ transform: `scaleX(${pos})` }} />
        <span>
          {LOOPER_STATES[state]}
          {state >= 2 && `: ${seconds.toFixed(1)} s, pass ${passes}`}
        </span>
      </div>
      <div className="row">
        <Button tone="accent" onClick={() => store.command("loopRecord")}>
          {["Record", "Close the loop", "Overdub", "Stop overdubbing", "Record"][state]}
        </Button>
        <Button onClick={() => store.command("loopClear")}>Clear</Button>
      </div>
      <div className="grid-2">
        <Section title="Tape">
          <div className="wide">
            <Choice param={idx("loop.source")} label="Records" />
          </div>
          {knobs(["loop.erosion", "loop.flakes", "loop.overdub"])}
          <p className="note">Erosion 0 repeats the loop exactly. Flakes are moments where the oxide comes away for good.</p>
        </Section>
        <StripSection stripId="loop" />
      </div>
    </Page>
  );
}

function LoopsPage() {
  useKey("telemetry");
  const [phases, notes, flashes] = store.telemetry?.perf.loops ?? [[], [], []];
  const count = Math.round(store.value("loops.count"));
  const names = store.schema!.noteNames;
  return (
    <Page title="Loops" subtitle="Music for Airports: each voice repeats one note on its own long cycle, so the pattern never comes round the same way twice.">
      <div className="loops-lanes">
        {Array.from({ length: count }, (_, k) => (
          <div className="loops-lane" key={k}>
            <span className="loops-note">{names[Math.round(notes[k] ?? 0) % 12] ?? ""}{Math.floor(Math.round(notes[k] ?? 0) / 12) - 1}</span>
            <div className="loops-track">
              <div className="loops-head" style={{ left: `${(phases[k] ?? 0) * 100}%`, opacity: 0.4 + (flashes[k] ?? 0) * 0.6 }} />
            </div>
          </div>
        ))}
      </div>
      <div className="grid-2">
        <Section title="Voices">
          <div className="wide">
            <Choice param={idx("loops.on")} label="Playing (E)" />
          </div>
          {knobs(["loops.count", "loops.rate", "loops.density", "loops.velocity"])}
        </Section>
        <Section title="Notes">
          {knobs(["loops.register", "loops.spread", "loops.pattern"])}
          <div className="wide">
            <Choice param={idx("loops.target")} label="Play into" />
          </div>
        </Section>
      </div>
    </Page>
  );
}

function WeatherPage() {
  return (
    <Page title="Weather" subtitle="Wind, rain and surf from shaped noise. Never loops, never repeats.">
      <div className="grid-2">
        <Section title="Elements">{knobs(["weather.wind", "weather.rain", "weather.surf"])}</Section>
        <Section title="Character">{knobs(["weather.gust", "weather.tone", "weather.distance"])}</Section>
      </div>
      <StripSection stripId="weather" />
    </Page>
  );
}

const PERIODS = [30, 60, 120, 300, 600, 1200, 1800, 3600];
/** What "Add a season" proposes next: things that change a piece slowly and well,
 *  on staggered cycles so they never line up. */
const SEASON_SUGGESTIONS: [string, number, number][] = [
  ["drone.cutoff", 0.25, 300],
  ["cloud1.position", 0.3, 600],
  ["res.brightness", 0.3, 1200],
  ["medium.age", 0.35, 1800],
  ["busA.level", 0.15, 600],
  ["drone.density", 0.3, 1200],
  ["bloom.tone", 0.25, 300],
  ["weather.wind", 0.3, 1800],
];
const SHAPES = ["Sine", "Triangle", "Drift"];

function paramLabel(i: number) {
  const schema = store.schema!;
  const p = schema.params[i];
  const slot = schema.fxSlots.find((s) => i >= s.first && i < s.first + 7);
  if (slot) return `${slot.name}: ${p.name}`;
  const prefix = p.id.split(".")[0];
  const strip = schema.strips.find((s) => s.id === prefix);
  const group = strip ? strip.name : prefix.charAt(0).toUpperCase() + prefix.slice(1);
  return `${group}: ${p.name}`;
}

function SeasonsPage() {
  const { seasons, schema } = useSlice("seasons");
  useKey("telemetry");
  const values = store.telemetry?.perf.seasons ?? [];
  const options = schema!.params.filter((p) => !p.discrete);
  const update = (k: number, patch: Partial<(typeof seasons)[number]>) => void store.call("season.set", k, { ...seasons[k], ...patch });
  return (
    <Page title="Seasons" subtitle="Very slow curves, minutes long, that move a parameter for you. The whole piece changes like weather over an afternoon.">
      <div className="season-list">
        {seasons.map((s, k) => (
          <div className="season-row" key={k}>
            <select value={s.param} onChange={(e) => update(k, { param: Number(e.target.value) })}>
              {options.map((p) => (
                <option key={p.i} value={p.i}>
                  {paramLabel(p.i)}
                </option>
              ))}
            </select>
            <label className="field">
              Depth
              <input type="range" min={-100} max={100} value={Math.round(s.depth * 100)} onChange={(e) => update(k, { depth: Number(e.target.value) / 100 })} />
              <span className="season-num">{Math.round(s.depth * 100)}%</span>
            </label>
            <select value={PERIODS.reduce((a, b) => (Math.abs(b - s.period) < Math.abs(a - s.period) ? b : a))} onChange={(e) => update(k, { period: Number(e.target.value) })}>
              {PERIODS.map((p) => (
                <option key={p} value={p}>
                  {p < 60 ? `${p} s` : `${p / 60} min`} cycle
                </option>
              ))}
            </select>
            <select value={s.shape} onChange={(e) => update(k, { shape: Number(e.target.value) })}>
              {SHAPES.map((name, v) => (
                <option key={v} value={v}>
                  {name}
                </option>
              ))}
            </select>
            <div className="season-meter">
              <div className="season-meter-dot" style={{ left: `${50 + (values[k] ?? 0) * 50 * Math.sign(s.depth || 1)}%` }} />
            </div>
            <button className="x" onClick={() => void store.call("season.remove", k)} title="Remove">
              ×
            </button>
          </div>
        ))}
        {seasons.length === 0 && <p className="note">No seasons yet. Add one, pick what it moves, and let it run for the length of the piece.</p>}
      </div>
      <div className="row">
        <Button
          tone="accent"
          disabled={seasons.length >= 8}
          onClick={() => {
            const used = new Set(seasons.map((s) => s.param));
            const pick = SEASON_SUGGESTIONS.find(([id]) => !used.has(store.index(id))) ?? SEASON_SUGGESTIONS[0];
            void store.call("season.set", seasons.length, { param: store.index(pick[0]), depth: pick[1], period: pick[2], shape: seasons.length % 3, phase: 0 });
          }}
        >
          Add a season
        </Button>
        <Knob param={idx("seasons.depth")} label="All seasons" size="sm" />
      </div>
    </Page>
  );
}

function MixerStrip({ strip, index }: { strip: StripSpec; index: number }) {
  return (
    <div className="mix-strip">
      <div className="mix-name">{strip.name}</div>
      <div className="mix-meter">
        <Meter strip={index} orientation="vertical" />
      </div>
      <Knob param={strip.level} size="sm" label="Level" />
      <Knob param={strip.pan} size="sm" label="Pan" />
      <Knob param={strip.sendA} size="sm" label="Reverb" />
      <Knob param={strip.sendB} size="sm" label="Delay" />
    </div>
  );
}

function MixerPage() {
  const schema = store.schema!;
  return (
    <Page title="Mixer" subtitle="Every source, its sends to the reverb and delay buses, and the returns.">
      <div className="mixer">
        {schema.strips.map((s, k) => (
          <MixerStrip key={s.id} strip={s} index={k} />
        ))}
        <div className="mix-strip returns">
          <div className="mix-name">Returns</div>
          <Knob param={idx("busA.level")} size="sm" label="Reverb" />
          <Knob param={idx("busB.level")} size="sm" label="Delay" />
          <Knob param={idx("master.level")} size="sm" label="Master" />
        </div>
      </div>
    </Page>
  );
}

function FxControl({ param, format, name }: { param: number; format: (v: number) => string; name: string }) {
  useParam(param);
  return <Knob param={param} label={name} format={format} size="sm" />;
}

function FxSlotCard({ slot }: { slot: number }) {
  const { fx, schema } = useSlice("fx");
  const info = schema!.fxSlots[slot];
  const type = fx[slot] ?? "";
  const proc = schema!.processors.find((p) => p.type === type);
  return (
    <div className={`fx-card${proc ? "" : " empty"}`}>
      <div className="fx-head">
        <span className="fx-name">{info.name}</span>
        <select value={type} onChange={(e) => void store.call("fx.setType", slot, e.target.value)}>
          <option value="">Empty</option>
          {schema!.processors.map((p) => (
            <option key={p.type} value={p.type}>
              {p.name}
            </option>
          ))}
        </select>
      </div>
      {proc && (
        <div className="fx-controls">
          {proc.controls.map((c, k) =>
            c.display.curve === "hidden" ? null : (
              <FxControl key={k} param={info.first + k} name={c.name} format={(v) => formatDisplay(c.display, v)} />
            ),
          )}
          <Knob param={info.first + 6} label="Mix" size="sm" />
        </div>
      )}
    </div>
  );
}

function FxPage() {
  const schema = store.schema!;
  const buses = [schema.busASlot, schema.busASlot + 1, schema.busBSlot, schema.busBSlot + 1, schema.masterSlot, schema.masterSlot + 1];
  return (
    <Page title="Effects" subtitle="Any effect in any slot. Swaps crossfade, so you can change them while playing.">
      <h2 className="sub">Buses and master</h2>
      <div className="fx-grid">{buses.map((s) => <FxSlotCard key={s} slot={s} />)}</div>
      <h2 className="sub">Source inserts</h2>
      <div className="fx-grid">{schema.strips.flatMap((st) => st.fx).map((s) => <FxSlotCard key={s} slot={s} />)}</div>
    </Page>
  );
}

function ScenesPage() {
  const { scenes } = useSlice("scenes");
  useKey("telemetry");
  const w = store.telemetry?.terrain.w ?? [];
  return (
    <Page title="Scenes" subtitle="Places on the terrain. The sound blends between the ones nearest the cursor.">
      <div className="scene-list">
        {scenes.map((s, k) => (
          <div className="scene-row" key={k}>
            <div className="scene-weight" style={{ transform: `scaleX(${w[k] ?? 0})` }} />
            <input
              className="scene-input"
              defaultValue={s.name}
              key={s.name}
              onBlur={(e) => e.target.value !== s.name && void store.call("scene.rename", k, e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && (e.target as HTMLInputElement).blur()}
            />
            <span className="scene-pos">
              {s.x.toFixed(2)}, {s.y.toFixed(2)}
            </span>
            <Button size="sm" tone="ghost" onClick={() => (store.setParamById("terrain.x", s.x), store.setParamById("terrain.y", s.y))}>
              Glide here
            </Button>
            <Button size="sm" tone="ghost" onClick={() => void store.call("scene.commit", k)}>
              Commit live
            </Button>
            <Button size="sm" tone="ghost" onClick={() => void store.call("scene.replace", k)}>
              Replace
            </Button>
            <Button size="sm" tone="danger" onClick={() => void store.call("scene.remove", k)}>
              Delete
            </Button>
          </div>
        ))}
        {scenes.length === 0 && <p className="note">No scenes yet. Shape a sound and capture it; then shape another and capture it somewhere else.</p>}
      </div>
      <div className="row">
        <Button tone="accent" onClick={() => void store.call("scene.capture")}>
          Capture the current sound
        </Button>
        <Button onClick={() => void store.call("scene.releaseLive")}>Release live layer</Button>
      </div>
    </Page>
  );
}

function MidiPage() {
  const { midi } = useSlice("midi");
  useKey("midiActivity");
  const m = store.lastMidi;
  const describe = () => {
    if (!m) return "No MIDI received yet";
    const type = m.status & 0xf0;
    const ch = (m.status & 0x0f) + 1;
    if (type === 0xb0) return `CC ${m.d1} = ${m.d2}, channel ${ch}`;
    if (type === 0x90 && m.d2 > 0) return `Note ${m.d1} on, velocity ${m.d2}, channel ${ch}`;
    if (type === 0x80 || type === 0x90) return `Note ${m.d1} off, channel ${ch}`;
    return `Status 0x${m.status.toString(16)}`;
  };
  const actions: [string, string][] = [
    ["catch", "Catch"],
    ["fadeToggle", "Fade in/out"],
    ["panic", "Panic"],
    ["releaseLive", "Release live layer"],
    ["captureScene", "Capture scene"],
    ["recordToggle", "Record"],
    ["loopRecord", "Loop record"],
    ["loopClear", "Loop clear"],
    ["freezeToggle", "Freeze all"],
    ["inputFreezeToggle", "Hold input"],
  ];
  return (
    <Page title="MIDI" subtitle="Right-click any control to learn it. Controllers pick up softly: an arrow shows which way to turn until they catch the value.">
      <div className="grid-2">
        <Section title="Devices">
          <div className="wide col">
            {midi.devices.length === 0 && <p className="note">No MIDI inputs found. Plug a controller in; it appears here.</p>}
            {midi.devices.map((d) => (
              <label key={d.id} className="check">
                <input type="checkbox" checked={d.enabled} onChange={(e) => void store.call("midi.setDevice", d.id, e.target.checked)} />
                {d.name}
                {d.enabled && !d.open && <span className="warn"> (could not open)</span>}
              </label>
            ))}
            <div className="activity">{describe()}</div>
          </div>
        </Section>
        <Section title="Notes">
          <div className="wide col">
            <label className="field">
              Channel
              <select value={midi.noteChannel} onChange={(e) => void store.call("midi.setNoteChannel", Number(e.target.value))}>
                <option value={-1}>Any</option>
                {Array.from({ length: 16 }, (_, k) => (
                  <option key={k} value={k}>
                    {k + 1}
                  </option>
                ))}
              </select>
            </label>
            <label className="check">
              <input type="checkbox" checked={midi.notesToDrone} onChange={(e) => void store.call("midi.setNotesToDrone", e.target.checked)} />
              Notes also set the drone root
            </label>
            <p className="note">Notes play Bloom. The sustain pedal (CC 64) holds Bloom notes.</p>
          </div>
        </Section>
        <Section title="Buttons and pads">
          <div className="wide action-grid">
            {actions.map(([id, label]) => (
              <Button key={id} size="sm" onClick={() => void store.call("midi.learnAction", id)}>
                Learn {label}
              </Button>
            ))}
          </div>
          {midi.learning && (
            <div className="wide learning">
              Learning: move a controller{midi.learnParam === null ? " or press a pad" : ""}.{" "}
              <Button size="sm" tone="ghost" onClick={() => void store.call("midi.cancelLearn")}>
                Cancel
              </Button>
            </div>
          )}
        </Section>
        <Section
          title="Mappings"
          actions={
            <>
              <Button size="sm" tone="ghost" onClick={() => void store.call("midi.defaults")}>
                Default 8 knobs
              </Button>
              <Button size="sm" tone="ghost" onClick={() => void store.call("midi.clearAll")}>
                Clear all
              </Button>
            </>
          }
        >
          <div className="wide col bindings">
            {midi.bindings.map((b, k) => (
              <div className="binding" key={k}>
                <span>{b.text}</span>
                <button className="x" onClick={() => void store.call("midi.remove", k)} title="Remove">
                  ×
                </button>
              </div>
            ))}
            {midi.bindings.length === 0 && <p className="note">No mappings.</p>}
          </div>
        </Section>
      </div>
    </Page>
  );
}

function Content({ page }: { page: string }) {
  if (page === "global") return <GlobalPage />;
  if (page === "drone") return <DronePage />;
  if (page.startsWith("cloud")) return <CloudPage key={page} cloud={Number(page.slice(5))} />;
  if (page === "res") return <ResonatorPage />;
  if (page === "bloom") return <BloomPage />;
  if (page === "input") return <InputPage />;
  if (page === "gestures") return <GesturesPage />;
  if (page === "looper") return <LooperPage />;
  if (page === "loops") return <LoopsPage />;
  if (page === "weather") return <WeatherPage />;
  if (page === "seasons") return <SeasonsPage />;
  if (page === "mixer") return <MixerPage />;
  if (page === "fx") return <FxPage />;
  if (page === "scenes") return <ScenesPage />;
  if (page === "midi") return <MidiPage />;
  return null;
}

export function EditView() {
  useSlice("view");
  useSlice("samples");
  if (!store.schema) return null;
  const page = store.editPage;
  return (
    <div className="edit">
      <nav className="edit-nav">
        {PAGES.map((g) => (
          <div key={g.group} className="nav-group">
            <div className="caps nav-title">{g.group}</div>
            {g.items.map((it) => {
              const cloud = it.id.startsWith("cloud") ? Number(it.id.slice(5)) : -1;
              const loaded = cloud >= 0 && store.samples.clouds[cloud];
              return (
                <button key={it.id} className={`nav-item${page === it.id ? " on" : ""}`} onClick={() => store.setView("edit", it.id)}>
                  {it.label}
                  {cloud >= 0 && <span className={`dot${loaded ? " on" : ""}`} />}
                </button>
              );
            })}
          </div>
        ))}
      </nav>
      <div className="edit-content">
        <Content page={page} />
      </div>
    </div>
  );
}
