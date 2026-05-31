#pragma once
#include <windows.h>
#include <vector>

// Tag every INPUT event we inject ourselves with this value in dwExtraInfo.
// The LL hook checks for it and passes our own events straight through.
static constexpr ULONG_PTR kClipboardPasteMagic = 0xCB00CB00UL;

// Action IDs sent via WM_HOTKEYACTION (WM_APP+4) to the main HWND.
// Slot actions use lParam = 0-based slot index.
enum class HotkeyAction : WPARAM {
    None             = 0,
    TogglePopup      = 1,
    ShowPopupSearch  = 2,
    Incognito        = 3,
    OpenSettings     = 4,
    PasteHistorySlot = 5,
    PasteVisibleSlot = 6,
};

struct KeyBinding {
    bool         ctrl{false};
    bool         shift{false};
    bool         alt{false};
    UINT         vkey{0};
    HotkeyAction action{HotkeyAction::None};
    int          data{0};

    bool Matches(bool c, bool s, bool a, UINT vk) const {
        return ctrl == c && shift == s && alt == a && vkey == vk;
    }
};

class HotkeyManager {
public:
    // Install the global keyboard hook.
    // msgTarget receives WM_HOTKEYACTION messages for hotkey-triggered actions.
    bool Install(HWND msgTarget);
    void Uninstall();

    bool IsInstalled() const { return m_hook != nullptr; }

    // Replace the active binding set (called from settings when user changes a combo)
    void SetBindings(std::vector<KeyBinding> bindings);

    // Default bindings returned as a starting point for settings UI.
    static std::vector<KeyBinding> DefaultBindings();
    static int SlotFromVKey(UINT vk, bool includeFunctionKeys);
    static char SlotLabel(int slot);

private:
    static LRESULT CALLBACK LLProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Called on WM_KEYDOWN / WM_SYSKEYDOWN.
    // Returns true if the key should be consumed.
    bool HandleKeyDown(UINT vk, bool ctrl, bool shift, bool alt);

    // Forward a raw key event to the popup's HWND so ImGui can process it.
    void ForwardKeyToPopup(UINT vk, bool shift) const;

    HHOOK                   m_hook{};
    HWND                    m_msgTarget{};
    std::vector<KeyBinding> m_bindings;

    static HotkeyManager*   s_instance;
};
