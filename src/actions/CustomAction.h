#pragma once

#include "../hotkeys/HotkeyManager.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum class CustomActionPlacement : uint8_t {
    Actions,
    Destinations,
};

enum class CustomActionInput : uint8_t {
    CurrentItem,
    OrderedSelection,
    NamedSlot,
    ActiveProfile,
    SearchText,
    WindowsClipboard,
};

enum class CustomActionStepType : uint8_t {
    Regex,
    Template,
    FormatJson,
    FormatXml,
    FormatSql,
    Join,
    Trim,
    Uppercase,
    Lowercase,
    UrlEncode,
};

enum class CustomActionOutput : uint8_t {
    Paste,
    Copy,
    OpenUrl,
    SendAndroid,
    SaveFile,
    MoveTop,
    MoveBottom,
    AddTag,
    Pin,
    LaunchExecutable,
};

enum class CustomActionConfirmation : uint8_t {
    Never,
    ExternalOnly,
    Always,
};

struct CustomActionStep {
    CustomActionStepType type{CustomActionStepType::Trim};
    std::string value;
    std::string replacement;
    bool caseSensitive{true};
    bool multiline{false};
    bool dotMatchesNewline{false};
    bool replaceAll{true};
};

struct CustomActionVisibility {
    bool requireText{true};
    int minimumSelection{1};
    int maximumSelection{0}; // zero means unlimited
    uint32_t requiredTags{};
    std::string applicationPattern;
};

struct CustomActionDefinition {
    int64_t actionId{};
    std::string label;
    std::string icon;
    int toolbarOrder{};
    bool enabled{true};
    CustomActionPlacement placement{CustomActionPlacement::Actions};
    CustomActionInput input{CustomActionInput::CurrentItem};
    std::string namedSlot;
    CustomActionVisibility visibility;
    std::vector<CustomActionStep> steps;
    CustomActionOutput output{CustomActionOutput::Paste};
    std::string outputValue;
    std::string executableArguments;
    CustomActionConfirmation confirmation{CustomActionConfirmation::ExternalOnly};
    int timeoutMs{5000};
    bool hotkeyEnabled{false};
    KeyBinding hotkey;
    int64_t createdAtMs{};
    int64_t updatedAtMs{};
};

struct CustomActionContext {
    std::vector<std::string> selectedTexts;
    uint32_t combinedTags{};
    bool allSelectedItemsAreText{true};
    std::vector<std::pair<std::string, std::string>> namedSlots;
    std::string activeProfile;
    std::string searchText;
    std::string windowsClipboard;
    std::string callingApplication;
};

struct CustomActionPreparation {
    bool ok{false};
    std::vector<std::string> values;
    std::string output;
    std::string error;
};

const char* CustomActionPlacementName(CustomActionPlacement value);
const char* CustomActionInputName(CustomActionInput value);
const char* CustomActionStepName(CustomActionStepType value);
const char* CustomActionOutputName(CustomActionOutput value);
const char* CustomActionConfirmationName(CustomActionConfirmation value);

bool CustomActionIsExternal(CustomActionOutput output);
bool CustomActionUsesCallingApp(CustomActionOutput output);
bool CustomActionMatches(const CustomActionDefinition& action,
                         const CustomActionContext& context);
std::string ValidateCustomAction(const CustomActionDefinition& action);
CustomActionPreparation PrepareCustomAction(
    const CustomActionDefinition& action,
    const CustomActionContext& context);

std::string QuoteWindowsArgument(const std::string& value);
std::string ExpandCustomActionPlaceholders(
    const std::string& value,
    const CustomActionContext& context,
    const std::string& preparedText,
    bool quoteInsertedValues);

std::string SerializeCustomAction(const CustomActionDefinition& action,
                                  bool pretty = false);
bool DeserializeCustomAction(const std::string& payload,
                             CustomActionDefinition& action,
                             std::string* error = nullptr);
