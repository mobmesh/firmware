// The app's Command Line tab opens the MobMesh console in a panel over the content area; the app bar and tab bar stay the app's own.
(() => {
  const TERMINAL = 984457;
  let frame = null;
  // A tab item holds its Icon widget, which holds the IconData; matching the code point works in every app language.
  const terminal = item => { const seen = new Set(), walk = (v, depth) => { if (!v || typeof v !== 'object' || seen.has(v) || depth > 3) return false; seen.add(v); if (v.a === TERMINAL) return true; return Object.values(v).some(x => walk(x, depth + 1)); }; return walk(item, 0); };
  function show() {
    if (!frame) {
      frame = document.createElement('iframe');
      frame.id = 'mm-console-panel';
      frame.title = 'MobMesh console';
      frame.src = new URL('../console/?embed=app', location.href.replace(/#.*$/, '')).href;
      frame.style.cssText = 'position:fixed;left:0;right:0;top:56px;bottom:56px;width:100%;height:calc(100% - 112px);border:0;z-index:10;background:transparent;color-scheme:dark';
      frame.setAttribute('allow', 'serial');
      document.body.appendChild(frame);
    }
    frame.hidden = false;
    frame.contentWindow?.mmEmbedShown?.();
  }
  function hide() { if (frame) frame.hidden = true; }
  self.mmNavTap = item => { if (terminal(item)) show(); else hide(); };
  self.mmNavGone = hide;
  addEventListener('hashchange', hide);
  // The app bar's wordmark links back to MobMesh tools on the home screen only, where no back arrow sits under it; with a companion connected it opens a new tab so the link stays up.
  const home = document.createElement('a');
  home.id = 'mm-home-link';
  home.href = 'https://tools.mobmesh.org/';
  home.title = 'MobMesh tools';
  home.setAttribute('aria-label', 'MobMesh tools');
  home.style.cssText = 'position:fixed;left:8px;top:0;width:176px;height:56px;z-index:11';
  home.addEventListener('click', e => { if (self.mmTap?.connected) { e.preventDefault(); open(home.href, '_blank', 'noopener'); } });
  const onHome = () => { home.style.display = location.hash && location.hash !== '#/' ? 'none' : 'block'; };
  // Flutter routes through history.pushState and replaceState, which fire no event, so both are wrapped to report route changes.
  for (const name of ['pushState', 'replaceState']) { const orig = history[name]; history[name] = function (...a) { const r = orig.apply(this, a); dispatchEvent(new Event('mm-route')); return r; }; }
  for (const ev of ['hashchange', 'popstate', 'mm-route']) addEventListener(ev, onHome);
  onHome();
  document.body.appendChild(home);
})();
