// Same keys, defaults and validation ranges as src/config.c on Linux.
#pragma once

#include "common.h"

namespace cronq {

enum class KeyMode { Mirror = 0, Exclusive = 1 };

struct AppConfig {
    int width = 360;
    int height = 640;
    int margin = 12;
    std::string corner = "bottom-left";  // bottom-left, bottom-right, top-left, top-right
    std::string monitor = "primary";     // "primary" or a monitor index
    double opacity = 1.0;                // 0.1 .. 1.0
    bool startVisible = false;
    int appearanceMode = 2;              // 0 dark, 1 light, 2 transparent black text (default), 3 transparent white text
    KeyMode defaultKeyMode = KeyMode::Mirror;

    std::string homeUrl = "https://html5test.com";
    std::string searchUrlTemplate = "https://duckduckgo.com/?q=%s";
    std::string userAgent =
        "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Mobile Safari/537.36";
    std::string chatgptAllowlist =
        "chatgpt.com,openai.com,oaistatic.com,oaiusercontent.com,auth0.com,google.com,google.co.in,"
        "gstatic.com,googleusercontent.com,googleapis.com,recaptcha.net,cloudflare.com,"
        "challenges.cloudflare.com,microsoft.com,microsoftonline.com,live.com,msftauth.net,apple.com";

    char toggleKey = 'x';
    char modeKey = 'm';
    char ghostKey = 'g';
    char snapKey = 'p';
    bool shieldClipboardKeys = true;
    bool hideFromCapture = true;  // [Privacy] hide_from_capture (default ON)

    std::string snapScope = "monitor";  // "monitor" or "all"
    int snapDelayMs = 80;
    bool snapSave = true;
    int snapKeepCount = 20;

    char ghostWriterKey = 'w';
    int ghostDelayMs = 0;
    std::string ghostNewline = "space";  // space, return, skip
    std::string ghostTab = "space";      // space, tab, skip
    int ghostMaxChars = 100000;
};

namespace config {

bool Init();                 // creates a default config.ini on first run, then loads it
bool Reload();               // re-reads config.ini; false if the file could not be read
bool SetAppearanceMode(int mode); // persists the Windows appearance preference atomically
bool SetCorner(const std::string& corner);
const AppConfig& Get();
std::wstring Path();         // %APPDATA%\cronq\config.ini

// Parses INI text into cfg using the Linux validation rules. Exposed for unit tests.
void ParseInto(const std::string& text, AppConfig* cfg);

}  // namespace config
}  // namespace cronq
