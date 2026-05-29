/* ============================================================
   storage.js - per-browser workbench persistence.
   Stores only local edits that differ from the bundled defaults.
   ============================================================ */

const PVStorage = (() => {
  const KEY = 'pointerverse.workbench.v1';

  function empty() { return { files: {}, branches: {}, session: {} }; }

  function read() {
    try {
      const parsed = JSON.parse(localStorage.getItem(KEY) || 'null');
      return parsed && typeof parsed === 'object' ? Object.assign(empty(), parsed) : empty();
    } catch (_) {
      return empty();
    }
  }

  function write(data) {
    try {
      localStorage.setItem(KEY, JSON.stringify(data));
    } catch (_) {}
  }

  function hydrate(worlds) {
    const data = read();
    worlds.forEach((world) => {
      const defaults = new Set(world.branches.map((branch) => branch.name));
      const saved = (data.branches && data.branches[world.id]) || [];
      saved.forEach((branch) => {
        if (!branch || !branch.name || defaults.has(branch.name)) return;
        if (!world.branches.some((item) => item.name === branch.name)) {
          world.branches.push(branch);
        }
      });
    });
  }

  function fileValue(worldId, file, fallback) {
    const data = read();
    return data.files && data.files[worldId] && data.files[worldId][file] != null
      ? data.files[worldId][file]
      : fallback;
  }

  function saveFile(worldId, file, value, bundledDefault) {
    const data = read();
    data.files[worldId] = data.files[worldId] || {};
    if (value === bundledDefault) {
      delete data.files[worldId][file];
      if (!Object.keys(data.files[worldId]).length) delete data.files[worldId];
    } else {
      data.files[worldId][file] = value;
    }
    write(data);
  }

  function saveBranches(worldId, branches, defaultBranches) {
    const defaults = new Set((defaultBranches || []).map((branch) => branch.name));
    const custom = branches.filter((branch) => !defaults.has(branch.name));
    const data = read();
    if (custom.length) data.branches[worldId] = custom;
    else delete data.branches[worldId];
    write(data);
  }

  function saveSession(session) {
    const data = read();
    data.session = Object.assign({}, session);
    write(data);
  }

  function session() {
    return read().session || {};
  }

  return { hydrate, fileValue, saveFile, saveBranches, saveSession, session };
})();
