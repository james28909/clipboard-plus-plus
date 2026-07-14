#include "VaultCli.h"
#include "CliStorage.h"

#include "../clipboard/ImageStore.h"
#include "../security/EncryptedSqliteVfs.h"
#include "../util/Win32Util.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

std::string Utf8(const std::wstring& value) {
    const std::string converted = win32util::WideToUtf8(
        value.c_str(), static_cast<int>(value.size()));
    return converted;
}

std::wstring Wide(const std::string& value) {
    return win32util::Utf8ToWide(value);
}

int64_t TimeMs(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
}

std::string Base64(const std::vector<uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = bytes[i];
        const uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

const char* StoredFormatName(StoredFormat format) {
    switch (format) {
    case StoredFormat::RawDib: return "dib";
    case StoredFormat::Jpeg: return "jpeg";
    default: return "png";
    }
}

const char* ExtensionFor(const ClipboardItem& item, StoredFormat format) {
    if (item.IsImage()) {
        switch (format) {
        case StoredFormat::RawDib: return ".dib";
        case StoredFormat::Jpeg: return ".jpg";
        default: return ".png";
        }
    }
    switch (item.type) {
    case ContentType::Html: return ".html";
    case ContentType::RichText: return ".rtf";
    case ContentType::FilePaths: return ".paths.txt";
    default: return ".txt";
    }
}

json ItemMetadata(const ClipboardVaultEntry& entry) {
    const ClipboardItem& item = entry.item;
    return {
        {"archiveId", entry.archiveId},
        {"archivedAt", entry.archivedAtMs},
        {"itemId", item.id},
        {"contentHash", item.contentHash},
        {"type", ContentTypeName(item.type)},
        {"tags", item.tags},
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
}

using VaultContext = cli_storage::ProfileContext;

bool OpenContext(const std::string& requestedProfile, VaultContext& context) {
    std::wstring error;
    if (!cli_storage::OpenProfile(requestedProfile, context, error)) {
        std::wcerr << error << L'\n';
        return false;
    }
    return true;
}

struct CommonOptions {
    std::string profile;
    std::string format{"text"};
    size_t limit{250};
};

bool ParseCommon(int argc, wchar_t** argv, int start, CommonOptions& options,
                 std::vector<std::wstring>* positional = nullptr) {
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
                options.limit = static_cast<size_t>(std::clamp(
                    std::stoll(argv[i]), int64_t{1}, int64_t{1000000}));
            } catch (...) { return false; }
        } else if (positional) {
            positional->push_back(argument);
        } else {
            return false;
        }
    }
    return true;
}

int RunCount(int argc, wchar_t** argv) {
    CommonOptions options;
    if (!ParseCommon(argc, argv, 3, options) ||
        (options.format != "text" && options.format != "json")) {
        std::wcerr << L"Usage: clipboardpp vault count [--profile <id-or-name>] "
                      L"[--format text|json]\n";
        return 1;
    }
    VaultContext context;
    if (!OpenContext(options.profile, context)) return 1;
    size_t count = 0;
    if (!context.database.VaultCount(context.profile.id, count)) return 1;
    if (options.format == "json") {
        std::cout << json{{"profileId", context.profile.id},
                          {"profileName", context.profile.name},
                          {"count", count}}.dump(2) << '\n';
    } else {
        std::wcout << Wide(context.profile.name) << L": " << count << L" archived item"
                   << (count == 1 ? L"\n" : L"s\n");
    }
    return 0;
}

int RunSearch(int argc, wchar_t** argv) {
    CommonOptions options;
    std::vector<std::wstring> positional;
    if (!ParseCommon(argc, argv, 3, options, &positional) || positional.empty() ||
        (options.format != "text" && options.format != "json")) {
        std::wcerr << L"Usage: clipboardpp vault search <query> [--profile <id-or-name>] "
                      L"[--limit N] [--format text|json]\n";
        return 1;
    }
    std::wstring wideQuery;
    for (const auto& part : positional) {
        if (!wideQuery.empty()) wideQuery += L" ";
        wideQuery += part;
    }
    VaultContext context;
    if (!OpenContext(options.profile, context)) return 1;
    std::vector<ClipboardVaultEntry> entries;
    if (!context.database.SearchVault(
            context.profile.id, Utf8(wideQuery), entries, options.limit))
        return 1;
    if (options.format == "json") {
        json output = json::array();
        for (const auto& entry : entries) {
            json item = ItemMetadata(entry);
            item["preview"] = entry.item.Preview(160);
            output.push_back(std::move(item));
        }
        std::cout << output.dump(2) << '\n';
    } else {
        for (const auto& entry : entries)
            std::wcout << entry.archiveId << L"\t"
                       << Wide(ContentTypeName(entry.item.type)) << L"\t"
                       << Wide(entry.item.Preview(160)) << L"\n";
    }
    return 0;
}

struct ExportRecord {
    ClipboardVaultEntry entry;
    std::vector<uint8_t> payload;
    StoredFormat imageFormat{StoredFormat::Png};
};

bool LoadExportRecords(VaultContext& context, const std::string& query,
                       std::vector<ExportRecord>& records) {
    std::vector<ClipboardVaultEntry> entries;
    if (!context.database.SearchVault(context.profile.id, query, entries,
                                      std::numeric_limits<size_t>::max()))
        return false;
    ImageStore images;
    bool imagesOpened = false;
    for (auto& entry : entries) {
        ExportRecord record;
        record.entry = std::move(entry);
        if (record.entry.item.IsImage()) {
            if (!imagesOpened) {
                imagesOpened = images.Open(cli_storage::DataDirectory() / "images.db");
                if (!imagesOpened) return false;
            }
            if (!images.GetStoredBytes(record.entry.item.imageStoreId,
                                       record.payload, record.imageFormat))
                return false;
        } else {
            record.payload.assign(record.entry.item.text.begin(),
                                  record.entry.item.text.end());
        }
        records.push_back(std::move(record));
    }
    return true;
}

bool WriteBytes(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

void WriteU32(std::ofstream& output, uint32_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
void WriteU64(std::ofstream& output, uint64_t value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool ExportFiles(const std::filesystem::path& directory,
                 const std::vector<ExportRecord>& records) {
    std::error_code ec;
    if (std::filesystem::exists(directory, ec) &&
        !std::filesystem::is_empty(directory, ec)) return false;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    json manifest = json::array();
    for (size_t i = 0; i < records.size(); ++i) {
        const ExportRecord& record = records[i];
        std::ostringstream name;
        name << std::setfill('0') << std::setw(6) << (i + 1) << "-"
             << record.entry.archiveId
             << ExtensionFor(record.entry.item, record.imageFormat);
        if (!WriteBytes(directory / name.str(), record.payload)) return false;
        json metadata = ItemMetadata(record.entry);
        metadata["file"] = name.str();
        if (record.entry.item.IsImage())
            metadata["storedFormat"] = StoredFormatName(record.imageFormat);
        manifest.push_back(std::move(metadata));
    }
    std::ofstream output(directory / "manifest.json", std::ios::binary);
    output << manifest.dump(2);
    return output.good();
}

bool ExportJson(const std::filesystem::path& path,
                const std::vector<ExportRecord>& records) {
    if (std::filesystem::exists(path)) return false;
    json output = json::array();
    for (const ExportRecord& record : records) {
        json item = ItemMetadata(record.entry);
        if (record.entry.item.IsImage()) {
            item["storedFormat"] = StoredFormatName(record.imageFormat);
            item["dataBase64"] = Base64(record.payload);
        } else {
            item["text"] = record.entry.item.text;
        }
        output.push_back(std::move(item));
    }
    std::ofstream file(path, std::ios::binary);
    file << output.dump(2);
    return file.good();
}

bool ExportBinary(const std::filesystem::path& path,
                  const std::vector<ExportRecord>& records) {
    if (std::filesystem::exists(path)) return false;
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    constexpr char magic[8] = {'C','P','P','V','L','T','1','\0'};
    output.write(magic, sizeof(magic));
    WriteU32(output, 1);
    WriteU64(output, static_cast<uint64_t>(records.size()));
    for (const ExportRecord& record : records) {
        json metadata = ItemMetadata(record.entry);
        if (record.entry.item.IsImage())
            metadata["storedFormat"] = StoredFormatName(record.imageFormat);
        const std::string metadataText = metadata.dump();
        if (metadataText.size() > UINT32_MAX) return false;
        WriteU32(output, static_cast<uint32_t>(metadataText.size()));
        WriteU64(output, static_cast<uint64_t>(record.payload.size()));
        output.write(metadataText.data(), static_cast<std::streamsize>(metadataText.size()));
        output.write(reinterpret_cast<const char*>(record.payload.data()),
                     static_cast<std::streamsize>(record.payload.size()));
    }
    return output.good();
}

int RunExport(int argc, wchar_t** argv) {
    CommonOptions options;
    options.format = "files";
    std::filesystem::path output;
    std::string query;
    for (int i = 3; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--output" && i + 1 < argc) output = argv[++i];
        else if (argument == L"--profile" && i + 1 < argc) options.profile = Utf8(argv[++i]);
        else if (argument == L"--format" && i + 1 < argc) options.format = Utf8(argv[++i]);
        else if (argument == L"--search" && i + 1 < argc) query = Utf8(argv[++i]);
        else {
            std::wcerr << L"Unknown or incomplete vault export option: " << argument << L"\n";
            return 1;
        }
    }
    if (output.empty() || (options.format != "files" && options.format != "json" &&
                           options.format != "binary")) {
        std::wcerr << L"Usage: clipboardpp vault export --output <path> "
                      L"[--format files|json|binary] [--profile <id-or-name>] "
                      L"[--search <query>]\n";
        return 1;
    }
    VaultContext context;
    if (!OpenContext(options.profile, context)) return 1;
    std::vector<ExportRecord> records;
    if (!LoadExportRecords(context, query, records)) {
        std::wcerr << L"Could not read all vault payloads. An image may be missing.\n";
        return 1;
    }
    std::wcerr << L"Warning: this export writes decrypted clipboard content. "
                  L"Protect the destination appropriately.\n";
    bool ok = false;
    if (options.format == "files") ok = ExportFiles(output, records);
    else if (options.format == "json") ok = ExportJson(output, records);
    else ok = ExportBinary(output, records);
    if (!ok) {
        std::wcerr << L"Export failed. The destination may already contain files.\n";
        return 1;
    }
    std::wcout << L"Exported " << records.size() << L" vault item"
               << (records.size() == 1 ? L"" : L"s") << L" to "
               << output.wstring() << L"\n";
    return 0;
}

int RunBackup(int argc, wchar_t** argv) {
    std::filesystem::path output;
    if (argc == 5 && std::wstring(argv[3]) == L"--output") output = argv[4];
    if (output.empty()) {
        std::wcerr << L"Usage: clipboardpp vault backup --output <empty-directory>\n";
        return 1;
    }
    std::error_code ec;
    if (std::filesystem::exists(output, ec) && !std::filesystem::is_empty(output, ec)) {
        std::wcerr << L"Backup destination must be empty.\n";
        return 1;
    }
    std::filesystem::create_directories(output, ec);
    if (ec) return 1;
    std::string error;
    const auto sourceClipboard = cli_storage::DataDirectory() / "clipboard.db";
    if (!EncryptedSqliteVfs::BackupEncryptedDatabase(
            sourceClipboard, output / "clipboard.db", &error)) {
        std::wcerr << L"Clipboard database backup failed: " << Wide(error) << L"\n";
        return 1;
    }
    const auto sourceImages = cli_storage::DataDirectory() / "images.db";
    if (EncryptedSqliteVfs::HasKey(sourceImages) &&
        !EncryptedSqliteVfs::BackupEncryptedDatabase(
            sourceImages, output / "images.db", &error)) {
        std::wcerr << L"Image database backup failed: " << Wide(error) << L"\n";
        return 1;
    }
    std::wcout << L"Created encrypted Clipboard++ backup in " << output.wstring() << L"\n";
    return 0;
}

} // namespace

int RunVaultCli(int argc, wchar_t** argv) {
    if (argc < 3 || std::wstring(argv[2]) == L"--help") {
        std::wcout <<
            L"Vault commands:\n"
            L"  vault count [--profile <id-or-name>] [--format text|json]\n"
            L"  vault search <query> [--profile <id-or-name>] [--limit N] [--format text|json]\n"
            L"  vault export --output <path> [--format files|json|binary] [--profile <id-or-name>] [--search <query>]\n"
            L"  vault backup --output <empty-directory>\n";
        return 0;
    }
    const std::wstring operation = argv[2];
    if (operation == L"count") return RunCount(argc, argv);
    if (operation == L"search") return RunSearch(argc, argv);
    if (operation == L"export") return RunExport(argc, argv);
    if (operation == L"backup") return RunBackup(argc, argv);
    std::wcerr << L"Unknown vault operation: " << operation << L"\n";
    return 1;
}
