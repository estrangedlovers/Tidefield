import { useEffect } from "react";
import { store, useSlice } from "./state/store";
import { TopBar } from "./views/TopBar";
import { PerformView } from "./views/PerformView";
import { EditView } from "./views/EditView";
import { ContextMenuLayer } from "./components/ContextMenu";
import { Toasts } from "./components/Toasts";
import "./App.css";

/** Keyboard shortcuts. Ignored while typing into a field. */
function useShortcuts() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "SELECT" || target.tagName === "TEXTAREA") return;
      const mod = e.metaKey || e.ctrlKey;
      const k = e.key.toLowerCase();
      if (mod && k === "s") void store.call(e.shiftKey ? "session.saveAs" : "session.save");
      else if (mod && k === "o") void store.call("session.open");
      else if (mod && k === "n") void store.call("session.new");
      else if (mod) return;
      else if (e.code === "Space") store.command("fadeToggle");
      else if (k === "escape") store.command("panicToggle");
      else if (k === "k") store.command("catch");
      else if (k === "c") void store.call("scene.capture");
      else if (k === "r") void store.call("scene.releaseLive");
      else if (k === "tab") store.setView(store.view === "perform" ? "edit" : "perform");
      else return;
      e.preventDefault();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);
}

export function App() {
  const { schema, view } = useSlice("schema");
  useSlice("view");
  useShortcuts();

  if (!schema) return <div className="loading">Tidefield</div>;
  return (
    <div className="app">
      <TopBar />
      <div className="app-body">{view === "perform" ? <PerformView /> : <EditView />}</div>
      {store.transport?.isMock && <div className="mock-badge">Browser preview: no audio</div>}
      <ContextMenuLayer />
      <Toasts />
    </div>
  );
}
