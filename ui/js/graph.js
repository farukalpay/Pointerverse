/* ============================================================
   graph.js - a dependency-free SVG graph renderer.

   Three modes share one canvas:
     causal  - objects as nodes, relations as role-colored edges
     law     - the same graph with violating edges/nodes lit red
     commit  - the content-addressed commit DAG for the active branch

   Layout is a deterministic Fruchterman-Reingold pass (fixed seed,
   fixed iterations) so a given world always lays out the same way -
   stable for live editing and reproducible for screenshots.
   ============================================================ */

const PVGraph = (() => {
  const SVGNS = 'http://www.w3.org/2000/svg';
  const ROLE_COLOR = { Generative: '#46d17a', Inhibitory: '#ff7b5d', Structural: '#6aa8ff' };
  const TYPE_PALETTE = ['#7c8cff', '#4fd1c5', '#f1c150', '#c792ea', '#6aa8ff', '#46d17a', '#ff9e7d', '#e07b9a'];

  let svg, canvas, cb = {};
  let mode = 'causal';
  let vb = { x: 0, y: 0, w: 1000, h: 700 };
  let posCache = new Map();
  let lastNodeKey = '';
  let current = null;     // last render payload
  let selected = null;    // {kind:'node'|'edge', id}

  function el(name, attrs) {
    const e = document.createElementNS(SVGNS, name);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    return e;
  }
  function applyVB() { svg.setAttribute('viewBox', `${vb.x} ${vb.y} ${vb.w} ${vb.h}`); }

  function mulberry32(a) {
    return function () {
      a |= 0; a = (a + 0x6D2B79F5) | 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  function hashStr(s) { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }

  function nodeSize(name) { return { w: Math.max(58, name.length * 6.7 + 34), h: 30 }; }
  function typeColor(type) { return TYPE_PALETTE[hashStr(type || 'x') % TYPE_PALETTE.length]; }

  // ---- deterministic force layout for causal/law ----
  function layout(nodes, edges) {
    const names = nodes.map((n) => n.name);
    const key = names.slice().sort().join('|');
    if (key === lastNodeKey && posCache.size >= nodes.length) {
      nodes.forEach((n) => { const p = posCache.get(n.name); if (p) { n.x = p.x; n.y = p.y; } });
      return;
    }
    lastNodeKey = key;
    const n = nodes.length || 1;
    const W = 1100, H = 760;
    const k = 0.82 * Math.sqrt((W * H) / n);
    const rnd = mulberry32(hashStr(key) || 1);
    const idx = new Map();
    nodes.forEach((nd, i) => {
      idx.set(nd.name, i);
      const a = rnd() * Math.PI * 2, r = 80 + rnd() * (Math.min(W, H) * 0.42);
      nd.x = W / 2 + Math.cos(a) * r;
      nd.y = H / 2 + Math.sin(a) * r;
    });
    const E = edges.map((e) => [idx.get(e.from), idx.get(e.to)]).filter((p) => p[0] != null && p[1] != null);
    let t = W / 8;
    const ITER = 420;
    for (let it = 0; it < ITER; it++) {
      const dx = new Array(n).fill(0), dy = new Array(n).fill(0);
      for (let i = 0; i < n; i++) for (let j = i + 1; j < n; j++) {
        let ddx = nodes[i].x - nodes[j].x, ddy = nodes[i].y - nodes[j].y;
        let d = Math.hypot(ddx, ddy) || 0.01;
        const f = (k * k) / d;
        ddx /= d; ddy /= d;
        dx[i] += ddx * f; dy[i] += ddy * f; dx[j] -= ddx * f; dy[j] -= ddy * f;
      }
      for (const [a, b] of E) {
        let ddx = nodes[a].x - nodes[b].x, ddy = nodes[a].y - nodes[b].y;
        let d = Math.hypot(ddx, ddy) || 0.01;
        const f = (d * d) / k;
        ddx /= d; ddy /= d;
        dx[a] -= ddx * f; dy[a] -= ddy * f; dx[b] += ddx * f; dy[b] += ddy * f;
      }
      for (let i = 0; i < n; i++) {
        // gravity to center keeps the cluster compact so it fills the panel
        dx[i] += (W / 2 - nodes[i].x) * 0.022;
        dy[i] += (H / 2 - nodes[i].y) * 0.022;
        const d = Math.hypot(dx[i], dy[i]) || 0.01;
        nodes[i].x += (dx[i] / d) * Math.min(d, t);
        nodes[i].y += (dy[i] / d) * Math.min(d, t);
      }
      t *= 0.985;
    }
    posCache = new Map(nodes.map((nd) => [nd.name, { x: nd.x, y: nd.y }]));
  }

  function clipToBox(cx, cy, w, h, tx, ty) {
    let dx = tx - cx, dy = ty - cy;
    if (dx === 0 && dy === 0) return { x: cx, y: cy };
    const hw = w / 2 + 3, hh = h / 2 + 3;
    const sx = dx === 0 ? Infinity : hw / Math.abs(dx);
    const sy = dy === 0 ? Infinity : hh / Math.abs(dy);
    const s = Math.min(sx, sy);
    return { x: cx + dx * s, y: cy + dy * s };
  }

  function fitTo(pts, pad) {
    if (!pts.length) { vb = { x: 0, y: 0, w: 1000, h: 700 }; applyVB(); return; }
    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    pts.forEach((p) => { minX = Math.min(minX, p.x); minY = Math.min(minY, p.y); maxX = Math.max(maxX, p.x); maxY = Math.max(maxY, p.y); });
    pad = pad || 70;
    const w = Math.max(220, maxX - minX + pad * 2), h = Math.max(180, maxY - minY + pad * 2);
    // match canvas aspect ratio so nothing is squashed
    const rect = canvas.getBoundingClientRect();
    const ar = rect.width / Math.max(1, rect.height);
    let W = w, H = h;
    if (W / H > ar) H = W / ar; else W = H * ar;
    vb = { x: minX - pad - (W - w) / 2, y: minY - pad - (H - h) / 2, w: W, h: H };
    applyVB();
  }

  // ============================ render ============================
  function render(payload) {
    current = payload || current;
    if (!current) return;
    if (mode === 'commit') return renderCommit();
    return renderCausal();
  }

  function renderCausal() {
    const { model, lawResults } = current;
    const lawMode = mode === 'law';
    const nodes = [...model.objects.values()];
    const edges = model.links;
    document.getElementById('graphEmpty').hidden = nodes.length > 0;
    layout(nodes, edges);
    const byName = new Map(nodes.map((n) => [n.name, n]));

    // which links violate (law mode highlight)
    const violSet = new Set();
    const violNodes = new Set();
    (lawResults || []).forEach((r) => r.violations.forEach((v) => {
      const lk = edges[v.linkIndex];
      if (lk) { violSet.add(lk); violNodes.add(lk.from); violNodes.add(lk.to); }
    }));

    while (svg.firstChild) svg.removeChild(svg.firstChild);
    svg.appendChild(buildDefs());
    const gEdges = el('g', {}), gNodes = el('g', {});

    // group parallel edges to offset curvature
    const pairCount = new Map();
    edges.forEach((e) => {
      const a = byName.get(e.from), b = byName.get(e.to);
      if (!a || !b) return;
      const pk = [e.from, e.to].sort().join('~');
      const ord = pairCount.get(pk) || 0; pairCount.set(pk, ord + 1);
      const role = e.role && ROLE_COLOR[e.role] ? e.role : null;
      const isViol = violSet.has(e);
      const dim = lawMode && !isViol;

      const ex = clipToBox(a.x, a.y, nodeSize(a.name).w, nodeSize(a.name).h, b.x, b.y);
      const sx = clipToBox(b.x, b.y, nodeSize(b.name).w, nodeSize(b.name).h, a.x, a.y);
      const mx = (ex.x + sx.x) / 2, my = (ex.y + sx.y) / 2;
      let nx = -(sx.y - ex.y), ny = (sx.x - ex.x);
      const nl = Math.hypot(nx, ny) || 1; nx /= nl; ny /= nl;
      const off = (ord % 2 === 0 ? 1 : -1) * (18 + Math.floor(ord / 2) * 22) * (e.from < e.to ? 1 : -1);
      const cx = mx + nx * off, cy = my + ny * off;
      const d = `M ${ex.x.toFixed(1)} ${ex.y.toFixed(1)} Q ${cx.toFixed(1)} ${cy.toFixed(1)} ${sx.x.toFixed(1)} ${sx.y.toFixed(1)}`;

      let cls = 'gedge' + (role ? ' role-' + role : '') + (isViol ? ' viol' : '') + (dim ? ' dim' : '');
      const marker = isViol ? 'arrow-viol' : (role ? 'arrow-' + role.slice(0, 3).toLowerCase() : 'arrow-def');
      const path = el('path', { d, class: cls, 'marker-end': `url(#${marker})` });
      const hit = el('path', { d, class: 'gedge-hit', 'data-edge': '1' });
      hit.__edge = e;
      hit.addEventListener('click', (ev) => { ev.stopPropagation(); if (panned) return; selectEdge(e); });
      gEdges.appendChild(path); gEdges.appendChild(hit);

      // edge label
      const lx = mx + nx * (off * 0.5), ly = my + ny * (off * 0.5);
      const lab = el('text', { x: lx.toFixed(1), y: (ly - 2).toFixed(1), class: 'gedge-label' + (isViol ? ' viol' : ''), 'text-anchor': 'middle' });
      lab.textContent = e.rel;
      if (!dim) gEdges.appendChild(lab);
    });

    // nodes
    nodes.forEach((nd) => {
      const { w, h } = nodeSize(nd.name);
      const dim = lawMode && violNodes.size && !violNodes.has(nd.name);
      const isViol = lawMode && violNodes.has(nd.name);
      const g = el('g', { class: 'gnode' + (isViol ? ' viol' : '') + (dim ? ' dim' : '') + (selected && selected.kind === 'node' && selected.id === nd.name ? ' sel' : ''), 'data-node': nd.name });
      g.__node = nd;
      g.appendChild(el('rect', { x: (nd.x - w / 2).toFixed(1), y: (nd.y - h / 2).toFixed(1), width: w, height: h, rx: 8, class: 'gnode-box' }));
      g.appendChild(el('circle', { cx: (nd.x - w / 2 + 13).toFixed(1), cy: nd.y.toFixed(1), r: 4, fill: typeColor(nd.type), class: 'gnode-dot' }));
      const name = el('text', { x: (nd.x - w / 2 + 23).toFixed(1), y: (nd.y - 3).toFixed(1), class: 'gnode-name' });
      name.textContent = nd.name.length > 16 ? nd.name.slice(0, 15) + '…' : nd.name;
      const type = el('text', { x: (nd.x - w / 2 + 23).toFixed(1), y: (nd.y + 8).toFixed(1), class: 'gnode-type' });
      type.textContent = nd.type;
      g.appendChild(name); g.appendChild(type);
      g.addEventListener('click', (ev) => { ev.stopPropagation(); if (panned) return; selectNode(nd.name); });
      gNodes.appendChild(g);
    });

    svg.appendChild(gEdges); svg.appendChild(gNodes);
    if (current.refit !== false) fitTo(nodes.map((n) => ({ x: n.x, y: n.y })), 58);
    renderLegend(lawMode, lawResults);
  }

  function renderCommit() {
    const hist = current.history || [];
    document.getElementById('graphEmpty').hidden = hist.length > 0;
    while (svg.firstChild) svg.removeChild(svg.firstChild);
    svg.appendChild(buildDefs());
    const g = el('g', {});
    const x0 = 120, top = 56, gap = 58;
    const pts = [];
    hist.forEach((c, i) => {
      const y = top + i * gap;
      pts.push({ x: x0, y });
      if (i > 0) {
        const path = el('path', { d: `M ${x0} ${top + (i - 1) * gap} L ${x0} ${y}`, class: 'gedge role-Structural' });
        g.appendChild(path);
      }
    });
    hist.forEach((c, i) => {
      const y = top + i * gap;
      let dotColor = '#7c8cff';
      if (c.kind === 'violation') dotColor = '#ff5d6c';
      else if (c.kind === 'pass') dotColor = '#46d17a';
      else if (c.kind === 'merge') dotColor = '#4fd1c5';
      else if (c.kind === 'genesis') dotColor = '#6f7d96';
      const ring = el('circle', { cx: x0, cy: y, r: 11, fill: 'none', stroke: dotColor, 'stroke-width': c.kind === 'violation' ? 2.4 : 1.4, opacity: 0.5 });
      const dot = el('circle', { cx: x0, cy: y, r: 6, fill: dotColor });
      if (c.kind === 'violation') dot.setAttribute('filter', 'drop-shadow(0 0 6px #ff5d6c)');
      g.appendChild(ring); g.appendChild(dot);
      const hash = el('text', { x: x0 + 26, y: y - 6, class: 'gcommit-hash' });
      hash.textContent = c.hash;
      const msg = el('text', { x: x0 + 26, y: y + 8, class: 'gcommit-msg' });
      let m = c.msg;
      if (m.length > 46) m = m.slice(0, 45) + '…';
      msg.textContent = m;
      if (c.kind === 'violation') { msg.setAttribute('fill', '#ff9aa2'); }
      g.appendChild(hash); g.appendChild(msg);
      // epoch chip
      const ep = el('text', { x: x0 - 26, y: y + 4, class: 'gcommit-hash', 'text-anchor': 'end', fill: '#6f7d96' });
      ep.textContent = 'e' + c.epoch;
      g.appendChild(ep);
    });
    svg.appendChild(g);
    const maxY = top + Math.max(0, hist.length - 1) * gap;
    const rect = canvas.getBoundingClientRect();
    const ar = rect.width / Math.max(1, rect.height);
    let H = maxY + 90, W = H * ar;
    if (W < 460) { W = 460; H = W / ar; }
    vb = { x: 0, y: 0, w: W, h: H };
    applyVB();
    renderLegendCommit();
  }

  function buildDefs() {
    const defs = el('defs', {});
    const markers = [
      ['arrow-def', '#8a93a8'], ['arrow-gen', '#46d17a'], ['arrow-inh', '#ff7b5d'],
      ['arrow-str', '#6aa8ff'], ['arrow-viol', '#ff5d6c'],
    ];
    markers.forEach(([id, color]) => {
      const m = el('marker', { id, viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' });
      m.appendChild(el('path', { d: 'M 0 0 L 10 5 L 0 10 z', fill: color }));
      defs.appendChild(m);
    });
    return defs;
  }

  function renderLegend(lawMode, lawResults) {
    const lg = document.getElementById('graphLegend');
    if (lawMode) {
      const fails = (lawResults || []).filter((r) => r.status === 'fail').length;
      lg.innerHTML = `
        <div class="lg-row"><span class="lg-line" style="border-color:#ff5d6c"></span> law violation${fails === 1 ? '' : 's'} (${fails})</div>
        <div class="lg-row"><span class="lg-line" style="border-color:#46d17a"></span> Generative</div>
        <div class="lg-row"><span class="lg-line" style="border-color:#ff7b5d"></span> Inhibitory</div>`;
    } else {
      lg.innerHTML = `
        <div class="lg-row"><span class="lg-line" style="border-color:#46d17a"></span> Generative</div>
        <div class="lg-row"><span class="lg-line" style="border-color:#ff7b5d"></span> Inhibitory</div>
        <div class="lg-row"><span class="lg-line" style="border-color:#6aa8ff"></span> Structural</div>`;
    }
    lg.hidden = false;
  }
  function renderLegendCommit() {
    const lg = document.getElementById('graphLegend');
    lg.innerHTML = `
      <div class="lg-row"><span class="dot" style="background:#ff5d6c"></span> rejected by a law</div>
      <div class="lg-row"><span class="dot" style="background:#46d17a"></span> law satisfied</div>
      <div class="lg-row"><span class="dot" style="background:#4fd1c5"></span> fork point</div>`;
    lg.hidden = false;
  }

  // ---- selection ----
  function selectNode(name) { selected = { kind: 'node', id: name }; render(); cb.onSelectNode && cb.onSelectNode(name); }
  function selectEdge(e) { selected = { kind: 'edge', id: `${e.from}:${e.rel}:${e.to}` }; cb.onSelectEdge && cb.onSelectEdge(e); render(); }
  function clearSelection() { selected = null; render(); }

  // ---- pan / zoom ----
  let panning = false, panned = false, sx0 = 0, sy0 = 0, vbx0 = 0, vby0 = 0;
  function toSvgScale() { const r = canvas.getBoundingClientRect(); return vb.w / r.width; }
  function setupInteractions() {
    canvas.addEventListener('pointerdown', (e) => {
      panning = true; panned = false; sx0 = e.clientX; sy0 = e.clientY; vbx0 = vb.x; vby0 = vb.y;
      canvas.setPointerCapture(e.pointerId); canvas.classList.add('is-panning');
    });
    canvas.addEventListener('pointermove', (e) => {
      if (!panning) return;
      const s = toSvgScale();
      const ddx = (e.clientX - sx0), ddy = (e.clientY - sy0);
      if (Math.abs(ddx) + Math.abs(ddy) > 4) panned = true;
      vb.x = vbx0 - ddx * s; vb.y = vby0 - ddy * s; applyVB();
    });
    const end = (e) => { panning = false; canvas.classList.remove('is-panning'); };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
    canvas.addEventListener('click', (e) => { if (!panned && (e.target === svg || e.target === canvas)) clearSelection(); });
    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const r = canvas.getBoundingClientRect();
      const fx = (e.clientX - r.left) / r.width, fy = (e.clientY - r.top) / r.height;
      const factor = e.deltaY > 0 ? 1.12 : 0.89;
      const px = vb.x + fx * vb.w, py = vb.y + fy * vb.h;
      vb.w = Math.min(8000, Math.max(120, vb.w * factor));
      vb.h = Math.min(6000, Math.max(90, vb.h * factor));
      vb.x = px - fx * vb.w; vb.y = py - fy * vb.h; applyVB();
    }, { passive: false });
  }

  function zoom(factor) {
    const cx = vb.x + vb.w / 2, cy = vb.y + vb.h / 2;
    vb.w = Math.min(8000, Math.max(120, vb.w * factor));
    vb.h = Math.min(6000, Math.max(90, vb.h * factor));
    vb.x = cx - vb.w / 2; vb.y = cy - vb.h / 2; applyVB();
  }
  function fit() { if (mode === 'commit') { render(); } else if (current) { const ns = [...current.model.objects.values()]; layout(ns, current.model.links); fitTo(ns.map((n) => ({ x: n.x, y: n.y })), 58); } }

  function setMode(m) { mode = m; selected = null; render(); }
  function getMode() { return mode; }
  function resetLayout() { posCache = new Map(); lastNodeKey = ''; }

  function init(svgEl, canvasEl, callbacks) {
    svg = svgEl; canvas = canvasEl; cb = callbacks || {};
    applyVB(); setupInteractions();
  }

  return { init, render, setMode, getMode, fit, zoom, selectNode, clearSelection, resetLayout };
})();
