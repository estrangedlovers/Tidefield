import type { ReactNode } from "react";
import "./Section.css";

export function Section({ title, actions, children, className }: { title?: string; actions?: ReactNode; children: ReactNode; className?: string }) {
  return (
    <section className={`section${className ? ` ${className}` : ""}`}>
      {(title || actions) && (
        <header className="section-head">
          {title && <span className="caps">{title}</span>}
          {actions && <div className="section-actions">{actions}</div>}
        </header>
      )}
      <div className="section-body">{children}</div>
    </section>
  );
}
