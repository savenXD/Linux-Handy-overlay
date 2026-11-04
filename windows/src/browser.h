#pragma once
#include "common.h"
namespace cronq {
namespace browser {
void Recover(unsigned index, unsigned generation);
bool Init(HWND parent);
void Cleanup();
bool IsAvailable();
bool IsUiVisible();
const std::string& FailureReason();
void SetActiveApp(const std::string&);
void Load(const std::string&);
void LoadInBrowser(const std::string&);
void LoadInChatGpt(const std::string&);
void GoBack();
void GoForward();
void Reload();
void Stop();
void GoHome();
bool CanGoBack();
bool IsLoading();
double Progress();
std::string CurrentUri();
std::string CurrentTitle();
void Pause();
void Resume();
void UniversalReset();
void ResizeContent(const RECT&);
void ResizeHost();
bool HasPopup();
void ClosePopup();
bool PostUiMessage(const std::string&);

void DispatchKeyEvent(WPARAM vk, LPARAM lParam);
void DispatchClipboardAction(WPARAM vk, LPARAM lParam);
void PasteImageOrText();
void SetUrlInputFocused(bool focused);
void NotifyPointerClick();
}  // namespace browser
}  // namespace cronq
