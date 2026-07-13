#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdint>
using UINT = unsigned int;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using ULONG_PTR = std::uintptr_t;
using HWND = void*;
using HHOOK = void*;
#ifndef CALLBACK
#define CALLBACK
#endif
static constexpr UINT VK_OEM_COMMA = 0xBC;
static constexpr UINT VK_OEM_PERIOD = 0xBE;
static constexpr UINT VK_OEM_MINUS = 0xBD;
static constexpr UINT VK_OEM_PLUS = 0xBB;
static constexpr UINT VK_OEM_1 = 0xBA;
static constexpr UINT VK_OEM_2 = 0xBF;
static constexpr UINT VK_OEM_3 = 0xC0;
static constexpr UINT VK_OEM_4 = 0xDB;
static constexpr UINT VK_OEM_5 = 0xDC;
static constexpr UINT VK_OEM_6 = 0xDD;
static constexpr UINT VK_OEM_7 = 0xDE;
static constexpr UINT VK_F1 = 0x70;
static constexpr UINT VK_F12 = 0x7B;
static constexpr UINT VK_F24 = 0x87;
static constexpr UINT VK_SPACE = 0x20;
static constexpr UINT VK_TAB = 0x09;
static constexpr UINT VK_ESCAPE = 0x1B;
static constexpr UINT VK_RETURN = 0x0D;
static constexpr UINT VK_BACK = 0x08;
static constexpr UINT VK_DELETE = 0x2E;
static constexpr UINT VK_INSERT = 0x2D;
static constexpr UINT VK_HOME = 0x24;
static constexpr UINT VK_END = 0x23;
static constexpr UINT VK_PRIOR = 0x21;
static constexpr UINT VK_NEXT = 0x22;
#endif
#include <string>
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
    PastePinnedSlot  = 7,
    SelectClipboardProfileSlot = 8,
    LaunchWebSearch = 9,
    LaunchClipboardWebSearch = 10,
    ToggleDebugWindow = 11,
    ToggleEditorWindow = 12,
    SendSelectionToAndroid = 13,
    PasteSelectedItems = 14,
    ClearSelectedItems = 15,
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

struct HotkeySettings {
    std::vector<KeyBinding> bindings;
    std::vector<std::string> passthroughHotkeys;
    bool hiddenPasteCtrl{true};
    bool hiddenPasteShift{false};
    bool hiddenPasteAlt{true};
    bool hiddenPasteFunctionKeys{true};
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
    void ApplySettings(const HotkeySettings& settings);
    const HotkeySettings& GetSettings() const { return m_settings; }

    void BeginCapture();
    void CancelCapture();
    bool IsCapturing() const { return m_captureActive; }
    bool ConsumeCapturedBinding(KeyBinding& binding);
    std::string CapturePreviewText() const;
    bool IsCtrlDown() const { return m_ctrlDown; }
    bool IsShiftDown() const { return m_shiftDown; }
    bool IsAltDown() const { return m_altDown; }

    // Default bindings returned as a starting point for settings UI.
    static std::vector<KeyBinding> DefaultBindings();
    static HotkeySettings DefaultSettings();
    static int SlotFromVKey(UINT vk, bool includeFunctionKeys);
    static char SlotLabel(int slot);
    static std::string SlotLabelText(int slot);
    static const char* ActionName(HotkeyAction action);
    static std::string BindingText(const KeyBinding& binding);
    static std::string ModifiersText(bool ctrl, bool shift, bool alt);

private:
    static LRESULT CALLBACK LLProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseLLProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Called on WM_KEYDOWN / WM_SYSKEYDOWN.
    // Returns true if the key should be consumed.
    bool HandleKeyDown(UINT vk, bool ctrl, bool shift, bool alt);

    // Forward a raw key event to the popup's HWND so ImGui can process it.
    void ForwardKeyToPopup(UINT vk, bool shift) const;
    void UpdateModifierState(UINT vk, bool isDown);
    bool ConsumeActionPress(UINT vk);
    void ReleaseActionPress(UINT vk);

    HHOOK                   m_hook{};
    HHOOK                   m_mouseHook{};
    HWND                    m_msgTarget{};
    std::vector<KeyBinding> m_bindings;
    HotkeySettings          m_settings;
    bool                    m_captureActive{false};
    bool                    m_captureReady{false};
    KeyBinding              m_capturedBinding{};
    bool                    m_ctrlDown{false};
    bool                    m_shiftDown{false};
    bool                    m_altDown{false};
    bool                    m_actionKeyDown[256]{};

    static HotkeyManager*   s_instance;
};
