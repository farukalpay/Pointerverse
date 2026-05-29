/* ============================================================
   api.js - the bridge to the engine.

   On load it probes /api/health. If serve.py is running, the IDE
   switches to "live" mode and the panels are driven by the real
   pointerverse binary. Otherwise it stays in "demo data" mode and
   serves the captured transcripts from data.js. Either way the app
   code calls the same functions.
   ============================================================ */

const PVApi = (() => {
  let live = false;
  let version = '';
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));

  async function probe() {
    try {
      const ctrl = new AbortController();
      const t = setTimeout(() => ctrl.abort(), 1200);
      const res = await fetch('api/health', { signal: ctrl.signal });
      clearTimeout(t);
      if (res.ok) { const j = await res.json(); live = !!j.ok; version = j.version || ''; }
    } catch (_) { live = false; }
    return live;
  }

  function isLive() { return live; }
  function engineVersion() { return version; }

  async function getJSON(url) {
    const res = await fetch(url);
    if (!res.ok) throw new Error('http ' + res.status);
    return res.json();
  }
  async function postJSON(url, body) {
    const res = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    if (!res.ok) throw new Error('http ' + res.status);
    return res.json();
  }

  // --------- run ---------
  async function run({ worldId, branch, src }) {
    if (live) {
      try { const j = await postJSON('api/run', { world: worldId, branch, src }); return { text: j.output || '' }; }
      catch (e) { /* fall through to demo */ }
    }
    await wait(120);
    const t = PV.transcripts;
    const map = {
      'mohacs:historical': t.mohacs_historical_run, 'mohacs:reinforced': t.mohacs_reinforced_run,
      'mohacs:main': t.mohacs_main_run, 'rust_wipe_blackbox:main': t.rust_run,
      'rust_wipe_blackbox:beach_rush': t.rust_run, 'starter:main': t.starter_run,
    };
    return { text: map[`${worldId}:${branch}`] || map[`${worldId}:main`] || t.starter_run };
  }

  // --------- why ---------
  async function why({ worldId, branch, link }) {
    if (live) {
      try {
        const j = await getJSON(`api/why?world=${worldId}&branch=${branch}&from=${link.from}&rel=${link.rel}&to=${link.to}`);
        return { bornEpoch: j.born_epoch, commit: j.commit, relatedCommits: j.related_commits || [], text: j.output };
      } catch (e) {}
    }
    await wait(60);
    const key = `${worldId}:${link.from}:${link.rel}:${link.to}`;
    const cap = PV.why && PV.why[key];
    return cap ? { bornEpoch: cap.born_epoch, commit: cap.commit, relatedCommits: cap.related_commits } : null;
  }

  // --------- compare ---------
  async function compare({ worldId, a, b }) {
    if (live) {
      try { const j = await getJSON(`api/compare?world=${worldId}&a=${a}&b=${b}`); if (j && j.ancestor) return j; } catch (e) {}
    }
    await wait(140);
    const cap = PV.compare[`${worldId}:${a}:${b}`] || PV.compare[`${worldId}:${b}:${a}`];
    return cap || { ancestor: '-', status: 'Equal', left: { branch: a, commit: '-', fact: 'no divergence' }, right: { branch: b, commit: '-', fact: 'no divergence' }, conflicts: [], outcome: {} };
  }

  // --------- fsck ---------
  async function fsck({ worldId }) {
    if (live) {
      try { const j = await getJSON(`api/fsck?world=${worldId}`); if (j && j.status) return j; } catch (e) {}
    }
    await wait(160);
    return PV.fsck[worldId] || PV.fsck.mohacs;
  }

  // --------- history ---------
  async function history({ worldId, branch }) {
    if (live) {
      try { const j = await getJSON(`api/history?world=${worldId}&branch=${branch}`); if (j && j.commits) return j.commits; } catch (e) {}
    }
    await wait(80);
    return PV.history[`${worldId}_${branch}`] || PV.history.mohacs_historical;
  }

  // --------- sentinel ---------
  async function sentinel({ worldId }) {
    if (live) {
      try { const j = await getJSON(`api/sentinel?world=${worldId}`); if (j && j.output) return { text: j.output }; } catch (e) {}
    }
    await wait(220);
    return { text: PV.sentinel[worldId] || PV.sentinel.mohacs };
  }

  return { probe, isLive, engineVersion, run, why, compare, fsck, history, sentinel };
})();
