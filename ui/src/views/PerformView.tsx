import { store, useKey, useSlice } from "../state/store";
import { TerrainView } from "../visuals/TerrainView";
import { Fader } from "../components/Fader";
import { Choice } from "../components/Choice";
import { Knob } from "../components/Knob";
import { Keyboard } from "../components/Keyboard";
import { Button } from "../components/Button";
import { GestureBar } from "./GestureBar";
import "./PerformView.css";

const MEDIUM_CAPTIONS = ["clean", "hiss, wow, warmth", "crackle, dust", "grit, steps"];
const BLOOM_CAPTIONS = ["reverse rise", "stretched", "held moment", "only the tail", "scattered chord", "varispeed"];

function idx(id: string) {
  return store.index(id);
}

function SceneStrip() {
  const { scenes } = useSlice("scenes");
  useKey("telemetry");
  const weights = store.telemetry?.terrain.w ?? [];
  const liveCount = store.live.reduce((a, b) => a + b, 0);
  return (
    <div className="scene-strip">
      {scenes.map((s, k) => (
        <button
          key={k}
          className="scene-chip"
          style={{ ["--w" as string]: (weights[k] ?? 0).toFixed(3) }}
          onClick={() => {
            store.setParamById("terrain.x", s.x);
            store.setParamById("terrain.y", s.y);
          }}
          title="Glide to this scene"
        >
          <span className="scene-chip-fill" />
          <span className="scene-chip-name">{s.name}</span>
        </button>
      ))}
      <button className="scene-chip add" onClick={() => void store.call("scene.capture")} title="C: capture what you hear as a scene at the cursor">
        + Capture
      </button>
      {liveCount > 0 && (
        <button className="scene-chip live" onClick={() => void store.call("scene.releaseLive")} title="R: hand held controls back to the terrain">
          Release {liveCount} held
        </button>
      )}
    </div>
  );
}

function AutoMasterToggle() {
  const i = idx("master.auto");
  useKey(`p:${i}`);
  const on = store.params[i] > 0.5;
  const a = store.telemetry?.auto ?? [];
  const target = ["-23", "-16", "-14"][Math.round(store.value("master.autoTarget"))];
  return (
    <button
      className={`auto-master${on ? " on" : ""}`}
      onClick={() => store.setParam(i, on ? 0 : 1)}
      title="Auto master: listens to the mix and eases tone, glue, width and loudness toward a finished sound. Settings in Edit, Master."
    >
      <span className="auto-dot" />
      <span className="auto-title">Auto master</span>
      <span className="auto-read">{on ? `${target} LUFS, ${(a[1] ?? 0) >= 0 ? "+" : ""}${(a[1] ?? 0).toFixed(1)} dB` : "off"}</span>
    </button>
  );
}

function MasterBlock() {
  useKey("telemetry");
  const t = store.telemetry;
  const state = t?.fade[1] ?? 0;
  const panic = t?.panic ?? false;
  const label = panic ? "Silenced" : ["Fade in", "Fading in", "Fade out", "Fading out"][state];
  const progress = t?.fade[0] ?? 0;
  return (
    <div className="master-block">
      <button
        className={`fade-button state-${state}${panic ? " panic" : ""}`}
        onClick={() => store.command(panic ? "resume" : "fadeToggle")}
        title="Space"
      >
        <span className="fade-progress" style={{ transform: `scaleX(${progress})` }} />
        <span className="fade-label">{panic ? "Resume" : label}</span>
      </button>
      <AutoMasterToggle />
      <div className="master-row">
        <Knob param={idx("master.fadeSeconds")} label="Fade length" size="sm" />
        <Knob param={idx("master.level")} label="Master" size="sm" />
        <Button tone="danger" size="md" onClick={() => store.command("panicToggle")} title="Esc: fast fade to silence and reset">
          {panic ? "Resume" : "Panic"}
        </Button>
      </div>
    </div>
  );
}

export function PerformView() {
  useSlice("schema");
  if (!store.schema) return null;
  return (
    <div className="perform fade-in">
      <aside className="perform-left">
        <div className="faders">
          <Fader param={idx("tide.rate")} label="Tide" hint="How fast everything drifts" />
          <Fader param={idx("terrain.wander")} label="Wander" hint="How far the sound roams on its own" />
          <Fader param={idx("harmony.gravity")} label="Gravity" hint="How strongly pitches settle into the key" />
        </div>
        <div className="key-block">
          <Choice param={idx("harmony.root")} label="Key" variant="pills" columns={6} />
          <div className="scale-pick">
            <Choice param={idx("harmony.scale")} label="" variant="pills" columns={2} />
          </div>
        </div>
      </aside>

      <main className="perform-center">
        <TerrainView />
        <div className="center-foot">
          <SceneStrip />
          <Choice param={idx("terrain.wanderStyle")} label="" variant="pills" />
        </div>
        <GestureBar />
      </main>

      <aside className="perform-right">
        <Choice param={idx("medium.type")} label="Recording type" variant="tiles" captions={MEDIUM_CAPTIONS} />
        <div className="knob-row secondary">
          <Knob param={idx("medium.age")} size="sm" />
          <Knob param={idx("medium.noise")} size="sm" />
          <Knob param={idx("medium.wobble")} size="sm" />
        </div>

        <button className="catch-button" onClick={() => store.command("catch")} title="K: capture the last seconds into a cloud">
          <span className="catch-title">Catch</span>
          <span className="catch-sub">last {Math.round(store.value("catch.seconds"))} s into a cloud</span>
        </button>

        <div className="bloom-tiles" title={BLOOM_CAPTIONS.join(" / ")}>
          <Choice param={idx("bloom.transform")} label="Bloom" variant="tiles" />
        </div>
        <div className="knob-row secondary">
          <Knob param={idx("bloom.amount")} size="sm" />
          <Knob param={idx("bloom.length")} size="sm" />
          <Knob param={idx("bloom.level")} label="Level" size="sm" />
        </div>

        <MasterBlock />
      </aside>

      <footer className="perform-keys">
        <Keyboard low={41} octaves={4} />
      </footer>
    </div>
  );
}
