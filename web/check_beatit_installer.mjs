// (Beat It) the one-file installer: its package (inlined as a data: URL) is read the way the page reads it, checked
// against the page's SHA-256 and identity, and its logical image built, as the page does before Install.
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { logicalImage, productOf } from "./fm1pkg.js";

const html = readFileSync(process.argv[2], "utf8");
const m = html.match(/\{"version": "[^"]*", "product": "[^"]*", "pkg": "(data:[^"]+)", "sha256": "([0-9a-f]{64})"\}/);
if (!m) { console.log("FAIL: no inlined package metadata"); process.exit(1); }
const meta = JSON.parse(m[0]);
const r = await fetch(meta.pkg, { cache: "no-cache" });
const pkg = new Uint8Array(await r.arrayBuffer());
const file = readFileSync(process.argv[3]);
const h = createHash("sha256").update(pkg).digest("hex");
const ok = [
  [r.ok, "the data: URL fetches"],
  [pkg.length === file.length && Buffer.compare(Buffer.from(pkg), file) === 0, `the inlined package is the .fwsc byte for byte (${pkg.length} B)`],
  [h === meta.sha256, "its SHA-256 matches the page's"],
  [productOf(pkg) === meta.product && meta.product === "FM-1_900", `identity ${productOf(pkg)}`],
];
let img; try { img = logicalImage(pkg); } catch (e) { img = null; }
ok.push([img && img.length > 0, `logical image built (${img ? img.length : 0} B)`]);
let bad = 0;
for (const [c, w] of ok) { console.log(`${c ? "ok  " : "FAIL"} ${w}`); bad += !c; }
process.exit(bad ? 1 : 0);
