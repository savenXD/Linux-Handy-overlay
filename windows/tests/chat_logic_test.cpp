#include "chat_logic.h"

#include <cstdio>
using namespace cronq::chat_logic;
#define CHECK(x)                                             \
    do {                                                     \
        if (!(x)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                        \
        }                                                    \
    } while (0)
int main() {
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
    std::puts("chat_logic_test: all checks passed");
}
