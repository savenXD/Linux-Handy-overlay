#pragma once
#include <map>
#include <string>
namespace cronq {
namespace json {
bool ParseFlat(const std::string&, std::map<std::string, std::string>* out);
bool ParseInt(const std::string&, int minimum, int maximum, int* out);
std::string Escape(const std::string&);
}  // namespace json
}  // namespace cronq
