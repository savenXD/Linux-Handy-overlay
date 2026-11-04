// Browser-only mock of the native bridge for visual QA. Never shipped in ui/.
(() => {
  let listener;
  const state = {type:'state', mode:'mirror', ghost:false, grab:'none', app:'home', opacity:1, width:360, height:280, canBack:false, loading:false, progress:1, url:'https://html5test.com', popup:false, scale:1};
  function send(message) { if (listener) listener({data:message}); }
  const push = () => { state.height = state.app === 'home' ? 280 : 640; send({...state}); };
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    addEventListener: (_, fn) => { listener = fn; },
    postMessage: m => {
      document.documentElement.dataset.lastMessage = JSON.stringify(m);
      if (m.type === 'ready') push();
      else if (m.type === 'nav') {state.app = m.app; push();}
      else if (m.type === 'search-home') {state.app = 'browser'; push();}
      else if (m.type === 'back') {state.app = 'home'; push();}
      else if (m.type === 'pill') { if (m.which === 'mode') state.mode = state.mode === 'mirror' ? 'exclusive' : 'mirror'; else state.ghost = !state.ghost; push(); }
      else if (m.type === 'reset') {state.app = 'home'; state.mode = 'mirror'; state.ghost = false; send({type:'reset-editor'}); push();send({type:'toast',text:'Cronq reset to new state'});}
      else if (m.type === 'ghost-start') send({type:'toast',text:'Ghost Writer arrives in Phase 3'});
      else if (m.type === 'snap') send({type:'toast',text:'Snap arrives in Phase 3'});
      else if (m.type === 'url') {state.url = m.value; push();}
      else if (m.type === 'reload') {state.loading = true;state.progress = 0.5;push();}
      else if (m.type === 'stop') {state.loading = false;push();}
    }
  };
})();
