import type { Transport } from "./types";

// Minimal client for JUCE 8's WebBrowserComponent native integration. JUCE injects
// window.__JUCE__.backend; native functions are invoked by emitting "__juce__invoke"
// and answered with "__juce__complete". Everything Tidefield needs goes through one
// native function, "tidefield", whose first argument is the method name.

interface JuceBackend {
  addEventListener(event: string, fn: (payload: any) => void): unknown;
  removeEventListener(token: unknown): void;
  emitEvent(event: string, payload: unknown): void;
}

declare global {
  interface Window {
    __JUCE__?: { backend: JuceBackend; initialisationData: Record<string, unknown> };
  }
}

export function juceAvailable(): boolean {
  return typeof window !== "undefined" && window.__JUCE__ !== undefined;
}

export function createJuceTransport(): Transport {
  const backend = window.__JUCE__!.backend;
  const pending = new Map<number, (result: unknown) => void>();
  let nextId = 0;

  backend.addEventListener("__juce__complete", ({ promiseId, result }: { promiseId: number; result: unknown }) => {
    const resolve = pending.get(promiseId);
    if (resolve) {
      pending.delete(promiseId);
      resolve(result);
    }
  });

  return {
    isMock: false,
    call(method, ...args) {
      const resultId = nextId++;
      return new Promise((resolve) => {
        pending.set(resultId, resolve);
        backend.emitEvent("__juce__invoke", { name: "tidefield", params: [method, ...args], resultId });
      });
    },
    on(event, listener) {
      const token = backend.addEventListener(event, listener);
      return () => backend.removeEventListener(token);
    },
  };
}
