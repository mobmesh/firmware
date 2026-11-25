// The MobMesh card in a repeater's Settings tab opens the console's settings page for one group, over the whole app.
(() => {
  // Repeaters known to run or not run MobMesh, by full key; unknown ones still show the card.
  const known = new Map();
  let frame = null;
  const hex = b => Array.from(b, x => x.toString(16).padStart(2, '0')).join('');
  self.mmSettingsKnown = (key, mobmesh) => { if (key) known.set(key, mobmesh); };
  // Board names by full key, asked once per repeater by the settings page.
  self.mmSettingsBoard = new Map();
  let shown = null;
  // MobMesh nodes end their advertised name with the ᴹ marker; the name comes from the contact record the app read.
  self.mmSettingsShow = keyBytes => {
    const key = keyBytes && keyBytes.length === 32 ? hex(Array.from(keyBytes)) : self.mmTap?.loginKey;
    const contact = key && self.mmTap?.contacts.get(key);
    const name = contact ? new TextDecoder().decode(contact.slice(100, 132)).replace(/\0[\s\S]*$/, '').trim() : '';
    return name.endsWith('\u1d39') && known.get(key) !== false;
  };
  self.mmSettingsClose = () => { frame?.remove(); frame = null; };
  // The row passes the repeater's key bytes from the admin screen; the last login is the fallback.
  self.mmSettings = (group, keyBytes) => {
    self.mmSettingsClose();
    shown = keyBytes && keyBytes.length === 32 ? hex(Array.from(keyBytes)) : self.mmTap?.loginKey || null;
    frame = document.createElement('iframe');
    frame.id = 'mm-settings-page';
    frame.title = 'MobMesh settings';
    frame.src = new URL('mm-settings.html?group=' + encodeURIComponent(group) + (shown ? '&key=' + shown : ''), location.href.replace(/#.*$/, '')).href;
    frame.style.cssText = 'position:fixed;inset:0;width:100%;height:100%;border:0;z-index:20;background:#061320;color-scheme:dark';
    document.body.appendChild(frame);
    frame.focus();
  };
  addEventListener('hashchange', self.mmSettingsClose);
  // A version reply from the logged-in repeater, sent by anything on the link, says whether it runs MobMesh.
  const watch = () => {
    if (!self.mmTap) return setTimeout(watch, 200);
    self.mmTap.onFrame(f => {
      const offset = f[0] === 16 ? 4 : f[0] === 7 ? 1 : 0, key = self.mmTap.loginKey;
      if (!offset || !key || f.length < offset + 12 || f[offset + 7] !== 1 || hex(f.slice(offset, offset + 6)) !== key.slice(0, 12)) return;
      const text = new TextDecoder().decode(f.slice(offset + 12)).replace(/\0+$/, '');
      if (/^\s*(?:>\s*)?v?\d+\.\d+\S*\s+\(.*\)/.test(text)) known.set(key, /\+ MobMesh \(/.test(text));
    });
  };
  watch();
})();
