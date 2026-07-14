#include "PopupWindow.h"

#include "../app/Application.h"
#include "../clipboard/ContentDetector.h"
#include "../util/Win32Util.h"
#include "GeneratedPaste.h"
#include "ToastWindow.h"

#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

std::wstring PreviewMessage(const CustomActionDefinition& action,
                            const CustomActionPreparation& prepared,
                            const CustomActionContext& context) {
    std::string preview = prepared.output;
    if (preview.size() > 600)
        preview = preview.substr(0, 597) + "...";
    std::string message = "Action: " + action.label + "\nOutput: " +
        CustomActionOutputName(action.output) + "\nTarget: " +
        (context.callingApplication.empty() ? "unknown" : context.callingApplication) +
        "\n\nData preview:\n" + preview + "\n\nContinue?";
    return win32util::Utf8ToWide(message);
}

bool WriteUtf8(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    return output.good();
}

uint32_t ParseTagMask(const std::string& value) {
    try {
        const unsigned long parsed = std::stoul(value, nullptr, 0);
        return parsed <= std::numeric_limits<uint32_t>::max()
            ? static_cast<uint32_t>(parsed)
            : 0;
    } catch (...) {
        return 0;
    }
}

} // namespace

std::vector<uint64_t> PopupWindow::ResolveCustomActionItemIds() const {
    if (m_visible && !m_itemSelection.Empty())
        return m_itemSelection.Ids();

    ClipboardHistory* history = Application::Get()
        ? Application::Get()->GetHistory()
        : nullptr;
    if (!history) return {};

    if (m_visible) {
        const std::vector<uint64_t> visible = BuildVisibleItemIds();
        if (!visible.empty()) return {visible.front()};
    }

    ClipboardItem first;
    if (history->GetRegularCopy(0, first) || history->GetPinnedCopy(0, first))
        return {first.id};
    return {};
}

CustomActionContext PopupWindow::BuildCustomActionContext(
    HWND targetWindow, bool readClipboard) const {
    CustomActionContext context;
    Application* app = Application::Get();
    ClipboardHistory* history = app ? app->GetHistory() : nullptr;
    const std::vector<uint64_t> ids = ResolveCustomActionItemIds();
    bool first = true;
    for (uint64_t id : ids) {
        ClipboardItem item;
        if (!history || !history->GetByIdCopy(id, item)) continue;
        context.selectedTexts.push_back(item.text);
        context.allSelectedItemsAreText &= item.IsText();
        if (first) {
            context.combinedTags = item.tags;
            first = false;
        } else {
            context.combinedTags &= item.tags;
        }
    }
    for (const NamedClipboardSlot& slot : app ? app->GetNamedSlots()
                                               : std::vector<NamedClipboardSlot>{})
        context.namedSlots.emplace_back(slot.name, slot.text);
    if (app) {
        if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile())
            context.activeProfile = profile->name;
    }
    context.searchText = m_searchBuf;
    if (readClipboard)
        context.windowsClipboard = win32util::ClipboardUnicodeText();
    else if (IsClipboardFormatAvailable(CF_UNICODETEXT))
        context.windowsClipboard = "available";

    HWND target = targetWindow ? GetAncestor(targetWindow, GA_ROOT) : ResolvePasteTarget();
    context.callingApplication = win32util::ProcessNameFromWindow(target);
    return context;
}

bool PopupWindow::RunCustomAction(int64_t actionId, HWND targetWindow) {
    Application* app = Application::Get();
    if (!app) return false;
    const std::vector<CustomActionDefinition> actions = app->GetCustomActions();
    auto found = std::find_if(actions.begin(), actions.end(),
        [&](const CustomActionDefinition& action) {
            return action.actionId == actionId;
        });
    if (found == actions.end()) {
        ToastWindow::Show(L"Custom action no longer exists");
        return false;
    }

    const std::vector<uint64_t> ids = ResolveCustomActionItemIds();
    CustomActionContext context = BuildCustomActionContext(targetWindow, true);
    const CustomActionPreparation prepared = PrepareCustomAction(*found, context);
    if (!prepared.ok) {
        ToastWindow::Show(win32util::Utf8ToWide(
            "Action could not run: " + prepared.error));
        return false;
    }

    const bool confirm = found->confirmation == CustomActionConfirmation::Always ||
        (found->confirmation == CustomActionConfirmation::ExternalOnly &&
         CustomActionIsExternal(found->output));
    if (confirm) {
        const std::wstring message = PreviewMessage(*found, prepared, context);
        if (MessageBoxW(m_hwnd, message.c_str(), L"Confirm custom action",
                        MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND) != IDYES)
            return false;
    }
    return ExecutePreparedCustomAction(*found, context, prepared,
                                       targetWindow, ids);
}

bool PopupWindow::ExecutePreparedCustomAction(
    const CustomActionDefinition& action,
    const CustomActionContext& context,
    const CustomActionPreparation& prepared,
    HWND targetWindow,
    const std::vector<uint64_t>& itemIds) {
    Application* app = Application::Get();
    ClipboardHistory* history = app ? app->GetHistory() : nullptr;
    if (!app) return false;
    const auto failed = [&]() {
        ToastWindow::Show(L"Custom action failed");
        return false;
    };

    switch (action.output) {
    case CustomActionOutput::Paste: {
        HWND target = targetWindow ? GetAncestor(targetWindow, GA_ROOT) : ResolvePasteTarget();
        if (!target) return failed();
        ClipboardItem item = MakeGeneratedTextPaste(prepared.output);
        WriteToClipboard(item, target);
        if (m_visible) Hide();
        RestoreFocusAndPaste(target);
        return true;
    }
    case CustomActionOutput::Copy:
        return app->CopyTextToClipboard(prepared.output) || failed();
    case CustomActionOutput::OpenUrl: {
        const std::string urlTemplate = action.outputValue.empty()
            ? "{{text}}" : action.outputValue;
        const std::string url = ExpandCustomActionPlaceholders(
            urlTemplate, context, prepared.output, false);
        const HINSTANCE result = ShellExecuteW(
            nullptr, L"open", win32util::Utf8ToWide(url).c_str(),
            nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            app->AddDeveloperEvent("custom action open-url failed: id=" +
                                   std::to_string(action.actionId));
            return failed();
        }
        if (m_visible) Hide();
        return true;
    }
    case CustomActionOutput::SendAndroid: {
        std::string error;
        if (!app->SendTextItemsToAndroid(prepared.values, &error)) {
            app->AddDeveloperEvent("custom action Android output failed: id=" +
                                   std::to_string(action.actionId));
            return failed();
        }
        return true;
    }
    case CustomActionOutput::SaveFile: {
        const std::string expanded = ExpandCustomActionPlaceholders(
            action.outputValue, context, prepared.output, false);
        if (!WriteUtf8(std::filesystem::path(win32util::Utf8ToWide(expanded)),
                       prepared.output)) {
            app->AddDeveloperEvent("custom action file output failed: id=" +
                                   std::to_string(action.actionId));
            return failed();
        }
        return true;
    }
    case CustomActionOutput::MoveTop:
        return history && history->MoveItemsByIdToTop(itemIds);
    case CustomActionOutput::MoveBottom:
        return history && history->MoveItemsByIdToBottom(itemIds);
    case CustomActionOutput::AddTag: {
        const uint32_t tags = ParseTagMask(action.outputValue);
        return history && tags != 0 && history->AddTagsByIdMany(itemIds, tags);
    }
    case CustomActionOutput::Pin:
        return history && history->SetPinnedByIdMany(itemIds, true);
    case CustomActionOutput::LaunchExecutable: {
        const std::string executable = ExpandCustomActionPlaceholders(
            action.outputValue, context, prepared.output, false);
        const std::string arguments = ExpandCustomActionPlaceholders(
            action.executableArguments, context, prepared.output, true);
        std::string command = QuoteWindowsArgument(executable);
        if (!arguments.empty()) command += " " + arguments;
        std::wstring wideCommand = win32util::Utf8ToWide(command);
        std::vector<wchar_t> buffer(wideCommand.begin(), wideCommand.end());
        buffer.push_back(L'\0');
        const std::filesystem::path executablePath(win32util::Utf8ToWide(executable));
        const std::wstring workingDirectory = executablePath.has_parent_path()
            ? executablePath.parent_path().wstring() : std::wstring{};
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0,
                            nullptr,
                            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                            &startup, &process)) {
            app->AddDeveloperEvent("custom action process launch failed: id=" +
                std::to_string(action.actionId) + " error=" +
                std::to_string(GetLastError()));
            return failed();
        }
        CloseHandle(process.hThread);
        const int64_t actionId = action.actionId;
        const DWORD timeout = static_cast<DWORD>(action.timeoutMs);
        std::thread([handle = process.hProcess, timeout, actionId]() {
            const DWORD result = WaitForSingleObject(handle, timeout);
            CloseHandle(handle);
            if (result == WAIT_TIMEOUT) {
                if (Application* current = Application::Get())
                    current->AddDeveloperEvent(
                        "custom action process timeout: id=" +
                        std::to_string(actionId));
            }
        }).detach();
        if (m_visible) Hide();
        return true;
    }
    }
    return false;
}

