import { describe, expect, it } from "vitest";
import schemaJson from "../bridge/schema.json";
import type { Schema } from "../bridge/types";
import { formatDisplay, formatParam, fromNorm, toNorm } from "./format";

const schema = schemaJson as unknown as Schema;
const spec = (id: string) => schema.params.find((p) => p.id === id)!;

describe("parameter mapping", () => {
  it("round-trips normalised values for every parameter", () => {
    for (const p of schema.params) {
      if (p.discrete) continue;
      for (const n of [0, 0.25, 0.5, 1]) expect(toNorm(p, fromNorm(p, n))).toBeCloseTo(n, 4);
    }
  });

  it("uses a log taper where the engine does", () => {
    const cutoff = spec("drone.cutoff");
    expect(fromNorm(cutoff, 0.5)).toBeCloseTo(Math.sqrt(cutoff.min * cutoff.max), 1);
  });
});

describe("formatting", () => {
  it("formats units the way a musician reads them", () => {
    expect(formatParam(spec("drone.cutoff"), 900, schema)).toBe("900 Hz");
    expect(formatParam(spec("drone.cutoff"), 4200, schema)).toBe("4.20 kHz");
    expect(formatParam(spec("drone.level"), -60, schema)).toBe("Off");
    expect(formatParam(spec("drone.level"), -6, schema)).toBe("-6.0 dB");
    expect(formatParam(spec("drone.pan"), 0, schema)).toBe("C");
    expect(formatParam(spec("drone.pan"), -0.3, schema)).toBe("L 30");
    expect(formatParam(spec("drone.root"), 38, schema)).toBe("D2");
    expect(formatParam(spec("harmony.scale"), 1, schema)).toBe("Minor");
    expect(formatParam(spec("bloom.attack"), 0.05, schema)).toBe("50 ms");
    expect(formatParam(spec("medium.type"), 2, schema)).toBe("Vinyl");
  });

  it("formats FX controls exactly like the engine's DisplayMap", () => {
    const reverb = schema.processors.find((p) => p.type === "tf.reverb")!;
    expect(formatDisplay(reverb.controls[1].display, 0.4)).toBe("2.5 s");
    const delay = schema.processors.find((p) => p.type === "tf.delay")!;
    expect(formatDisplay(delay.controls[0].display, 1)).toBe("2.00 s");
    expect(formatDisplay(delay.controls[1].display, 0.5)).toBe("55%");
    const medium = schema.processors.find((p) => p.type === "tf.medium")!;
    expect(formatDisplay(medium.controls[0].display, 0.6)).toBe("Vinyl");
  });
});
