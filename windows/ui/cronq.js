'use strict';
(() => {
  const $ = id => document.getElementById(id);
  let state = {app: 'home', popup: false};
  let slotFrame;
  const host = window.chrome && window.chrome.webview;
  const post = (type, fields = {}) => { if (host) host.postMessage({type, ...fields}); };
  function setSnapIcon(name) {
    const btn = $('snap');
    if (!btn) return;
    const oldSvg = btn.querySelector('svg');
    const tpl = $(name);
    if (oldSvg && tpl) oldSvg.replaceWith(tpl.content.cloneNode(true));
  }
  function setSnapState(st) {
    if (st === 'snapping') setSnapIcon('icon-snapping');
    else if (st === 'done') setSnapIcon('icon-check');
    else if (st === 'failed') setSnapIcon('icon-failed');
    else setSnapIcon('icon-camera');
  }
  function slot() {
    cancelAnimationFrame(slotFrame);
    slotFrame = requestAnimationFrame(() => {
      const target = state.popup ? $('popup-slot') : $(state.app)?.querySelector('.content-slot');
      if (!target) return;
      const r = target.getBoundingClientRect(), d = window.devicePixelRatio;
      post('slot', {app: state.app, popup: state.popup, x: Math.round(r.x*d), y: Math.round(r.y*d), w: Math.round(r.width*d), h: Math.max(0,Math.round(r.height*d))});
    });
  }
  function apply(next) {
    state = next;
    if (state.palette) {
      const root = document.documentElement;
      root.dataset.appearance = String(state.appearance >= 2 ? 2 : state.appearance);
      root.style.colorScheme = state.palette.light ? 'light' : 'dark';
      for (const key of ['background','surface','raised','text','muted','border']) {
        root.style.setProperty(`--theme-${key}`, state.palette[key]);
      }

    }
    for (const app of ['home','chatgpt']) $(app).hidden = app !== state.app;
    $('mode').className = `nav-pill ${state.mode === 'overlay' ? 'pill-overlay' : 'pill-normal'}`;
    $('mode').setAttribute('aria-label', state.mode === 'overlay' ? 'Typing: overlay (Alt+M to return)' : 'Typing: normal app (Alt+M to type in overlay)');
    $('mode').setAttribute('aria-pressed', String(state.mode === 'overlay'));
    $('ghost-pill').className = `nav-pill ${state.ghost ? 'pill-ghost' : 'pill-solid'}`;
    $('ghost-pill').setAttribute('aria-label', state.ghost ? 'Ghost' : 'Solid');
    $('ghost-pill').setAttribute('aria-pressed', String(!!state.ghost));
    $('grab').hidden = state.grab !== 'failed';
    $('back').disabled = state.app === 'home';
    for (const b of document.querySelectorAll('.bottom-nav [data-app]')) b.classList.toggle('nav-selected', b.dataset.app === state.app);
    $('popup').hidden = !state.popup;
    slot();
  }
  if (host) host.addEventListener('message', e => {
    const m = e.data;
    if (!m || typeof m !== 'object') return;
    if (m.type === 'state') apply(m);
    else if (m.type === 'snap-state') setSnapState(m.state);
  });
  for (const b of document.querySelectorAll('[data-app]')) {
    b.setAttribute('aria-label', b.textContent.trim());
    b.addEventListener('click', () => post('nav', {app: b.dataset.app}));
  }
  $('recovery').addEventListener('dblclick', e => {
    if (e.button === 0 && state.app === 'home') {
      e.preventDefault(); post('nav', {app:'chatgpt'});
    }
  });
  for (const b of document.querySelectorAll('[data-action]')) b.addEventListener('click', () => post(b.dataset.action));
  for (const b of document.querySelectorAll('[data-pill]')) b.addEventListener('click', () => post('pill', {which: b.dataset.pill}));
  for (const b of document.querySelectorAll('[data-window]')) b.addEventListener('click', () => post('window', {action: b.dataset.window}));
  $('popup-close').addEventListener('click', () => post('popup-close'));
  $('handle').addEventListener('mousedown', e => { if (e.button === 0) { e.preventDefault(); post('drag', {kind:'move'}); } });
  $('handle').addEventListener('dblclick', () => post('dblclick-handle'));
  for (const edge of document.querySelectorAll('[data-edges]')) edge.addEventListener('mousedown', e => {
    if (e.button === 0) {e.preventDefault(); post('drag', {kind:'resize', edges:Number(edge.dataset.edges)});}
  });
  new ResizeObserver(slot).observe($('viewport'));
  window.addEventListener('resize', slot);
  post('ready');
})();
