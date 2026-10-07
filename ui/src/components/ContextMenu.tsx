import { useSyncExternalStore } from "react";
import "./ContextMenu.css";

export interface MenuItem {
  label: string;
  onSelect: () => void;
  danger?: boolean;
  disabled?: boolean;
}

interface MenuState {
  x: number;
  y: number;
  items: (MenuItem | "-")[];
  title?: string;
}

let current: MenuState | null = null;
const listeners = new Set<() => void>();
const notify = () => listeners.forEach((l) => l());
const subscribe = (l: () => void) => {
  listeners.add(l);
  return () => void listeners.delete(l);
};

export function openMenu(event: { clientX: number; clientY: number }, items: (MenuItem | "-")[], title?: string) {
  current = { x: event.clientX, y: event.clientY, items, title };
  notify();
}

export function closeMenu() {
  current = null;
  notify();
}

/** Single global menu layer; mount once at the root. */
export function ContextMenuLayer() {
  const menu = useSyncExternalStore(subscribe, () => current);
  if (!menu) return null;
  const { x, y, items, title } = menu;
  const left = Math.min(x, window.innerWidth - 240);
  const top = Math.min(y, window.innerHeight - 40 - items.length * 30);
  return (
    <div className="ctx-backdrop" onPointerDown={closeMenu} onContextMenu={(e) => (e.preventDefault(), closeMenu())}>
      <div className="ctx-menu fade-in" style={{ left, top }} onPointerDown={(e) => e.stopPropagation()}>
        {title && <div className="ctx-title caps">{title}</div>}
        {items.map((item, k) =>
          item === "-" ? (
            <div key={k} className="ctx-sep" />
          ) : (
            <button
              key={k}
              className={`ctx-item${item.danger ? " danger" : ""}`}
              disabled={item.disabled}
              onClick={() => {
                closeMenu();
                item.onSelect();
              }}
            >
              {item.label}
            </button>
          ),
        )}
      </div>
    </div>
  );
}
