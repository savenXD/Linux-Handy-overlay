#pragma once
#include <string>
namespace cronq {
namespace browser_logic {
std::string NormalizeInput(const std::string&, const std::string& searchTemplate);
bool IsAuthUri(const std::string&);
std::string ExtractHost(const std::string&);
bool IsDomainAllowedForChatGpt(const std::string&, const std::string& allowlistCsv);
std::string ChatGptUserScript();
}  // namespace browser_logic
}  // namespace cronq
