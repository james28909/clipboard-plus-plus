#include "HotkeyManager.h"

std::vector<KeyBinding> HotkeyManager::DefaultBindings() {
    return {
        {true, true, false, 'V',          HotkeyAction::TogglePopup,     0},
        {true, true, false, 'S',          HotkeyAction::ShowPopupSearch, 0},
        {true, true, false, 'I',          HotkeyAction::Incognito,       0},
        {true, true, false, VK_OEM_COMMA, HotkeyAction::OpenSettings,    0},
        {true, true, false, 'E',          HotkeyAction::ToggleEditorWindow, 0},
        {true, true, false, 'G',          HotkeyAction::LaunchClipboardWebSearch, 0},
        {true, true, true,  'Z',          HotkeyAction::SendSelectionToAndroid, 0},
        {false, true, true, 'D',          HotkeyAction::ToggleDebugWindow, 0},
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
