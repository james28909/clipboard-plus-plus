#include "HotkeyManager.h"
#include "../app/Application.h"
#include "../ui/PopupWindow.h"

#include <utility>

HotkeyManager* HotkeyManager::s_instance = nullptr;

bool HotkeyManager::Install(HWND msgTarget) {
    m_msgTarget = msgTarget;
    m_bindings  = DefaultBindings();
    s_instance  = this;
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LLProc, nullptr, 0);
    return m_hook != nullptr;
}

void HotkeyManager::Uninstall() {
    if (m_hook) { UnhookWindowsHookEx(m_hook); m_hook = nullptr; }
    if (s_instance == this) s_instance = nullptr;
}

void HotkeyManager::SetBindings(std::vector<KeyBinding> bindings) {
    m_bindings = std::move(bindings);
}

std::vector<KeyBinding> HotkeyManager::DefaultBindings() {
    return {
        {true, true, false, 'V',          HotkeyAction::TogglePopup,     0}, // Ctrl+Shift+V
        {true, true, false, 'S',          HotkeyAction::ShowPopupSearch, 0}, // Ctrl+Shift+S
        {true, true, false, 'I',          HotkeyAction::Incognito,       0}, // Ctrl+Shift+I
        {true, true, false, VK_OEM_COMMA, HotkeyAction::OpenSettings,    0}, // Ctrl+Shift+,
    };
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

LRESULT CALLBACK HotkeyManager::LLProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (kb->dwExtraInfo == kClipboardPasteMagic)
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if (isDown) {
            bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
            bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
            if (s_instance->HandleKeyDown(kb->vkCode, ctrl, shift, alt))
                return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool HotkeyManager::HandleKeyDown(UINT vk, bool ctrl, bool shift, bool alt) {
    PopupWindow* popup     = Application::Get() ? Application::Get()->GetPopup() : nullptr;
    const bool   popupOpen = popup && popup->IsVisible();

    for (const auto& b : m_bindings) {
        if (b.Matches(ctrl, shift, alt, vk)) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(b.action),
                         static_cast<LPARAM>(b.data));
            return true;
        }
    }

    if (popupOpen) {
        if (vk == VK_ESCAPE) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::TogglePopup), 0);
            return true;
        }

        const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                      || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        if (win) return false;

        const int visibleSlot = SlotFromVKey(vk, false);
        if (!ctrl && !shift && !alt && visibleSlot >= 0 && !popup->IsSearchActive()) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::PasteVisibleSlot),
                         static_cast<LPARAM>(visibleSlot));
            return true;
        }

        ForwardKeyToPopup(vk, shift);
        return true;
    }

    const int historySlot = SlotFromVKey(vk, true);
    if (ctrl && alt && !shift && historySlot >= 0) {
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
