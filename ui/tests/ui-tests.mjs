import { resolve } from "node:path";

// CI sets LOGOS_QT_MCP automatically; for interactive use:
//   nix build .#test-framework -o result-mcp
const root = process.env.LOGOS_QT_MCP || new URL("../result-mcp", import.meta.url).pathname;
const { test, run } = await import(resolve(root, "test-framework/framework.mjs"));

// The view loads and reaches its backend. netgraph_ui's backend forwards to
// netgraph_module, which is off until turned on — so the load-time state is the
// "off" copy and an empty table, which is exactly what we assert here.
test("netgraph_ui: loads UI", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["netgraph"]); },
    { timeout: 15000, interval: 500, description: "UI to load" }
  );
});

test("netgraph_ui: connects to backend (collection off by default)", async (app) => {
  await app.waitFor(
    async () => { await app.expectTexts(["off — no connections are being read"]); },
    { timeout: 15000, interval: 500, description: "backend to connect and report off" }
  );
});

run();
