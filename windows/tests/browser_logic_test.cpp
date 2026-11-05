#include "browser_logic.h"

#include <cstdio>
using namespace cronq::browser_logic;
#define CHECK(x)                                             \
    do {                                                     \
        if (!(x)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                        \
        }                                                    \
    } while (0)
int main() {
    const std::string t = "https://duckduckgo.com/?q=%s";
    CHECK(NormalizeInput("hello world", t) == "https://duckduckgo.com/?q=hello%20world");
    CHECK(NormalizeInput("example.com", t) == "https://example.com");
    CHECK(NormalizeInput("localhost:3000", t) == "https://localhost:3000");
    CHECK(NormalizeInput("cats", t) == "https://duckduckgo.com/?q=cats");
    CHECK(NormalizeInput("a b.com", t) == "https://duckduckgo.com/?q=a%20b.com");
    CHECK(NormalizeInput("https://x.y/z?q=1", t) == "https://x.y/z?q=1");
    CHECK(NormalizeInput("  rust  ", t) == "https://duckduckgo.com/?q=rust");
    CHECK(NormalizeInput("a&b=c/d", t) == "https://duckduckgo.com/?q=a%26b%3Dc%2Fd");
    CHECK(NormalizeInput("naïve", t) == "https://duckduckgo.com/?q=naïve");
    CHECK(NormalizeInput(" \t\r\n", t).empty());
    CHECK(NormalizeInput("a b", "https://s/?q=%s&x=%s") == "https://s/?q=a%20b&x=%s");
    CHECK(NormalizeInput("a b", "bad") == "https://duckduckgo.com/?q=a%20b");
    CHECK(NormalizeInput("file:///x", t) == "file:///x");
    CHECK(NormalizeInput("about:blank", t) == "about:blank");
    CHECK(ExtractHost("https://user:pass@example.org:80?q=2") == "example.org");
    CHECK(ExtractHost("http://[::1]:8080/#f") == "::1");
    CHECK(ExtractHost("about:blank").empty());
    CHECK(IsAuthUri("https://x/SetSID"));
    CHECK(!IsAuthUri("https://x/LOGIN"));
    for (auto uri : {"https://chatgpt.com/", "https://accounts.google.com/x", "https://sub.openai.com", "about:blank",
                     "data:text/plain,hi", "", "https://sub.CHATGPT.COM"})
        CHECK(IsDomainAllowedForChatGpt(uri, "chatgpt.com"));
    CHECK(!IsDomainAllowedForChatGpt("https://evil.example/", "chatgpt.com"));
    CHECK(!IsDomainAllowedForChatGpt("https://example.org/", "chatgpt.com"));
    CHECK(IsDomainAllowedForChatGpt("https://containschatgpt.com.evil/", "chatgpt.com"));
    CHECK(IsDomainAllowedForChatGpt("https://xapple.com/", ""));
    CHECK(IsDomainAllowedForChatGpt("https://one.test/", " other.test; one.test "));
    CHECK(ChatGptUserScript().find("DOMContentLoaded") != std::string::npos);
    CHECK(ChatGptUserScript().find("__cronq_chatgpt_enter_installed") != std::string::npos);
    CHECK(NoTooltipScript().find("removeAttribute('title')") != std::string::npos);
    CHECK(NoTooltipScript().find("MutationObserver") != std::string::npos);
    CHECK(ChatGptUserScript().find("#ffffff") == std::string::npos);  // caret follows the theme text colour
    std::puts("browser_logic_test: all checks passed");
}
