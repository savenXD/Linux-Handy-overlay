#include "theme.h"
#include "config.h"
#include "browser.h"
#include "overlay.h"
#include "ui_bridge.h"
#include <algorithm>
#include <cstdlib>
namespace cronq::theme {
namespace {
int g_mode = 0;
auto g_palette = theme_logic::MakePalette(0);
void Publish() { browser::RefreshAppearance(); ui::PushState(); overlay::RefreshAppearance(); }
}
void Init() { Reload(); }
void Cleanup() {}
void Reload() { g_mode=config::Get().appearanceMode; g_palette=theme_logic::MakePalette(g_mode==2 ? 1 : g_mode==3 ? 0 : g_mode); Publish(); }
bool SetMode(int mode) {
    if (mode < 0 || mode > 3 || !config::SetAppearanceMode(mode)) return false;
    Reload(); return true;
}
bool ToggleCamouflage() { return SetMode(g_mode==2 ? 1 : g_mode==3 ? 0 : g_mode==0 ? 3 : 2); }
bool ToggleTone() { return SetMode(g_mode==2 ? 3 : g_mode==3 ? 2 : g_mode==0 ? 1 : 0); }
bool IsCamouflage() { return g_mode>=2; }
int Mode() { return g_mode; }
const theme_logic::Palette& Get() { return g_palette; }
std::string PaletteJson() {
    const auto& p=g_palette;
    return "{\"background\":\""+theme_logic::Hex(p.background)+"\",\"surface\":\""+theme_logic::Hex(p.surface)+"\",\"raised\":\""+theme_logic::Hex(p.raised)+"\",\"text\":\""+theme_logic::Hex(p.text)+"\",\"muted\":\""+theme_logic::Hex(p.muted)+"\",\"border\":\""+theme_logic::Hex(p.border)+"\",\"light\":"+(p.light ? "true" : "false")+"}";
}
std::string WebScript() {
    // Only ChatGPT receives surface overrides. Other sites retain their own design and get the profile color preference.
    auto palette=PaletteJson();
    if(IsCamouflage()) {
        palette.pop_back();palette+=",\"minimal\":true}";
    }
    return "(()=>{if(location.hostname!=='chatgpt.com'&&!location.hostname.endsWith('.chatgpt.com'))return;const palette="+palette+R"JS(;
if(!window.__cronqApplyAppearance){
  let current, style, observer;
  const apply=()=>{
    const root=document.documentElement;if(!root||!current)return;
    if(!observer){observer=new MutationObserver(apply);observer.observe(root,{attributes:true,attributeFilter:['class','style']});}
    const mode=current.light?'light':'dark', other=current.light?'dark':'light';
    if(root.classList.contains(other))root.classList.remove(other);
    if(!root.classList.contains(mode))root.classList.add(mode);
    if(root.style.colorScheme!==mode)root.style.colorScheme=mode;
    if(!style||!style.isConnected){style=document.createElement('style');style.id='cronq-appearance';(document.head||root).appendChild(style);}
    const p=current;
    const css=`:root{color-scheme:${mode}!important;--bg-primary:${p.background}!important;--bg-secondary:${p.surface}!important;--bg-tertiary:${p.raised}!important;--main-surface-primary:${p.background}!important;--main-surface-secondary:${p.surface}!important;--main-surface-tertiary:${p.raised}!important;--sidebar-surface-primary:${p.surface}!important;--text-primary:${p.text}!important;--text-secondary:${p.muted}!important;--text-tertiary:${p.muted}!important;--border-light:${p.border}!important;--border-medium:${p.border}!important}html,body{background:${p.background}!important;color:${p.text}!important}main{background-color:${p.background}!important}`;
    const minimal=p.minimal ? `
      header,nav,aside,footer,#stage-slideover-sidebar,#stage-sidebar,
      [data-testid="sidebar"],[data-testid="model-switcher-dropdown-button"],
      [data-testid="conversation-options-button"],[data-testid="share-chat-button"],
      [data-testid*="avatar"],[data-testid="thread-bottom-disclaimer"],[data-testid="chatgpt-logo"],svg[aria-label="ChatGPT"],
      article button,article [role="group"],.avatar {display:none!important}
      html,body,body :where(div,section,article,main,form,header,footer,nav,aside,textarea,[contenteditable]){background:transparent!important;border-color:transparent!important;box-shadow:none!important;border-radius:0!important}
      body,body :where(p,span,li,div,textarea,[contenteditable],h1,h2,h3,h4,pre,code,button,a){color:${p.text}!important}
      main{padding-top:0!important}
      textarea:focus-visible,[contenteditable]:focus-visible{outline:none!important}
      button[data-testid="send-button"] svg{display:none!important}
      button[data-testid="send-button"]::after{content:'Send';font-size:12px}
      button[data-testid="send-button"]{color:${p.text}!important;background:transparent!important;border:0!important}
    ` : '';
    if(style.textContent!==css+minimal)style.textContent=css+minimal;
  };
  window.__cronqApplyAppearance=p=>{current=p;apply();};
  document.addEventListener('DOMContentLoaded',apply,{once:true});
}
window.__cronqApplyAppearance(palette);})();)JS";
}
}
