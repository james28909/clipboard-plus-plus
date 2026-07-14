#include "HistoryCli.h"

#include "CliStorage.h"
#include "../clipboard/ClipboardHistory.h"
#include "../ipc/IpcClient.h"
#include "../util/Win32Util.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

struct Options {
    std::string profile;
    std::string format{"text"};
    size_t limit{kMaxClipboardHistoryItems};
    std::vector<std::wstring> positional;
};

std::string Utf8(const std::wstring& value) {
    return win32util::WideToUtf8(value.c_str(), static_cast<int>(value.size()));
}

std::wstring Wide(const std::string& value) {
    return win32util::Utf8ToWide(value);
}

int64_t TimeMs(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
}

bool ParseOptions(int argc, wchar_t** argv, int start, Options& options) {
    for (int i = start; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--profile") {
            if (++i >= argc) return false;
            options.profile = Utf8(argv[i]);
        } else if (argument == L"--format") {
            if (++i >= argc) return false;
            options.format = Utf8(argv[i]);
        } else if (argument == L"--limit") {
            if (++i >= argc) return false;
            try {
                size_t consumed = 0;
                const long long value = std::stoll(argv[i], &consumed);
                if (consumed != std::wstring(argv[i]).size()) return false;
                options.limit = static_cast<size_t>(std::clamp(
                    value, int64_t{1},
                    static_cast<int64_t>(kMaxClipboardHistoryItems)));
            } catch (...) {
                return false;
            }
        } else {
            options.positional.push_back(argument);
        }
    }
    return options.format == "text" || options.format == "json";
}

bool ParseItemNumber(const std::wstring& text, size_t& itemNumber) {
    try {
        size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);
        if (consumed != text.size() || value < 1 ||
            value > kMaxClipboardHistoryItems)
            return false;
        itemNumber = static_cast<size_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool LoadHistory(const std::string& requestedProfile,
                 cli_storage::ProfileContext& context,
                 ClipboardHistory& history) {
    std::wstring error;
    if (!cli_storage::OpenProfile(requestedProfile, context, error)) {
        std::wcerr << error << L'\n';
        return false;
    }
    bool found = false;
    if (!context.database.LoadHistory(context.profile.id, history, found)) {
        std::wcerr << L"Could not read history for profile "
                   << Wide(context.profile.name) << L".\n";
        return false;
    }
    return true;
}

json ItemJson(size_t index, const ClipboardItem& item, bool includeContent) {
    json result = {
        {"index", index + 1},
        {"itemId", item.id},
        {"contentHash", item.contentHash},
        {"type", ContentTypeName(item.type)},
        {"tags", item.tags},
        {"pinned", item.pinned},
        {"preview", item.Preview(160)},
        {"sourceProcess", item.sourceProcess},
        {"sourceFilePath", item.sourceFilePath},
        {"sourceKind", item.sourceKind},
        {"imageStoreId", item.imageStoreId},
        {"imageWidth", item.imageW},
        {"imageHeight", item.imageH},
        {"capturedAt", TimeMs(item.timestamp)},
        {"createdAt", TimeMs(item.createdAt)},
        {"updatedAt", TimeMs(item.updatedAt)},
        {"lastUsedAt", TimeMs(item.lastUsedAt)},
    };
    if (includeContent)
        result["text"] = item.text;
    return result;
}

void PrintRow(size_t index, const ClipboardItem& item) {
    std::wcout << (index + 1) << L'\t'
               << (item.pinned ? L"pinned" : L"-") << L'\t'
               << Wide(ContentTypeName(item.type)) << L'\t'
               << Wide(item.Preview(160)) << L'\n';
}

int RunList(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, 3, options) || !options.positional.empty()) {
        std::wcerr << L"Usage: clipboardpp get --list [--profile <id-or-name>] "
                      L"[--limit N] [--format text|json]\n";
        return 1;
    }
    cli_storage::ProfileContext context;
    ClipboardHistory history(kMaxClipboardHistoryItems);
    if (!LoadHistory(options.profile, context, history)) return 1;
    const auto items = history.Snapshot();
    const size_t count = std::min(items.size(), options.limit);
    if (options.format == "json") {
        json output = json::array();
        for (size_t i = 0; i < count; ++i)
            output.push_back(ItemJson(i, items[i], false));
        std::cout << output.dump(2) << '\n';
    } else {
        for (size_t i = 0; i < count; ++i)
            PrintRow(i, items[i]);
    }
    return 0;
}

int RunSearch(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, 3, options) || options.positional.empty()) {
        std::wcerr << L"Usage: clipboardpp get --search <query> "
                      L"[--profile <id-or-name>] [--limit N] "
                      L"[--format text|json]\n";
        return 1;
    }
    std::wstring query;
    for (const auto& part : options.positional) {
        if (!query.empty()) query += L' ';
        query += part;
    }
    cli_storage::ProfileContext context;
    ClipboardHistory history(kMaxClipboardHistoryItems);
    if (!LoadHistory(options.profile, context, history)) return 1;
    const auto items = history.Snapshot();
    const auto matches = history.Search(Utf8(query));
    const size_t count = std::min(matches.size(), options.limit);
    if (options.format == "json") {
        json output = json::array();
        for (size_t i = 0; i < count; ++i)
            output.push_back(ItemJson(matches[i], items[matches[i]], false));
        std::cout << output.dump(2) << '\n';
    } else {
        for (size_t i = 0; i < count; ++i)
            PrintRow(matches[i], items[matches[i]]);
    }
    return 0;
}

int RunItem(int argc, wchar_t** argv) {
    Options options;
    if (!ParseOptions(argc, argv, 3, options) || options.positional.size() != 1) {
        std::wcerr << L"Usage: clipboardpp get --item <n> "
                      L"[--profile <id-or-name>] [--format text|json]\n";
        return 1;
    }
    size_t requested = 0;
    if (!ParseItemNumber(options.positional.front(), requested)) {
        std::wcerr << L"History item number must be between 1 and "
                   << kMaxClipboardHistoryItems << L".\n";
        return 1;
    }
    cli_storage::ProfileContext context;
    ClipboardHistory history(kMaxClipboardHistoryItems);
    if (!LoadHistory(options.profile, context, history)) return 1;
    const auto items = history.Snapshot();
    if (requested > items.size()) {
        std::wcerr << L"History item " << requested << L" does not exist in profile "
                   << Wide(context.profile.name) << L".\n";
        return 1;
    }
    const ClipboardItem& item = items[requested - 1];
    if (options.format == "json") {
        std::cout << ItemJson(requested - 1, item, true).dump(2) << '\n';
    } else {
        std::cout.write(item.text.data(), static_cast<std::streamsize>(item.text.size()));
    }
    return 0;
}

} // namespace

int RunHistoryCli(int argc, wchar_t** argv) {
    if (argc < 3 || std::wstring(argv[2]) == L"--help") {
        std::wcout <<
            L"History commands:\n"
            L"  get --list [--profile <id-or-name>] [--limit N] [--format text|json]\n"
            L"  get --search <query> [--profile <id-or-name>] [--limit N] [--format text|json]\n"
            L"  get --item <n> [--profile <id-or-name>] [--format text|json]\n";
        return 0;
    }
    if (std::wstring(argv[2]) == L"--list") return RunList(argc, argv);
    if (std::wstring(argv[2]) == L"--search") return RunSearch(argc, argv);
    if (std::wstring(argv[2]) == L"--item") return RunItem(argc, argv);
    std::wcerr << L"Unknown get operation: " << argv[2] << L'\n';
    return 1;
}

int RunHistoryMutationCli(int argc, wchar_t** argv) {
    const std::wstring operationText = argc > 2 ? argv[2] : L"";
    HistoryMutationOperation operation{};
    const bool clear = operationText == L"--clear";
    if (operationText == L"--delete") operation = HistoryMutationOperation::Delete;
    else if (operationText == L"--pin") operation = HistoryMutationOperation::Pin;
    else if (operationText == L"--unpin") operation = HistoryMutationOperation::Unpin;
    else if (clear) operation = HistoryMutationOperation::Clear;
    else return 1;

    std::string requestedProfile;
    std::vector<std::wstring> positional;
    for (int i = 3; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--profile") {
            if (++i >= argc) {
                std::wcerr << L"--profile requires an ID or name.\n";
                return 1;
            }
            requestedProfile = Utf8(argv[i]);
        } else {
            positional.emplace_back(argv[i]);
        }
    }
    if ((clear && !positional.empty()) || (!clear && positional.size() != 1)) {
        std::wcerr << L"Usage: clipboardpp set " << operationText
                   << (clear ? L" [--profile <id-or-name>]\n"
                             : L" <n> [--profile <id-or-name>]\n");
        return 1;
    }

    size_t itemNumber = 0;
    if (!clear && !ParseItemNumber(positional.front(), itemNumber)) {
        std::wcerr << L"History item number must be between 1 and "
                   << kMaxClipboardHistoryItems << L".\n";
        return 1;
    }

    cli_storage::ProfileContext context;
    ClipboardHistory history(kMaxClipboardHistoryItems);
    if (!LoadHistory(requestedProfile, context, history)) return 1;
    const auto items = history.Snapshot();
    if (!clear && itemNumber > items.size()) {
        std::wcerr << L"History item " << itemNumber << L" does not exist in profile "
                   << Wide(context.profile.name) << L".\n";
        return 1;
    }
    const uint64_t itemId = clear ? 0 : items[itemNumber - 1].id;

    const bool applied = ipc::SendHistoryMutation(
        operation, itemId, context.profile.id);
    if (!applied) {
        std::wcerr << L"Could not apply the history mutation. The tray app may not be "
                      L"running, or the item may have changed.\n";
        return 1;
    }

    if (clear) {
        std::wcout << L"Cleared " << items.size() << L" active history item"
                   << (items.size() == 1 ? L"" : L"s") << L" from "
                   << Wide(context.profile.name) << L". The vault was not changed.\n";
    } else {
        const wchar_t* verb = operation == HistoryMutationOperation::Delete ? L"Deleted" :
                              operation == HistoryMutationOperation::Pin ? L"Pinned" : L"Unpinned";
        std::wcout << verb << L" history item " << itemNumber << L" in "
                   << Wide(context.profile.name) << L".\n";
    }
    return 0;
}
