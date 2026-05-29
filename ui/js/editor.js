/* ============================================================
   editor.js - a tiny syntax-highlighted code editor: a transparent
   <textarea> over a highlighted <pre>, with a line-number gutter and
   synced scrolling. No dependencies, no build step.
   ============================================================ */

const PVEditor = (() => {
  function init({ textarea, hlCode, gutter, hl }, onChange) {
    let violLines = new Set();
    let timer = null;

    function renderHighlight() {
      hlCode.innerHTML = PVParse.highlight(textarea.value, violLines);
    }

    function renderGutter() {
      const n = textarea.value.split('\n').length;
      let html = '';
      for (let i = 0; i < n; i++) {
        html += `<div class="gl${violLines.has(i) ? ' has-violation' : ''}">${i + 1}</div>`;
      }
      gutter.innerHTML = html;
    }

    function syncScroll() {
      hl.scrollTop = textarea.scrollTop;
      hl.scrollLeft = textarea.scrollLeft;
      gutter.scrollTop = textarea.scrollTop;
    }

    function render() { renderHighlight(); renderGutter(); syncScroll(); }

    textarea.addEventListener('input', () => {
      renderHighlight(); renderGutter();
      clearTimeout(timer);
      timer = setTimeout(() => onChange && onChange(textarea.value), 140);
    });
    textarea.addEventListener('scroll', syncScroll, { passive: true });
    // keep tab key inserting two spaces instead of leaving the field
    textarea.addEventListener('keydown', (e) => {
      if (e.key === 'Tab') {
        e.preventDefault();
        const s = textarea.selectionStart, t = textarea.value;
        textarea.value = t.slice(0, s) + '  ' + t.slice(textarea.selectionEnd);
        textarea.selectionStart = textarea.selectionEnd = s + 2;
        renderHighlight(); renderGutter();
      }
    });

    return {
      el: textarea,
      setValue(text) { textarea.value = text; violLines = new Set(); render(); },
      getValue() { return textarea.value; },
      setViolations(set) { violLines = set || new Set(); renderHighlight(); renderGutter(); },
      render,
      focusLine(n) {
        const lines = textarea.value.split('\n');
        let pos = 0;
        for (let i = 0; i < n && i < lines.length; i++) pos += lines[i].length + 1;
        textarea.focus();
        textarea.setSelectionRange(pos, pos + (lines[n] ? lines[n].length : 0));
        const lineHeight = 19;
        textarea.scrollTop = Math.max(0, n * lineHeight - textarea.clientHeight / 2);
        syncScroll();
      },
    };
  }

  return { init };
})();
