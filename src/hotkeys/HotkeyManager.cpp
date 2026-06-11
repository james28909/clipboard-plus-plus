#include "HotkeyManager.h"

#ifdef _WIN32
#include "../app/Application.h"
#include "../ui/PopupWindow.h"
#endif

#include <algorithm>
#include <sstream>
#include <utility>

HotkeyManager* HotkeyManager::s_instance = nullptr;

#ifdef _WIN32
bool HotkeyManager::Install(HWND msgTarget) {
    m_msgTarget = msgTarget;
    ApplySettings(DefaultSettings());
    s_instance  = this;
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LLProc, nullptr, 0);
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseLLProc, nullptr, 0);
    return m_hook != nullptr;
}

void HotkeyManager::Uninstall() {
    if (m_hook) { UnhookWindowsHookEx(m_hook); m_hook = nullptr; }
    if (m_mouseHook) { UnhookWindowsHookEx(m_mouseHook); m_mouseHook = nullptr; }
    if (s_instance == this) s_instance = nullptr;
}
#else
bool HotkeyManager::Install(HWND) {
    ApplySettings(DefaultSettings());
    return false;
}

void HotkeyManager::Uninstall() {
    if (s_instance == this) s_instance = nullptr;
}
#endif

void HotkeyManager::SetBindings(std::vector<KeyBinding> bindings) {
    m_bindings = std::move(bindings);
    m_settings.bindings = m_bindings;
}

void HotkeyManager::ApplySettings(const HotkeySettings& settings) {
    m_settings = settings;
    m_bindings = settings.bindings;
}

void HotkeyManager::BeginCapture() {
    m_captureActive = true;
    m_captureReady = false;
    m_capturedBinding = {};
#ifdef _WIN32
    m_ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    m_shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    m_altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
#else
    m_ctrlDown = false;
    m_shiftDown = false;
    m_altDown = false;
#endif
}

bool HotkeyManager::ConsumeCapturedBinding(KeyBinding& binding) {
    if (!m_captureReady) return false;
    binding = m_capturedBinding;
    m_captureReady = false;
    return true;
}

std::vector<KeyBinding> HotkeyManager::DefaultBindings() {
    return {
        {true, true, false, 'V',          HotkeyAction::TogglePopup,     0}, // Ctrl+Shift+V
        {true, true, false, 'S',          HotkeyAction::ShowPopupSearch, 0}, // Ctrl+Shift+S
        {true, true, false, 'I',          HotkeyAction::Incognito,       0}, // Ctrl+Shift+I
        {true, true, false, VK_OEM_COMMA, HotkeyAction::OpenSettings,    0}, // Ctrl+Shift+,
        {true, true, false, 'G',          HotkeyAction::LaunchClipboardWebSearch, 0}, // Ctrl+Shift+G
        {false, true, true, 'D',          HotkeyAction::ToggleDebugWindow, 0}, // Alt+Shift+D
    };
}

HotkeySettings HotkeyManager::DefaultSettings() {
    HotkeySettings settings;
    settings.bindings = DefaultBindings();
    settings.hiddenPasteCtrl = true;
    settings.hiddenPasteShift = false;
    settings.hiddenPasteAlt = true;
    settings.hiddenPasteFunctionKeys = true;
    return settings;
}

int HotkeyManager::SlotFromVKey(UINT vk, bool includeFunctionKeys) {
    if (vk >= '1' && vk <= '9') return static_cast<int>(vk - '1');
    if (vk >= 'A' && vk <= 'Z') return 9 + static_cast<int>(vk - 'A');
    if (includeFunctionKeys && vk >= VK_F1 && vk <= VK_F12)
        return 35 + static_cast<int>(vk - VK_F1);
    return -1;
}

char HotkeyManager::SlotLabel(int slot) {
    if (slot >= 0 && slot < 9) return static_cast<char>('1' + slot);
    if (slot >= 9 && slot < 35) return static_cast<char>('a' + (slot - 9));
    return '?';
}

std::string HotkeyManager::SlotLabelText(int slot) {
    if (slot >= 0 && slot < 9) return std::string(1, static_cast<char>('1' + slot));
    if (slot >= 9 && slot < 35) return std::string(1, static_cast<char>('A' + (slot - 9)));
    if (slot >= 35 && slot < 47) return "F" + std::to_string(slot - 34);
    return {};  // slots beyond F12 have no keyboard label — still shown in list
}

const char* HotkeyManager::ActionName(HotkeyAction action) {
    switch (action) {
    case HotkeyAction::TogglePopup:     return "Toggle popup";
    case HotkeyAction::ShowPopupSearch: return "Focus popup search";
    case HotkeyAction::Incognito:       return "Toggle incognito mode";
    case HotkeyAction::OpenSettings:    return "Open settings";
    case HotkeyAction::PastePinnedSlot: return "Paste pinned slot";
    case HotkeyAction::SelectClipboardProfileSlot: return "Select clipboard";
    case HotkeyAction::LaunchWebSearch: return "Search web";
    case HotkeyAction::LaunchClipboardWebSearch: return "Search clipboard web";
    case HotkeyAction::ToggleDebugWindow: return "Toggle debug output";
    default:                            return "Unassigned";
    }
}

static std::string VKeyText(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    switch (vk) {
    case VK_OEM_COMMA:  return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_MINUS:  return "-";
    case VK_OEM_PLUS:   return "=";
    case VK_OEM_1:      return ";";
    case VK_OEM_2:      return "/";
    case VK_OEM_3:      return "`";
    case VK_OEM_4:      return "[";
    case VK_OEM_5:      return "\\";
    case VK_OEM_6:      return "]";
    case VK_OEM_7:      return "'";
    case VK_SPACE:      return "Space";
    case VK_TAB:        return "Tab";
    case VK_ESCAPE:     return "Esc";
    case VK_RETURN:     return "Enter";
    case VK_BACK:       return "Backspace";
    case VK_DELETE:     return "Delete";
    case VK_INSERT:     return "Insert";
    case VK_HOME:       return "Home";
    case VK_END:        return "End";
    case VK_PRIOR:      return "PageUp";
    case VK_NEXT:       return "PageDown";
    default:            return "Key " + std::to_string(vk);
    }
}

std::string HotkeyManager::ModifiersText(bool ctrl, bool shift, bool alt) {
    std::string text;
    if (ctrl) text += "Ctrl+";
    if (shift) text += "Shift+";
    if (alt) text += "Alt+";
    if (!text.empty() && text.back() == '+') text.pop_back();
    return text.empty() ? "None" : text;
}

std::string HotkeyManager::BindingText(const KeyBinding& binding) {
    if (binding.vkey == 0) return "Unassigned";
    std::string text;
    if (binding.ctrl) text += "Ctrl+";
    if (binding.shift) text += "Shift+";
    if (binding.alt) text += "Alt+";
    text += VKeyText(binding.vkey);
    return text;
}

#ifdef _WIN32
LRESULT CALLBACK HotkeyManager::LLProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (kb->dwExtraInfo == kClipboardPasteMagic)
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        if (isDown || isUp)
            s_instance->UpdateModifierState(kb->vkCode, isDown);
        if (isUp)
            s_instance->ReleaseActionPress(kb->vkCode);

        if (isDown) {
            bool ctrl  = s_instance->m_ctrlDown;
            bool shift = s_instance->m_shiftDown;
            bool alt   = s_instance->m_altDown;
            if (s_instance->HandleKeyDown(kb->vkCode, ctrl, shift, alt))
                return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void HotkeyManager::UpdateModifierState(UINT vk, bool isDown) {
    switch (vk) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        m_ctrlDown = isDown;
        break;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        m_shiftDown = isDown;
        break;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        m_altDown = isDown;
        break;
    default:
        break;
    }
}

bool HotkeyManager::ConsumeActionPress(UINT vk) {
    if (vk >= 256)
        return true;
    if (m_actionKeyDown[vk])
        return false;
    m_actionKeyDown[vk] = true;
    return true;
}

void HotkeyManager::ReleaseActionPress(UINT vk) {
    if (vk < 256)
        m_actionKeyDown[vk] = false;
}

LRESULT CALLBACK HotkeyManager::MouseLLProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
         wParam == WM_MBUTTONDOWN || wParam == WM_XBUTTONDOWN)) {
        PopupWindow* popup = Application::Get() ? Application::Get()->GetPopup() : nullptr;
        if (popup && popup->IsVisible()) {
            const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            popup->NoteExternalMouseDown(mouse->pt);
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool HotkeyManager::HandleKeyDown(UINT vk, bool ctrl, bool shift, bool alt) {
    PopupWindow* popup     = Application::Get() ? Application::Get()->GetPopup() : nullptr;
    const bool   popupOpen = popup && popup->IsVisible();

    if (m_captureActive) {
        if (vk == VK_ESCAPE) {
            m_captureActive = false;
            return true;
        }
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) {
            return true;
        }

        m_capturedBinding = {ctrl, shift, alt, vk, HotkeyAction::None, 0};
        m_captureReady = true;
        m_captureActive = false;
        return true;
    }

    for (const auto& b : m_bindings) {
        if (b.Matches(ctrl, shift, alt, vk)) {
            if (!ConsumeActionPress(vk))
                return true;
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(b.action),
                         static_cast<LPARAM>(b.data));
            return true;
        }
    }

    const int pinnedSlot = SlotFromVKey(vk, true);
    if (ctrl && shift && !alt && pinnedSlot >= 0) {
        if (!ConsumeActionPress(vk))
            return true;
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(HotkeyAction::PastePinnedSlot),
                     static_cast<LPARAM>(pinnedSlot));
        return true;
    }

    const int clipboardSlot = SlotFromVKey(vk, true);
    if (!ctrl && shift && alt && clipboardSlot >= 0) {
        if (!ConsumeActionPress(vk))
            return true;
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(HotkeyAction::SelectClipboardProfileSlot),
                     static_cast<LPARAM>(clipboardSlot));
        return true;
    }

    if (popupOpen) {
        if (vk == VK_ESCAPE) {
            if (!ConsumeActionPress(vk))
                return true;
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::TogglePopup), 0);
            return true;
        }

        const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                      || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        if (win) return false;

        if (!popup->IsKeyboardCaptureActive())
            return false;

        if (!ctrl && shift && !alt && vk == VK_RETURN && popup->IsSearchActive()) {
            if (!ConsumeActionPress(vk))
                return true;
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::LaunchWebSearch), 0);
            return true;
        }

        const int visibleSlot = SlotFromVKey(vk, false);
        if (!ctrl && !shift && !alt && visibleSlot >= 0 && !popup->IsTextEntryActive()) {
            if (!ConsumeActionPress(vk))
                return true;
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::PasteVisibleSlot),
                         static_cast<LPARAM>(visibleSlot));
            return true;
        }

        ForwardKeyToPopup(vk, shift);
        return true;
    }

    const bool hiddenPasteEnabled = m_settings.hiddenPasteCtrl ||
                                    m_settings.hiddenPasteShift ||
                                    m_settings.hiddenPasteAlt;
    const int historySlot = hiddenPasteEnabled
        ? SlotFromVKey(vk, m_settings.hiddenPasteFunctionKeys)
        : -1;
    if (ctrl == m_settings.hiddenPasteCtrl &&
        shift == m_settings.hiddenPasteShift &&
        alt == m_settings.hiddenPasteAlt &&
        historySlot >= 0) {
        if (!ConsumeActionPress(vk))
            return true;
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(HotkeyAction::PasteHistorySlot),
                     static_cast<LPARAM>(historySlot));
        return true;
    }

    return false;
}

void HotkeyManager::ForwardKeyToPopup(UINT vk, bool shift) const {
    PopupWindow* popup = Application::Get()->GetPopup();
    if (!popup) return;
    HWND hw = popup->GetHwnd();

    switch (vk) {
    case VK_BACK:
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_UP:
    case VK_DOWN:
        PostMessageW(hw, WM_KEYDOWN, vk, 0);
        PostMessageW(hw, WM_KEYUP, vk, 0);
        return;
    default: break;
    }

    BYTE keyState[256]{};
    keyState[VK_SHIFT] = shift ? 0x80 : 0x00;

    wchar_t chars[4]{};
    UINT    scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    int     n    = ToUnicode(vk, scan, keyState, chars, 4, 0);

    if (n < 0)
        n = ToUnicode(vk, scan, keyState, chars, 4, 0);

    for (int i = 0; i < n; ++i)
        PostMessageW(hw, WM_CHAR, static_cast<WPARAM>(chars[i]), 0);
}
#else
LRESULT CALLBACK HotkeyManager::LLProc(int, WPARAM, LPARAM) {
    return 0;
}

LRESULT CALLBACK HotkeyManager::MouseLLProc(int, WPARAM, LPARAM) {
    return 0;
}

void HotkeyManager::UpdateModifierState(UINT, bool) {}

bool HotkeyManager::ConsumeActionPress(UINT) {
    return true;
}

void HotkeyManager::ReleaseActionPress(UINT) {}

bool HotkeyManager::HandleKeyDown(UINT, bool, bool, bool) {
    return false;
}

void HotkeyManager::ForwardKeyToPopup(UINT, bool) const {}
#endif
