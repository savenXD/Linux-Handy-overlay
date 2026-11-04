#include "browser_logic.h"

#include <algorithm>
namespace cronq {
namespace browser_logic {
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
std::string NormalizeInput(const std::string& input, const std::string& tmpl) {
    auto a = input.find_first_not_of(" \t\r\n\v\f");
    if (a == std::string::npos)
        return {};
    std::string s = input.substr(a, input.find_last_not_of(" \t\r\n\v\f") - a + 1);
    for (auto p : {"http://", "https://", "file://", "about:"})
        if (Starts(s, p))
            return s;
    if (s.find(' ') == std::string::npos && (s.find('.') != std::string::npos || Starts(s, "localhost")))
        return "https://" + s;
    std::string encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    // src/browser.c load_uri_internal: GLib allows UTF-8, but no reserved ASCII.
    for (unsigned char c : s) {
        if (c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '.' || c == '_' || c == '~')
            encoded += char(c);
        else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 15];
        }
    }
    auto pos = tmpl.find("%s");
    return pos == std::string::npos ? "https://duckduckgo.com/?q=" + encoded
                                    : tmpl.substr(0, pos) + encoded + tmpl.substr(pos + 2);
}
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
std::string ChatGptUserScript() {
    return
#include "chatgpt_script.inc"
        ;
}
}  // namespace browser_logic
}  // namespace cronq
