import { createRoot } from "react-dom/client";
import "./theme/global.css";
import { App } from "./App";
import { store } from "./state/store";
import { createJuceTransport, juceAvailable } from "./bridge/juce";

async function start() {
  // Inside the app, JUCE injects window.__JUCE__ before the page scripts run. In a
  // plain browser (npm run dev) the mock engine stands in.
  const transport = juceAvailable() ? createJuceTransport() : (await import("./bridge/mock")).createMockTransport();
  createRoot(document.getElementById("root")!).render(<App />);
  await store.connect(transport);
}

void start();
