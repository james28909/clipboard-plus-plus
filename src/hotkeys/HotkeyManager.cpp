#include "HotkeyManager.h"

#ifdef _WIN32
#include "../app/Application.h"
#include "../clipboard/ScreenshotTracker.h"
#include "../ui/PasteDiagnostics.h"
#include "../ui/PopupWindow.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <windows.h>

// Appends to the same log file PopupWindow uses so all events are in one place.
static void HkLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[512]{};
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    PasteDiagnostics::Log("%s", msg);
}

HotkeyManager* HotkeyManager::s_instance = nullptr;

namespace {

std::string Trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool VKeyFromName(const std::string& token, UINT& vk) {
    const std::string key = Upper(Trim(token));
    if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
        vk = static_cast<UINT>(key[0]);
        return true;
    }
    if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
        vk = static_cast<UINT>(key[0]);
        return true;
    }
    if (key.size() >= 2 && key[0] == 'F') {
        int number = std::atoi(key.c_str() + 1);
        if (number >= 1 && number <= 24) {
            vk = VK_F1 + static_cast<UINT>(number - 1);
            return true;
        }
    }
    if (key == "TAB") vk = VK_TAB;
    else if (key == "ESC" || key == "ESCAPE") vk = VK_ESCAPE;
    else if (key == "ENTER" || key == "RETURN") vk = VK_RETURN;
    else if (key == "SPACE") vk = VK_SPACE;
    else if (key == "BACKSPACE") vk = VK_BACK;
    else if (key == "DELETE" || key == "DEL") vk = VK_DELETE;
    else if (key == "INSERT" || key == "INS") vk = VK_INSERT;
    else if (key == "HOME") vk = VK_HOME;
    else if (key == "END") vk = VK_END;
    else if (key == "PAGEUP") vk = VK_PRIOR;
    else if (key == "PAGEDOWN") vk = VK_NEXT;
    else if (key == ",") vk = VK_OEM_COMMA;
    else if (key == ".") vk = VK_OEM_PERIOD;
    else if (key == "-") vk = VK_OEM_MINUS;
    else if (key == "=" || key == "+") vk = VK_OEM_PLUS;
    else return false;
    return true;
}

bool IsModifierKey(UINT vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
}

bool ParseHotkeyText(const std::string& text, KeyBinding& out) {
    KeyBinding parsed{};
    uint8_t genericFamilies = 0;
    std::stringstream ss(text);
    std::string token;
    bool hasKey = false;
    while (std::getline(ss, token, '+')) {
        const std::string part = Upper(Trim(token));
        if (part.empty())
            continue;
        if (part == "LEFT CTRL" || part == "LCTRL") {
            parsed.ctrl = true;
            parsed.ctrlSide = ModifierSide::Left;
            parsed.physicalModifiers |= ModifierState::LeftCtrlBit;
        } else if (part == "RIGHT CTRL" || part == "RCTRL") {
            parsed.ctrl = true;
            parsed.ctrlSide = ModifierSide::Right;
            parsed.physicalModifiers |= ModifierState::RightCtrlBit;
        } else if (part == "CTRL" || part == "CONTROL") {
            parsed.ctrl = true;
            genericFamilies |= 1u;
        } else if (part == "LEFT SHIFT" || part == "LSHIFT") {
            parsed.shift = true;
            parsed.shiftSide = ModifierSide::Left;
            parsed.physicalModifiers |= ModifierState::LeftShiftBit;
        } else if (part == "RIGHT SHIFT" || part == "RSHIFT") {
            parsed.shift = true;
            parsed.shiftSide = ModifierSide::Right;
            parsed.physicalModifiers |= ModifierState::RightShiftBit;
        } else if (part == "SHIFT") {
            parsed.shift = true;
            genericFamilies |= 2u;
        } else if (part == "LEFT ALT" || part == "LALT") {
            parsed.alt = true;
            parsed.altSide = ModifierSide::Left;
            parsed.physicalModifiers |= ModifierState::LeftAltBit;
        } else if (part == "RIGHT ALT" || part == "RALT") {
            parsed.alt = true;
            parsed.altSide = ModifierSide::Right;
            parsed.physicalModifiers |= ModifierState::RightAltBit;
        } else if (part == "ALT" || part == "MENU") {
            parsed.alt = true;
            genericFamilies |= 4u;
        } else {
            UINT vk = 0;
            if (!VKeyFromName(part, vk))
                return false;
            parsed.vkey = vk;
            hasKey = true;
        }
    }
    if (!hasKey)
        return false;
    parsed.exactModifiers = parsed.physicalModifiers != 0 && genericFamilies == 0;
    out = parsed;
    return true;
}

bool MatchesPassthrough(const HotkeySettings& settings,
                        const ModifierState& modifiers, UINT vk) {
    for (const std::string& text : settings.passthroughHotkeys) {
        KeyBinding parsed{};
        if (ParseHotkeyText(text, parsed) && parsed.Matches(modifiers, vk))
            return true;
    }
    return false;
}

ModifierSide CapturedSide(bool left, bool right) {
    if (left && !right) return ModifierSide::Left;
    if (right && !left) return ModifierSide::Right;
    return ModifierSide::Any;
}

} // namespace

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
    if (!settings.hotkeyDoubleTaps) {
        ClearPendingDoubleTap();
        m_completedDoubleTap = false;
        m_completedDoubleTrigger = 0;
        m_completedDoubleModifierMask = 0;
    }
}

void HotkeyManager::BeginCapture() {
    m_captureActive = true;
    m_captureReady = false;
    m_modifierCaptureMode = false;
    m_modifierCaptureMask = 0;
    m_capturedBinding = {};
#ifdef _WIN32
    m_ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    m_shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    m_altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    m_leftCtrlDown = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    m_rightCtrlDown = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
    m_leftShiftDown = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    m_rightShiftDown = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    m_leftAltDown = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
    m_rightAltDown = (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
#else
    m_ctrlDown = false;
    m_shiftDown = false;
    m_altDown = false;
    m_leftCtrlDown = false;
    m_rightCtrlDown = false;
    m_leftShiftDown = false;
    m_rightShiftDown = false;
    m_leftAltDown = false;
    m_rightAltDown = false;
#endif
}

void HotkeyManager::BeginModifierCapture() {
    BeginCapture();
    m_modifierCaptureMode = true;
    m_modifierCaptureMask = ModifierState{
        m_leftCtrlDown, m_rightCtrlDown,
        m_leftShiftDown, m_rightShiftDown,
        m_leftAltDown, m_rightAltDown}.Mask();
}

void HotkeyManager::CancelCapture() {
    m_captureActive = false;
    m_captureReady = false;
    m_modifierCaptureMode = false;
    m_modifierCaptureMask = 0;
    m_capturedBinding = {};
}

bool HotkeyManager::ConsumeCapturedBinding(KeyBinding& binding) {
    if (!m_captureReady) return false;
    binding = m_capturedBinding;
    m_captureReady = false;
    m_captureActive = false;
    m_modifierCaptureMode = false;
    m_modifierCaptureMask = 0;
    return true;
}

std::string HotkeyManager::CapturePreviewText() const {
    if (m_captureReady)
        return m_capturedBinding.vkey == 0
            ? ModifierChordText(m_capturedBinding)
            : BindingText(m_capturedBinding);
    if (m_captureActive) {
        KeyBinding preview;
        preview.exactModifiers = true;
        preview.physicalModifiers = m_modifierCaptureMode
            ? m_modifierCaptureMask
            : ModifierState{
                m_leftCtrlDown, m_rightCtrlDown,
                m_leftShiftDown, m_rightShiftDown,
                m_leftAltDown, m_rightAltDown}.Mask();
        const std::string modifiers = ModifierChordText(preview);
        return modifiers == "None" ? "Press keys..." : modifiers + "+";
    }
    return "Press New to capture a hotkey";
}

std::vector<KeyBinding> HotkeyManager::DefaultBindings() {
    return {
        {true, true, false, 'V',          HotkeyAction::TogglePopup,     0}, // Ctrl+Shift+V
        {true, true, false, 'S',          HotkeyAction::ShowPopupSearch, 0}, // Ctrl+Shift+S
        {true, true, false, 'I',          HotkeyAction::Incognito,       0}, // Ctrl+Shift+I
        {true, true, false, VK_OEM_COMMA, HotkeyAction::OpenSettings,    0}, // Ctrl+Shift+,
        {true, true, false, 'E',          HotkeyAction::ToggleEditorWindow, 0}, // Ctrl+Shift+E
        {true, true, false, 'G',          HotkeyAction::LaunchClipboardWebSearch, 0}, // Ctrl+Shift+G
        {true, true, true,  'Z',          HotkeyAction::SendSelectionToAndroid, 0}, // Ctrl+Alt+Shift+Z
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
    settings.hiddenPasteCtrlSides = 3;
    settings.hiddenPasteShiftSides = 0;
    settings.hiddenPasteAltSides = 3;
    settings.popupHistoryBank.enabled = true;
    settings.popupHistoryBank.chord.exactModifiers = true;
    settings.popupHistoryBank.chord.physicalModifiers = 0;
    settings.popupHistoryBank.numberKeys = true;
    settings.popupHistoryBank.letterKeys = true;
    settings.popupHistoryBank.functionKeys = false;

    settings.globalHistoryBank.enabled = true;
    settings.globalHistoryBank.chord.ctrl = true;
    settings.globalHistoryBank.chord.alt = true;
    settings.globalHistoryBank.numberKeys = true;
    settings.globalHistoryBank.letterKeys = true;
    settings.globalHistoryBank.functionKeys = true;

    settings.pinnedHistoryBank.enabled = true;
    settings.pinnedHistoryBank.chord.ctrl = true;
    settings.pinnedHistoryBank.chord.shift = true;
    settings.pinnedHistoryBank.numberKeys = true;
    settings.pinnedHistoryBank.letterKeys = true;
    settings.pinnedHistoryBank.functionKeys = true;

    settings.profileBank.enabled = true;
    settings.profileBank.chord.shift = true;
    settings.profileBank.chord.alt = true;
    settings.profileBank.numberKeys = true;
    settings.profileBank.letterKeys = true;
    settings.profileBank.functionKeys = true;
    settings.passthroughHotkeys = {"Alt+Tab", "Alt+F4"};
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
    return {};  // slots beyond F12 have no keyboard label - still shown in list
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
    case HotkeyAction::ToggleEditorWindow: return "Open text/script editor";
    case HotkeyAction::SendSelectionToAndroid: return "Send selection to Android";
    case HotkeyAction::PasteSelectedItems: return "Paste selected items";
    case HotkeyAction::ClearSelectedItems: return "Clear selected items";
    case HotkeyAction::PasteNamedSlot: return "Paste named slot";
    case HotkeyAction::RunCustomAction: return "Run custom action";
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

std::string HotkeyManager::ModifiersText(bool ctrl, bool shift, bool alt,
                                         ModifierSide ctrlSide,
                                         ModifierSide shiftSide,
                                         ModifierSide altSide) {
    std::string text;
    auto append = [&](bool enabled, ModifierSide side, const char* name) {
        if (!enabled) return;
        if (side == ModifierSide::Left) text += "Left ";
        if (side == ModifierSide::Right) text += "Right ";
        text += name;
        text += "+";
    };
    append(ctrl, ctrlSide, "Ctrl");
    append(shift, shiftSide, "Shift");
    append(alt, altSide, "Alt");
    if (!text.empty() && text.back() == '+') text.pop_back();
    return text.empty() ? "None" : text;
}

std::string HotkeyManager::BindingText(const KeyBinding& binding) {
    if (binding.vkey == 0) return "Unassigned";
    std::string text = ModifierChordText(binding);
    if (text != "None") text += "+";
    else text.clear();
    text += VKeyText(binding.vkey);
    return text;
}

std::string HotkeyManager::ModifierChordText(const KeyBinding& binding) {
    if (binding.exactModifiers) {
        const ModifierState state = ModifierState::FromMask(binding.physicalModifiers);
        std::string text;
        auto append = [&](bool enabled, const char* label) {
            if (!enabled) return;
            if (!text.empty()) text += "+";
            text += label;
        };
        append(state.leftCtrl, "Left Ctrl");
        append(state.rightCtrl, "Right Ctrl");
        append(state.leftShift, "Left Shift");
        append(state.rightShift, "Right Shift");
        append(state.leftAlt, "Left Alt");
        append(state.rightAlt, "Right Alt");
        return text.empty() ? "None" : text;
    }
    const std::string modifiers = ModifiersText(
        binding.ctrl, binding.shift, binding.alt,
        binding.ctrlSide, binding.shiftSide, binding.altSide);
    return modifiers;
}

#ifdef _WIN32
LRESULT CALLBACK HotkeyManager::LLProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (kb->dwExtraInfo == kClipboardPasteMagic)
            return CallNextHookEx(nullptr, nCode, wParam, lParam);

        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        UINT eventVk = kb->vkCode;
        if (eventVk == VK_CONTROL)
            eventVk = (kb->flags & LLKHF_EXTENDED) ? VK_RCONTROL : VK_LCONTROL;
        else if (eventVk == VK_SHIFT)
            eventVk = kb->scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        else if (eventVk == VK_MENU)
            eventVk = (kb->flags & LLKHF_EXTENDED) ? VK_RMENU : VK_LMENU;
        if (isDown || isUp)
            s_instance->UpdateModifierState(eventVk, isDown);
        ModifierState modifiers{
            s_instance->m_leftCtrlDown, s_instance->m_rightCtrlDown,
            s_instance->m_leftShiftDown, s_instance->m_rightShiftDown,
            s_instance->m_leftAltDown, s_instance->m_rightAltDown};
        if (isUp) {
            s_instance->ReleaseActionPress(eventVk);
            if (s_instance->HandleKeyUp(eventVk, modifiers))
                return 1;
        }

        if (isDown) {
            const bool ctrl = modifiers.Ctrl();
            const bool shift = modifiers.Shift();
            const bool alt = modifiers.Alt();
            const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
            if (kb->vkCode == VK_SNAPSHOT) {
                ScreenshotTracker::Instance().NoteHotkey(
                    win ? "Win+Print Screen" : (alt ? "Alt+Print Screen" : "Print Screen"));
            } else if (win && shift && kb->vkCode == 'S') {
                ScreenshotTracker::Instance().NoteHotkey("Win+Shift+S");
            }
            if (s_instance->HandleKeyDown(kb->vkCode, modifiers))
                return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void HotkeyManager::UpdateModifierState(UINT vk, bool isDown) {
    switch (vk) {
    case VK_LCONTROL:
        m_leftCtrlDown = isDown;
        m_ctrlDown = m_leftCtrlDown || m_rightCtrlDown;
        break;
    case VK_RCONTROL:
        m_rightCtrlDown = isDown;
        m_ctrlDown = m_leftCtrlDown || m_rightCtrlDown;
        break;
    case VK_CONTROL:
        m_ctrlDown = isDown;
        break;
    case VK_LSHIFT:
        m_leftShiftDown = isDown;
        m_shiftDown = m_leftShiftDown || m_rightShiftDown;
        break;
    case VK_RSHIFT:
        m_rightShiftDown = isDown;
        m_shiftDown = m_leftShiftDown || m_rightShiftDown;
        break;
    case VK_SHIFT:
        m_shiftDown = isDown;
        break;
    case VK_LMENU:
        m_leftAltDown = isDown;
        m_altDown = m_leftAltDown || m_rightAltDown;
        break;
    case VK_RMENU:
        m_rightAltDown = isDown;
        m_altDown = m_leftAltDown || m_rightAltDown;
        break;
    case VK_MENU:
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

bool HotkeyManager::HandleKeyDown(UINT vk, const ModifierState& modifiers) {
    PopupWindow* popup     = Application::Get() ? Application::Get()->GetPopup() : nullptr;
    const bool   popupOpen = popup && popup->IsVisible();
    const bool ctrl = modifiers.Ctrl();
    const bool shift = modifiers.Shift();
    const bool alt = modifiers.Alt();

    if (m_captureActive) {
        if (vk == VK_ESCAPE) {
            CancelCapture();
            return true;
        }
        if (IsModifierKey(vk)) {
            if (m_modifierCaptureMode)
                m_modifierCaptureMask |= modifiers.Mask();
            return true;
        }

        if (m_modifierCaptureMode)
            return true;

        m_capturedBinding = {ctrl, shift, alt, vk, HotkeyAction::None, 0};
        m_capturedBinding.ctrlSide = CapturedSide(
            modifiers.leftCtrl, modifiers.rightCtrl);
        m_capturedBinding.shiftSide = CapturedSide(
            modifiers.leftShift, modifiers.rightShift);
        m_capturedBinding.altSide = CapturedSide(
            modifiers.leftAlt, modifiers.rightAlt);
        m_capturedBinding.physicalModifiers = modifiers.Mask();
        m_capturedBinding.exactModifiers = true;
        m_captureReady = true;
        m_captureActive = false;
        return true;
    }

    if (m_completedDoubleTap && vk == m_completedDoubleTrigger &&
        modifiers.Mask() == m_completedDoubleModifierMask) {
        ConsumeActionPress(vk);
        return true;
    }

    if (m_pendingDoubleTap && !IsModifierKey(vk)) {
        if (vk == m_pendingTrigger && !m_pendingTriggerReleased)
            return true; // held-key repeat is not another tap
        if (vk == m_pendingTrigger && m_pendingTriggerReleased &&
            modifiers.Mask() == m_pendingModifierMask) {
            if (!ConsumeActionPress(vk))
                return true;
            const HotkeyAction action = m_pendingDoubleAction;
            const int data = m_pendingDoubleData;
            const UINT trigger = m_pendingTrigger;
            const uint8_t modifierMask = m_pendingModifierMask;
            ClearPendingDoubleTap();
            m_completedDoubleTap = true;
            m_completedDoubleTrigger = trigger;
            m_completedDoubleModifierMask = modifierMask;
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(action), static_cast<LPARAM>(data));
            return true;
        }
        DispatchPendingSingleTap();
    }

    const KeyBinding* explicitBinding = nullptr;
    for (int specificity = 6; specificity >= 0 && !explicitBinding; --specificity) {
      for (const auto& b : m_bindings) {
        int bindingSpecificity = 0;
        if (b.exactModifiers) {
            uint8_t mask = b.physicalModifiers;
            while (mask) {
                bindingSpecificity += mask & 1u;
                mask >>= 1;
            }
        } else {
            bindingSpecificity =
                (b.ctrl && b.ctrlSide != ModifierSide::Any ? 1 : 0) +
                (b.shift && b.shiftSide != ModifierSide::Any ? 1 : 0) +
                (b.alt && b.altSide != ModifierSide::Any ? 1 : 0);
        }
        if (bindingSpecificity == specificity && b.Matches(modifiers, vk)) {
            explicitBinding = &b;
            break;
        }
      }
    }

    int bankSlot = -1;
    const HotkeyAction bankAction = ResolveSlotBank(
        m_settings, modifiers, vk,
        popupOpen && !popup->IsTextEntryActive(), bankSlot);

    if (explicitBinding) {
        if (m_settings.hotkeyDoubleTaps &&
            explicitBinding->action == HotkeyAction::PasteNamedSlot &&
            bankAction != HotkeyAction::None && modifiers.Mask() != 0) {
            if (!ConsumeActionPress(vk))
                return true;
            m_pendingDoubleTap = true;
            m_pendingTriggerReleased = false;
            m_pendingTrigger = vk;
            m_pendingModifierMask = modifiers.Mask();
            m_pendingSingleAction = bankAction;
            m_pendingSingleData = bankSlot;
            m_pendingDoubleAction = explicitBinding->action;
            m_pendingDoubleData = explicitBinding->data;
            return true;
        }
        if (!ConsumeActionPress(vk))
            return true;
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(explicitBinding->action),
                     static_cast<LPARAM>(explicitBinding->data));
        return true;
    }

    // Explicit bindings above commandeer matching generated bank routes.
    if (bankAction != HotkeyAction::None) {
        if (!ConsumeActionPress(vk))
            return true;
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(bankAction),
                     static_cast<LPARAM>(bankSlot));
        return true;
    }

    if (MatchesPassthrough(m_settings, modifiers, vk))
        return false;

    if (popupOpen) {
        HkLog("[HK-KEY-DOWN] popup=OPEN vk=0x%02X ctrl=%d shift=%d alt=%d kbCapture=%d txtEntry=%d search=%d",
              vk, ctrl, shift, alt,
              popup->IsKeyboardCaptureActive(),
              popup->IsTextEntryActive(),
              popup->IsSearchActive());

        if (vk == VK_ESCAPE) {
            if (!ConsumeActionPress(vk))
                return true;
            if (popup->HasMultipleSelectedItems()) {
                HkLog("[HK-ESCAPE] clearing selected items");
                PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                             static_cast<WPARAM>(HotkeyAction::ClearSelectedItems), 0);
                return true;
            }
            HkLog("[HK-ESCAPE] closing popup");
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::TogglePopup), 0);
            return true;
        }

        const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                      || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        if (win) {
            HkLog("[HK-WIN-KEY] win modifier active, passing through");
            return false;
        }

        // Remaining keys (text forwarding, Shift+Enter search) only engage when
        // keyboard capture is active, so we don't intercept normal typing in the
        // foreground app after the user has clicked away from the popup.
        if (!popup->IsKeyboardCaptureActive()) {
            HkLog("[HK-NO-CAPTURE] kbCapture=false, passing vk=0x%02X through to foreground", vk);
            return false;
        }

        if (!ctrl && shift && !alt && vk == VK_RETURN && popup->IsSearchActive()) {
            if (!ConsumeActionPress(vk))
                return true;
            HkLog("[HK-WEB-SEARCH] Shift+Enter with search active -> LaunchWebSearch");
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::LaunchWebSearch), 0);
            return true;
        }

        if (ctrl && !shift && !alt && vk == 'V' &&
            popup->HasMultipleSelectedItems() && !popup->IsTextEntryActive()) {
            if (!ConsumeActionPress(vk))
                return true;
            HkLog("[HK-SELECTION-PASTE] Ctrl+V -> PasteSelectedItems");
            PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                         static_cast<WPARAM>(HotkeyAction::PasteSelectedItems), 0);
            return true;
        }

        HkLog("[HK-FORWARD] vk=0x%02X shift=%d -> ForwardKeyToPopup", vk, shift);
        ForwardKeyToPopup(vk, shift);
        return true;
    }

    return false;
}

bool HotkeyManager::HandleKeyUp(UINT vk, const ModifierState& modifiers) {
    if (m_captureActive && m_modifierCaptureMode && IsModifierKey(vk)) {
        if (m_modifierCaptureMask != 0 &&
            (modifiers.Mask() & m_modifierCaptureMask) == 0) {
            m_capturedBinding = {};
            m_capturedBinding.exactModifiers = true;
            m_capturedBinding.physicalModifiers = m_modifierCaptureMask;
            m_captureReady = true;
            m_captureActive = false;
            m_modifierCaptureMode = false;
        }
        return true;
    }
    if (m_pendingDoubleTap && vk == m_pendingTrigger) {
        m_pendingTriggerReleased = true;
        return true;
    }
    if (m_completedDoubleTap && vk == m_completedDoubleTrigger)
        return true;
    if (m_pendingDoubleTap && IsModifierKey(vk) &&
        (modifiers.Mask() & m_pendingModifierMask) != m_pendingModifierMask) {
        DispatchPendingSingleTap();
    }
    if (m_completedDoubleTap && IsModifierKey(vk) &&
        (modifiers.Mask() & m_completedDoubleModifierMask) !=
            m_completedDoubleModifierMask) {
        m_completedDoubleTap = false;
        m_completedDoubleTrigger = 0;
        m_completedDoubleModifierMask = 0;
    }
    return false;
}

void HotkeyManager::ClearPendingDoubleTap() {
    m_pendingDoubleTap = false;
    m_pendingTriggerReleased = false;
    m_pendingTrigger = 0;
    m_pendingModifierMask = 0;
    m_pendingSingleAction = HotkeyAction::None;
    m_pendingSingleData = 0;
    m_pendingDoubleAction = HotkeyAction::None;
    m_pendingDoubleData = 0;
}

void HotkeyManager::DispatchPendingSingleTap() {
    if (!m_pendingDoubleTap)
        return;
    const HotkeyAction action = m_pendingSingleAction;
    const int data = m_pendingSingleData;
    ClearPendingDoubleTap();
    if (action != HotkeyAction::None) {
        PostMessageW(m_msgTarget, WM_HOTKEYACTION,
                     static_cast<WPARAM>(action), static_cast<LPARAM>(data));
    }
}

void HotkeyManager::ForwardKeyToPopup(UINT vk, bool shift) const {
    PopupWindow* popup = Application::Get()->GetPopup();
    if (!popup) return;
    HWND hw = popup->GetHwnd();

    switch (vk) {
    case VK_TAB:
    case VK_RETURN:
    case VK_SPACE:
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

bool HotkeyManager::HandleKeyDown(UINT, const ModifierState&) {
    return false;
}

bool HotkeyManager::HandleKeyUp(UINT, const ModifierState&) {
    return false;
}

void HotkeyManager::ClearPendingDoubleTap() {}
void HotkeyManager::DispatchPendingSingleTap() {}

void HotkeyManager::ForwardKeyToPopup(UINT, bool) const {}
#endif
