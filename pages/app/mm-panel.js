// The app's Command Line tab opens the MobMesh console in a panel over the content area; the app bar and tab bar stay the app's own.
(() => {
  const TERMINAL = 984457;
  let frame = null;
  // A tab item holds its Icon widget, which holds the IconData; matching the code point works in every app language.
  const terminal = item => { const seen = new Set(), walk = (v, depth) => { if (!v || typeof v !== 'object' || seen.has(v) || depth > 3) return false; seen.add(v); if (v.a === TERMINAL) return true; return Object.values(v).some(x => walk(x, depth + 1)); }; return walk(item, 0); };
  function show() {
    // Offline, the app's own Command Line screen is used: the console page is not part of the offline copy.
    if (!navigator.onLine) { hide(); return; }
    if (!frame) {
      frame = document.createElement('iframe');
      frame.id = 'mm-console-panel';
      frame.title = 'MobMesh console';
      frame.src = new URL('../console/?embed=app', location.href.replace(/#.*$/, '')).href;
      frame.style.cssText = 'position:fixed;left:0;right:0;top:56px;bottom:56px;width:100%;height:calc(100% - 112px);border:0;z-index:10;background:transparent;color-scheme:dark';
      frame.setAttribute('allow', 'serial');
      // A console that failed to load (the browser's error page has no console in it) is removed, leaving the app's own screen; the next tap tries again.
      frame.addEventListener('load', () => { let ok = false; try { ok = !!frame?.contentDocument?.getElementById('command'); } catch {} if (!ok) { frame?.remove(); frame = null; } });
      document.body.appendChild(frame);
    }
    frame.hidden = false;
    frame.contentWindow?.mmEmbedShown?.();
  }
  function hide() { if (frame) frame.hidden = true; }
  // Phones keep the app's own Command Line screen; same phone test as the mesh background in index.html.
  const phone = () => matchMedia('(pointer: coarse)').matches && matchMedia('(max-width: 700px)').matches;
  self.mmNavTap = item => { if (terminal(item) && !phone()) show(); else hide(); };
  self.mmNavGone = hide;
  addEventListener('hashchange', hide);
})();
