/* ============================================================
   app.js - the IDE shell. State, panels, actions, and wiring.
   ============================================================ */

(() => {
  const $ = (id) => document.getElementById(id);
  const state = { worldId: null, world: null, branch: null, file: null, model: null, lawResults: [] };

  let editor, els, bundledBranches;

  document.addEventListener('DOMContentLoaded', boot);

  async function boot() {
    els = {
      editor: $('editor'), hlCode: $('hlCode'), hl: $('hl'), gutter: $('gutter'),
      out: $('paneOutput'), laws: $('paneLaws'), problems: $('paneProblems'), agent: $('paneAgent'),
    };
    editor = PVEditor.init({ textarea: els.editor, hlCode: els.hlCode, hl: els.hl, gutter: els.gutter }, onEdit);
    PVGraph.init($('graphSvg'), $('graphCanvas'), { onSelectNode, onSelectEdge });
    bundledBranches = new Map(PV.WORLDS.map((w) => [w.id, w.branches.map((b) => Object.assign({}, b))]));
    PVStorage.hydrate(PV.WORLDS);
    injectMobileNote();
    wireChrome();
    wireActions();
    wirePalette();
    wireKeys();

    await PVApi.probe();
    setMode(PVApi.isLive() ? 'live' : 'static');

    renderAgentPane();
    const saved = PVStorage.session();
    const initialWorld = PV.byId(saved.worldId) ? saved.worldId : 'mohacs';
    loadWorld(initialWorld, saved.branch || (PV.byId(initialWorld) || {}).defaultBranch || 'historical');
    if (saved.file && state.world && state.world.files[saved.file] != null) openFile(saved.file, true);
    bootBanner();
  }

  function setMode(m) {
    document.body.dataset.mode = m;
    $('dockConnLabel').textContent = m === 'live' ? `engine: ${PVApi.engineVersion() || 'pointerverse'}` : 'demo data';
    $('stEngine').textContent = m === 'live' ? 'live engine' : 'Pointerverse';
  }

  /* ----------------------------- world / files ----------------------------- */
  function loadWorld(id, branch) {
    const w = PV.byId(id);
    if (!w) return;
    state.worldId = id; state.world = w;
    state.branch = branch || w.defaultBranch;
    PVGraph.resetLayout();
    resetInspector();
    renderTree();
    const br = w.branches.find((b) => b.name === state.branch) || w.branches[0];
    openFile(br.file, false);
    renderBranchTabs();
    refresh();
    PVGraph.fit();
  }

  function openFile(file, doRefresh) {
    state.file = file;
    editor.setValue(PVStorage.fileValue(state.worldId, file, state.world.files[file] || ''));
    // map file -> branch when one matches
    const br = state.world.branches.find((b) => b.file === file && b.name === state.branch)
      || state.world.branches.find((b) => b.file === file);
    if (br) state.branch = br.name;
    $('crumbFile').textContent = file;
    showEditor();
    renderTree();
    saveSession();
    if (doRefresh !== false) refresh();
  }

  function onEdit() {
    PVStorage.saveFile(state.worldId, state.file, editor.getValue(), state.world.files[state.file] || '');
    refresh();
  }

  function saveSession() {
    if (!state.worldId || !state.branch || !state.file) return;
    PVStorage.saveSession({ worldId: state.worldId, branch: state.branch, file: state.file });
  }

  function refresh() {
    const w = state.world;
    state.model = PVParse.parse(editor.getValue());
    state.lawResults = PVParse.evalLaws(state.model, w.laws);
    editor.setViolations(PVParse.violationLines(state.lawResults));
    PVGraph.render({ model: state.model, lawResults: state.lawResults, history: PV.history[`${w.id}_${state.branch}`] || PV.history.mohacs_historical });
    renderLawsPane();
    renderProblemsPane();
    renderBranchTabs();
    updateStatus();
    updateBreadcrumb();
  }

  /* ----------------------------- left rail ----------------------------- */
  function renderTree() {
    const w = state.world;
    // worlds
    $('treeWorlds').innerHTML = PV.WORLDS.map((x) => `
      <li data-world="${x.id}" class="${x.id === state.worldId ? 'is-active' : ''}" title="${x.title}">
        ${icon(x.surface === 'realms' ? 'globe' : x.surface === 'audit' ? 'pulse' : 'cube')}
        <span>${x.id}</span><span class="t-meta">${x.surface}</span>
      </li>`).join('');
    [...$('treeWorlds').children].forEach((li) => li.onclick = () => loadWorld(li.dataset.world));

    // files
    $('treeFiles').innerHTML = Object.keys(w.files).map((f) => `
      <li data-file="${f}" class="tree-mono ${f === state.file ? 'is-active' : ''}">${icon('file')}<span>${f}</span></li>`).join('');
    [...$('treeFiles').children].forEach((li) => li.onclick = () => openFile(li.dataset.file));

    // branches
    $('treeBranches').innerHTML = w.branches.map((b) => `
      <li data-branch="${b.name}" data-status="${b.status}" class="${b.name === state.branch ? 'is-active' : ''}">
        <span class="br-dot"></span><span class="tree-mono">${b.name}</span>
        <span class="t-meta">${b.commit.slice(0, 7)}</span></li>`).join('');
    [...$('treeBranches').children].forEach((li) => li.onclick = () => selectBranch(li.dataset.branch));

    // laws
    $('treeLaws').innerHTML = (w.laws || []).map((law) => {
      const r = state.lawResults.find((x) => x.name === law.name);
      const st = r ? r.status : 'stable';
      const pill = st === 'fail' ? 'fail' : 'pass';
      const label = st === 'fail' ? 'FAIL' : (st === 'pass' ? 'PASS' : 'IDLE');
      return `<li title="${esc(law.when || '')}"><span class="law-pill ${pill}">${label}</span><span class="law-name">${law.name}</span></li>`;
    }).join('');
  }

  function selectBranch(name) {
    const w = state.world;
    const br = w.branches.find((b) => b.name === name);
    if (!br) return;
    state.branch = name;
    openFile(br.file);
    saveSession();
    toast(`Checked out ${name}`, `${br.commit} - epoch ${br.epoch}`, br.status === 'fail' ? 'warn' : 'ok');
  }

  function renderBranchTabs() {
    const w = state.world;
    const files = Object.keys(w.files);
    const tabs = files.map((f) => `
      <button class="tab ${f === state.file ? 'is-active' : ''}" data-file="${f}">
        <span class="tab-dot" style="background:${f === state.file ? 'var(--accent)' : 'var(--tx-3)'}"></span>${f}</button>`).join('');
    const timelineTab = `<button class="tab" data-kind="timeline" id="tabTimeline">${icon('pulse')} Timeline</button>`;
    $('tabstrip').innerHTML = tabs + timelineTab;
    [...$('tabstrip').querySelectorAll('.tab[data-file]')].forEach((t) => t.onclick = () => openFile(t.dataset.file));
    $('tabTimeline').onclick = () => showTimeline();
  }

  /* ----------------------------- center views ----------------------------- */
  function showEditor() { $('tabTimeline') && $('tabTimeline').classList.remove('is-active'); document.querySelector('.center').classList.remove('show-timeline'); }

  function showTimeline() {
    document.querySelector('.center').classList.add('show-timeline');
    [...$('tabstrip').querySelectorAll('.tab')].forEach((t) => t.classList.remove('is-active'));
    $('tabTimeline').classList.add('is-active');
    renderTimeline();
  }

  function renderTimeline() {
    const w = state.world;
    const wrap = $('timelineWrap');
    const branchesToShow = w.branches.filter((b) => b.name !== 'main');
    let html = '';
    branchesToShow.forEach((b) => {
      const audit = PV.rustAudit && PV.rustAudit[b.name];
      let rows, firstBroke;
      if (audit) {
        rows = audit.events.map((e) => ({ t: e.t, text: e.text, kind: e.kind }));
        firstBroke = audit.firstBroke;
      } else {
        const m = PVParse.parse(w.files[b.file] || '');
        const lr = PVParse.evalLaws(m, w.laws);
        const violLines = PVParse.violationLines(lr);
        rows = m.links.map((lk, i) => ({
          t: 'e' + (b.epoch - m.links.length + i + 1),
          text: `${lk.from} ${lk.rel} ${lk.to}`,
          kind: violLines.has(lk.line) ? 'fail' : (lk.role === 'Inhibitory' ? '' : 'ok'),
        }));
        const firstViol = lr.find((r) => r.status === 'fail');
        firstBroke = firstViol ? { law: firstViol.name, reason: firstViol.violations[0].reason } : null;
      }
      html += `<div class="tl-branch"><h4>${icon('branch')} ${b.name} <span style="color:var(--tx-3);font-weight:400">- ${b.commit.slice(0, 7)}</span></h4>`;
      if (firstBroke && firstBroke.law) {
        html += `<div class="tl-fail-banner">first broken rule: <b>${firstBroke.law}</b><br>${esc(firstBroke.reason)}</div>`;
      } else {
        html += `<div class="tl-fail-banner" style="background:var(--ok-soft);border-color:rgba(70,209,122,.3);color:#bff0cf">no law broke on this branch - the line holds</div>`;
      }
      html += rows.map((r) => `
        <div class="tl-row"><div class="tl-time">${r.t}</div>
        <div class="tl-node ${r.kind ? 'evt-' + r.kind : ''}"><div class="tl-text">${fmtFact(r.text)}</div></div></div>`).join('');
      html += '</div>';
    });
    wrap.innerHTML = html || '<div class="insp-placeholder">No forked branches to chart.</div>';
  }

  /* ----------------------------- inspector / why ----------------------------- */
  function onSelectNode(name) {
    const o = state.model.objects.get(name);
    if (!o) return;
    const inc = state.model.links.filter((l) => l.to === name);
    const out = state.model.links.filter((l) => l.from === name);
    $('inspTitle').textContent = 'Object';
    $('inspHint').textContent = o.inferred ? 'inferred from a relation' : o.type;
    const attrs = Object.entries(o.attrs);
    $('inspBody').innerHTML = `
      <div class="why-q"><span class="why-obj">${name}</span> <span style="color:var(--tx-2)">:</span> <span class="t-type" style="color:var(--accent-2)">${o.type}</span></div>
      <dl class="kv">
        <dt>incoming</dt><dd>${inc.length}</dd>
        <dt>outgoing</dt><dd>${out.length}</dd>
        ${attrs.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${esc(String(v))}</dd>`).join('')}
      </dl>
      ${out.length ? `<div class="why-from-title">relations out</div>${out.slice(0, 6).map((l) => `<div class="why-fact">${name} <span class="wf-rel">${l.rel}</span> ${l.to}</div>`).join('')}` : ''}`;
  }

  async function onSelectEdge(link) {
    $('inspTitle').textContent = 'Why does this fact exist?';
    $('inspHint').textContent = `${state.branch}`;
    const base = PVParse.whyFor(state.model, link, state.worldId, state.branch, branchBaseEpoch());
    let info = base;
    const live = await PVApi.why({ worldId: state.worldId, branch: state.branch, link });
    if (live) info = Object.assign({}, base, { bornEpoch: live.bornEpoch, commit: live.commit });
    $('inspBody').innerHTML = `
      <div class="why-q"><span class="why-obj">${link.from}</span> <span class="why-rel">${link.rel}</span> <span class="why-obj">${link.to}</span></div>
      <dl class="kv">
        <dt>born at</dt><dd>epoch ${info.bornEpoch}</dd>
        <dt>commit</dt><dd class="hash">${info.commit}</dd>
        <dt>branch</dt><dd>${info.branch}</dd>
        ${link.role ? `<dt>role</dt><dd>${link.role}</dd>` : ''}
        ${link.attrs && link.attrs.weight ? `<dt>weight</dt><dd>${esc(link.attrs.weight)}</dd>` : (link.weight != null ? `<dt>weight</dt><dd>${link.weight}</dd>` : '')}
      </dl>
      <div class="why-from-title">previous related facts</div>
      ${info.related.length ? info.related.map((r) => `<div class="why-fact">${r.from} <span class="wf-rel">${r.rel}</span> ${r.to}</div>`).join('')
        : '<div class="insp-placeholder">This is a root fact - nothing precedes it.</div>'}`;
  }

  function branchBaseEpoch() { const b = state.world.branches.find((x) => x.name === state.branch); return b ? Math.max(0, b.epoch - state.model.links.length) : 20; }

  function resetInspector() {
    $('inspTitle').textContent = 'Inspector';
    $('inspHint').textContent = 'Select a node or edge';
    $('inspBody').innerHTML = '<div class="insp-placeholder">Click any node or relation in the graph to ask <em>why this fact exists</em> - its birth epoch, commit, branch, and the facts it came from.</div>';
  }

  /* ----------------------------- dock panes ----------------------------- */
  function renderLawsPane() {
    const fails = state.lawResults.filter((r) => r.status === 'fail').length;
    setBadge('lawBadge', fails, false);
    els.laws.innerHTML = state.lawResults.map((r) => {
      const cls = r.status === 'fail' ? 'fail' : 'pass';
      const status = r.status === 'fail' ? 'FAILED' : (r.status === 'pass' ? 'PASSED' : 'STABLE');
      const reason = r.status === 'fail'
        ? `<div class="lc-reason">${esc(r.violations[0].reason)}</div>`
        : (r.status === 'pass' ? `<div class="lc-reason ok">precondition satisfied before the trigger fired</div>` : `<div class="lc-reason ok" style="color:var(--tx-2)">no trigger in this world yet</div>`);
      return `<div class="law-card ${cls}">
        <span class="law-status">${status}</span>
        <div class="law-body">
          <div class="lc-name">${r.name}</div>
          <div class="lc-rule">when ${esc(r.when || '')}<br>require ${esc(r.require || '')}</div>
          ${reason}
        </div></div>`;
    }).join('') || '<div class="prob-empty">No laws declared in this world.</div>';
  }

  function renderProblemsPane() {
    const probs = [];
    state.lawResults.forEach((r) => r.violations.forEach((v) => probs.push({ law: r.name, reason: v.reason, line: v.line })));
    setBadge('probBadge', probs.length, true);
    if (!probs.length) { els.problems.innerHTML = '<div class="prob-empty">No problems. Every committed transition satisfies its laws.</div>'; return; }
    els.problems.innerHTML = probs.map((p) => `
      <div class="prob-row err" data-line="${p.line}">${icon('x-circle')}
        <div><div>${esc(p.reason)}</div><div style="color:var(--tx-3);font-family:var(--font-mono);font-size:10.5px">law ${p.law}</div></div>
        <span class="prob-loc">${state.file}:${p.line + 1}</span></div>`).join('');
    [...els.problems.querySelectorAll('.prob-row')].forEach((row) => row.onclick = () => { showEditor(); renderBranchTabs(); editor.focusLine(+row.dataset.line); });
  }

  function renderAgentPane() {
    els.agent.innerHTML = `
      <div class="agent-grid">
        <div class="agent-col">
          <h5>Agent plan - guarded branch</h5>
          <div class="agent-plan-row"><span class="step-n">1</span> direct patch repository.cpp <span class="step-r failed">failed</span></div>
          <div class="agent-plan-row"><span class="step-n">2</span> add storage regression test <span class="step-r passed">passed</span></div>
          <div class="agent-plan-row"><span class="step-n">3</span> apply patch <span class="step-r passed">passed</span></div>
          <div class="agent-plan-row"><span class="step-n">4</span> run sentinel verification <span class="step-r passed">passed</span></div>
          <div class="agent-note">An agent forks a world, tests the consequences against your laws, hits a wall, and finds the legal route - all before the real repository is touched. This is the Guard surface (<code>guard run</code>) rendered as a branch.</div>
        </div>
        <div class="agent-col">
          <h5>Active rules</h5>
          <div class="agent-rule broke">cannot modify storage without updating integrity tests</div>
          <div class="agent-rule">cannot change public API without a changelog entry</div>
          <div class="agent-rule">cannot remove sentinel checks</div>
          <div class="agent-rule">every commit must replay to the same hashes</div>
        </div>
      </div>`;
  }

  /* ----------------------------- status / breadcrumb ----------------------------- */
  function updateStatus() {
    const b = state.world.branches.find((x) => x.name === state.branch) || {};
    $('stBranch').textContent = state.branch;
    $('stEpoch').textContent = 'epoch ' + (b.epoch != null ? b.epoch : '-');
    $('stCommit').textContent = b.commit || '-';
    const fails = state.lawResults.filter((r) => r.status === 'fail').length;
    const stLaws = $('stLaws');
    stLaws.textContent = fails ? `${fails} law${fails > 1 ? 's' : ''} failed` : 'laws pass';
    stLaws.classList.toggle('ok', fails === 0);
    const integ = PV.fsck[state.worldId];
    $('stIntegrity').innerHTML = `<span class="dot dot-ok"></span> store ${integ ? integ.status : 'clean'}`;
  }
  function updateBreadcrumb() {
    $('crumbWorld').textContent = state.worldId;
    $('crumbBranch').lastChild.textContent = ' ' + state.branch;
    $('crumbFile').textContent = state.file;
  }

  /* ----------------------------- actions ----------------------------- */
  function wireActions() {
    $('actRun').onclick = act.run;
    $('actFork').onclick = act.fork;
    $('actCompare').onclick = act.compare;
    $('actReplay').onclick = act.replay;
    $('actAudit').onclick = act.audit;
    $('actSentinel').onclick = act.sentinel;
  }

  const act = {
    async run() {
      switchDock('output');
      const r = await PVApi.run({ worldId: state.worldId, branch: state.branch, src: editor.getValue() });
      await streamOutput(r.text);
      const fails = state.lawResults.filter((x) => x.status === 'fail').length;
      if (fails) toast('Run rejected a transition', `${fails} law violation${fails > 1 ? 's' : ''} - see Laws`, 'fail');
      else toast('Run committed', 'every transition satisfied its laws', 'ok');
    },
    fork() {
      const w = state.world;
      const baseName = state.branch;
      let name = baseName + '_fork', i = 2;
      while (w.branches.some((b) => b.name === name)) name = `${baseName}_fork${i++}`;
      const base = w.branches.find((b) => b.name === baseName);
      const file = state.file;
      w.branches.push({ name, file, status: base ? base.status : 'ok', epoch: base ? base.epoch : 0, commit: PVParse.shortHash(name + Date.now()), snapshot: PVParse.shortHash(name), parent: baseName });
      state.branch = name;
      PVStorage.saveBranches(w.id, w.branches, bundledBranches.get(w.id) || []);
      saveSession();
      renderTree();
      updateStatus(); updateBreadcrumb();
      toast(`Forked ${baseName} -> ${name}`, `at ${base ? base.commit : 'HEAD'} - edit and Run to diverge`, 'ok');
    },
    async compare() {
      const w = state.world;
      const others = w.branches.filter((b) => b.name !== state.branch);
      const a = state.branch;
      const b = (w.branches.find((x) => x.name === 'reinforced') && a !== 'reinforced') ? 'reinforced'
        : (others.find((x) => x.status !== (w.branches.find((y) => y.name === a) || {}).status) || others[0] || {}).name;
      if (!b) { toast('Nothing to compare', 'this world has a single branch', 'warn'); return; }
      const data = await PVApi.compare({ worldId: state.worldId, a, b });
      openCompare(a, b, data);
    },
    async replay() {
      switchDock('output');
      const f = await PVApi.fsck({ worldId: state.worldId });
      await streamOutput(`repo replay --verify\n=> replaying ${f.commits} commits across ${f.refs} branch refs\n=> ${f.objects} objects, ${f.snapshots} snapshots checked\n=> proof roots recomputed bit for bit\n=> status: ${f.status} - the whole store is reproducible`);
      toast('Replay complete', `${f.commits} commits, hashes match`, 'ok');
    },
    audit() { showTimeline(); switchDock('problems'); toast('Audit timeline', 'charting each branch and its first broken rule', 'ok'); },
    async sentinel() {
      switchDock('output');
      const s = await PVApi.sentinel({ worldId: state.worldId });
      await streamOutput(s.text);
      updateStatus();
      toast('Sentinel: store clean', 'proof chain verified, 0 mismatches', 'ok');
    },
  };

  /* ----------------------------- compare modal ----------------------------- */
  function openCompare(a, b, d) {
    const body = $('compareBody');
    const oc = d.outcome || {};
    body.innerHTML = `
      <div class="cmp-meta">
        <span class="cmp-chip"><span class="lab">common ancestor</span> ${d.ancestor}</span>
        <span class="cmp-chip ${d.status === 'Conflict' ? 'conflict' : ''}"><span class="lab">status</span> ${d.status}</span>
      </div>
      <div class="cmp-section-title">First divergent commit</div>
      <div class="cmp-diverge">
        <div class="cmp-branch-row"><span class="cmp-branch-name left">${d.left.branch}</span>
          <span class="cmp-fact"><span class="h">${d.left.commit}</span> ${fmtFact(stripLink(d.left.fact))}</span></div>
        <div class="cmp-branch-row"><span class="cmp-branch-name right">${d.right.branch}</span>
          <span class="cmp-fact"><span class="h">${d.right.commit}</span> ${fmtFact(stripLink(d.right.fact))}</span></div>
      </div>
      ${oc[a] || oc[b] ? `<div class="cmp-outcome"><div class="cmp-section-title">Outcome</div>
        ${oc[a] ? `<div class="cmp-branch-row"><span class="cmp-branch-name left">${a}</span><span class="cmp-fact res-${oc[a].kind}">${esc(oc[a].text)}</span></div>` : ''}
        ${oc[b] ? `<div class="cmp-branch-row"><span class="cmp-branch-name right">${b}</span><span class="cmp-fact res-${oc[b].kind}">${esc(oc[b].text)}</span></div>` : ''}
      </div>` : ''}
      ${d.conflicts && d.conflicts.length ? `<div class="cmp-conflicts"><div class="cmp-section-title">Object conflicts</div>
        ${d.conflicts.map((c) => `<div class="cmp-conflict">${esc(c)}</div>`).join('')}</div>` : ''}`;
    $('compareOverlay').hidden = false;
  }

  /* ----------------------------- output streaming ----------------------------- */
  let streamToken = 0;
  async function streamOutput(text) {
    const my = ++streamToken;
    els.out.innerHTML = '';
    const lines = text.split('\n');
    for (let i = 0; i < lines.length; i++) {
      if (my !== streamToken) return;
      els.out.insertAdjacentHTML('beforeend', colorLine(lines[i]) + '\n');
      els.out.scrollTop = els.out.scrollHeight;
      await new Promise((r) => setTimeout(r, 14 + Math.random() * 12));
    }
    els.out.insertAdjacentHTML('beforeend', '<span class="cursor"></span>');
  }

  function colorLine(line) {
    let s = esc(line);
    if (/rejected|error|out of|before its|before a|on the spine/i.test(line)) return `<span class="o-fail">${s}</span>`;
    if (/\bok\b|clean|passed|satisfied|holds|reproducible|match/i.test(line)) s = `<span class="o-ok">${s}</span>`;
    else if (/law\./i.test(line)) s = `<span class="o-warn">${s}</span>`;
    s = s.replace(/(=&gt;)/g, '<span class="o-arrow">$1</span>');
    s = s.replace(/\b([0-9a-f]{12})\b/g, '<span class="o-hash">$1</span>');
    return s;
  }

  function bootBanner() {
    const v = PVApi.isLive() ? (PVApi.engineVersion() || 'live') : 'demo data';
    const txt = `Pointerverse - causal IDE\n=> source: ${v}\n=> world: ${state.worldId}   branch: ${state.branch}   file: ${state.file}\n=> ${state.model.objects.size} objects, ${state.model.links.length} relations parsed\n=> press Run to record this world into commits and check it against its laws`;
    streamOutput(txt);
  }

  /* ----------------------------- dock + chrome ----------------------------- */
  function switchDock(name) {
    document.body.classList.remove('dock-min');
    [...document.querySelectorAll('.dock-tab')].forEach((t) => t.classList.toggle('is-active', t.dataset.dock === name));
    [...document.querySelectorAll('.dock-pane')].forEach((p) => p.classList.toggle('is-active', p.dataset.dock === name));
  }

  function wireChrome() {
    [...document.querySelectorAll('.dock-tab')].forEach((t) => t.onclick = () => switchDock(t.dataset.dock));
    $('dockToggle').onclick = () => document.body.classList.toggle('dock-min');
    $('railCollapse').onclick = () => document.body.classList.add('rail-collapsed');
    $('railReopen').onclick = () => document.body.classList.remove('rail-collapsed');
    [...document.querySelectorAll('.seg-btn')].forEach((b) => b.onclick = () => {
      [...document.querySelectorAll('.seg-btn')].forEach((x) => x.classList.remove('is-active'));
      b.classList.add('is-active'); PVGraph.setMode(b.dataset.gmode);
    });
    $('gZoomIn').onclick = () => PVGraph.zoom(0.82);
    $('gZoomOut').onclick = () => PVGraph.zoom(1.22);
    $('gFit').onclick = () => PVGraph.fit();
    $('compareClose').onclick = () => $('compareOverlay').hidden = true;
    $('compareOverlay').onclick = (e) => { if (e.target === $('compareOverlay')) $('compareOverlay').hidden = true; };
    $('crumbBranch').onclick = act.compare;
    window.addEventListener('resize', debounce(() => { if (PVGraph.getMode && PVGraph.getMode() !== 'commit') PVGraph.fit(); }, 200));
  }

  /* ----------------------------- command palette ----------------------------- */
  const COMMANDS = [
    { label: 'Run world', hint: 'record into commits, check laws', kbd: '⌘↵', icon: 'play', run: () => act.run() },
    { label: 'Fork branch', hint: 'alternate history from HEAD', icon: 'branch', run: () => act.fork() },
    { label: 'Compare branches', hint: 'first divergence', icon: 'compare', run: () => act.compare() },
    { label: 'Replay store', hint: 'verify bit for bit', icon: 'replay', run: () => act.replay() },
    { label: 'Audit timeline', hint: 'first broken rule per branch', icon: 'pulse', run: () => act.audit() },
    { label: 'Sentinel check', hint: 'proof chain + fsck', icon: 'shield', run: () => act.sentinel() },
    { label: 'Graph: Causal view', icon: 'globe', run: () => setGraph('causal') },
    { label: 'Graph: Commit view', icon: 'cube', run: () => setGraph('commit') },
    { label: 'Graph: Law view', icon: 'shield', run: () => setGraph('law') },
    { label: 'Load world: Mohacs 1526', icon: 'globe', run: () => loadWorld('mohacs', 'historical') },
    { label: 'Load world: Rust wipe blackbox', icon: 'pulse', run: () => loadWorld('rust_wipe_blackbox', 'main') },
    { label: 'Load world: New starter', icon: 'file', run: () => loadWorld('starter', 'main') },
  ];
  let palSel = 0, palItems = COMMANDS;
  function setGraph(m) { [...document.querySelectorAll('.seg-btn')].forEach((x) => x.classList.toggle('is-active', x.dataset.gmode === m)); PVGraph.setMode(m); }
  function wirePalette() {
    const inp = $('paletteInput');
    inp.addEventListener('input', () => renderPalette(inp.value));
    inp.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowDown') { e.preventDefault(); palSel = Math.min(palItems.length - 1, palSel + 1); renderPalette(inp.value, true); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); palSel = Math.max(0, palSel - 1); renderPalette(inp.value, true); }
      else if (e.key === 'Enter') { e.preventDefault(); if (palItems[palSel]) { closePalette(); palItems[palSel].run(); } }
      else if (e.key === 'Escape') closePalette();
    });
  }
  function openPalette() { $('paletteOverlay').hidden = false; const inp = $('paletteInput'); inp.value = ''; palSel = 0; renderPalette(''); inp.focus(); }
  function closePalette() { $('paletteOverlay').hidden = true; }
  function renderPalette(q, keepSel) {
    q = (q || '').toLowerCase();
    palItems = COMMANDS.filter((c) => c.label.toLowerCase().includes(q) || (c.hint || '').toLowerCase().includes(q));
    if (!keepSel) palSel = 0;
    palSel = Math.min(palSel, Math.max(0, palItems.length - 1));
    $('paletteList').innerHTML = palItems.map((c, i) => `
      <li class="${i === palSel ? 'sel' : ''}" data-i="${i}">${icon(c.icon)}<span>${c.label}</span>
      ${c.hint ? `<span style="color:var(--tx-3);font-size:11px">- ${c.hint}</span>` : ''}
      ${c.kbd ? `<span class="pl-kbd">${c.kbd}</span>` : ''}</li>`).join('');
    [...$('paletteList').children].forEach((li) => { li.onclick = () => { closePalette(); palItems[+li.dataset.i].run(); }; });
  }

  /* ----------------------------- keyboard ----------------------------- */
  function wireKeys() {
    document.addEventListener('keydown', (e) => {
      const meta = e.metaKey || e.ctrlKey;
      if (meta && e.key.toLowerCase() === 'k') { e.preventDefault(); openPalette(); return; }
      if (meta && e.key === 'Enter') { e.preventDefault(); act.run(); return; }
      if (e.key === 'Escape') { closePalette(); $('compareOverlay').hidden = true; PVGraph.clearSelection(); }
    });
  }

  /* ----------------------------- helpers ----------------------------- */
  function esc(s) { return PVParse.esc(String(s == null ? '' : s)); }
  function stripLink(s) { return s.replace(/^link\s+/, ''); }
  function fmtFact(text) {
    // "A rel B" -> colorize the relation
    const m = text.match(/^(\S+)\s+(->\s+)?(\S+)\s*:?\s*(\S+)?(.*)$/);
    if (text.includes('->')) {
      const mm = text.match(/^(\S+)\s*->\s*(\S+)\s*:\s*(\S+)(.*)$/);
      if (mm) return `<strong>${esc(mm[1])}</strong> <span class="wf-rel" style="color:var(--sy-rel)">${esc(mm[3])}</span> <strong>${esc(mm[2])}</strong>${esc(mm[4] || '')}`;
    }
    const parts = text.split(/\s+/);
    if (parts.length >= 3) return `<strong>${esc(parts[0])}</strong> <span style="color:var(--sy-rel)">${esc(parts[1])}</span> <strong>${esc(parts.slice(2).join(' '))}</strong>`;
    return esc(text);
  }
  function setBadge(id, n, warn) { const b = $(id); if (!b) return; b.textContent = n; b.hidden = !n; }
  function debounce(fn, ms) { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; }

  function toast(title, msg, kind) {
    const t = document.createElement('div');
    t.className = 'toast ' + (kind || '');
    t.innerHTML = `<div class="t-title">${esc(title)}</div>${msg ? `<div class="t-msg">${esc(msg)}</div>` : ''}`;
    $('toastStack').appendChild(t);
    setTimeout(() => { t.style.opacity = '0'; t.style.transform = 'translateX(14px)'; t.style.transition = 'all .25s'; setTimeout(() => t.remove(), 260); }, 3200);
  }

  function injectMobileNote() {
    const note = document.createElement('div');
    note.className = 'mobile-note';
    note.innerHTML = `${icon('info')} Compact view - the graph and editor are tuned for the desktop workbench.`;
    document.querySelector('.workbench').prepend(note);
  }

  /* inline icons */
  function icon(name) {
    const p = {
      globe: '<circle cx="8" cy="8" r="6.2" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M2 8h12M8 2c2 2 2 10 0 12M8 2c-2 2-2 10 0 12" fill="none" stroke="currentColor" stroke-width="1.1"/>',
      cube: '<path d="M8 2l5 2.8v6.4L8 14 3 11.2V4.8L8 2z" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M3 4.8 8 7.6l5-2.8M8 7.6V14" fill="none" stroke="currentColor" stroke-width="1.1"/>',
      pulse: '<path d="M1.5 8h3l1.5-4 2.5 8 1.5-4h3.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/>',
      file: '<path d="M4 2h5l3 3v9H4z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M9 2v3h3" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      branch: '<path d="M5 3.5a1.5 1.5 0 1 0-2 1.41V11.1a1.5 1.5 0 1 0 1 0V8.2c.5.5 1.2.8 2 .8h1.3a1.5 1.5 0 1 0 0-1H7c-1 0-2-.7-2-2V4.9A1.5 1.5 0 0 0 5 3.5Z" fill="currentColor"/>',
      compare: '<path d="M5 2v12M11 2v12" stroke="currentColor" stroke-width="1.3"/><path d="M3 5l2-2 2 2M13 11l-2 2-2-2" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/>',
      replay: '<path d="M8 3V1L5.5 3.2 8 5.5V4a4 4 0 1 1-4 4" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>',
      shield: '<path d="M8 1.6 3 3.5v4c0 3 2 5.6 5 6.7 3-1.1 5-3.7 5-6.7v-4L8 1.6Z" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      play: '<path d="M5 3.5v9l7-4.5z" fill="currentColor"/>',
      'x-circle': '<circle cx="8" cy="8" r="6.4" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M6 6l4 4M10 6l-4 4" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/>',
      info: '<circle cx="8" cy="8" r="6.4" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M8 7v4M8 5h.01" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>',
    }[name] || '';
    return `<svg viewBox="0 0 16 16" class="ic" aria-hidden="true">${p}</svg>`;
  }
})();
