#include "CustomAction.h"

#include "../formatting/StructuredFormatter.h"
#include "../templates/PasteTemplate.h"
#include "../transforms/RegexTransform.h"
#include "../../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

using json = nlohmann::json;

namespace {

std::string Trim(std::string value) {
    auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string UrlEncode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[c >> 4]);
            result.push_back(hex[c & 0x0F]);
        }
    }
    return result;
}

std::string Join(const std::vector<std::string>& values,
                 const std::string& separator) {
    std::string result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) result += separator;
        result += values[i];
    }
    return result;
}

bool GlobMatches(const std::string& patternValue, const std::string& textValue) {
    const std::string pattern = Lower(patternValue);
    const std::string text = Lower(textValue);
    size_t p = 0, t = 0, star = std::string::npos, retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p; ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++retry;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string NamedSlotValue(
    const std::vector<std::pair<std::string, std::string>>& slots,
    const std::string& name) {
    const std::string wanted = Lower(name);
    for (const auto& slot : slots)
        if (Lower(slot.first) == wanted)
            return slot.second;
    return {};
}

void ReplaceAll(std::string& target, const std::string& token,
                const std::string& replacement) {
    if (token.empty()) return;
    size_t at = 0;
    while ((at = target.find(token, at)) != std::string::npos) {
        target.replace(at, token.size(), replacement);
        at += replacement.size();
    }
}

json BindingToJson(const KeyBinding& value) {
    return {
        {"ctrl", value.ctrl}, {"shift", value.shift}, {"alt", value.alt},
        {"vkey", value.vkey}, {"ctrlSide", static_cast<int>(value.ctrlSide)},
        {"shiftSide", static_cast<int>(value.shiftSide)},
        {"altSide", static_cast<int>(value.altSide)},
        {"physicalModifiers", value.physicalModifiers},
        {"exactModifiers", value.exactModifiers},
    };
}

KeyBinding BindingFromJson(const json& value) {
    KeyBinding binding;
    binding.ctrl = value.value("ctrl", false);
    binding.shift = value.value("shift", false);
    binding.alt = value.value("alt", false);
    binding.vkey = value.value("vkey", UINT{0});
    binding.ctrlSide = static_cast<ModifierSide>(
        std::clamp(value.value("ctrlSide", 0), 0, 2));
    binding.shiftSide = static_cast<ModifierSide>(
        std::clamp(value.value("shiftSide", 0), 0, 2));
    binding.altSide = static_cast<ModifierSide>(
        std::clamp(value.value("altSide", 0), 0, 2));
    binding.physicalModifiers = static_cast<uint8_t>(
        value.value("physicalModifiers", 0));
    binding.exactModifiers = value.value("exactModifiers", false);
    return binding;
}

} // namespace

const char* CustomActionPlacementName(CustomActionPlacement value) {
    return value == CustomActionPlacement::Destinations ? "Destinations" : "Actions";
}

const char* CustomActionInputName(CustomActionInput value) {
    switch (value) {
    case CustomActionInput::OrderedSelection: return "Ordered selection";
    case CustomActionInput::NamedSlot: return "Named slot";
    case CustomActionInput::ActiveProfile: return "Active profile";
    case CustomActionInput::SearchText: return "Search text";
    case CustomActionInput::WindowsClipboard: return "Windows clipboard";
    default: return "Current history item";
    }
}

const char* CustomActionStepName(CustomActionStepType value) {
    switch (value) {
    case CustomActionStepType::Regex: return "Regex transform";
    case CustomActionStepType::Template: return "Template interpolation";
    case CustomActionStepType::FormatJson: return "Format JSON";
    case CustomActionStepType::FormatXml: return "Format XML";
    case CustomActionStepType::FormatSql: return "Format SQL";
    case CustomActionStepType::Join: return "Join with separator";
    case CustomActionStepType::Uppercase: return "Uppercase";
    case CustomActionStepType::Lowercase: return "Lowercase";
    case CustomActionStepType::UrlEncode: return "URL encode";
    default: return "Trim";
    }
}

const char* CustomActionOutputName(CustomActionOutput value) {
    switch (value) {
    case CustomActionOutput::Copy: return "Copy without pasting";
    case CustomActionOutput::OpenUrl: return "Open URL";
    case CustomActionOutput::SendAndroid: return "Send to Android";
    case CustomActionOutput::SaveFile: return "Save to file";
    case CustomActionOutput::MoveTop: return "Move history to top";
    case CustomActionOutput::MoveBottom: return "Move history to bottom";
    case CustomActionOutput::AddTag: return "Add history tag";
    case CustomActionOutput::Pin: return "Pin history";
    case CustomActionOutput::LaunchExecutable: return "Launch executable";
    default: return "Paste into calling app";
    }
}

const char* CustomActionConfirmationName(CustomActionConfirmation value) {
    switch (value) {
    case CustomActionConfirmation::Never: return "Never";
    case CustomActionConfirmation::Always: return "Always";
    default: return "External actions only";
    }
}

bool CustomActionIsExternal(CustomActionOutput output) {
    return output == CustomActionOutput::OpenUrl ||
           output == CustomActionOutput::SaveFile ||
           output == CustomActionOutput::LaunchExecutable;
}

bool CustomActionUsesCallingApp(CustomActionOutput output) {
    return output == CustomActionOutput::Paste;
}

bool CustomActionMatches(const CustomActionDefinition& action,
                         const CustomActionContext& context) {
    if (!action.enabled) return false;
    const int selection = static_cast<int>(context.selectedTexts.size());
    if (selection < std::max(0, action.visibility.minimumSelection)) return false;
    if (action.visibility.maximumSelection > 0 &&
        selection > action.visibility.maximumSelection) return false;
    if (action.visibility.requireText && !context.allSelectedItemsAreText) return false;
    if (action.visibility.requiredTags != 0 &&
        (context.combinedTags & action.visibility.requiredTags) !=
            action.visibility.requiredTags) return false;
    if (!action.visibility.applicationPattern.empty() &&
        !GlobMatches(action.visibility.applicationPattern,
                     context.callingApplication)) return false;
    if (action.input == CustomActionInput::SearchText && context.searchText.empty()) return false;
    if (action.input == CustomActionInput::WindowsClipboard &&
        context.windowsClipboard.empty()) return false;
    if (action.input == CustomActionInput::NamedSlot &&
        NamedSlotValue(context.namedSlots, action.namedSlot).empty()) return false;
    return true;
}

std::string ValidateCustomAction(const CustomActionDefinition& action) {
    if (Trim(action.label).empty()) return "Action label is required.";
    if (action.label.size() > 96) return "Action label is too long.";
    if (action.icon.size() > 32) return "Action icon is too long.";
    if (action.input == CustomActionInput::NamedSlot && Trim(action.namedSlot).empty())
        return "Choose a named slot for this input.";
    if (action.visibility.minimumSelection < 0 ||
        action.visibility.maximumSelection < 0 ||
        (action.visibility.maximumSelection > 0 &&
         action.visibility.maximumSelection < action.visibility.minimumSelection))
        return "Selection visibility limits are invalid.";
    if (action.timeoutMs < 100 || action.timeoutMs > 600000)
        return "External timeout must be between 100 ms and 10 minutes.";
    if (action.hotkeyEnabled && action.hotkey.vkey == 0)
        return "Capture a key for the action hotkey.";
    for (const CustomActionStep& step : action.steps) {
        if (step.type == CustomActionStepType::Regex) {
            RegexTransformDefinition regex;
            regex.name = action.label;
            regex.pattern = step.value;
            regex.replacement = step.replacement;
            regex.caseSensitive = step.caseSensitive;
            regex.multiline = step.multiline;
            regex.dotMatchesNewline = step.dotMatchesNewline;
            regex.replaceAll = step.replaceAll;
            const std::string error = ValidateRegexTransform(regex);
            if (!error.empty()) return error;
        }
        if (step.type == CustomActionStepType::Template && step.value.empty())
            return "Template steps require a template body.";
    }
    if ((action.output == CustomActionOutput::SaveFile ||
         action.output == CustomActionOutput::LaunchExecutable) &&
        Trim(action.outputValue).empty())
        return action.output == CustomActionOutput::SaveFile
            ? "Choose an output file path."
            : "Choose an executable path.";
    return {};
}

CustomActionPreparation PrepareCustomAction(
    const CustomActionDefinition& action,
    const CustomActionContext& context) {
    CustomActionPreparation result;
    result.error = ValidateCustomAction(action);
    if (!result.error.empty()) return result;
    if (!CustomActionMatches(action, context)) {
        result.error = "The action is not compatible with the current context.";
        return result;
    }

    switch (action.input) {
    case CustomActionInput::OrderedSelection:
        result.values = context.selectedTexts;
        break;
    case CustomActionInput::NamedSlot:
        result.values = {NamedSlotValue(context.namedSlots, action.namedSlot)};
        break;
    case CustomActionInput::ActiveProfile:
        result.values = {context.activeProfile};
        break;
    case CustomActionInput::SearchText:
        result.values = {context.searchText};
        break;
    case CustomActionInput::WindowsClipboard:
        result.values = {context.windowsClipboard};
        break;
    default:
        if (!context.selectedTexts.empty()) result.values = {context.selectedTexts.front()};
        break;
    }
    if (result.values.empty()) {
        result.error = "The selected input has no value.";
        return result;
    }

    for (const CustomActionStep& step : action.steps) {
        if (step.type == CustomActionStepType::Join) {
            result.values = {Join(result.values, step.value)};
            continue;
        }
        if (step.type == CustomActionStepType::Template) {
            PasteTemplateDefinition value;
            value.name = action.label;
            value.body = step.value;
            PasteTemplateResult applied = ApplyPasteTemplate(
                value, result.values, context.namedSlots);
            if (!applied.ok) {
                result.error = applied.error;
                return result;
            }
            result.values = {std::move(applied.output)};
            continue;
        }

        for (std::string& value : result.values) {
            switch (step.type) {
            case CustomActionStepType::Regex: {
                RegexTransformDefinition regex;
                regex.name = action.label;
                regex.pattern = step.value;
                regex.replacement = step.replacement;
                regex.caseSensitive = step.caseSensitive;
                regex.multiline = step.multiline;
                regex.dotMatchesNewline = step.dotMatchesNewline;
                regex.replaceAll = step.replaceAll;
                RegexTransformResult applied = ApplyRegexTransform(regex, value);
                if (!applied.ok) { result.error = applied.error; return result; }
                value = std::move(applied.output);
                break;
            }
            case CustomActionStepType::FormatJson:
            case CustomActionStepType::FormatXml:
            case CustomActionStepType::FormatSql: {
                StructuredFormat format = StructuredFormat::Json;
                if (step.type == CustomActionStepType::FormatXml) format = StructuredFormat::Xml;
                if (step.type == CustomActionStepType::FormatSql) format = StructuredFormat::Sql;
                StructuredFormatResult formatted = FormatStructuredText(value, format);
                if (!formatted.ok) { result.error = formatted.error; return result; }
                value = std::move(formatted.output);
                break;
            }
            case CustomActionStepType::Trim: value = Trim(std::move(value)); break;
            case CustomActionStepType::Uppercase: value = Upper(std::move(value)); break;
            case CustomActionStepType::Lowercase: value = Lower(std::move(value)); break;
            case CustomActionStepType::UrlEncode: value = UrlEncode(value); break;
            default: break;
            }
        }
    }
    result.output = Join(result.values, "\n");
    result.ok = true;
    return result;
}

std::string QuoteWindowsArgument(const std::string& value) {
    if (value.empty()) return "\"\"";
    if (value.find_first_of(" \t\n\v\"") == std::string::npos) return value;
    std::string result = "\"";
    size_t slashes = 0;
    for (char c : value) {
        if (c == '\\') {
            ++slashes;
        } else if (c == '"') {
            result.append(slashes * 2 + 1, '\\');
            result.push_back('"');
            slashes = 0;
        } else {
            result.append(slashes, '\\');
            slashes = 0;
            result.push_back(c);
        }
    }
    result.append(slashes * 2, '\\');
    result.push_back('"');
    return result;
}

std::string ExpandCustomActionPlaceholders(
    const std::string& value,
    const CustomActionContext& context,
    const std::string& preparedText,
    bool quoteInsertedValues) {
    auto inserted = [&](const std::string& text) {
        return quoteInsertedValues ? QuoteWindowsArgument(text) : text;
    };
    std::string result = value;
    ReplaceAll(result, "{{text}}", inserted(preparedText));
    ReplaceAll(result, "{{app}}", inserted(context.callingApplication));
    ReplaceAll(result, "{{profile}}", inserted(context.activeProfile));
    ReplaceAll(result, "{{search}}", inserted(context.searchText));
    ReplaceAll(result, "{{clipboard}}", inserted(context.windowsClipboard));
    for (const auto& slot : context.namedSlots)
        ReplaceAll(result, "{{slot:" + slot.first + "}}", inserted(slot.second));
    return result;
}

std::string SerializeCustomAction(const CustomActionDefinition& action,
                                  bool pretty) {
    json steps = json::array();
    for (const CustomActionStep& step : action.steps) {
        steps.push_back({
            {"type", static_cast<int>(step.type)}, {"value", step.value},
            {"replacement", step.replacement},
            {"caseSensitive", step.caseSensitive}, {"multiline", step.multiline},
            {"dotMatchesNewline", step.dotMatchesNewline},
            {"replaceAll", step.replaceAll},
        });
    }
    json value = {
        {"schema", 1}, {"actionId", action.actionId}, {"label", action.label},
        {"icon", action.icon}, {"toolbarOrder", action.toolbarOrder},
        {"enabled", action.enabled}, {"placement", static_cast<int>(action.placement)},
        {"input", static_cast<int>(action.input)}, {"namedSlot", action.namedSlot},
        {"visibility", {
            {"requireText", action.visibility.requireText},
            {"minimumSelection", action.visibility.minimumSelection},
            {"maximumSelection", action.visibility.maximumSelection},
            {"requiredTags", action.visibility.requiredTags},
            {"applicationPattern", action.visibility.applicationPattern},
        }},
        {"steps", std::move(steps)}, {"output", static_cast<int>(action.output)},
        {"outputValue", action.outputValue},
        {"executableArguments", action.executableArguments},
        {"confirmation", static_cast<int>(action.confirmation)},
        {"timeoutMs", action.timeoutMs}, {"hotkeyEnabled", action.hotkeyEnabled},
        {"hotkey", BindingToJson(action.hotkey)},
        {"createdAtMs", action.createdAtMs}, {"updatedAtMs", action.updatedAtMs},
    };
    return value.dump(pretty ? 2 : -1);
}

bool DeserializeCustomAction(const std::string& payload,
                             CustomActionDefinition& action,
                             std::string* error) {
    try {
        const json value = json::parse(payload);
        if (value.value("schema", 0) != 1) {
            if (error) *error = "Unsupported custom-action schema.";
            return false;
        }
        CustomActionDefinition parsed;
        parsed.actionId = value.value("actionId", int64_t{});
        parsed.label = value.value("label", std::string{});
        parsed.icon = value.value("icon", std::string{});
        parsed.toolbarOrder = value.value("toolbarOrder", 0);
        parsed.enabled = value.value("enabled", true);
        parsed.placement = static_cast<CustomActionPlacement>(
            std::clamp(value.value("placement", 0), 0, 1));
        parsed.input = static_cast<CustomActionInput>(
            std::clamp(value.value("input", 0), 0, 5));
        parsed.namedSlot = value.value("namedSlot", std::string{});
        const json visibility = value.value("visibility", json::object());
        parsed.visibility.requireText = visibility.value("requireText", true);
        parsed.visibility.minimumSelection = visibility.value("minimumSelection", 1);
        parsed.visibility.maximumSelection = visibility.value("maximumSelection", 0);
        parsed.visibility.requiredTags = visibility.value("requiredTags", uint32_t{});
        parsed.visibility.applicationPattern =
            visibility.value("applicationPattern", std::string{});
        for (const json& item : value.value("steps", json::array())) {
            CustomActionStep step;
            step.type = static_cast<CustomActionStepType>(
                std::clamp(item.value("type", 0), 0, 9));
            step.value = item.value("value", std::string{});
            step.replacement = item.value("replacement", std::string{});
            step.caseSensitive = item.value("caseSensitive", true);
            step.multiline = item.value("multiline", false);
            step.dotMatchesNewline = item.value("dotMatchesNewline", false);
            step.replaceAll = item.value("replaceAll", true);
            parsed.steps.push_back(std::move(step));
        }
        parsed.output = static_cast<CustomActionOutput>(
            std::clamp(value.value("output", 0), 0, 9));
        parsed.outputValue = value.value("outputValue", std::string{});
        parsed.executableArguments = value.value("executableArguments", std::string{});
        parsed.confirmation = static_cast<CustomActionConfirmation>(
            std::clamp(value.value("confirmation", 1), 0, 2));
        parsed.timeoutMs = value.value("timeoutMs", 5000);
        parsed.hotkeyEnabled = value.value("hotkeyEnabled", false);
        parsed.hotkey = BindingFromJson(value.value("hotkey", json::object()));
        parsed.createdAtMs = value.value("createdAtMs", int64_t{});
        parsed.updatedAtMs = value.value("updatedAtMs", int64_t{});
        const std::string validation = ValidateCustomAction(parsed);
        if (!validation.empty()) {
            if (error) *error = validation;
            return false;
        }
        action = std::move(parsed);
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}
