// Offline copy of the app: each stamped version precaches its files, installs in the background and takes over at the next launch.
const VERSION = 'dev';
const FILES = [];
const CACHE = `mm-app-${VERSION}`;
const FONTS = 'mm-fonts';
const scope = new URL(self.registration.scope);
// The full CanvasKit build, which the skin makes every browser load, so one copy serves all.
const CANVASKIT = ['canvaskit/canvaskit.js', 'canvaskit/canvaskit.wasm'];
// Console modules the settings page imports sit outside the app's folder; they are precached and served like app files.
const SHARED = new Set(FILES.filter(f => f.startsWith('../')).map(f => new URL(f, scope).href));

self.addEventListener('install', event => {
  const urls = ['./', ...FILES, ...CANVASKIT].map(f => new Request(new URL(f, scope), { cache: 'reload' }));
  event.waitUntil(caches.open(CACHE).then(cache => cache.addAll(urls)));
});

self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    for (const name of await caches.keys()) if (name.startsWith('mm-app-') && name !== CACHE) await caches.delete(name);
    await self.clients.claim();
  })());
});

// Cache first; a file missing from the cache is fetched once and kept with its version.
async function fromCache(name, request) {
  const cache = await caches.open(name);
  const hit = await cache.match(request, { ignoreSearch: true });
  if (hit) return hit;
  const response = await fetch(request);
  if (response.ok) cache.put(request, response.clone());
  return response;
}

self.addEventListener('fetch', event => {
  const request = event.request;
  if (request.method !== 'GET') return;
  const url = new URL(request.url);
  // Fallback fonts Flutter downloads for emoji and other scripts are kept across versions.
  if (url.origin === 'https://fonts.gstatic.com') { event.respondWith(fromCache(FONTS, request)); return; }
  if (!url.href.startsWith(scope.href) && !SHARED.has(url.origin + url.pathname)) return;
  if (request.mode === 'navigate') {
    // A page of its own (the settings page) loads itself, even when not precached; every other app route loads the app.
    if (/\.html$/.test(url.pathname) && !url.pathname.endsWith('/index.html')) { event.respondWith(fromCache(CACHE, request)); return; }
    event.respondWith(caches.open(CACHE).then(async cache => await cache.match(request, { ignoreSearch: true }) || await cache.match(scope.href)).then(hit => hit || fetch(request)));
    return;
  }
  event.respondWith(fromCache(CACHE, request));
});
