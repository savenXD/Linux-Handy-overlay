// Browser-only mock of the native bridge for visual QA. Never shipped in ui/.
(() => {
  let listener;
  const state = {type:'state', mode:'normal', ghost:false, grab:'none', app:'home', opacity:1, width:360, height:280, canBack:false, popup:false, scale:1};
  function send(message) { if (listener) listener({data:message}); }
  const push = () => { state.height = state.app === 'home' ? 280 : 640; send({...state}); };
  window.__previewState = next => { Object.assign(state,next); push(); };
  window.__previewEvents = [];
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    addEventListener: (_, fn) => { listener = fn; },
    postMessage: m => {
      window.__previewEvents.push(m);
      document.documentElement.dataset.lastMessage = JSON.stringify(m);
      if (m.type === 'ready') push();
      else if (m.type === 'nav') {state.app = m.app; push();}
      else if (m.type === 'back') {state.app = 'home'; push();}
      else if (m.type === 'pill') { if (m.which === 'mode') state.mode = state.mode === 'normal' ? 'overlay' : 'normal'; else state.ghost = !state.ghost; push(); }
      else if (m.type === 'reset') {state.app = 'home'; state.mode = 'normal'; state.ghost = false; push();send({type:'toast',text:'Cronq reset to new state'});}
      else if (m.type === 'snap') send({type:'snap-state',state:'done'});
    }
  };
})();
