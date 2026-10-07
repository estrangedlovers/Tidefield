# Tidefield

A desktop instrument for performing ambient music live. No tracks or timeline: a small
ecosystem of sound sources drifts on its own and the performer steers the overall
state.

- Design: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- Status and next steps: [`PROGRESS.md`](PROGRESS.md)
- Rules for contributors (human or AI): [`CLAUDE.md`](CLAUDE.md)

## Build

Requires CMake 3.24+, Ninja and a C++20 compiler. JUCE 8 and Catch2 are fetched
automatically.

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

On macOS this also builds `Tidefield.app` (the React UI in `ui/` is built with npm
and bundled; Node 20+ needed). Open the folder in CLion and pick a preset.

To work on the UI in a browser without audio: `cd ui && npm install && npm run dev`,
then open http://localhost:5173/?demo.
