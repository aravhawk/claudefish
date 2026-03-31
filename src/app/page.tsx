import EngineWorkerProbe from "./EngineWorkerProbe";

const setupSteps = [
  "Next.js App Router scaffold with strict TypeScript and ESLint",
  "Emscripten build output served from public/engine",
  "Dependencies ready for the upcoming worker + hook integration",
];

export default function Home() {
  return (
    <main className="page">
      <section className="panel">
        <p className="eyebrow">Claudefish</p>
        <h1>WASM build scaffold ready</h1>
        <p className="lede">
          The Next.js shell and WebAssembly build pipeline are configured for the
          chess engine integration work.
        </p>
        <ul className="checklist">
          {setupSteps.map((step) => (
            <li key={step}>{step}</li>
          ))}
        </ul>
        <EngineWorkerProbe />
      </section>
    </main>
  );
}
