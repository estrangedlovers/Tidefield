import { store, useSlice } from "../state/store";
import { Meter } from "../components/Meter";
import { openMenu } from "../components/ContextMenu";
import "./TopBar.css";

export function TopBar() {
  const { session, view } = useSlice("session");
  useSlice("view");
  const cpu = Math.round(session.cpu * 100);

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
        <div className="device" title={`${session.sampleRate} Hz, ${session.blockSize} samples`}>
          {session.device ? (
            <>
              <span>{session.device}</span>
              <span className={cpu > 70 ? "cpu hot" : "cpu"}>{cpu}% CPU</span>
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
