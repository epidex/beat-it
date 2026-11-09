#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""(Beat It) the one-file Windows installer: make_site.py's installer page, renamed Beat It, with the package inlined
as a data: URL (no server, no second file). Chrome or Edge open it from the disk; the page checks the package's SHA-256.

  python3 web/make_beatit_installer.py build/felucca.fwsc 1.1 Beat-It-1.1-installer.html
"""
import base64
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main(pkg, version, out):
    raw = Path(pkg).read_bytes()
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([sys.executable, str(HERE / "make_site.py"), pkg, version, d], check=True, stdout=subprocess.DEVNULL)
        html = (Path(d) / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")
    for old, new in (('"pkg": "../../firmware/', None), ("<title>SLOOP installer</title>", "<title>Beat It installer (FM-1)</title>"),
                     ("say(`SLOOP ${meta.version}`);", "say(`Beat It ${meta.version}`);")):
        if html.count(old) != 1:
            raise SystemExit(f"installer page: {old!r} not found once; update make_beatit_installer.py")
        if new:
            html = html.replace(old, new)
    data = "data:application/octet-stream;base64," + base64.b64encode(raw).decode()
    html = re.sub(r'"pkg": "\.\./\.\./firmware/[^"]+"', lambda m: '"pkg": "' + data + '"', html)
    Path(out).write_text(html, encoding="utf-8")
    print(f"installer: {out} ({len(html)} B), package {len(raw)} B inlined")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
