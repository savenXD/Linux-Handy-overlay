(function () {
  function run() {
(function() {
  function injectCaretStyle() {
    if (document.getElementById('__cronq_caret_style')) return;
    var style = document.createElement('style');
    style.id = '__cronq_caret_style';
    style.textContent = 'input, textarea, [contenteditable], #prompt-textarea, .ProseMirror { caret-color: #ffffff !important; }';
    (document.head || document.documentElement).appendChild(style);
  }
  injectCaretStyle();
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', injectCaretStyle);
  }

  if (window.__cronq_chatgpt_enter_installed) return;
  window.__cronq_chatgpt_enter_installed = true;

  function findSendButton() {
    var btn = document.querySelector('button[data-testid="send-button"]') ||
              document.querySelector('button[data-testid="fruitjuice-send-button"]');
    if (btn && !btn.disabled) return btn;

    btn = document.querySelector('button[aria-label*="Send" i]') ||
          document.querySelector('button[aria-label*="Submit" i]');
    if (btn && !btn.disabled) return btn;

    var promptEl = document.querySelector('#prompt-textarea') || document.querySelector('textarea');
    if (promptEl) {
      var form = promptEl.closest('form');
      if (form) {
        var btns = Array.from(form.querySelectorAll('button:not(:disabled)'));
        for (var i = 0; i < btns.length; i++) {
          var b = btns[i];
          var aria = (b.getAttribute('aria-label') || '').toLowerCase();
          var testid = (b.getAttribute('data-testid') || '').toLowerCase();
          if (aria.includes('add') || aria.includes('attach') || aria.includes('voice') || aria.includes('mic')) continue;
          if (testid.includes('voice') || testid.includes('mic') || testid.includes('attach')) continue;
          return b;
        }
      }
    }
    return null;
  }

  function triggerSubmit() {
    var btn = findSendButton();
    if (btn && !btn.disabled) {
      btn.focus();
      btn.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true, view: window }));
      btn.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, view: window }));
      btn.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, cancelable: true, view: window }));
      btn.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, view: window }));
      btn.click();
      return true;
    }
    var promptEl = document.querySelector('#prompt-textarea') || document.querySelector('textarea');
    var form = promptEl ? promptEl.closest('form') : document.querySelector('form');
    if (form && typeof form.requestSubmit === 'function') {
      try { form.requestSubmit(); return true; } catch(err) {}
    }
    return false;
  }

  window.addEventListener('keydown', function(e) {
    if (e.key === 'Enter' && !e.shiftKey && !e.ctrlKey && !e.altKey && !e.metaKey && !e.isComposing) {
      var target = e.target;
      if (!target) return;
      var isPrompt = (target.id === 'prompt-textarea') ||
                     (target.tagName === 'TEXTAREA') ||
                     (target.isContentEditable) ||
                     (target.closest && target.closest('#prompt-textarea'));
      if (isPrompt) {
        e.preventDefault();
        e.stopPropagation();
        e.stopImmediatePropagation();
        triggerSubmit();
      }
    }
  }, true);

  document.addEventListener('pointerup', function(e) {
    var btn = e.target && e.target.closest && (e.target.closest('button[data-testid="send-button"]') || e.target.closest('button[data-testid="fruitjuice-send-button"]'));
    if (btn && !btn.disabled) {
      setTimeout(function() { triggerSubmit(); }, 50);
    }
  }, true);
})();
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run);
  else run();
})();
