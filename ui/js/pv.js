/* ============================================================
   pv.js - parse, tokenize, and check a .pv world entirely in the
   browser. This drives the live graph and the instant law feedback;
   the binary remains the source of truth when serve.py is running.
   ============================================================ */

const PVParse = (() => {
  const KW = new Set([
    'object', 'link', 'pointer', 'law', 'add', 'remove', 'domain', 'load',
    'evolve', 'inspect', 'world', 'new', 'repl', 'rule', 'when', 'require',
    'forbid', 'before', 'after', 'exists', 'deny', 'reason', 'derive', 'from',
    'make', 'morphism', 'set', 'emit', 'type', 'relation', 'schema', 'checkout',
  ]);

  const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

  // Tokenize one line into {t, c} where c is a CSS class (or undefined).
  // Every character is captured so the overlay stays glued to the textarea.
  function tokenizeLine(line) {
    const out = [];
    let code = line, comment = '';
    const h = line.indexOf('#');
    if (h >= 0) { code = line.slice(0, h); comment = line.slice(h); }

    const re = /(\s+)|("[^"]*")|(->)|([A-Za-z_][\w]*)(=)?|(\d[\w._:-]*)|(.)/g;
    let m, leadKw = null, afterColon = false, expectVal = false;
    while ((m = re.exec(code)) !== null) {
      if (m[1]) { out.push({ t: m[1] }); continue; }                 // whitespace
      if (m[2]) { out.push({ t: m[2], c: 't-str' }); expectVal = false; continue; } // string
      if (m[3]) { out.push({ t: '->', c: 't-arrow' }); continue; }   // arrow
      if (m[4] !== undefined) {                                      // word (+ optional '=')
        const w = m[4];
        if (m[5] === '=') { out.push({ t: w, c: 't-attr' }, { t: '=', c: 't-attr' }); expectVal = true; continue; }
        if (expectVal) { out.push({ t: w, c: 't-str' }); expectVal = false; continue; }
        if (KW.has(w)) { if (leadKw === null) leadKw = w; out.push({ t: w, c: 't-kw' }); continue; }
        if (afterColon) {
          afterColon = false;
          if (leadKw === 'object' || leadKw === 'type') { out.push({ t: w, c: 't-type' }); continue; }
          if (leadKw === 'link' || leadKw === 'pointer' || leadKw === 'relation') { out.push({ t: w, c: 't-rel' }); continue; }
        }
        out.push({ t: w, c: 't-id' });
        continue;
      }
      if (m[6]) { out.push({ t: m[6], c: expectVal ? 't-num' : (afterColon ? 't-num' : 't-num') }); expectVal = false; continue; } // number/date
      if (m[7]) { if (m[7] === ':') afterColon = true; out.push({ t: m[7], c: m[7] === ':' ? 't-arrow' : undefined }); continue; }
    }
    if (comment) out.push({ t: comment, c: 't-cmt' });
    return out;
  }

  // Produce the highlighted HTML overlay. violLines: Set of 0-based line numbers.
  function highlight(text, violLines) {
    const lines = text.split('\n');
    const v = violLines || new Set();
    return lines.map((ln, i) => {
      const toks = tokenizeLine(ln).map((tk) => tk.c ? `<span class="${tk.c}">${esc(tk.t)}</span>` : esc(tk.t)).join('');
      const cls = v.has(i) ? 'ln viol' : 'ln';
      return `<span class="${cls}">${toks || '​'}</span>`;
    }).join('');
  }

  function parseAttrs(rest) {
    const attrs = {};
    const re = /([A-Za-z_][\w]*)=("[^"]*"|\S+)/g;
    let m;
    while ((m = re.exec(rest)) !== null) {
      let val = m[2];
      if (val.startsWith('"')) val = val.slice(1, -1);
      attrs[m[1]] = val;
    }
    return attrs;
  }

  // Parse a world into a typed graph model.
  function parse(text) {
    const objects = new Map();   // name -> {name, type, attrs}
    const links = [];            // {from,to,rel,attrs,weight,role,line}
    const commands = [];
    const lines = text.split('\n');

    lines.forEach((raw, idx) => {
      const line = raw.replace(/#.*$/, '').trim();
      if (!line) return;
      let m;
      if ((m = line.match(/^object\s+([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)\s*(.*)$/))) {
        objects.set(m[1], { name: m[1], type: m[2], attrs: parseAttrs(m[3]) });
      } else if ((m = line.match(/^(?:link|pointer)\s+([A-Za-z_]\w*)\s*->\s*([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)\s*(.*)$/))) {
        const attrs = parseAttrs(m[4]);
        const parsedWeight = attrs.weight !== undefined && attrs.weight !== 'auto' ? parseFloat(attrs.weight) : null;
        links.push({
          from: m[1], to: m[2], rel: m[3], attrs,
          weight: Number.isFinite(parsedWeight) ? parsedWeight : null,
          role: attrs.role || null, line: idx,
        });
      } else if (/^(evolve|inspect|law|domain|world|rule|derive|morphism|checkout)\b/.test(line)) {
        commands.push({ line: idx, text: line });
      }
    });

    // Ensure endpoints exist as inferred objects (e.g. Beach2x1Site appears only in a link).
    for (const lk of links) {
      for (const n of [lk.from, lk.to]) {
        if (!objects.has(n)) objects.set(n, { name: n, type: 'Object', attrs: {}, inferred: true });
      }
    }
    return { objects, links, commands };
  }

  // Evaluate a world's laws client-side for instant feedback.
  // Returns [{name, status:'fail'|'pass'|'stable', triggered, reason, violations:[{line,reason,linkIndex}]}]
  function evalLaws(model, laws) {
    const results = [];
    (laws || []).forEach((law) => {
      const violations = [];
      let triggered = false;

      if (law.requireRel === '__bounded__') {
        model.links.forEach((lk, i) => {
          if (lk.weight !== null) {
            triggered = true;
            if (lk.weight < 0 || lk.weight > 1) violations.push({ line: lk.line, linkIndex: i, reason: `${lk.from} -> ${lk.to} : ${lk.rel} weight ${lk.weight} out of [0,1]` });
          } else if (lk.attrs.weight === 'auto') {
            triggered = true;
          }
        });
      } else if (law.requireRel === '__none__') {
        // forbid: a builds_at whose target is visible_from a route (on the spine)
        model.links.forEach((lk, i) => {
          if (lk.rel !== law.trigger.rel) return;
          triggered = true;
          const onSpine = model.links.some((o) => o.from === lk.to && o.rel === 'visible_from');
          if (onSpine) violations.push({ line: lk.line, linkIndex: i, reason: fmtDeny(law.deny, lk) });
        });
      } else {
        // require BEFORE: trigger link needs a prior same-endpoint requireRel link.
        model.links.forEach((lk, i) => {
          if (lk.rel !== law.trigger.rel) return;
          triggered = true;
          const ok = model.links.some((o, j) => j < i && o.from === lk.from && o.to === lk.to && o.rel === law.requireRel);
          if (!ok) violations.push({ line: lk.line, linkIndex: i, reason: fmtDeny(law.deny, lk) });
        });
      }

      results.push({
        name: law.name,
        when: law.when, require: law.require, deny: law.deny,
        triggered,
        status: violations.length ? 'fail' : (triggered ? 'pass' : 'stable'),
        violations,
      });
    });
    return results;
  }

  function fmtDeny(tmpl, lk) {
    return (tmpl || '{from} {rel} {to}')
      .replace(/\{from\}/g, lk.from).replace(/\{to\}/g, lk.to).replace(/\{rel\}/g, lk.rel);
  }

  // Set of 0-based line numbers that carry a violation, across all laws.
  function violationLines(lawResults) {
    const s = new Set();
    lawResults.forEach((r) => r.violations.forEach((v) => s.add(v.line)));
    return s;
  }

  // Derive a "why does this fact exist" view from graph structure + captured data.
  function whyFor(model, link, worldId, branch, baseEpoch) {
    const key = `${worldId}:${link.from}:${link.rel}:${link.to}`;
    const cap = PV.why && PV.why[key];
    const bornEpoch = cap ? cap.born_epoch : (baseEpoch || 20) + link.line;
    const commit = cap ? cap.commit : shortHash(key + branch);
    const related = [];
    // facts that feed into the 'from' node (its incoming edges), earliest first
    model.links.forEach((o) => {
      if (o === link) return;
      if (o.to === link.from || (o.to === link.to && o.from !== link.from)) {
        related.push(o);
      }
    });
    related.sort((a, b) => a.line - b.line);
    return { bornEpoch, commit, branch, related: related.slice(0, 4) };
  }

  function shortHash(str) {
    let h = 0x811c9dc5;
    for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 0x01000193); }
    return (h >>> 0).toString(16).padStart(8, '0') + ((h * 31) >>> 0).toString(16).slice(0, 4);
  }

  return { tokenizeLine, highlight, parse, parseAttrs, evalLaws, violationLines, whyFor, shortHash, esc };
})();
