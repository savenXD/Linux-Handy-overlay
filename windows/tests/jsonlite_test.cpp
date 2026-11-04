#include "jsonlite.h"

#include <cstdio>
using namespace cronq::json;
#define CHECK(x)                                             \
    do {                                                     \
        if (!(x)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                        \
        }                                                    \
    } while (0)
int main() {
    std::map<std::string, std::string> m;
    CHECK(ParseFlat(" {} \n", &m) && m.empty());
    CHECK(ParseFlat("{\"n\":-1.5e3,\"b\":true,\"f\":false,\"nil\":null}", &m));
    CHECK(m["n"] == "-1.5e3" && m["b"] == "true" && m["nil"] == "null");
    CHECK(ParseFlat("{\"s\":\"\\uD83D\\uDE00\\u00EF\\u0000\"}", &m));
    CHECK(m["s"] == std::string("😀ï\0", 7));
    std::string s = "\"\\/\b\f\n\r\t";
    s += char(1);
    s += "日本語";
    CHECK(ParseFlat("{\"s\":\"" + Escape(s) + "\"}", &m) && m["s"] == s);
    for (auto bad : {"", "[]", "{\"a\":{}}", "{\"a\":[]}", "{}junk", "{\"a\":01}", "{\"a\":1.}", "{\"a\":+1}",
                     "{\"a\":1e}", "{\"a\":truex}", "{\"a\":\"\\uD800\"}", "{\"a\":\"\\uDC00\"}",
                     "{\"a\":\"\\uD800\\u0041\"}", "{\"a\":\"\\q\"}", "{\"a\":1,}", "{\"a\":1,\"a\":2}",
                     "{\"a\":\"\n\"}"})
        CHECK(!ParseFlat(bad, &m));
    CHECK(!ParseFlat("{}", nullptr));
    int value = -1;
    CHECK(ParseInt("15", 0, 15, &value) && value == 15);
    CHECK(ParseInt("0", 0, 15, &value) && value == 0);
    for (const auto* invalid :
         {"", "16", "-1", "1.5", "1e1", "NaN", "Infinity", "1000000000000000000", "1x", " 1"})
        CHECK(!ParseInt(invalid, 0, 15, &value));
    CHECK(!ParseInt("1", 0, 15, nullptr));
    const std::string editor(100000, 'x');
    CHECK(ParseFlat("{\"value\":\"" + editor + "\"}", &m) && m["value"] == editor);
    std::puts("jsonlite_test: all checks passed");
}
