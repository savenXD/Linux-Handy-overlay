#include "config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <fstream>
#include "ini_writer.h"

namespace cronq {
namespace config {

namespace {

AppConfig g_cfg;
std::wstring g_path;

const char* const kDefaultIni =
    "# Cronq Configuration\n"
    "\n"
    "[General]\n"
    "width = 360\n"
    "height = 640\n"
    "margin = 12\n"
    "# Options: bottom-left, bottom-right, top-left, top-right\n"
    "corner = bottom-left\n"
    "# Options: primary or monitor index (0, 1, ...)\n"
    "monitor = primary\n"
    "opacity = 1.0\n"
    "start_visible = false\n"
    "# Options: mirror, exclusive\n"
    "default_key_mode = mirror\n"
    "\n"
    "[Browser]\n"
    "home_url = https://html5test.com\n"
    "search_url_template = https://duckduckgo.com/?q=%s\n"
    "user_agent = Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Mobile Safari/537.36\n"
    "\n"
    "[Hotkeys]\n"
    "# Shortcuts combine Alt + <key> (Alt+X=Toggle, Alt+G=Ghost, Alt+A/S=Opacity, Alt+F=Fullscreen, "
    "Alt+D=DefaultSize, Alt+P=Snap)\n"
    "toggle_key = x\n"
    "mode_key = m\n"
    "ghost_key = g\n"
    "snap_key = p\n"
    "ghost_writer_key = w\n"
    "\n"
    "[Privacy]\n"
    "# Hides the overlay from screen recorders and screen sharing (you still see it). On by default.\n"
    "hide_from_capture = true\n"
    "\n"
    "[Snap]\n"
    "# Options: monitor (primary), all (all screens)\n"
    "snap_scope = monitor\n"
    "snap_delay_ms = 80\n"
    "snap_save = true\n"
    "snap_keep_count = 20\n"
    "\n"
    "[GhostWriter]\n"
    "ghost_delay_ms = 0\n"
    "# Options: space, return, skip\n"
    "ghost_newline = space\n"
    "# Options: space, tab, skip\n"
    "ghost_tab = space\n"
    "ghost_max_chars = 100000\n";

// section -> key -> value, mirroring GKeyFile: '#' starts a comment line, keys are case-sensitive.
using Ini = std::map<std::string, std::map<std::string, std::string>>;

Ini ParseIni(const std::string& textIn) {
    std::string text = textIn;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        text.erase(0, 3);  // UTF-8 BOM (Notepad adds one)
    }

    Ini ini;
    std::string section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = Trim(text.substr(pos, eol - pos));
        pos = eol + 1;

        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos) section = line.substr(1, close - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (!key.empty()) ini[section][key] = val;
    }
    return ini;
}

const std::string* Find(const Ini& ini, const char* section, const char* key) {
    auto s = ini.find(section);
    if (s == ini.end()) return nullptr;
    auto k = s->second.find(key);
    if (k == s->second.end()) return nullptr;
    return &k->second;
}

bool GetInt(const Ini& ini, const char* section, const char* key, int* out) {
    const std::string* v = Find(ini, section, key);
    if (!v || v->empty()) return false;
    char* end = nullptr;
    errno = 0;
    long n = strtol(v->c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || n < -2147483647L || n > 2147483647L) return false;
    *out = (int)n;
    return true;
}

bool GetDouble(const Ini& ini, const char* section, const char* key, double* out) {
    const std::string* v = Find(ini, section, key);
    if (!v || v->empty()) return false;
    char* end = nullptr;
    errno = 0;
    double d = strtod(v->c_str(), &end);
    if (errno != 0 || *end != '\0') return false;
    *out = d;
    return true;
}

bool GetBool(const Ini& ini, const char* section, const char* key, bool* out) {
    const std::string* v = Find(ini, section, key);
    if (!v) return false;
    if (*v == "true" || *v == "1") { *out = true; return true; }
    if (*v == "false" || *v == "0") { *out = false; return true; }
    return false;
}

bool GetStr(const Ini& ini, const char* section, const char* key, std::string* out) {
    const std::string* v = Find(ini, section, key);
    if (!v || v->empty()) return false;
    *out = *v;
    return true;
}

bool ReadFileText(const std::wstring& path, std::string* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out->clear();
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) out->append(buf, n);
    CloseHandle(h);
    return true;
}

void WriteDefaultFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LogErr("CONFIG", "Failed to create default config at '%s'.", Narrow(path).c_str());
        return;
    }
    DWORD written = 0;
    WriteFile(h, kDefaultIni, (DWORD)strlen(kDefaultIni), &written, nullptr);
    CloseHandle(h);
    Log("CONFIG", "Generated default configuration at '%s'.", Narrow(path).c_str());
}

}  // namespace

void ParseInto(const std::string& text, AppConfig* cfg) {
    Ini ini = ParseIni(text);
    int i = 0;
    double d = 0;
    bool b = false;
    std::string s;

    // [General]
    if (GetInt(ini, "General", "width", &i) && i >= 200 && i <= 3840) cfg->width = i;
    if (GetInt(ini, "General", "height", &i) && i >= 300 && i <= 2160) cfg->height = i;
    if (GetInt(ini, "General", "margin", &i) && i >= 0 && i <= 500) cfg->margin = i;
    if (GetStr(ini, "General", "corner", &s)) cfg->corner = s;
    if (GetStr(ini, "General", "monitor", &s)) cfg->monitor = s;
    if (GetDouble(ini, "General", "opacity", &d) && d >= 0.1 && d <= 1.0) cfg->opacity = d;
    if (GetBool(ini, "General", "start_visible", &b)) cfg->startVisible = b;
    if (const std::string* km = Find(ini, "General", "default_key_mode")) {
        cfg->defaultKeyMode = EqNoCase(*km, "exclusive") ? KeyMode::Exclusive : KeyMode::Mirror;
    }

    // [Browser]
    if (GetInt(ini, "Appearance", "mode", &i) && i >= 0 && i <= 3) cfg->appearanceMode = i;
    if (GetStr(ini, "Browser", "home_url", &s)) cfg->homeUrl = s;
    if (GetStr(ini, "Browser", "search_url_template", &s)) cfg->searchUrlTemplate = s;
    if (GetStr(ini, "Browser", "user_agent", &s)) cfg->userAgent = s;
    if (GetStr(ini, "Browser", "chatgpt_allowlist", &s)) cfg->chatgptAllowlist = s;

    // [Privacy]
    if (GetBool(ini, "Privacy", "hide_from_capture", &b)) cfg->hideFromCapture = b;

    // [Hotkeys]
    if (GetStr(ini, "Hotkeys", "toggle_key", &s)) cfg->toggleKey = s[0];
    if (GetStr(ini, "Hotkeys", "mode_key", &s)) cfg->modeKey = s[0];
    if (GetStr(ini, "Hotkeys", "ghost_key", &s)) cfg->ghostKey = s[0];
    if (GetBool(ini, "Hotkeys", "shield_clipboard_keys", &b)) cfg->shieldClipboardKeys = b;
    // snap_key: [Hotkeys] first, then [Snap]
    if (GetStr(ini, "Hotkeys", "snap_key", &s) || GetStr(ini, "Snap", "snap_key", &s)) cfg->snapKey = s[0];

    // [Snap]
    if (GetStr(ini, "Snap", "snap_scope", &s)) cfg->snapScope = s;
    if (GetInt(ini, "Snap", "snap_delay_ms", &i) && i >= 60 && i <= 5000) cfg->snapDelayMs = i;
    if (GetBool(ini, "Snap", "snap_save", &b)) cfg->snapSave = b;
    if (GetInt(ini, "Snap", "snap_keep_count", &i) && i >= 1 && i <= 500) cfg->snapKeepCount = i;

    // [GhostWriter]; ghost_writer_key: [Hotkeys] first, then [GhostWriter]
    if (GetStr(ini, "Hotkeys", "ghost_writer_key", &s) || GetStr(ini, "GhostWriter", "ghost_writer_key", &s)) {
        cfg->ghostWriterKey = s[0];
    }
    if (GetInt(ini, "GhostWriter", "ghost_delay_ms", &i) && i >= 0 && i <= 5000) cfg->ghostDelayMs = i;
    if (GetStr(ini, "GhostWriter", "ghost_newline", &s)) cfg->ghostNewline = s;
    if (GetStr(ini, "GhostWriter", "ghost_tab", &s)) cfg->ghostTab = s;
    if (GetInt(ini, "GhostWriter", "ghost_max_chars", &i) && i >= 0 && i <= 5000000) cfg->ghostMaxChars = i;
}

bool Init() {
    g_cfg = AppConfig();
    g_path = ConfigDir() + L"\\config.ini";

    if (GetFileAttributesW(g_path.c_str()) == INVALID_FILE_ATTRIBUTES) WriteDefaultFile(g_path);

    std::string text;
    if (ReadFileText(g_path, &text)) {
        ParseInto(text, &g_cfg);
        Log("CONFIG", "Loaded configuration from '%s' (%dx%d, corner=%s, monitor=%s, home='%s').",
            Narrow(g_path).c_str(), g_cfg.width, g_cfg.height, g_cfg.corner.c_str(), g_cfg.monitor.c_str(),
            g_cfg.homeUrl.c_str());
    } else {
        LogErr("CONFIG", "Failed to load config '%s'. Using defaults.", Narrow(g_path).c_str());
    }
    return true;
}

bool Reload() {
    if (g_path.empty()) return false;
    std::string text;
    if (!ReadFileText(g_path, &text)) {
        LogErr("CONFIG", "Failed to reload config '%s'.", Narrow(g_path).c_str());
        return false;
    }
    ParseInto(text, &g_cfg);  // like Linux: keys that are missing/invalid keep their current value
    Log("CONFIG", "Reloaded configuration from '%s'.", Narrow(g_path).c_str());
    return true;
}

const AppConfig& Get() { return g_cfg; }
bool SetAppearanceMode(int mode) {
    if (mode < 0 || mode > 3 || g_path.empty()) return false;
    std::string content;
    if (!ReadFileText(g_path, &content)) return false;
    ini_writer::SetIniInt(content,"Appearance","mode",mode);
    auto temp = g_path + L".appearance.tmp";
    std::ofstream out(temp.c_str(),std::ios::binary|std::ios::trunc);
    out.write(content.data(),static_cast<std::streamsize>(content.size())); out.close();
    if (!out || !MoveFileExW(temp.c_str(),g_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str()); return false;
    }
    g_cfg.appearanceMode=mode; return true;
}
std::wstring Path() { return g_path; }
bool SetCorner(const std::string& corner) {
    if ((corner!="bottom-left" && corner!="bottom-right") || g_path.empty()) return false;
    std::string content;
    if (!ReadFileText(g_path,&content)) return false;
    if (!ini_writer::SetIniString(content,"General","corner",corner)) return false;
    const auto temp=g_path+L".corner.tmp";
    std::ofstream out(temp.c_str(),std::ios::binary|std::ios::trunc);
    out.write(content.data(),static_cast<std::streamsize>(content.size()));out.close();
    if (!out || !MoveFileExW(temp.c_str(),g_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());return false;
    }
    g_cfg.corner=corner;return true;
}

}  // namespace config
}  // namespace cronq
