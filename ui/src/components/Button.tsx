import type { ButtonHTMLAttributes, ReactNode } from "react";
import "./Button.css";

interface Props extends ButtonHTMLAttributes<HTMLButtonElement> {
  tone?: "ghost" | "plain" | "accent" | "live" | "danger";
  size?: "sm" | "md" | "lg";
  active?: boolean;
  children: ReactNode;
}

export function Button({ tone = "plain", size = "md", active, className, children, ...rest }: Props) {
  return (
    <button className={`btn btn-${tone} btn-${size}${active ? " active" : ""}${className ? ` ${className}` : ""}`} {...rest}>
      {children}
    </button>
  );
}
