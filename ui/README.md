# Pointerverse workbench

A browser causal IDE for Pointerverse: write a world, define laws, run the
consequences into commits, compare branches, and ask why any fact exists.

It is plain static files (no build step, no npm) plus an optional standard-library
server that bridges the panels to the real `pointerverse` binary.

## Run it

Demo data only - renders the full UI from the bundled transcripts, needs nothing
but Python:

```sh
python3 -m http.server 8787 --directory ui
# open http://localhost:8787
```

Driven by the engine - Run, Why, Compare, Replay, Audit, and Sentinel call the
actual binary (build it first):

```sh
cmake --build build
python3 ui/serve.py            # http://localhost:8787
python3 ui/serve.py --port 9000 --bin ./build/pointerverse
```

`serve.py` binds localhost only and falls back to the demo data when a command is
unavailable, so the UI never looks empty.

## Layout

```
ui/
  index.html        single-page workbench
  css/app.css        dark, dense, terminal-grade theme
  js/
    data.js          seeded worlds + captured engine transcripts
    pv.js            .pv parser, tokenizer, client-side law checker
    editor.js        syntax-highlighted editor
    graph.js         SVG graph: causal / commit / law views
    api.js           bridge to serve.py (degrades to demo data)
    app.js           the IDE shell and wiring
  serve.py           stdlib dev server + engine bridge
```

The graph, the editor highlighting, and the instant law feedback all run in the
browser. When `serve.py` is up, the binary is the source of truth and these
become the fallback. There is no framework and no bundler - open `index.html`
and it works.
