// Install MobMesh app, the Tools page row: the browser's own prompt on Chromium, a Home Screen and Bluefy tip on iOS; a Bluefy tip on the first Connect on iOS.
(() => {
  const TEAL = '#31d0bc';
  const standalone = () => matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
  const ios = /iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
  let deferred = null, tip = null;
  const style = document.createElement('style');
  style.textContent = `#mm-install-tip{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;background:#0008;z-index:30}
#mm-install-tip>div{max-width:300px;margin:16px;padding:16px 18px;border:1px solid #2a4a6a;border-radius:12px;background:#0e1f33;color:#e8f0f8;font:500 14px/1.4 Inter,system-ui,sans-serif;box-shadow:0 2px 8px #0008}
#mm-install-tip a{color:${TEAL};font-weight:600}
#mm-install-tip button{display:block;margin:12px 0 0 auto;padding:4px 8px;border:0;background:none;color:${TEAL};font:600 14px Inter,system-ui,sans-serif;cursor:pointer}`;
  document.head.appendChild(style);
  function showTip(html) {
    tip?.remove();
    tip = document.createElement('div');
    tip.id = 'mm-install-tip';
    tip.innerHTML = `<div>${html}<button>OK</button></div>`;
    tip.onclick = e => { if (e.target === tip || e.target.tagName === 'BUTTON') { tip.remove(); tip = null; } };
    document.body.appendChild(tip);
  }
  self.mmCanInstall = () => !standalone() && (!!deferred || ios);
  self.mmInstall = async () => {
    if (deferred) {
      const offer = deferred;
      deferred = null;
      offer.prompt();
      await offer.userChoice;
    } else if (ios) {
      showTip('To install: tap <b>Share</b>, then <b>Add to Home Screen</b>.<br><br>iOS browsers cannot reach a radio. To connect over Bluetooth, open this page in the <a href="https://bluefy.app/" target="_blank" rel="noopener">Bluefy</a> browser. USB does not work on iOS.');
    } else {
      showTip('This browser is not offering an install right now. Use <b>Install</b> in the browser menu, or reload the page and try again.');
    }
  };
  // iOS has no Web Bluetooth or Web Serial: the app's first reach for either (a Connect tap) shows the Bluefy tip once.
  const bluefy = 'bluetooth' in navigator;
  if (ios) for (const api of ['bluetooth', 'serial']) if (!(api in navigator)) {
    Object.defineProperty(navigator, api, { configurable: true, get() {
      if (!self.mmRadioTipShown) {
        self.mmRadioTipShown = true;
        showTip(bluefy ? 'USB does not work on iOS. Connect over <b>Bluetooth</b> instead.'
          : 'This browser cannot reach a radio: iOS browsers have no Bluetooth or USB access.<br><br>To connect over Bluetooth, open this page in the <a href="https://bluefy.app/" target="_blank" rel="noopener">Bluefy</a> browser. USB does not work on iOS.');
      }
      return undefined;
    } });
  }
  addEventListener('beforeinstallprompt', e => { e.preventDefault(); deferred = e; });
  addEventListener('appinstalled', () => { deferred = null; });
})();
