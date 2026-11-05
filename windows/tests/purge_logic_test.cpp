#include <cstdio>

#include "purge_logic.h"

using namespace cronq::purge_logic;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)

int main() {
    CHECK(IsCronqRunValue(L"Cronq", L"anything"));
    CHECK(IsCronqRunValue(L"X", L"\"C:\\Users\\a\\AppData\\Local\\Programs\\Cronq\\cronqw.exe\" --supervise"));
    CHECK(IsCronqRunValue(L"X", L"cronq.exe show"));
    CHECK(!IsCronqRunValue(L"OneDrive", L"\"C:\\Program Files\\OneDrive\\OneDrive.exe\" /background"));
    CHECK(!IsCronqRunValue(L"Other", L"C:\\tools\\mycronqhelper.exe"));  // not our executable
    CHECK(!IsCronqRunValue(L"Other", L"C:\\cronq-notes\\notes.exe"));

    CHECK(IsCronqShortcut(L"Cronq.lnk"));
    CHECK(IsCronqShortcut(L"cronq (2).LNK"));
    CHECK(!IsCronqShortcut(L"Cronq notes.txt"));
    CHECK(!IsCronqShortcut(L"Chrome.lnk"));

    std::wstring out;
    CHECK(RemovePathEntry(L"C:\\a;C:\\Users\\u\\Cronq\\;%USERPROFILE%\\bin", L"c:\\users\\u\\cronq", &out));
    CHECK(out == L"C:\\a;%USERPROFILE%\\bin");
    CHECK(!RemovePathEntry(L"C:\\a;C:\\b", L"C:\\Users\\u\\Cronq", &out));
    CHECK(out == L"C:\\a;C:\\b");
    CHECK(RemovePathEntry(L"C:\\Cronq", L"C:\\Cronq", &out) && out.empty());
    CHECK(!RemovePathEntry(L"C:\\Cronq2;C:\\Cron", L"C:\\Cronq", &out));  // no partial matches
    CHECK(!RemovePathEntry(L"C:\\a", L"", &out));

    CHECK(RemoveWholeInstallDir(L"C:\\Users\\u\\AppData\\Local\\Programs\\Cronq\\",
                                L"c:\\users\\u\\appdata\\local\\programs\\cronq"));
    CHECK(!RemoveWholeInstallDir(L"C:\\Users\\u\\Downloads", L"C:\\Users\\u\\AppData\\Local\\Programs\\Cronq"));
    CHECK(!RemoveWholeInstallDir(L"C:\\x", L""));

    CHECK(IsOwnFile(L"CRONQW.EXE"));
    CHECK(IsOwnFile(L"unins000.exe"));
    CHECK(IsOwnFile(L"ui"));
    CHECK(!IsOwnFile(L"report.docx"));
    CHECK(!IsOwnFile(L"cronq-notes.txt"));
    std::puts("purge_logic_test: all checks passed");
}
