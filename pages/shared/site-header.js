// The one MobMesh site header for tools.mobmesh.org pages and mobmesh.org: <mm-header current="tools"></mm-header> followed by this script.
(() => {
  const base = new URL('.', document.currentScript?.src || location.href);
  const NAV = [
    ['setup', 'Setup', 'https://tools.mobmesh.org/flasher/'],
    ['app', 'App', 'https://tools.mobmesh.org/app/'],
    ['console', 'Console', 'https://tools.mobmesh.org/console/'],
    ['maps', 'Maps', 'https://analyzer.gulfcoastmesh.org/?theme=dark#/live?lat=30.6954&lon=-88.0399&zoom=9&layer=dark', true],
    ['docs', 'Docs', 'https://github.com/mobmesh', true],
    ['tools', 'Tools', 'https://tools.mobmesh.org/'],
  ];
  const DISCORD = 'M20.317 4.37a19.79 19.79 0 0 0-4.885-1.515.074.074 0 0 0-.079.037c-.21.375-.444.864-.608 1.25a18.27 18.27 0 0 0-5.487 0 12.64 12.64 0 0 0-.617-1.25.077.077 0 0 0-.079-.037A19.74 19.74 0 0 0 3.677 4.37a.07.07 0 0 0-.032.027C.533 9.046-.32 13.58.099 18.057a.082.082 0 0 0 .031.057 19.9 19.9 0 0 0 5.993 3.03.078.078 0 0 0 .084-.028c.462-.63.874-1.295 1.226-1.994a.076.076 0 0 0-.041-.106 13.1 13.1 0 0 1-1.872-.892.077.077 0 0 1-.008-.128c.126-.094.252-.192.372-.291a.074.074 0 0 1 .077-.01c3.928 1.793 8.18 1.793 12.062 0a.074.074 0 0 1 .078.009c.12.099.246.198.373.292a.077.077 0 0 1-.006.127 12.3 12.3 0 0 1-1.873.892.077.077 0 0 0-.041.107c.36.698.772 1.362 1.225 1.993a.076.076 0 0 0 .084.028 19.84 19.84 0 0 0 6.002-3.03.077.077 0 0 0 .032-.054c.5-5.177-.838-9.674-3.549-13.66a.061.061 0 0 0-.031-.03ZM8.02 15.33c-1.183 0-2.157-1.085-2.157-2.419 0-1.333.956-2.419 2.157-2.419 1.21 0 2.176 1.096 2.157 2.42 0 1.333-.956 2.418-2.157 2.418Zm7.975 0c-1.183 0-2.157-1.085-2.157-2.419 0-1.333.955-2.419 2.157-2.419 1.21 0 2.176 1.096 2.157 2.42 0 1.333-.946 2.418-2.157 2.418Z';
  const GITHUB = 'M10.226 17.284c-2.965-.36-5.054-2.493-5.054-5.256 0-1.123.404-2.336 1.078-3.144-.292-.741-.247-2.314.09-2.965.898-.112 2.111.36 2.83 1.01.853-.269 1.752-.404 2.853-.404 1.1 0 1.999.135 2.807.382.696-.629 1.932-1.1 2.83-.988.315.606.36 2.179.067 2.942.72.854 1.101 2 1.101 3.167 0 2.763-2.089 4.852-5.098 5.234.763.494 1.28 1.572 1.28 2.807v2.336c0 .674.561 1.056 1.235.786 4.066-1.55 7.255-5.615 7.255-10.646C23.5 6.188 18.334 1 11.978 1 5.62 1 .5 6.188.5 12.545c0 4.986 3.167 9.12 7.435 10.669.606.225 1.19-.18 1.19-.786V20.63a2.9 2.9 0 0 1-1.078.224c-1.483 0-2.359-.808-2.987-2.313-.247-.607-.517-.966-1.034-1.033-.27-.023-.359-.135-.359-.27 0-.27.45-.471.898-.471.652 0 1.213.404 1.797 1.235.45.651.921.943 1.483.943.561 0 .92-.202 1.437-.719.382-.381.674-.718.944-.943';
  const CSS = `
mm-header{display:block;position:relative;z-index:20;min-height:3.6rem;line-height:1.5;font-family:"Inter",-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.mm-hdr{width:clamp(320px,94%,960px);margin:0 auto;padding:1.15rem 1.25rem 0;display:grid;grid-template-columns:1fr auto 1fr;align-items:center;gap:1rem;box-sizing:border-box}
.mm-hdr-brand{display:inline-flex;align-items:center;gap:.6rem;text-decoration:none;justify-self:start}
.mm-hdr-brand img{display:block;width:1.9rem;height:1.9rem}
.mm-hdr-brand span{font-family:"JetBrains Mono",ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.78rem;font-weight:600;letter-spacing:.18em;text-transform:uppercase;color:#e8f0f8;transition:color .15s ease}
.mm-hdr-brand:hover span{color:#2dd1bd}
.mm-hdr-nav{justify-self:center;display:flex;align-items:center;gap:1.4rem;font-size:.82rem}
.mm-hdr-nav a{color:#9eb5ce;text-decoration:none;white-space:nowrap;transition:color .15s ease}
.mm-hdr-nav a:hover{color:#2dd1bd}
.mm-hdr-nav a[aria-current="page"]{color:#2dd1bd;text-shadow:0 0 .85rem rgba(45,209,189,.55)}
.mm-hdr-nav [aria-disabled="true"]{color:#9eb5ce;opacity:.45;white-space:nowrap;cursor:default}
.mm-hdr-links{justify-self:end;display:flex;align-items:center;gap:1.1rem}
.mm-hdr-icon{position:relative;display:flex;color:#9eb5ce;transition:color .15s ease}
.mm-hdr-icon:hover,.mm-hdr-icon:focus-visible{color:#2dd1bd}
.mm-hdr-icon::after{content:attr(data-tip);position:absolute;top:calc(100% + .55rem);left:50%;transform:translate(-50%,-.25rem);padding:.3rem .65rem;font-family:"JetBrains Mono",ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.6rem;font-weight:600;letter-spacing:.18em;text-transform:uppercase;white-space:nowrap;color:#e8f0f8;background:#0e1f33;border:1px solid #223a58;border-radius:999px;box-shadow:0 8px 24px -8px rgba(45,209,189,.45);opacity:0;pointer-events:none;transition:opacity .15s ease,transform .15s ease;z-index:30}
.mm-hdr-icon:hover::after,.mm-hdr-icon:focus-visible::after{opacity:1;transform:translate(-50%,0)}
.mm-hdr-pill{display:inline-flex;align-items:center;padding:.4rem .95rem;border:1.5px solid #2dd1bd;border-radius:999px;color:#2dd1bd;font-size:.82rem;font-weight:600;text-decoration:none;white-space:nowrap;transition:background .15s ease,box-shadow .15s ease}
.mm-hdr-pill:hover,.mm-hdr-pill:focus-visible{background:rgba(45,209,189,.14);box-shadow:0 8px 24px -8px rgba(45,209,189,.45)}
@media (max-width:760px){.mm-hdr{grid-template-columns:1fr auto;row-gap:.85rem}.mm-hdr-nav{grid-row:2;grid-column:1/-1;flex-wrap:wrap;justify-content:center;gap:1rem;font-size:.78rem}}
@media (max-width:700px){.mm-hdr-pill{display:none}}
`;
  // Open App shows once a board has been flashed: a cookie on .mobmesh.org, so mobmesh.org sees what the flasher set; each visit renews it.
  const FLAG = 'mm_flashed';
  const flagAttrs = () => `; path=/; max-age=${400 * 86400}; SameSite=Lax${location.protocol === 'https:' ? '; Secure' : ''}${/(^|\.)mobmesh\.org$/.test(location.hostname) ? '; domain=mobmesh.org' : ''}`;
  const flashed = () => document.cookie.split('; ').includes(`${FLAG}=1`);
  globalThis.mmMarkFlashed = () => { document.cookie = `${FLAG}=1${flagAttrs()}`; document.querySelectorAll('mm-header').forEach(h => h.showPill?.()); };
  if (flashed()) document.cookie = `${FLAG}=1${flagAttrs()}`;
  const icon = (href, label, tip, path) => `<a class="mm-hdr-icon" href="${href}" target="_blank" rel="noopener noreferrer" aria-label="${label}" data-tip="${tip}"><svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="${path}"/></svg></a>`;
  class MobMeshHeader extends HTMLElement {
    connectedCallback() {
      if (this.dataset.ready) return;
      this.dataset.ready = '1';
      if (!document.getElementById('mm-header-style')) {
        const style = document.createElement('style');
        style.id = 'mm-header-style';
        style.textContent = CSS;
        document.head.appendChild(style);
      }
      const current = this.getAttribute('current');
      const nav = NAV.map(([id, label, href, external]) => `<a href="${href}"${external ? ' target="_blank" rel="noopener noreferrer"' : ''}${id === current ? ' aria-current="page"' : ''}>${label}</a>`).join('')
        + '<span aria-disabled="true">Hardware</span>';
      this.innerHTML = `<header class="mm-hdr">
<a class="mm-hdr-brand" href="https://mobmesh.org"><img src="${new URL('logo.svg', base)}" alt="" width="28" height="28"><span>MobMesh</span></a>
<nav class="mm-hdr-nav" aria-label="Main">${nav}</nav>
<div class="mm-hdr-links">${icon('https://mobmesh.org/discord', 'Join the MobMesh channel on Discord', 'Discord', DISCORD)}${icon('https://github.com/mobmesh/firmware#readme', 'View this project on GitHub', 'GitHub', GITHUB)}</div>
</header>`;
      if (flashed()) this.showPill();
    }
    showPill() {
      const links = this.querySelector('.mm-hdr-links');
      if (!links || links.querySelector('.mm-hdr-pill') || this.getAttribute('current') === 'app') return;
      links.insertAdjacentHTML('beforeend', '<a class="mm-hdr-pill" href="https://tools.mobmesh.org/app/">Open App</a>');
    }
  }
  if (!customElements.get('mm-header')) customElements.define('mm-header', MobMeshHeader);
})();
