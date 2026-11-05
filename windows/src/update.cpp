#include "update.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdio>
#include <string>
#include <vector>

#include "update_logic.h"
#include "version.h"

namespace cronq {
namespace update {

namespace {

constexpr wchar_t kHost[] = L"api.github.com";
constexpr wchar_t kLatestPath[] = L"/repos/savenXD/Linux-Handy-overlay/releases/latest";
constexpr char kDownloadBase[] = "https://github.com/savenXD/Linux-Handy-overlay/releases/download/";
constexpr char kSetupName[] = "cronq-setup.exe";
constexpr size_t kMaxDownload = 200u * 1024u * 1024u;

struct Internet {
    HINTERNET h = nullptr;
    explicit Internet(HINTERNET handle) : h(handle) {}
    ~Internet() {
        if (h) WinHttpCloseHandle(h);
    }
};

// HTTPS GET with redirects (GitHub release downloads redirect to its CDN). HTTPS only.
bool Get(const std::string& url, std::string* body, DWORD* status) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    std::wstring wurl = Widen(url);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return false;

    // A generic client name: nothing that identifies the user.
    Internet session(WinHttpOpen(L"cronq-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) return false;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    Internet connect(WinHttpConnect(session.h, host, parts.nPort, 0));
    if (!connect.h) return false;
    Internet request(WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h) return false;
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream, */*\r\n";
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr))
        return false;
    DWORD code = 0, size = sizeof(code);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &code, &size, WINHTTP_NO_HEADER_INDEX);
    if (status) *status = code;
    body->clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) return false;
        if (!available) break;
        if (body->size() + available > kMaxDownload) return false;
        size_t old = body->size();
        body->resize(old + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, &(*body)[old], available, &read)) return false;
        body->resize(old + read);
    }
    return code == 200;
}

std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    unsigned char digest[32] = {};
    bool ok = BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                         static_cast<ULONG>(data.size()), digest, sizeof(digest)) == 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

bool AskYes(const char* question) {
    printf("%s [y/N] ", question);
    fflush(stdout);
    char answer[16] = {};
    if (!fgets(answer, sizeof(answer), stdin)) return false;
    return answer[0] == 'y' || answer[0] == 'Y';
}

}  // namespace

int Run(bool assumeYes) {
    printf("Checking for a newer Cronq...\n");
    std::string json;
    DWORD status = 0;
    if (!Get(std::string("https://") + Narrow(kHost) + Narrow(kLatestPath), &json, &status)) {
        fprintf(stderr, "Could not reach GitHub (HTTP %lu). Check your connection and try again.\n",
                (unsigned long)status);
        return 1;
    }
    const std::string tag = update_logic::TagName(json);
    if (tag.empty()) {
        fprintf(stderr, "Could not read the latest release information.\n");
        return 1;
    }
    if (update_logic::CompareVersions(tag, CRONQ_VERSION) <= 0) {
        printf("Cronq is up to date (%s).\n", CRONQ_VERSION);
        return 0;
    }
    printf("Cronq %s is available (you have %s).\n", tag.c_str(), CRONQ_VERSION);
    if (!assumeYes && !AskYes("Download and install it now?")) {
        printf("Not updated.\n");
        return 0;
    }

    const std::string base = std::string(kDownloadBase) + tag + "/";
    std::string sums, setup;
    if (!Get(base + "SHA256SUMS.txt", &sums, &status)) {
        fprintf(stderr, "Could not download the checksum file (HTTP %lu).\n", (unsigned long)status);
        return 1;
    }
    const std::string expected = update_logic::Sha256For(sums, kSetupName);
    if (expected.empty()) {
        fprintf(stderr, "The release has no checksum for %s; not installing.\n", kSetupName);
        return 1;
    }
    printf("Downloading %s...\n", kSetupName);
    if (!Get(base + kSetupName, &setup, &status)) {
        fprintf(stderr, "Download failed (HTTP %lu).\n", (unsigned long)status);
        return 1;
    }
    if (Sha256Hex(setup) != expected) {
        fprintf(stderr, "The download does not match the published checksum; not installing.\n");
        return 1;
    }

    wchar_t tmp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tmp)) return 1;
    const std::wstring path = std::wstring(tmp) + L"cronq-setup-" + Widen(tag) + L".exe";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    bool saved = file != INVALID_HANDLE_VALUE &&
                 WriteFile(file, setup.data(), static_cast<DWORD>(setup.size()), &written, nullptr) &&
                 written == setup.size();
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!saved) {
        fprintf(stderr, "Could not save the installer.\n");
        DeleteFileW(path.c_str());
        return 1;
    }
    printf("Checksum verified. Starting the installer (it closes the running Cronq itself).\n");
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        fprintf(stderr, "Could not start the installer: %s\n", Narrow(path).c_str());
        return 1;
    }
    return 0;
}

}  // namespace update
}  // namespace cronq
