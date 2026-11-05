'use strict';
(() => {
  const $ = id => document.getElementById(id);
  let state = {app: 'home', popup: false, loading: false, url: ''};
  let toastTimer, slotFrame, pendingUrlFocus = false;
  const host = window.chrome && window.chrome.webview;
  const post = (type, fields = {}) => { if (host) host.postMessage({type, ...fields}); };
  const icon = (element, name) => element.replaceChildren($(name).content.cloneNode(true));
  let typerRevertTimer;
  function setTyperIcon(name) {
    const btn = $('typer');
    if (!btn) return;
    const oldSvg = btn.querySelector('svg');
    const tpl = $(name);
    if (oldSvg && tpl) oldSvg.replaceWith(tpl.content.cloneNode(true));
  }
  function setTyperState(st) {
    clearTimeout(typerRevertTimer);
    if (st === 'typing') setTyperIcon('icon-ghost-typing');
    else if (st === 'done') {
      setTyperIcon('icon-check');
      typerRevertTimer = setTimeout(() => setTyperIcon('icon-typer'), 2000);
    } else if (st === 'failed') {
      setTyperIcon('icon-failed');
      typerRevertTimer = setTimeout(() => setTyperIcon('icon-typer'), 2000);
    } else setTyperIcon('icon-typer');
  }
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
    else setSnapIcon('icon-camera');
  }
  function slot() {
    cancelAnimationFrame(slotFrame);
    slotFrame = requestAnimationFrame(() => {
      const target = state.popup ? $('popup-slot') : $(state.app)?.querySelector('.content-slot');
      if (!target) return;
      const r = target.getBoundingClientRect(), d = window.devicePixelRatio;
      // DECISION: reserve the toast row while visible; windowed content otherwise covers HTML.
      const bottom = state.appearance < 2 && $('toast').classList.contains('visible') ? Math.min(r.bottom, $('toast').getBoundingClientRect().top - 6) : r.bottom;
      post('slot', {app: state.app, popup: state.popup, x: Math.round(r.x*d), y: Math.round(r.y*d), w: Math.round(r.width*d), h: Math.max(0,Math.round((bottom-r.top)*d))});
    });
  }
  function apply(next) {
    const old = state;
    state = next;
    if (state.palette) {
      const root = document.documentElement;
      root.dataset.appearance = String(state.appearance >= 2 ? 2 : state.appearance);
      root.style.colorScheme = state.palette.light ? 'light' : 'dark';
      for (const key of ['background','surface','raised','text','muted','border']) {
        root.style.setProperty(`--theme-${key}`, state.palette[key]);
      }
      const name = ['Dark','Light','Camo Light','Camo Dark'][state.appearance] || 'Camo Light';
      $('appearance').textContent = name;
      // No `title` tooltips anywhere: they are native windows that screen-capture exclusion cannot hide.
      $('appearance').setAttribute('aria-label', `Appearance: ${name}. Alt+C toggles camouflage; Alt+T switches Light/Dark`);
    }
    for (const app of ['home','browser','chatgpt','ghost']) $(app).hidden = app !== state.app;
    $('mode').className = `nav-pill ${state.mode === 'exclusive' ? 'pill-excl' : 'pill-mirror'}`;
    $('mode').setAttribute('aria-label', state.mode === 'exclusive' ? 'Exclusive' : 'Mirror');
    $('ghost-pill').className = `nav-pill ${state.ghost ? 'pill-ghost' : 'pill-solid'}`;
    $('ghost-pill').setAttribute('aria-label', state.ghost ? 'Ghost' : 'Solid');
    $('grab').hidden = state.grab !== 'failed';
    $('back').disabled = state.app === 'home';
    for (const b of document.querySelectorAll('.bottom-nav [data-app]')) b.classList.toggle('nav-selected', b.dataset.app === state.app);
    // Do not overwrite an address that is being edited on unrelated state updates.
    if (old.url !== state.url) $('url').value = state.url;
    $('progress').hidden = !state.loading;
    $('progress').firstElementChild.style.width = `${Math.max(0,Math.min(1,state.progress))*100}%`;
    icon($('reload'), state.loading ? 'icon-stop' : 'icon-reload');
    $('reload').setAttribute('aria-label', state.loading ? 'Stop loading' : 'Reload page');
    $('popup').hidden = !state.popup;
    if (pendingUrlFocus && state.app === 'browser') { pendingUrlFocus = false; $('url').focus(); }
    slot();
  }
  function toast(text) {
    clearTimeout(toastTimer);
    $('toast').firstElementChild.textContent = text;
    $('toast').classList.add('visible');
    slot();
    toastTimer = setTimeout(() => { $('toast').classList.remove('visible'); slot(); }, 2200);
  }
  if (host) host.addEventListener('message', e => {
    const m = e.data;
    if (!m || typeof m !== 'object') return;
    if (m.type === 'state') apply(m);
    else if (m.type === 'toast') toast(m.text);
    else if (m.type === 'reset-editor') { $('editor').value = ''; $('url').value = ''; updateEditor(); }
    else if (m.type === 'snap-state') setSnapState(m.state);
    else if (m.type === 'ghost-status') {
      $('ghost-status').textContent = m.text || 'Ready';
      if (m.eta) {
        $('eta').textContent = m.eta;
        $('eta').hidden = false;
      } else {
        $('eta').hidden = true;
      }
      if (m.button) $('start').textContent = m.button;
      setTyperState(m.state);
    } else if (m.type === 'ghost-settings') {
      $('human').checked = m.human;
      $('safe').checked = m.safe;
      $('human-options').hidden = !m.human;
      for (const b of document.querySelectorAll('[data-speed]')) b.setAttribute('aria-checked',String(Number(b.dataset.speed)===m.speed));
    } else if (m.type === 'set-text') {
      $('editor').value = m.value || '';
      updateEditor();
    }
  });
  for (const b of document.querySelectorAll('[data-app]')) {
    b.setAttribute('aria-label', b.textContent.trim());
    b.addEventListener('click', () => post('nav', {app: b.dataset.app}));
  }
  $('appearance').addEventListener('click', () => post('appearance', {mode: ((state.appearance || 0) + 1) % 3}));
  for (const b of document.querySelectorAll('[data-action]')) b.addEventListener('click', () => post(b.dataset.action));
  for (const b of document.querySelectorAll('[data-pill]')) b.addEventListener('click', () => post('pill', {which: b.dataset.pill}));
  for (const b of document.querySelectorAll('[data-window]')) b.addEventListener('click', () => post('window', {action: b.dataset.window}));
  $('home-search').addEventListener('click', () => { pendingUrlFocus = true; post('search-home'); });
  $('url-form').addEventListener('submit', e => { e.preventDefault(); post('url', {value: $('url').value}); });
  $('url').addEventListener('focusin', () => post('focus', {where: 'url'}));
  $('url').addEventListener('focusout', () => post('focus', {where: 'page'}));
  $('reload').addEventListener('click', () => post(state.loading ? 'stop' : 'reload'));
  $('popup-close').addEventListener('click', () => post('popup-close'));
  $('handle').addEventListener('mousedown', e => { if (e.button === 0) { e.preventDefault(); post('drag', {kind:'move'}); } });
  $('handle').addEventListener('dblclick', () => post('dblclick-handle'));
  for (const edge of document.querySelectorAll('[data-edges]')) edge.addEventListener('mousedown', e => {
    if (e.button === 0) {e.preventDefault(); post('drag', {kind:'resize', edges:Number(edge.dataset.edges)});}
  });
  function updateEditor() {
    const text = $('editor').value, lines = text.split('\n').length;
    $('count').textContent = `${Array.from(text).length} chars · ${lines} line${lines === 1 ? '' : 's'}`;
    $('editor').style.height = '72px';
    $('editor').style.height = `${Math.min(180, Math.max(72, $('editor').scrollHeight+2))}px`;
  }
  let ghostTextTimer;
  function onEditorInput() {
    updateEditor();
    clearTimeout(ghostTextTimer);
    ghostTextTimer = setTimeout(() => {
      post('ghost-text', {value: $('editor').value});
    }, 100);
  }
  $('editor').addEventListener('input', onEditorInput);

  function sendGhostSettings() {
    const human = $('human').checked;
    const safe = $('safe').checked;
    let speed = 0;
    for (const b of document.querySelectorAll('[data-speed]')) {
      if (b.getAttribute('aria-checked') === 'true') {
        speed = Number(b.dataset.speed);
        break;
      }
    }
    post('ghost-settings', {human, speed, safe});
  }
  $('human').addEventListener('change', () => {
    $('human-options').hidden = !$('human').checked;
    sendGhostSettings();
  });
  $('safe').addEventListener('change', sendGhostSettings);
  for (const b of document.querySelectorAll('[data-speed]')) {
    b.addEventListener('click', () => {
      for (const mode of document.querySelectorAll('[data-speed]')) mode.setAttribute('aria-checked', String(mode === b));
      sendGhostSettings();
    });
  }
  $('start').addEventListener('click', () => { clearTimeout(ghostTextTimer); post('ghost-text',{value:$('editor').value}); post('ghost-start'); });
  new ResizeObserver(slot).observe($('viewport'));
  window.addEventListener('resize', slot);
  post('ready');
})();
