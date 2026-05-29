#!/usr/bin/env python3
"""
Pointerverse causal IDE - local dev server.

Serves the static UI and bridges a handful of /api/* endpoints to the real
`pointerverse` binary, so the panels are driven by the actual engine instead
of the bundled demo data.

    python3 ui/serve.py                 # http://localhost:8787
    python3 ui/serve.py --port 9000
    python3 ui/serve.py --bin ./build/pointerverse

Standard library only - no pip install. Bind is localhost by design: the /run
endpoint executes the engine on the world you post, so do not expose it.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.parse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

UI_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(UI_DIR)

# world id -> pack id under examples/packs/<pack>/.pack-store
PACKS = {
    "mohacs": "mohacs",
    "rust_wipe_blackbox": "rust_wipe_blackbox",
}
SAFE = re.compile(r"^[A-Za-z0-9_]+$")
HEX = re.compile(r"\b[0-9a-f]{12}\b")


class App:
    def __init__(self, binary):
        self.binary = binary

    # ---- subprocess helper -------------------------------------------------
    def engine(self, args, timeout=45):
        try:
            p = subprocess.run(
                [self.binary, *args],
                cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout,
            )
            return (p.stdout or "") + (p.stderr or "")
        except subprocess.TimeoutExpired:
            return "engine timed out"
        except FileNotFoundError:
            return f"binary not found: {self.binary}"

    def binary_ok(self):
        return os.path.isfile(self.binary) and os.access(self.binary, os.X_OK)

    def store_for(self, world):
        pack = PACKS.get(world)
        if not pack:
            return None
        store = os.path.join(REPO_ROOT, "examples", "packs", pack, ".pack-store")
        if not os.path.isdir(store):
            # build it once from the pack
            self.engine(["pack", "run", pack], timeout=120)
        return store if os.path.isdir(store) else None

    def repo(self, world, *args):
        store = self.store_for(world)
        if not store:
            return ""
        return self.engine(["repo", "--store", store, *args])

    # ---- endpoints ---------------------------------------------------------
    def health(self):
        return {"ok": self.binary_ok(), "version": "pointerverse engine"}

    def run(self, body):
        src = body.get("src", "")
        with tempfile.NamedTemporaryFile("w", suffix=".pv", delete=False, dir=tempfile.gettempdir()) as f:
            f.write(src)
            path = f.name
        try:
            out = self.engine(["world", "run", path])
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
        return {"output": out}

    def history(self, q):
        world, branch = q.get("world", [""])[0], q.get("branch", [""])[0]
        if not SAFE.match(branch or ""):
            return {"commits": []}
        out = self.repo(world, "history", branch)
        commits = []
        for line in out.splitlines():
            m = re.match(r"^([0-9a-f]{12})\s+(.*?)\s+epoch\s+(\d+)", line.strip())
            if not m:
                continue
            h, msg, epoch = m.group(1), m.group(2).strip(), int(m.group(3))
            kind = "link" if msg.startswith("link") else "object" if msg.startswith("object") else "genesis" if "genesis" in msg else "link"
            if "gives_battle" in msg:
                kind = "violation" if "historical" in branch else "pass"
            commits.append({"hash": h, "msg": msg, "epoch": epoch, "kind": kind})
        # thin a long spine for the visual
        if len(commits) > 16:
            commits = commits[:1] + commits[1:-12:3] + commits[-12:]
        return {"commits": commits, "output": out}

    def why(self, q):
        world = q.get("world", [""])[0]
        branch = q.get("branch", [""])[0]
        frm, rel, to = q.get("from", [""])[0], q.get("rel", [""])[0], q.get("to", [""])[0]
        for v in (branch, frm, rel, to):
            if not SAFE.match(v or ""):
                return {}
        out = self.repo(world, "why", branch, frm, rel, to)
        born = re.search(r"born_at:\s*epoch\s*(\d+)", out)
        commits = HEX.findall(out)
        return {
            "born_epoch": int(born.group(1)) if born else None,
            "commit": commits[0] if commits else None,
            "related_commits": commits,
            "output": out,
        }

    def compare(self, q):
        world = q.get("world", [""])[0]
        a, b = q.get("a", [""])[0], q.get("b", [""])[0]
        if not (SAFE.match(a or "") and SAFE.match(b or "")):
            return {}
        out = self.repo(world, "branch", "compare", a, b)
        anc = re.search(r"common ancestor:\s*([0-9a-f]+)", out)
        status = re.search(r"status:\s*(\w+)", out)
        left = re.search(r"left \([^)]*\):\s*([0-9a-f]+)\s+(.*)", out)
        right = re.search(r"right \([^)]*\):\s*([0-9a-f]+)\s+(.*)", out)
        conflicts = re.findall(r"^\s{2,}(\w+:.*)$", out, re.M)
        return {
            "ancestor": anc.group(1) if anc else "-",
            "status": status.group(1) if status else "Equal",
            "left": {"branch": a, "commit": left.group(1) if left else "-", "fact": left.group(2).strip() if left else ""},
            "right": {"branch": b, "commit": right.group(1) if right else "-", "fact": right.group(2).strip() if right else ""},
            "conflicts": [c for c in conflicts if "->" not in c][:6],
            "outcome": {},
            "output": out,
        }

    def fsck(self, q):
        world = q.get("world", [""])[0]
        out = self.repo(world, "fsck")
        def num(label):
            m = re.search(label + r":\s*(\d+)", out)
            return int(m.group(1)) if m else 0
        status = re.search(r"status:\s*(\w+)", out)
        return {
            "objects": num("objects checked"), "commits": num("commits checked"),
            "snapshots": num("snapshots checked"), "refs": num("branch refs"),
            "status": status.group(1) if status else "clean", "output": out,
        }

    def sentinel(self, q):
        world = q.get("world", [""])[0]
        store = self.store_for(world)
        if not store:
            return {"output": "no store for this world"}
        out = self.engine(["sentinel", "boot", store])
        return {"output": out}


class Handler(SimpleHTTPRequestHandler):
    app = None

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s\n" % (fmt % args))

    def _send(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path.startswith("/api/"):
            q = urllib.parse.parse_qs(parsed.query)
            ep = parsed.path[5:]
            try:
                if ep == "health":
                    return self._send(self.app.health())
                if ep == "history":
                    return self._send(self.app.history(q))
                if ep == "why":
                    return self._send(self.app.why(q))
                if ep == "compare":
                    return self._send(self.app.compare(q))
                if ep == "fsck":
                    return self._send(self.app.fsck(q))
                if ep == "sentinel":
                    return self._send(self.app.sentinel(q))
                return self._send({"error": "unknown endpoint"}, 404)
            except Exception as e:  # noqa: BLE001 - dev server, surface the error
                return self._send({"error": str(e)}, 500)
        return super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/run":
            length = int(self.headers.get("Content-Length", 0))
            try:
                body = json.loads(self.rfile.read(length) or b"{}")
                return self._send(self.app.run(body))
            except Exception as e:  # noqa: BLE001
                return self._send({"error": str(e)}, 500)
        self._send({"error": "not found"}, 404)


def main():
    ap = argparse.ArgumentParser(description="Pointerverse causal IDE dev server")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--bin", default=os.path.join(REPO_ROOT, "build", "pointerverse"),
                    help="path to the pointerverse binary")
    args = ap.parse_args()

    Handler.app = App(os.path.abspath(args.bin))
    handler = partial(Handler, directory=UI_DIR)
    httpd = ThreadingHTTPServer((args.host, args.port), handler)

    live = Handler.app.binary_ok()
    url = f"http://{args.host}:{args.port}/"
    print("\n  Pointerverse - causal IDE")
    print("  " + "-" * 30)
    print(f"  serving   {url}")
    print(f"  ui dir    {UI_DIR}")
    print(f"  engine    {args.bin}  [{'live' if live else 'not built - demo data only'}]")
    if not live:
        print("  hint      build the engine first:  cmake --build build")
    print("\n  press Ctrl+C to stop\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n  stopped.\n")
        httpd.server_close()


if __name__ == "__main__":
    main()
