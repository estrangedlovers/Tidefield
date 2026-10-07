import { useEffect, useRef, useState } from "react";
import { store, useSlice } from "../state/store";
import { openMenu } from "../components/ContextMenu";
import "./TerrainView.css";

// The terrain: scenes as places, the performer's cursor, and the sound itself drawn as
// a living field around where the sound is. Everything eases; nothing jumps.
//
// Interactions: drag empty space to move the cursor (the sound glides there), drag a
// scene to move it, double-click to capture the current sound as a scene, right-click
// a scene for more. All drawing happens in one requestAnimationFrame loop that reads
// telemetry directly, so React never re-renders at frame rate.

interface Particle {
  x: number;
  y: number;
  vx: number;
  vy: number;
  age: number;
  life: number;
  size: number;
  hue: number;
  alpha: number;
}

interface Ripple {
  x: number;
  y: number;
  age: number;
  strength: number;
}

const CLOUD_TINTS = ["142,201,198", "182,222,214", "106,169,176", "227,198,144"];
const SCENE_HIT = 16;

export function TerrainView() {
  const canvas = useRef<HTMLCanvasElement>(null);
  const wrap = useRef<HTMLDivElement>(null);
  const { scenes } = useSlice("scenes");
  const [rename, setRename] = useState<{ index: number; name: string; x: number; y: number } | null>(null);
  const scenesRef = useRef(scenes);
  scenesRef.current = scenes;
  const hover = useRef(-1);
  const drag = useRef<{ kind: "cursor" | "scene"; index: number } | null>(null);
  const lastMove = useRef(0);

  // --- Geometry helpers (terrain y grows upward) -------------------------------------
  const geom = () => {
    const c = canvas.current!;
    const w = c.clientWidth;
    const h = c.clientHeight;
    const side = Math.min(w, h) - 24;
    return { x0: (w - side) / 2, y0: (h - side) / 2, side };
  };
  const toTerrain = (px: number, py: number) => {
    const { x0, y0, side } = geom();
    return [Math.min(1, Math.max(0, (px - x0) / side)), Math.min(1, Math.max(0, 1 - (py - y0) / side))] as const;
  };
  const sceneAt = (px: number, py: number) => {
    const { x0, y0, side } = geom();
    let best = -1;
    let bestD = SCENE_HIT;
    scenesRef.current.forEach((s, k) => {
      const d = Math.hypot(px - (x0 + s.x * side), py - (y0 + (1 - s.y) * side));
      if (d < bestD) {
        bestD = d;
        best = k;
      }
    });
    return best;
  };

  useEffect(() => {
    const c = canvas.current!;
    const ctx = c.getContext("2d")!;
    let raf = 0;
    let last = performance.now();
    const particles: Particle[] = [];
    const ripples: Ripple[] = [];
    const trail: [number, number][] = [];
    let trailClock = 0;
    let tidePhase = 0;
    let orbit = 0;
    const disp = { x: 0.5, y: 0.5, cx: 0.5, cy: 0.5, energy: 0, weights: [] as number[] };
    const modeLevels: number[] = [];
    let emitCarry = [0, 0, 0, 0];

    const resize = () => {
      const dpr = window.devicePixelRatio || 1;
      c.width = Math.round(c.clientWidth * dpr);
      c.height = Math.round(c.clientHeight * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    const ro = new ResizeObserver(resize);
    ro.observe(c);
    resize();

    const frame = (now: number) => {
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      const t = store.telemetry;
      const w = c.clientWidth;
      const h = c.clientHeight;
      const { x0, y0, side } = geom();
      const X = (x: number) => x0 + x * side;
      const Y = (y: number) => y0 + (1 - y) * side;
      const ease = (tau: number) => 1 - Math.exp(-dt / tau);

      const tide = t?.tide ?? 1;
      const fade = t ? Math.max(t.fade[0] ** 1.5, 0) : 0;
      const quiet = t?.panic ? 0 : fade;
      disp.energy += (quiet - disp.energy) * ease(0.6);
      if (t) {
        disp.x += (t.terrain.pos[0] - disp.x) * ease(0.12);
        disp.y += (t.terrain.pos[1] - disp.y) * ease(0.12);
        disp.cx += (t.terrain.cursor[0] - disp.cx) * ease(0.08);
        disp.cy += (t.terrain.cursor[1] - disp.cy) * ease(0.08);
        disp.weights = scenesRef.current.map((_, k) => {
          const target = t.terrain.w[k] ?? 0;
          return (disp.weights[k] ?? target) + (target - (disp.weights[k] ?? target)) * ease(0.25);
        });
      }
      tidePhase += dt * 0.12 * tide;
      orbit += dt * 0.08 * tide;

      // Background.
      ctx.clearRect(0, 0, w, h);
      const bg = ctx.createRadialGradient(X(disp.x), Y(disp.y), 0, X(disp.x), Y(disp.y), side * 0.9);
      bg.addColorStop(0, `rgba(30, 44, 52, ${0.25 + 0.35 * disp.energy})`);
      bg.addColorStop(1, "rgba(10, 13, 17, 0)");
      ctx.fillStyle = "#0b0f13";
      roundRect(ctx, x0, y0, side, side, 18);
      ctx.fill();
      ctx.save();
      roundRect(ctx, x0, y0, side, side, 18);
      ctx.clip();
      ctx.fillStyle = bg;
      ctx.fillRect(x0, y0, side, side);

      // Tide lines: slow currents across the terrain.
      ctx.lineWidth = 1;
      for (let k = 0; k < 9; k++) {
        const yy = (k + 0.5) / 9;
        ctx.beginPath();
        for (let s = 0; s <= 48; s++) {
          const xx = s / 48;
          const off = Math.sin(xx * 5.5 + tidePhase * (1 + k * 0.13) + k) * 0.012 + Math.sin(xx * 13 - tidePhase * 1.7 + k * 2) * 0.004;
          const px = X(xx);
          const py = Y(yy + off);
          s === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
        }
        ctx.strokeStyle = `rgba(142, 201, 198, ${0.025 + 0.03 * disp.energy})`;
        ctx.stroke();
      }

      // Scene influence.
      ctx.globalCompositeOperation = "lighter";
      scenesRef.current.forEach((s, k) => {
        const wt = disp.weights[k] ?? 0;
        const r = side * (0.1 + 0.28 * Math.sqrt(wt));
        const g = ctx.createRadialGradient(X(s.x), Y(s.y), 0, X(s.x), Y(s.y), r);
        g.addColorStop(0, `rgba(142, 201, 198, ${0.05 + 0.2 * wt})`);
        g.addColorStop(1, "rgba(142, 201, 198, 0)");
        ctx.fillStyle = g;
        ctx.fillRect(X(s.x) - r, Y(s.y) - r, 2 * r, 2 * r);
      });

      // Particles: one per grain the clouds are playing, born near the sound.
      if (t) {
        t.clouds.forEach((cloud, k) => {
          if (!cloud.loaded) return;
          emitCarry[k] += cloud.count * dt * 2.2 * disp.energy;
          while (emitCarry[k] >= 1) {
            emitCarry[k] -= 1;
            const g = cloud.grains[Math.floor(Math.random() * Math.max(1, cloud.grains.length))];
            const angle = (g ? g[0] : Math.random()) * Math.PI * 2 + k * 1.57;
            const radius = 0.015 + 0.06 * Math.random();
            particles.push({
              x: disp.x + Math.cos(angle) * radius + (g ? g[2] * 0.03 : 0),
              y: disp.y + Math.sin(angle) * radius,
              vx: Math.cos(angle) * 0.02 + (Math.random() - 0.5) * 0.01,
              vy: Math.sin(angle) * 0.02 + (Math.random() - 0.5) * 0.01,
              age: 0,
              life: 1.4 + Math.random() * 2.2,
              size: 0.8 + Math.random() * 1.6,
              hue: k,
              alpha: 0.35 + 0.65 * (g ? g[1] : 0.5),
            });
          }
        });
        if (particles.length > 700) particles.splice(0, particles.length - 700);
      }
      const sampler = t?.medium === 3;
      for (let k = particles.length - 1; k >= 0; k--) {
        const p = particles[k];
        p.age += dt;
        if (p.age >= p.life) {
          particles.splice(k, 1);
          continue;
        }
        p.vx *= 0.995;
        p.vy *= 0.995;
        p.vx += Math.sin(p.y * 9 + tidePhase * 3) * 0.004 * dt;
        p.vy += Math.cos(p.x * 9 - tidePhase * 3) * 0.004 * dt;
        p.x += p.vx * dt * tide;
        p.y += p.vy * dt * tide;
        const env = Math.sin(Math.PI * (p.age / p.life));
        let px = X(p.x);
        let py = Y(p.y);
        if (sampler) {
          px = Math.round(px / 4) * 4;
          py = Math.round(py / 4) * 4;
        }
        ctx.fillStyle = `rgba(${CLOUD_TINTS[p.hue]}, ${(p.alpha * env * 0.55).toFixed(3)})`;
        ctx.beginPath();
        ctx.arc(px, py, p.size, 0, Math.PI * 2);
        ctx.fill();
      }

      // Drone voices: lights orbiting the sound, farther for higher pitch.
      if (t) {
        t.drone.forEach(([level, note], v) => {
          if (level < 0.01) return;
          const radius = side * (0.04 + Math.max(0, note - 20) * 0.0028);
          const a = orbit * (1 + v * 0.17) + v * 1.05;
          const px = X(disp.x) + Math.cos(a) * radius;
          const py = Y(disp.y) + Math.sin(a) * radius * 0.82;
          const r = 10 + 26 * level;
          const g = ctx.createRadialGradient(px, py, 0, px, py, r);
          g.addColorStop(0, `rgba(210, 236, 232, ${0.35 * level * disp.energy})`);
          g.addColorStop(1, "rgba(142, 201, 198, 0)");
          ctx.fillStyle = g;
          ctx.fillRect(px - r, py - r, 2 * r, 2 * r);
        });

        // Resonator: strikes become ripples.
        t.modes.forEach(([level, note], m) => {
          const prev = modeLevels[m] ?? 0;
          if (level > prev * 1.6 + 0.02) {
            const a = ((((note % 12) + 12) % 12) / 12) * Math.PI * 2 + m * 0.4;
            const rr = 0.05 + 0.12 * Math.random();
            ripples.push({ x: disp.x + Math.cos(a) * rr, y: disp.y + Math.sin(a) * rr, age: 0, strength: Math.min(1, level * 3) });
          }
          modeLevels[m] = level;
        });
        if (ripples.length > 40) ripples.splice(0, ripples.length - 40);

        // Bloom voices: blooms at the sound.
        t.bloom.voices.forEach(([active, note, level]) => {
          if (!active || level < 0.005) return;
          const a = ((note % 12) / 12) * Math.PI * 2;
          const px = X(disp.x) + Math.cos(a) * side * 0.05;
          const py = Y(disp.y) + Math.sin(a) * side * 0.05;
          const r = side * (0.03 + 0.12 * Math.min(1, level * 2.5));
          const g = ctx.createRadialGradient(px, py, 0, px, py, r);
          g.addColorStop(0, `rgba(227, 198, 144, ${Math.min(0.5, level * 1.5)})`);
          g.addColorStop(1, "rgba(227, 198, 144, 0)");
          ctx.fillStyle = g;
          ctx.fillRect(px - r, py - r, 2 * r, 2 * r);
        });
      }
      for (let k = ripples.length - 1; k >= 0; k--) {
        const r = ripples[k];
        r.age += dt * (0.5 + 0.25 * tide);
        if (r.age > 1) {
          ripples.splice(k, 1);
          continue;
        }
        ctx.strokeStyle = `rgba(182, 222, 214, ${((1 - r.age) * 0.35 * r.strength * disp.energy).toFixed(3)})`;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(X(r.x), Y(r.y), 4 + r.age * side * 0.09, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.globalCompositeOperation = "source-over";

      // Medium texture.
      if (t?.medium === 1) {
        ctx.fillStyle = "rgba(255,255,255,0.012)";
        for (let k = 0; k < 6; k++) {
          const yy = ((now / 9000 + k / 6) % 1) * side;
          ctx.fillRect(x0, y0 + yy, side, 1);
        }
      } else if (t?.medium === 2) {
        ctx.fillStyle = "rgba(255,255,255,0.35)";
        for (let k = 0; k < 3; k++)
          if (Math.random() < 0.25) ctx.fillRect(x0 + Math.random() * side, y0 + Math.random() * side, 1.2, 1.2);
      }

      // Wander trail.
      trailClock += dt;
      if (trailClock > 1 / 15) {
        trailClock = 0;
        trail.push([disp.x, disp.y]);
        if (trail.length > 150) trail.shift();
      }
      for (let k = 1; k < trail.length; k++) {
        ctx.strokeStyle = `rgba(142, 201, 198, ${((k / trail.length) * 0.22).toFixed(3)})`;
        ctx.lineWidth = 1.2;
        ctx.beginPath();
        ctx.moveTo(X(trail[k - 1][0]), Y(trail[k - 1][1]));
        ctx.lineTo(X(trail[k][0]), Y(trail[k][1]));
        ctx.stroke();
      }

      // Grid.
      ctx.fillStyle = "rgba(255,255,255,0.05)";
      for (let gx = 1; gx < 8; gx++) for (let gy = 1; gy < 8; gy++) ctx.fillRect(X(gx / 8) - 0.5, Y(gy / 8) - 0.5, 1, 1);
      ctx.restore();

      // Frame.
      ctx.strokeStyle = "rgba(255,255,255,0.06)";
      ctx.lineWidth = 1;
      roundRect(ctx, x0 + 0.5, y0 + 0.5, side - 1, side - 1, 18);
      ctx.stroke();

      // Scenes.
      ctx.font = "500 11.5px Inter Variable, Inter, system-ui, sans-serif";
      ctx.textAlign = "center";
      scenesRef.current.forEach((s, k) => {
        const wt = disp.weights[k] ?? 0;
        const px = X(s.x);
        const py = Y(s.y);
        const hovered = hover.current === k;
        if (wt > 0.01) {
          ctx.strokeStyle = `rgba(142, 201, 198, ${0.35 + 0.5 * wt})`;
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.arc(px, py, 11, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * wt);
          ctx.stroke();
        }
        ctx.fillStyle = hovered ? "#eef3f5" : `rgba(221, 227, 234, ${0.55 + 0.45 * wt})`;
        ctx.beginPath();
        ctx.arc(px, py, hovered ? 5 : 4, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = `rgba(221, 227, 234, ${0.5 + 0.5 * Math.max(wt, hovered ? 1 : 0)})`;
        ctx.fillText(s.name, px, py + 26);
      });

      // Cursor (where you are steering) and position (where the sound is).
      const cx = X(disp.cx);
      const cy = Y(disp.cy);
      const sx = X(disp.x);
      const sy = Y(disp.y);
      ctx.strokeStyle = "rgba(142, 201, 198, 0.25)";
      ctx.setLineDash([2, 4]);
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(sx, sy);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.strokeStyle = "rgba(221, 227, 234, 0.85)";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.arc(cx, cy, 13, 0, Math.PI * 2);
      ctx.stroke();
      const glow = ctx.createRadialGradient(sx, sy, 0, sx, sy, 22);
      glow.addColorStop(0, "rgba(142, 201, 198, 0.9)");
      glow.addColorStop(0.25, "rgba(142, 201, 198, 0.35)");
      glow.addColorStop(1, "rgba(142, 201, 198, 0)");
      ctx.fillStyle = glow;
      ctx.fillRect(sx - 22, sy - 22, 44, 44);
      ctx.fillStyle = "#e9f5f3";
      ctx.beginPath();
      ctx.arc(sx, sy, 3, 0, Math.PI * 2);
      ctx.fill();

      if (scenesRef.current.length === 0) {
        ctx.fillStyle = "rgba(151, 161, 175, 0.75)";
        ctx.font = "13px Inter Variable, Inter, system-ui, sans-serif";
        ctx.fillText("Shape a sound, then double-click anywhere to place it here as a scene.", w / 2, y0 + side - 30);
      }

      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
    };
  }, []);

  // --- Pointer handling ------------------------------------------------------------
  const local = (e: React.PointerEvent | React.MouseEvent) => {
    const r = canvas.current!.getBoundingClientRect();
    return [e.clientX - r.left, e.clientY - r.top] as const;
  };

  const moveCursor = (px: number, py: number) => {
    const [x, y] = toTerrain(px, py);
    store.setParamById("terrain.x", x);
    store.setParamById("terrain.y", y);
  };

  return (
    <div className="terrain" ref={wrap}>
      <canvas
        ref={canvas}
        onPointerDown={(e) => {
          if (e.button !== 0) return;
          const [px, py] = local(e);
          (e.target as Element).setPointerCapture(e.pointerId);
          const hit = sceneAt(px, py);
          drag.current = hit >= 0 && !e.altKey ? { kind: "scene", index: hit } : { kind: "cursor", index: -1 };
          if (drag.current.kind === "cursor") moveCursor(px, py);
        }}
        onPointerMove={(e) => {
          const [px, py] = local(e);
          const hit = sceneAt(px, py);
          hover.current = hit;
          (e.target as HTMLElement).style.cursor = hit >= 0 ? "grab" : "crosshair";
          if (!drag.current) return;
          const now = performance.now();
          if (now - lastMove.current < 16) return;
          lastMove.current = now;
          if (drag.current.kind === "cursor") moveCursor(px, py);
          else {
            const [x, y] = toTerrain(px, py);
            void store.call("scene.move", drag.current.index, x, y);
          }
        }}
        onPointerUp={() => (drag.current = null)}
        onPointerLeave={() => (hover.current = -1)}
        onDoubleClick={(e) => {
          const [px, py] = local(e);
          if (sceneAt(px, py) >= 0) return;
          const [x, y] = toTerrain(px, py);
          void store.call("scene.capture", x, y);
        }}
        onContextMenu={(e) => {
          e.preventDefault();
          const [px, py] = local(e);
          const hit = sceneAt(px, py);
          if (hit < 0) {
            const [x, y] = toTerrain(px, py);
            openMenu(e, [
              { label: "Capture the current sound here", onSelect: () => void store.call("scene.capture", x, y) },
              { label: "Release live layer", onSelect: () => void store.call("scene.releaseLive") },
            ]);
            return;
          }
          const s = scenes[hit];
          openMenu(
            e,
            [
              { label: "Glide here", onSelect: () => (store.setParamById("terrain.x", s.x), store.setParamById("terrain.y", s.y)) },
              { label: "Commit live layer into this scene", onSelect: () => void store.call("scene.commit", hit) },
              { label: "Replace with the current sound", onSelect: () => void store.call("scene.replace", hit) },
              { label: "Rename...", onSelect: () => setRename({ index: hit, name: s.name, x: px, y: py }) },
              "-",
              { label: "Delete scene", danger: true, onSelect: () => void store.call("scene.remove", hit) },
            ],
            s.name,
          );
        }}
      />
      {rename && (
        <form
          className="terrain-rename fade-in"
          style={{ left: rename.x, top: rename.y }}
          onSubmit={(e) => {
            e.preventDefault();
            void store.call("scene.rename", rename.index, rename.name.trim() || `Scene ${rename.index + 1}`);
            setRename(null);
          }}
        >
          <input
            autoFocus
            value={rename.name}
            onChange={(e) => setRename({ ...rename, name: e.target.value })}
            onBlur={() => setRename(null)}
            onKeyDown={(e) => e.key === "Escape" && setRename(null)}
          />
        </form>
      )}
    </div>
  );
}

function roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}
