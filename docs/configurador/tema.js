/* The theme before the first paint: the choice saved by any page of the site
 * (angulo:tema), else the system's. A file of its own rather than an inline
 * <script>, so the page's Content-Security-Policy can say script-src 'self'. */
(function () {
  var d = document.documentElement, t = null;
  try { t = localStorage.getItem('angulo:tema'); } catch (e) {}
  if (t !== 'claro' && t !== 'escuro') {
    t = window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches ? 'escuro' : 'claro';
  }
  d.dataset.theme = t;
})();
