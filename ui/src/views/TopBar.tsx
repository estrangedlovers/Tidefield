import { useEffect, useRef } from "react";
import { store, useSlice } from "../state/store";
import { Meter } from "../components/Meter";
import { openMenu } from "../components/ContextMenu";
import "./TopBar.css";

const GUARD_HINT =
  "CPU guardrails are lightening the load: fewer grains per cloud, resonator modes, drone and Bloom voices. Full quality returns by itself when there is headroom.";

/** DSP load and guardrail level, read from telemetry a few times a second without
 *  re-rendering React. */
function CpuReadout() {
  const text = useRef<HTMLSpanElement>(null);
  const badge = useRef<HTMLSpanElement>(null);
  useEffect(() => {
    let raf = 0;
    let last = 0;
    const draw = (now: number) => {
      if (now - last > 250) {
        last = now;
        const t = store.telemetry;
        const load = t?.load?.[0] || store.session.cpu;
        const level = t?.load?.[1] ?? 0;
        const pct = Math.round(load * 100);
        if (text.current) {
          text.current.textContent = `${pct}% CPU`;
          text.current.classList.toggle("hot", pct > 70);
        }
        if (badge.current) {
          badge.current.style.display = level > 0 ? "" : "none";
          badge.current.textContent = `lite ${level}`;
        }
      }
      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, []);
  return (
    <>
      <span ref={text} className="cpu" title="Share of the audio deadline the engine uses" />
      <span ref={badge} className="guard-badge" title={GUARD_HINT} style={{ display: "none" }} />
    </>
  );
}

function clock(seconds: number) {
  const s = Math.max(0, Math.floor(seconds));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const ss = String(s % 60).padStart(2, "0");
  return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${ss}` : `${m}:${ss}`;
}

function RecordButton() {
  const { record } = useSlice("record");
  const on = record.state === "recording";
  const finishing = record.state === "finishing";

  const menu = (e: { clientX: number; clientY: number }) =>
    openMenu(
      e,
      [
        {
          label: `${record.stems ? "✓ " : ""}Record stems too`,
          onSelect: () => void store.call("record.setStems", !record.stems),
          disabled: record.state !== "idle",
        },
        { label: "Recordings folder...", onSelect: () => void store.call("record.chooseFolder") },
        { label: record.last ? "Show last recording" : "Show recordings", onSelect: () => void store.call("record.reveal") },
      ],
      "Recording",
    );

  return (
    <button
      className={`record-btn${on ? " on" : ""}${finishing ? " finishing" : ""}`}
      onClick={() => void store.call("record.toggle")}
      onContextMenu={(e) => {
        e.preventDefault();
        menu(e);
      }}
      title={
        on
          ? "Stop recording (Shift+R)"
          : `Record what you hear to ${record.folder || "disk"}${record.stems ? ", with stems" : ""} (Shift+R). Right-click for options.`
      }
    >
      <span className="rec-dot" />
      <span className="rec-label">{on ? clock(record.seconds) : finishing ? "Saving" : "Rec"}</span>
      {record.stems && <span className="rec-stems">stems</span>}
      {on && record.dropped > 0 && (
        <span className="rec-warn" title="The disk fell behind and some audio was lost">
          !
        </span>
      )}
    </button>
  );
}

export function TopBar() {
  const { session, view } = useSlice("session");
  useSlice("view");

  return (
    <header className="topbar">
      <div className="topbar-left">
        <div className="wordmark">Tidefield</div>
        <button
          className="session-name"
          onClick={(e) => {
            const r = e.currentTarget.getBoundingClientRect();
            openMenu(
              { clientX: r.left, clientY: r.bottom + 6 },
              [
                { label: "New session", onSelect: () => void store.call("session.new") },
                { label: "Open...", onSelect: () => void store.call("session.open") },
                "-",
                { label: "Save", onSelect: () => void store.call("session.save") },
                { label: "Save as...", onSelect: () => void store.call("session.saveAs") },
              ],
              "Session",
            );
          }}
          title="New, open and save (Cmd+N, Cmd+O, Cmd+S)"
        >
          {session.name}
          <span className="chev">⌄</span>
        </button>
      </div>

      <nav className="view-switch">
        <button className={view === "perform" ? "on" : ""} onClick={() => store.setView("perform")}>
          Perform
        </button>
        <button className={view === "edit" ? "on" : ""} onClick={() => store.setView("edit")}>
          Edit
        </button>
      </nav>

      <div className="topbar-right">
        <RecordButton />
        <div className="device" title={`${session.sampleRate} Hz, ${session.blockSize} samples`}>
          {session.device ? (
            <>
              <span className="device-name">{session.device}</span>
              <CpuReadout />
            </>
          ) : (
            <span className="no-device">No audio output</span>
          )}
        </div>
        <Meter />
        <button className="icon-btn" title="Audio settings" onClick={() => void store.call("audio.settings")}>
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4">
            <path d="M2 4h7M12 4h2M2 12h3M8 12h6M9 2v4M5 10v4" strokeLinecap="round" />
          </svg>
        </button>
      </div>
    </header>
  );
}
