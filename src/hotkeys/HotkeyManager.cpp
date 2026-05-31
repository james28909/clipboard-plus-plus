#include "HotkeyManager.h"
#include "../app/Application.h"
#include "../ui/PopupWindow.h"

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
    if (m_hook) { UnhookWindowsHookEx(m_hook); m_hook = nullptr; }
    if (s_instance == this) s_instance = nullptr;
}

void HotkeyManager::SetBindings(std::vector<KeyBinding> bindings) {
    m_bindings = std::move(bindings);
}

std::vector<KeyBinding> HotkeyManager::DefaultBindings() {
    return {
        {true, true, false, 'V',          HotkeyAction::TogglePopup,  0}, // Ctrl+Shift+V
        {true, true, false, 'I',          HotkeyAction::Incognito,    0}, // Ctrl+Shift+I
        {true, true, false, VK_OEM_COMMA, HotkeyAction::OpenSettings, 0}, // Ctrl+Shift+,
    };
}

// ── Hook proc ─────────────────────────────────────────────────────────────────

LRESULT CALLBACK HotkeyManager::LLProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Events we injected via SendInput carry kClipboardPasteMagic.
        // Pass them straight through — never process our own paste inputs.
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

// ── Key dispatch ──────────────────────────────────────────────────────────────

bool HotkeyManager::HandleKeyDown(UINT vk, bool ctrl, bool shift, bool alt) {
    PopupWindow* popup    = Application::Get() ? Application::Get()->GetPopup() : nullptr;
    const bool   popupOpen = popup && popup->IsVisible();

    // ── 1. Configured bindings — always checked, popup open or closed ─────────
    for (const auto& b : m_bindings) {
        if (b.Matches(ctrl, shift, alt, vk)) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(b.action),
                         static_cast<LPARAM>(b.data));
            return true;
        }
    }

    // ── 2. Popup open — all remaining keys go to the search bar ───────────────
    // Ctrl+Shift+slot is intentionally NOT checked here so that typing the
    // first letter while Ctrl/Shift are still physically held from opening
    // the popup (Ctrl+Shift+V) does not accidentally trigger a paste.
    if (popupOpen) {
        // Escape closes the popup
        if (vk == VK_ESCAPE) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::TogglePopup), 0);
            return true;
        }

        // Win+key combos are system shortcuts — let them pass through
        const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                      || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        if (win) return false;

        // Everything else (including keys with Ctrl/Shift still held from
        // the opening combo) is forwarded to the popup search bar
        ForwardKeyToPopup(vk, shift);
        return true;
    }

    // ── 3. Popup closed — Ctrl+Shift+slot direct paste ────────────────────────
    if (ctrl && shift) {
        int slot = -1;
        if (vk >= '1' && vk <= '9') slot = static_cast<int>(vk - '1');
        if (vk >= 'A' && vk <= 'Z') slot = 9 + static_cast<int>(vk - 'A');
        if (slot >= 0) {
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::PasteSlot),
                         static_cast<LPARAM>(slot));
            return true;
        }
    }

    return false;
}

// ── Key forwarding to popup search bar ───────────────────────────────────────

void HotkeyManager::ForwardKeyToPopup(UINT vk, bool shift) const {
    PopupWindow* popup = Application::Get()->GetPopup();
    if (!popup) return;
    HWND hw = popup->GetHwnd();

    // Navigation / editing keys forwarded as WM_KEYDOWN
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

    // Build a clean key state with ONLY the shift flag set.
    // Never call GetKeyboardState() — it returns the live system state which
    // still shows Ctrl/Shift from whatever combo the user just pressed, causing
    // ToUnicode to produce control characters instead of printable ones.
    BYTE keyState[256]{};
    keyState[VK_SHIFT] = shift ? 0x80 : 0x00;

    wchar_t chars[4]{};
    UINT    scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    int     n    = ToUnicode(vk, scan, keyState, chars, 4, 0);

    // n < 0 = dead key consumed internal state; flush it
    if (n < 0)
        n = ToUnicode(vk, scan, keyState, chars, 4, 0);

    for (int i = 0; i < n; ++i)
        PostMessageW(hw, WM_CHAR, static_cast<WPARAM>(chars[i]), 0);
}
