/* sw.js — lets the configurator open with no network and install as an app.
 *
 * Network first for everything, the kept copy only when the network fails: a
 * new model.json (a new firmware, new costs) reaches the reader the next time
 * they are online, never after a stale cache happens to age out. The network
 * is asked with cache "no-cache", so the browser revalidates (a 304 when
 * nothing changed) instead of trusting GitHub Pages' ten-minute max-age: in
 * that window a fresh model.json could otherwise meet a stale app.js. Only
 * GETs to this site are answered here; requests to api.github.com go straight
 * to the network, and nothing about them, the token included, is stored. */
const CACHE = "simut-configurador-2";
const SHELL = [
  "./", "index.html", "app.js", "logic.js", "rules.js", "tema.js", "model.json",
  "configurador.css", "manifest.webmanifest", "icon-192.png", "icon-512.png",
  "../assets/angulo.css", "../assets/site.css", "../assets/fonts/angulo-display-600.woff2",
  "../images/logo-mark.svg",
];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE)
    .then((c) => c.addAll(SHELL.map((url) => new Request(url, { cache: "no-cache" }))))
    .then(() => self.skipWaiting()));
});

self.addEventListener("activate", (event) => {
  event.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET" || new URL(req.url).origin !== self.location.origin) return;
  event.respondWith(fetch(req.url, { cache: "no-cache" }).then((res) => {
    if (res.ok) {
      const copy = res.clone();
      event.waitUntil(caches.open(CACHE).then((c) => c.put(req, copy)));
    }
    return res;
  }).catch(() => caches.match(req, { ignoreSearch: true }).then((hit) => hit || Response.error())));
});
