import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// `base: "./"` keeps asset URLs relative: the app serves the build from JUCE's
// resource-provider root, not from a web server's "/".
export default defineConfig({
  base: "./",
  plugins: [react()],
  build: {
    outDir: "dist",
    emptyOutDir: true,
    assetsInlineLimit: 0,
    target: "safari15",
  },
  server: { port: 5173, strictPort: true },
});
