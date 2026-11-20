// Windows overlay settings. Typing always starts in the normal application.
#pragma once

#include "common.h"

namespace cronq {

struct AppConfig {
    int width = 360;
    int height = 640;
    int margin = 12;
    std::string corner = "bottom-left";  // bottom-left, bottom-right, top-left, top-right
    std::string monitor = "primary";     // "primary" or a monitor index
    double opacity = 1.0;                // 0.1 .. 1.0
    bool startVisible = false;
    int appearanceMode = 2;              // 0 dark, 1 light, 2 transparent black text (default), 3 transparent white text

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
