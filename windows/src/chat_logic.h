#pragma once
#include <string>
namespace cronq {
namespace chat_logic {
bool IsAuthUri(const std::string&);
std::string ExtractHost(const std::string&);
bool IsDomainAllowedForChatGpt(const std::string&, const std::string& allowlistCsv);
std::string ChatGptUserScript();
// Removes `title` attributes and SVG <title> elements as they appear. Native tooltips are separate
// WebView2 windows that screen-capture exclusion cannot cover, so pages must never show them.
std::string NoTooltipScript();
}  // namespace chat_logic
}  // namespace cronq
