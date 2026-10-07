import { useSlice } from "../state/store";
import "./Toasts.css";

export function Toasts() {
  const { toasts } = useSlice("toasts");
  return (
    <div className="toasts">
      {toasts.map((t) => (
        <div key={t.id} className={`toast fade-in${t.warning ? " warn" : ""}`}>
          {t.message}
        </div>
      ))}
    </div>
  );
}
