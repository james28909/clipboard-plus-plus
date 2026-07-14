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
#include <cstdint>
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
    PasteNamedSlot = 16,
    RunCustomAction = 17,
};

enum class ModifierSide : uint8_t {
    Any = 0,
    Left = 1,
    Right = 2,
};

struct ModifierState {
    static constexpr uint8_t LeftCtrlBit = 1u << 0;
    static constexpr uint8_t RightCtrlBit = 1u << 1;
    static constexpr uint8_t LeftShiftBit = 1u << 2;
    static constexpr uint8_t RightShiftBit = 1u << 3;
    static constexpr uint8_t LeftAltBit = 1u << 4;
    static constexpr uint8_t RightAltBit = 1u << 5;
    bool leftCtrl{};
    bool rightCtrl{};
    bool leftShift{};
    bool rightShift{};
    bool leftAlt{};
    bool rightAlt{};

    bool Ctrl() const { return leftCtrl || rightCtrl; }
    bool Shift() const { return leftShift || rightShift; }
    bool Alt() const { return leftAlt || rightAlt; }
    uint8_t Mask() const {
        return static_cast<uint8_t>(
            (leftCtrl ? LeftCtrlBit : 0) |
            (rightCtrl ? RightCtrlBit : 0) |
            (leftShift ? LeftShiftBit : 0) |
            (rightShift ? RightShiftBit : 0) |
            (leftAlt ? LeftAltBit : 0) |
            (rightAlt ? RightAltBit : 0));
    }
    static ModifierState FromMask(uint8_t mask) {
        return {
            (mask & LeftCtrlBit) != 0, (mask & RightCtrlBit) != 0,
            (mask & LeftShiftBit) != 0, (mask & RightShiftBit) != 0,
            (mask & LeftAltBit) != 0, (mask & RightAltBit) != 0};
    }
};

struct KeyBinding {
    bool         ctrl{false};
    bool         shift{false};
    bool         alt{false};
    UINT         vkey{0};
    HotkeyAction action{HotkeyAction::None};
    int          data{0};
    ModifierSide ctrlSide{ModifierSide::Any};
    ModifierSide shiftSide{ModifierSide::Any};
    ModifierSide altSide{ModifierSide::Any};
    uint8_t physicalModifiers{0};
    bool exactModifiers{false};

    static bool SideMatches(ModifierSide side, bool left, bool right) {
        return side == ModifierSide::Any ||
               (side == ModifierSide::Left && left) ||
               (side == ModifierSide::Right && right);
    }
    static bool SidesOverlap(ModifierSide a, ModifierSide b) {
        return a == ModifierSide::Any || b == ModifierSide::Any || a == b;
    }
    bool ModifiersMatch(const ModifierState& state) const {
        if (exactModifiers)
            return physicalModifiers == state.Mask();
        return ctrl == state.Ctrl() && shift == state.Shift() &&
               alt == state.Alt() &&
               (!ctrl || SideMatches(ctrlSide, state.leftCtrl, state.rightCtrl)) &&
               (!shift || SideMatches(shiftSide, state.leftShift, state.rightShift)) &&
               (!alt || SideMatches(altSide, state.leftAlt, state.rightAlt));
    }
    bool Matches(const ModifierState& state, UINT vk) const {
        return vkey == vk && ModifiersMatch(state);
    }
    bool Overlaps(const KeyBinding& other) const {
        if (vkey != other.vkey)
            return false;
        if (exactModifiers && other.exactModifiers)
            return physicalModifiers == other.physicalModifiers;
        if (exactModifiers)
            return other.Matches(ModifierState::FromMask(physicalModifiers), vkey);
        if (other.exactModifiers)
            return Matches(ModifierState::FromMask(other.physicalModifiers), vkey);
        return ctrl == other.ctrl && shift == other.shift && alt == other.alt &&
               (!ctrl || SidesOverlap(ctrlSide, other.ctrlSide)) &&
               (!shift || SidesOverlap(shiftSide, other.shiftSide)) &&
               (!alt || SidesOverlap(altSide, other.altSide));
    }
};

struct SlotBankSettings {
    bool enabled{true};
    KeyBinding chord{};
    bool numberKeys{true};
    bool letterKeys{true};
    bool functionKeys{false};
};

struct HotkeySettings {
    std::vector<KeyBinding> bindings;
    std::vector<std::string> passthroughHotkeys;
    bool hiddenPasteCtrl{true};
    bool hiddenPasteShift{false};
    bool hiddenPasteAlt{true};
    bool hiddenPasteFunctionKeys{true};
    uint8_t hiddenPasteCtrlSides{3};
    uint8_t hiddenPasteShiftSides{0};
    uint8_t hiddenPasteAltSides{3};
    SlotBankSettings popupHistoryBank{};
    SlotBankSettings globalHistoryBank{};
    SlotBankSettings pinnedHistoryBank{};
    SlotBankSettings profileBank{};
    bool hotkeyDoubleTaps{false};
};

struct ConfiguredHotkeyRoute {
    HotkeyAction action{HotkeyAction::None};
    int data{-1};
    bool waitsForDoubleTap{false};
    HotkeyAction doubleTapAction{HotkeyAction::None};
    int doubleTapData{-1};
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
    void BeginModifierCapture();
    void CancelCapture();
    bool IsCapturing() const { return m_captureActive; }
    bool ConsumeCapturedBinding(KeyBinding& binding);
    std::string CapturePreviewText() const;
    bool IsCtrlDown() const { return m_ctrlDown; }
    bool IsShiftDown() const { return m_shiftDown; }
    bool IsAltDown() const { return m_altDown; }
    bool IsLeftAltDown() const { return m_leftAltDown; }

    // Default bindings returned as a starting point for settings UI.
    static std::vector<KeyBinding> DefaultBindings();
    static HotkeySettings DefaultSettings();
    static int SlotFromVKey(UINT vk, bool includeFunctionKeys);
    static char SlotLabel(int slot);
    static std::string SlotLabelText(int slot);
    static const char* ActionName(HotkeyAction action);
    static std::string BindingText(const KeyBinding& binding);
    static std::string ModifierChordText(const KeyBinding& binding);
    static int BankSlotFromVKey(const SlotBankSettings& bank, UINT vk) {
        if (bank.numberKeys && vk >= '1' && vk <= '9')
            return static_cast<int>(vk - '1');
        if (bank.letterKeys && vk >= 'A' && vk <= 'Z')
            return 9 + static_cast<int>(vk - 'A');
        if (bank.functionKeys && vk >= VK_F1 && vk <= VK_F12)
            return 35 + static_cast<int>(vk - VK_F1);
        return -1;
    }
    static HotkeyAction ResolveSlotBank(const HotkeySettings& settings,
                                        const ModifierState& modifiers,
                                        UINT vk, bool popupAvailable,
                                        int& slot) {
        struct Candidate {
            const SlotBankSettings* bank;
            HotkeyAction action;
            bool available;
        };
        const Candidate candidates[] = {
            {&settings.globalHistoryBank, HotkeyAction::PasteHistorySlot, true},
            {&settings.pinnedHistoryBank, HotkeyAction::PastePinnedSlot, true},
            {&settings.profileBank, HotkeyAction::SelectClipboardProfileSlot, true},
            {&settings.popupHistoryBank, HotkeyAction::PasteVisibleSlot, popupAvailable},
        };
        for (const Candidate& candidate : candidates) {
            if (!candidate.available || !candidate.bank->enabled ||
                !candidate.bank->chord.ModifiersMatch(modifiers))
                continue;
            const int resolved = BankSlotFromVKey(*candidate.bank, vk);
            if (resolved < 0) continue;
            slot = resolved;
            return candidate.action;
        }
        slot = -1;
        return HotkeyAction::None;
    }
    static ConfiguredHotkeyRoute ResolveConfiguredRoute(
        const HotkeySettings& settings,
        const std::vector<KeyBinding>& bindings,
        const ModifierState& modifiers, UINT vk, bool popupAvailable) {
        const KeyBinding* explicitBinding = nullptr;
        for (int specificity = 6; specificity >= 0 && !explicitBinding; --specificity) {
            for (const auto& binding : bindings) {
                int bindingSpecificity = 0;
                if (binding.exactModifiers) {
                    uint8_t mask = binding.physicalModifiers;
                    while (mask) {
                        bindingSpecificity += mask & 1u;
                        mask >>= 1;
                    }
                } else {
                    bindingSpecificity =
                        (binding.ctrl && binding.ctrlSide != ModifierSide::Any ? 1 : 0) +
                        (binding.shift && binding.shiftSide != ModifierSide::Any ? 1 : 0) +
                        (binding.alt && binding.altSide != ModifierSide::Any ? 1 : 0);
                }
                if (bindingSpecificity == specificity &&
                    binding.Matches(modifiers, vk)) {
                    explicitBinding = &binding;
                    break;
                }
            }
        }

        int bankSlot = -1;
        const HotkeyAction bankAction = ResolveSlotBank(
            settings, modifiers, vk, popupAvailable, bankSlot);
        if (explicitBinding) {
            if (settings.hotkeyDoubleTaps &&
                explicitBinding->action == HotkeyAction::PasteNamedSlot &&
                bankAction != HotkeyAction::None && modifiers.Mask() != 0) {
                return {bankAction, bankSlot, true,
                        explicitBinding->action, explicitBinding->data};
            }
            return {explicitBinding->action, explicitBinding->data};
        }
        if (bankAction != HotkeyAction::None)
            return {bankAction, bankSlot};
        return {};
    }
    static std::string ModifiersText(bool ctrl, bool shift, bool alt,
                                     ModifierSide ctrlSide = ModifierSide::Any,
                                     ModifierSide shiftSide = ModifierSide::Any,
                                     ModifierSide altSide = ModifierSide::Any);

private:
    static LRESULT CALLBACK LLProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseLLProc(int nCode, WPARAM wParam, LPARAM lParam);

    // Called on WM_KEYDOWN / WM_SYSKEYDOWN.
    // Returns true if the key should be consumed.
    bool HandleKeyDown(UINT vk, const ModifierState& modifiers);
    bool HandleKeyUp(UINT vk, const ModifierState& modifiers);
    void ClearPendingDoubleTap();
    void DispatchPendingSingleTap();

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
    bool                    m_modifierCaptureMode{false};
    uint8_t                 m_modifierCaptureMask{};
    KeyBinding              m_capturedBinding{};
    bool                    m_ctrlDown{false};
    bool                    m_shiftDown{false};
    bool                    m_altDown{false};
    bool                    m_leftCtrlDown{false};
    bool                    m_rightCtrlDown{false};
    bool                    m_leftShiftDown{false};
    bool                    m_rightShiftDown{false};
    bool                    m_leftAltDown{false};
    bool                    m_rightAltDown{false};
    bool                    m_actionKeyDown[256]{};
    bool                    m_pendingDoubleTap{false};
    bool                    m_pendingTriggerReleased{false};
    UINT                    m_pendingTrigger{};
    uint8_t                 m_pendingModifierMask{};
    HotkeyAction            m_pendingSingleAction{HotkeyAction::None};
    int                     m_pendingSingleData{};
    HotkeyAction            m_pendingDoubleAction{HotkeyAction::None};
    int                     m_pendingDoubleData{};
    bool                    m_completedDoubleTap{false};
    UINT                    m_completedDoubleTrigger{};
    uint8_t                 m_completedDoubleModifierMask{};

    static HotkeyManager*   s_instance;
};
