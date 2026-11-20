#include "chat_logic.h"

#include <algorithm>
namespace cronq {
namespace chat_logic {
namespace {
bool Starts(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}
bool Ends(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return s;
}
}  // namespace
bool IsAuthUri(const std::string& s) {
    for (auto p : {"oauth", "auth0", "accounts.google", "signin", "challenge", "login", "SetSID"})
        if (s.find(p) != std::string::npos)
            return true;
    return false;
}
std::string ExtractHost(const std::string& uri) {
    auto begin = uri.find("://");
    if (begin == std::string::npos)
        return {};
    begin += 3;
    auto end = uri.find_first_of("/?#", begin);
    std::string host = uri.substr(begin, end == std::string::npos ? end : end - begin);
    auto at = host.rfind('@');
    if (at != std::string::npos)
        host.erase(0, at + 1);
    if (!host.empty() && host[0] == '[') {
        auto close = host.find(']');
        return close == std::string::npos ? "" : host.substr(1, close - 1);
    }
    return host.substr(0, host.find(':'));
}
bool IsDomainAllowedForChatGpt(const std::string& uri, const std::string& csv) {
    if (uri.empty())
        return true;
    for (auto p : {"about:", "data:", "blob:", "javascript:", "file:"})
        if (Starts(uri, p))
            return true;
    const std::string host = ExtractHost(uri), lower = Lower(host);
    if (host.empty() || host.find("google.") != std::string::npos)
        return true;
    for (auto p : {".google.com", "gstatic.com", "googleusercontent.com", "googleapis.com", "recaptcha.net",
                   "cloudflare.com", "openai.com", "oaistatic.com", "oaiusercontent.com", "auth0.com", "microsoft.com",
                   "microsoftonline.com", "live.com", "msftauth.net", "apple.com"})
        if (Ends(host, p))
            return true;
    // Keep Linux's permissive, case-sensitive substring rule too.
    for (size_t i = 0; i < csv.size();) {
        i = csv.find_first_not_of(",; ", i);
        if (i == std::string::npos)
            break;
        auto end = csv.find_first_of(",; ", i);
        std::string token = csv.substr(i, end == std::string::npos ? end : end - i);
        if (lower == Lower(token) || Ends(lower, "." + Lower(token)) || host.find(token) != std::string::npos)
            return true;
        if (end == std::string::npos)
            break;
        i = end;
    }
    return false;
}
std::string NoTooltipScript() {
    return R"JS((()=>{if(window.__cronqNoTips)return;window.__cronqNoTips=1;
const strip=n=>{if(!n||n.nodeType!==1)return;
if(n.hasAttribute('title'))n.removeAttribute('title');
if(n.localName==='title'&&n.closest&&n.closest('svg')){n.remove();return;}
if(n.querySelectorAll){for(const e of n.querySelectorAll('[title]'))e.removeAttribute('title');
for(const t of n.querySelectorAll('svg title'))t.remove();}};
const start=()=>{const root=document.documentElement;if(!root)return;strip(root);
new MutationObserver(ms=>{
  const added=new Set();
  for(const m of ms){
    // A title change only affects this element, not all its descendants.
    if(m.type==='attributes'){
      if(m.target.hasAttribute('title'))m.target.removeAttribute('title');
    }else for(const a of m.addedNodes)if(a.nodeType===1&&a.isConnected)added.add(a);
  }
  // Streaming replies can add a container and many of its descendants in one
  // batch. Scan only the outermost added subtrees, once each.
  for(const a of added){
    let parent=a.parentElement;
    while(parent&&!added.has(parent))parent=parent.parentElement;
    if(!parent)strip(a);
  }
})
.observe(root,{subtree:true,childList:true,attributes:true,attributeFilter:['title']});};
if(document.documentElement)start();else document.addEventListener('DOMContentLoaded',start,{once:true});})())JS";
}

std::string ChatGptUserScript() {
    return
#include "chatgpt_script.inc"
        ;
}
}  // namespace chat_logic
}  // namespace cronq
