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
            <Choice param={idx("harmony.root")} label="Key" columns={12} />
          </div>
          <div className="wide">
            <Choice param={idx("harmony.scale")} label="Scale" columns={6} />
          </div>
        </Section>
        <Section title="Recording type">
          <div className="wide">
            <Choice param={idx("medium.type")} label="" />
          </div>
          {knobs(["medium.age", "medium.noise", "medium.wobble", "medium.drive", "medium.mix"])}
        </Section>
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
      </div>
      <p className="note">The input always feeds the resonator (From Input) and can be caught (Catch, From: Live input), even while it is not heard.</p>
      <StripSection stripId="input" />
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
