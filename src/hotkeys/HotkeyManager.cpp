#include "HotkeyManager.h"
#include "../app/Application.h"
#include "../ui/PopupWindow.h"

// WM_HOTKEYACTION is defined in Application.h
constexpr UINT WM_HOTKEYACTION = WM_APP + 4;

HotkeyManager* HotkeyManager::s_instance = nullptr;

// ── Public ────────────────────────────────────────────────────────────────────

bool HotkeyManager::Install(HWND msgTarget) {
    m_msgTarget = msgTarget;
    m_bindings  = DefaultBindings();
    s_instance  = this;

    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LLProc, nullptr, 0);
    return m_hook != nullptr;
}

void HotkeyManager::Uninstall() {
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
    if (s_instance == this)
        s_instance = nullptr;
}

void HotkeyManager::SetBindings(std::vector<KeyBinding> bindings) {
    m_bindings = std::move(bindings);
}

std::vector<KeyBinding> HotkeyManager::DefaultBindings() {
    return {
        // Ctrl+Shift+V → toggle popup
        {true,  true,  false, 'V',          HotkeyAction::TogglePopup,  0},
        // Ctrl+Shift+I → toggle incognito
        {true,  true,  false, 'I',          HotkeyAction::Incognito,    0},
        // Ctrl+Shift+, → open settings
        {true,  true,  false, VK_OEM_COMMA, HotkeyAction::OpenSettings, 0},
    };
}

// ── Hook proc ─────────────────────────────────────────────────────────────────

LRESULT CALLBACK HotkeyManager::LLProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if (isDown) {
            auto* kb  = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
            bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

            if (s_instance->HandleKeyDown(kb->vkCode, ctrl, shift, alt))
                return 1; // consumed — don't pass to other hooks / the app
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── Key dispatch ──────────────────────────────────────────────────────────────

bool HotkeyManager::HandleKeyDown(UINT vk, bool ctrl, bool shift, bool alt) {
    // ── 1. Check configured bindings first ────────────────────────────────────
    for (const auto& b : m_bindings) {
        if (b.Matches(ctrl, shift, alt, vk)) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(b.action),
                         static_cast<LPARAM>(b.data));
            return true;
        }
    }

    // ── 2. Ctrl+Alt + slot key → direct paste (no popup needed) ──────────────
    if (ctrl && alt && !shift) {
        int slot = -1;
        if (vk >= '1' && vk <= '9') slot = static_cast<int>(vk - '1');        // 0-8
        if (vk >= 'A' && vk <= 'Z') slot = 9 + static_cast<int>(vk - 'A');   // 9-34

        if (slot >= 0) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::PasteSlot),
                         static_cast<LPARAM>(slot));
            return true;
        }
    }

    // ── 3. Popup is open — forward keys to the search bar ─────────────────────
    PopupWindow* popup = Application::Get()
                       ? Application::Get()->GetPopup()
                       : nullptr;
    if (!popup || !popup->IsVisible()) return false;

    // Escape — close popup
    if (vk == VK_ESCAPE && !ctrl && !shift && !alt) {
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(HotkeyAction::TogglePopup), 0);
        return true;
    }

    // Don't forward modified keys (Ctrl+C, Alt+Tab, etc.) to the search bar
    if (ctrl || alt) return false;

    ForwardKeyToPopup(vk, shift);
    return true; // consume so it doesn't land in the background app
}

void HotkeyManager::ForwardKeyToPopup(UINT vk, bool shift) const {
    PopupWindow* popup = Application::Get()->GetPopup();
    if (!popup) return;
    HWND hw = popup->GetHwnd();

    // Navigation / editing keys — send as WM_KEYDOWN
    switch (vk) {
    case VK_BACK:
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
        PostMessageW(hw, WM_KEYDOWN, vk, 0);
        return;
    default: break;
    }

    // Translate virtual key → character using current keyboard layout
    BYTE keyState[256]{};
    GetKeyboardState(keyState);
    // Reflect shift state into keyState so ToUnicode produces the right case/symbol
    keyState[VK_SHIFT]   = shift ? 0x80 : 0x00;
    keyState[VK_CAPITAL] = 0x00; // ignore CapsLock for predictability

    wchar_t chars[4]{};
    int n = ToUnicode(vk, MapVirtualKeyW(vk, MAPVK_VK_TO_VSC),
                       keyState, chars, 4, 0);
    for (int i = 0; i < n; ++i)
        PostMessageW(hw, WM_CHAR, static_cast<WPARAM>(chars[i]), 0);
}
