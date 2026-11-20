#pragma once
#include "common.h"
namespace cronq {
namespace chat_view {
void Recover(unsigned index, unsigned generation);
void RefreshAppearance();
bool Init(HWND parent);
void Cleanup();
bool IsAvailable();
bool IsUiVisible();
const std::string& FailureReason();
void SetActiveApp(const std::string&);
void GoBack();
void Reload();
bool CanGoBack();
void Pause();
void Resume();
void UniversalReset();
void ResizeContent(const RECT&);
void ResizeHost();
bool HasPopup();
void ClosePopup();
bool PostUiMessage(const std::string&);

void PrepareTypingTarget();
void CheckTypingHover();
void DispatchKeyEvent(WPARAM vk, LPARAM lParam);
void DispatchClipboardAction(WPARAM vk, LPARAM lParam);
void PasteImageOrText();
}  // namespace chat_view
}  // namespace cronq
