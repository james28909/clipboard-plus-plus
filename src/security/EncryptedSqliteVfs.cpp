#include "EncryptedSqliteVfs.h"

#include "DpapiProtection.h"

#include <sqlite3.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace EncryptedSqliteVfs {
namespace {

constexpr char kVfsName[] = "clipboardpp-aes256";
constexpr std::array<uint8_t, 8> kKeyMagic = {'C','P','P','V','F','S','K','1'};
constexpr uint32_t kKeyVersion = 1;
constexpr size_t kRawKeySize = 64; // two AES-256 keys for XTS
constexpr int kWalHeaderSize = 32;
constexpr int kWalFrameHeaderSize = 24;
constexpr int kJournalRecordOverhead = 8;

enum class FileDomain : uint8_t {
    None = 0,
    MainDatabase = 1,
    Wal = 2,
    RollbackJournal = 3,
};

void SetError(std::string* error, const std::string& value) {
    if (error) *error = value;
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

bool ReadU32(const std::vector<uint8_t>& bytes, size_t offset, uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return false;
    value = static_cast<uint32_t>(bytes[offset]) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    return true;
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), {});
    return input.good() || input.eof();
}

bool WriteFileAtomically(const std::filesystem::path& path,
                         const std::vector<uint8_t>& bytes) {
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

struct KeyMaterial {
    std::array<uint8_t, kRawKeySize> raw{};
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_KEY_HANDLE dataKey{};
    BCRYPT_KEY_HANDLE tweakKey{};
    std::vector<uint8_t> dataObject;
    std::vector<uint8_t> tweakObject;
    std::mutex cryptoMutex;

    ~KeyMaterial() {
        if (dataKey) BCryptDestroyKey(dataKey);
        if (tweakKey) BCryptDestroyKey(tweakKey);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!dataObject.empty()) SecureZeroMemory(dataObject.data(), dataObject.size());
        if (!tweakObject.empty()) SecureZeroMemory(tweakObject.data(), tweakObject.size());
        SecureZeroMemory(raw.data(), raw.size());
    }

    bool Initialize() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM,
                                        nullptr, 0) < 0)
            return false;
        if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                              sizeof(BCRYPT_CHAIN_MODE_ECB), 0) < 0)
            return false;

        DWORD objectLength = 0;
        DWORD copied = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                              &copied, 0) < 0 || objectLength == 0)
            return false;
        dataObject.resize(objectLength);
        tweakObject.resize(objectLength);
        if (BCryptGenerateSymmetricKey(algorithm, &dataKey,
                dataObject.data(), static_cast<ULONG>(dataObject.size()),
                raw.data(), 32, 0) < 0)
            return false;
        if (BCryptGenerateSymmetricKey(algorithm, &tweakKey,
                tweakObject.data(), static_cast<ULONG>(tweakObject.size()),
                raw.data() + 32, 32, 0) < 0)
            return false;
        return true;
    }

    static void MultiplyAlpha(std::array<uint8_t, 16>& tweak) {
        unsigned carry = 0;
        for (size_t i = 0; i < tweak.size(); ++i) {
            const unsigned next = tweak[i] >> 7;
            tweak[i] = static_cast<uint8_t>((tweak[i] << 1) | carry);
            carry = next;
        }
        if (carry) tweak[0] ^= 0x87;
    }

    bool TransformPage(uint8_t* bytes, bool encrypt,
                       FileDomain domain, uint64_t unitNumber) {
        if (!bytes || !dataKey || !tweakKey) return false;
        std::lock_guard<std::mutex> lock(cryptoMutex);

        std::array<uint8_t, 16> tweakInput{};
        for (size_t i = 0; i < 8; ++i)
            tweakInput[i] = static_cast<uint8_t>(unitNumber >> (i * 8));
        tweakInput[8] = static_cast<uint8_t>(domain);

        std::array<uint8_t, 16> tweak{};
        ULONG outputSize = 0;
        if (BCryptEncrypt(tweakKey, tweakInput.data(), 16, nullptr, nullptr, 0,
                          tweak.data(), 16, &outputSize, 0) < 0 || outputSize != 16)
            return false;

        std::array<std::array<uint8_t, 16>, kPageSize / 16> tweaks{};
        std::vector<uint8_t> transformed(kPageSize);
        for (size_t block = 0; block < tweaks.size(); ++block) {
            tweaks[block] = tweak;
            for (size_t i = 0; i < 16; ++i)
                transformed[block * 16 + i] = bytes[block * 16 + i] ^ tweak[i];
            MultiplyAlpha(tweak);
        }

        ULONG resultSize = 0;
        NTSTATUS status = encrypt
            ? BCryptEncrypt(dataKey, transformed.data(), kPageSize, nullptr,
                            nullptr, 0, transformed.data(), kPageSize,
                            &resultSize, 0)
            : BCryptDecrypt(dataKey, transformed.data(), kPageSize, nullptr,
                            nullptr, 0, transformed.data(), kPageSize,
                            &resultSize, 0);
        if (status < 0 || resultSize != kPageSize) {
            SecureZeroMemory(transformed.data(), transformed.size());
            return false;
        }

        for (size_t block = 0; block < tweaks.size(); ++block)
            for (size_t i = 0; i < 16; ++i)
                bytes[block * 16 + i] = transformed[block * 16 + i] ^ tweaks[block][i];
        SecureZeroMemory(transformed.data(), transformed.size());
        SecureZeroMemory(tweaks.data(), sizeof(tweaks));
        return true;
    }
};

std::shared_ptr<KeyMaterial> LoadKey(const std::filesystem::path& databasePath) {
    std::vector<uint8_t> envelope;
    if (!ReadFile(KeyPath(databasePath), envelope) || envelope.size() < 16 ||
        !std::equal(kKeyMagic.begin(), kKeyMagic.end(), envelope.begin()))
        return {};
    uint32_t version = 0;
    uint32_t size = 0;
    if (!ReadU32(envelope, 8, version) || !ReadU32(envelope, 12, size) ||
        version != kKeyVersion || size != envelope.size() - 16)
        return {};
    std::vector<uint8_t> protectedBytes(envelope.begin() + 16, envelope.end());
    std::vector<uint8_t> raw;
    if (!DpapiProtection::Unprotect(protectedBytes, raw) || raw.size() != kRawKeySize)
        return {};

    auto key = std::make_shared<KeyMaterial>();
    std::copy(raw.begin(), raw.end(), key->raw.begin());
    SecureZeroMemory(raw.data(), raw.size());
    if (!key->Initialize()) return {};
    return key;
}

struct Segment {
    bool encrypted{};
    sqlite3_int64 start{};
    sqlite3_int64 end{};
    uint64_t unitNumber{};
};

Segment SegmentAt(FileDomain domain, sqlite3_int64 offset, int journalHeaderSize) {
    if (domain == FileDomain::MainDatabase) {
        const sqlite3_int64 start = (offset / kPageSize) * kPageSize;
        return {true, start, start + kPageSize,
                static_cast<uint64_t>(start / kPageSize)};
    }
    if (domain == FileDomain::Wal) {
        if (offset < kWalHeaderSize)
            return {false, 0, kWalHeaderSize, 0};
        constexpr sqlite3_int64 frameSize = kWalFrameHeaderSize + kPageSize;
        const sqlite3_int64 relative = offset - kWalHeaderSize;
        const sqlite3_int64 frame = relative / frameSize;
        const sqlite3_int64 frameStart = kWalHeaderSize + frame * frameSize;
        if (offset < frameStart + kWalFrameHeaderSize)
            return {false, frameStart, frameStart + kWalFrameHeaderSize, 0};
        const sqlite3_int64 pageStart = frameStart + kWalFrameHeaderSize;
        return {true, pageStart, pageStart + kPageSize,
                static_cast<uint64_t>(frame)};
    }
    if (domain == FileDomain::RollbackJournal) {
        if (offset < journalHeaderSize)
            return {false, 0, journalHeaderSize, 0};
        constexpr sqlite3_int64 recordSize = kPageSize + kJournalRecordOverhead;
        const sqlite3_int64 relative = offset - journalHeaderSize;
        const sqlite3_int64 record = relative / recordSize;
        const sqlite3_int64 recordStart = journalHeaderSize + record * recordSize;
        if (offset < recordStart + 4)
            return {false, recordStart, recordStart + 4, 0};
        if (offset < recordStart + 4 + kPageSize)
            return {true, recordStart + 4, recordStart + 4 + kPageSize,
                    static_cast<uint64_t>(record)};
        return {false, recordStart + 4 + kPageSize, recordStart + recordSize, 0};
    }
    return {false, offset, offset + 1, 0};
}

struct EncryptedFile {
    sqlite3_file base{};
    sqlite3_file* real{};
    std::shared_ptr<KeyMaterial> key;
    FileDomain domain{FileDomain::None};
    int journalHeaderSize{512};
};

constexpr size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}
constexpr size_t kRealFileOffset = AlignUp(sizeof(EncryptedFile), alignof(std::max_align_t));

EncryptedFile* AsEncrypted(sqlite3_file* file) {
    return reinterpret_cast<EncryptedFile*>(file);
}
sqlite3_file* RealFile(sqlite3_file* file) {
    return AsEncrypted(file)->real;
}

int RawRead(EncryptedFile* file, void* output, int amount, sqlite3_int64 offset) {
    return file->real->pMethods->xRead(file->real, output, amount, offset);
}
int RawWrite(EncryptedFile* file, const void* input, int amount, sqlite3_int64 offset) {
    return file->real->pMethods->xWrite(file->real, input, amount, offset);
}

int IoClose(sqlite3_file* file) {
    EncryptedFile* wrapped = AsEncrypted(file);
    const int result = wrapped->real && wrapped->real->pMethods
        ? wrapped->real->pMethods->xClose(wrapped->real) : SQLITE_OK;
    wrapped->~EncryptedFile();
    return result;
}

int IoRead(sqlite3_file* file, void* output, int amount, sqlite3_int64 offset) {
    EncryptedFile* wrapped = AsEncrypted(file);
    if (wrapped->domain == FileDomain::None)
        return RawRead(wrapped, output, amount, offset);

    auto* destination = static_cast<uint8_t*>(output);
    std::memset(destination, 0, static_cast<size_t>(amount));
    sqlite3_int64 fileSize = 0;
    int result = wrapped->real->pMethods->xFileSize(wrapped->real, &fileSize);
    if (result != SQLITE_OK) return result;
    if (offset >= fileSize) return SQLITE_IOERR_SHORT_READ;
    const sqlite3_int64 available = std::min<sqlite3_int64>(amount, fileSize - offset);

    sqlite3_int64 done = 0;
    while (done < available) {
        const sqlite3_int64 position = offset + done;
        const Segment segment = SegmentAt(wrapped->domain, position,
                                          wrapped->journalHeaderSize);
        const sqlite3_int64 count = std::min(segment.end - position, available - done);
        if (!segment.encrypted) {
            result = RawRead(wrapped, destination + done, static_cast<int>(count), position);
            if (result != SQLITE_OK) return result;
        } else {
            if (segment.end > fileSize) return SQLITE_IOERR_SHORT_READ;
            std::array<uint8_t, kPageSize> page{};
            result = RawRead(wrapped, page.data(), kPageSize, segment.start);
            if (result != SQLITE_OK ||
                !wrapped->key->TransformPage(page.data(), false,
                                             wrapped->domain, segment.unitNumber))
                return SQLITE_IOERR_READ;
            std::memcpy(destination + done,
                        page.data() + (position - segment.start),
                        static_cast<size_t>(count));
            SecureZeroMemory(page.data(), page.size());
        }
        done += count;
    }
    return available == amount ? SQLITE_OK : SQLITE_IOERR_SHORT_READ;
}

int IoWrite(sqlite3_file* file, const void* input, int amount, sqlite3_int64 offset) {
    EncryptedFile* wrapped = AsEncrypted(file);
    if (wrapped->domain == FileDomain::None)
        return RawWrite(wrapped, input, amount, offset);

    const auto* source = static_cast<const uint8_t*>(input);
    sqlite3_int64 fileSize = 0;
    int result = wrapped->real->pMethods->xFileSize(wrapped->real, &fileSize);
    if (result != SQLITE_OK) return result;

    // The rollback-journal header records the actual sector size as a big-endian
    // integer at offset 20. SQLite pads the header to exactly this boundary.
    if (wrapped->domain == FileDomain::RollbackJournal &&
        offset <= 20 && offset + amount >= 24) {
        const auto* header = source + (20 - offset);
        const uint32_t sectorSize = (static_cast<uint32_t>(header[0]) << 24) |
                                    (static_cast<uint32_t>(header[1]) << 16) |
                                    (static_cast<uint32_t>(header[2]) << 8) |
                                     static_cast<uint32_t>(header[3]);
        if (sectorSize >= 512 && sectorSize <= 65536)
            wrapped->journalHeaderSize = static_cast<int>(sectorSize);
    }

    sqlite3_int64 done = 0;
    while (done < amount) {
        const sqlite3_int64 position = offset + done;
        const Segment segment = SegmentAt(wrapped->domain, position,
                                          wrapped->journalHeaderSize);
        const sqlite3_int64 count = std::min<sqlite3_int64>(segment.end - position,
                                                            amount - done);
        if (!segment.encrypted) {
            result = RawWrite(wrapped, source + done, static_cast<int>(count), position);
            if (result != SQLITE_OK) return result;
            fileSize = std::max(fileSize, position + count);
        } else {
            std::array<uint8_t, kPageSize> page{};
            const bool completeWrite = position == segment.start && count == kPageSize;
            if (!completeWrite) {
                if (segment.end > fileSize) return SQLITE_IOERR_WRITE;
                result = RawRead(wrapped, page.data(), kPageSize, segment.start);
                if (result != SQLITE_OK ||
                    !wrapped->key->TransformPage(page.data(), false,
                                                 wrapped->domain, segment.unitNumber))
                    return SQLITE_IOERR_WRITE;
            }
            std::memcpy(page.data() + (position - segment.start), source + done,
                        static_cast<size_t>(count));
            if (!wrapped->key->TransformPage(page.data(), true,
                                             wrapped->domain, segment.unitNumber))
                return SQLITE_IOERR_WRITE;
            result = RawWrite(wrapped, page.data(), kPageSize, segment.start);
            SecureZeroMemory(page.data(), page.size());
            if (result != SQLITE_OK) return result;
            fileSize = std::max(fileSize, segment.end);
        }
        done += count;
    }
    return SQLITE_OK;
}

int IoTruncate(sqlite3_file* f, sqlite3_int64 size) { return RealFile(f)->pMethods->xTruncate(RealFile(f), size); }
int IoSync(sqlite3_file* f, int flags) { return RealFile(f)->pMethods->xSync(RealFile(f), flags); }
int IoFileSize(sqlite3_file* f, sqlite3_int64* size) { return RealFile(f)->pMethods->xFileSize(RealFile(f), size); }
int IoLock(sqlite3_file* f, int lock) { return RealFile(f)->pMethods->xLock(RealFile(f), lock); }
int IoUnlock(sqlite3_file* f, int lock) { return RealFile(f)->pMethods->xUnlock(RealFile(f), lock); }
int IoCheckReservedLock(sqlite3_file* f, int* out) { return RealFile(f)->pMethods->xCheckReservedLock(RealFile(f), out); }
int IoFileControl(sqlite3_file* f, int op, void* arg) { return RealFile(f)->pMethods->xFileControl(RealFile(f), op, arg); }
int IoSectorSize(sqlite3_file* f) { return RealFile(f)->pMethods->xSectorSize(RealFile(f)); }
int IoDeviceCharacteristics(sqlite3_file* f) { return RealFile(f)->pMethods->xDeviceCharacteristics(RealFile(f)); }
int IoShmMap(sqlite3_file* f, int page, int pageSize, int extend, void volatile** out) {
    auto* methods = RealFile(f)->pMethods;
    return methods->iVersion >= 2 && methods->xShmMap
        ? methods->xShmMap(RealFile(f), page, pageSize, extend, out) : SQLITE_IOERR_SHMMAP;
}
int IoShmLock(sqlite3_file* f, int offset, int count, int flags) {
    auto* methods = RealFile(f)->pMethods;
    return methods->iVersion >= 2 && methods->xShmLock
        ? methods->xShmLock(RealFile(f), offset, count, flags) : SQLITE_IOERR_SHMLOCK;
}
void IoShmBarrier(sqlite3_file* f) {
    auto* methods = RealFile(f)->pMethods;
    if (methods->iVersion >= 2 && methods->xShmBarrier) methods->xShmBarrier(RealFile(f));
}
int IoShmUnmap(sqlite3_file* f, int deleteFlag) {
    auto* methods = RealFile(f)->pMethods;
    return methods->iVersion >= 2 && methods->xShmUnmap
        ? methods->xShmUnmap(RealFile(f), deleteFlag) : SQLITE_OK;
}
int IoFetch(sqlite3_file*, sqlite3_int64, int, void** out) { *out = nullptr; return SQLITE_OK; }
int IoUnfetch(sqlite3_file*, sqlite3_int64, void*) { return SQLITE_OK; }

const sqlite3_io_methods kIoMethods = {
    3, IoClose, IoRead, IoWrite, IoTruncate, IoSync, IoFileSize,
    IoLock, IoUnlock, IoCheckReservedLock, IoFileControl, IoSectorSize,
    IoDeviceCharacteristics, IoShmMap, IoShmLock, IoShmBarrier, IoShmUnmap,
    IoFetch, IoUnfetch
};

FileDomain DomainFromFlags(int flags) {
    if (flags & SQLITE_OPEN_MAIN_DB) return FileDomain::MainDatabase;
    if (flags & SQLITE_OPEN_WAL) return FileDomain::Wal;
    if (flags & SQLITE_OPEN_MAIN_JOURNAL) return FileDomain::RollbackJournal;
    return FileDomain::None;
}

sqlite3_vfs g_vfs{};
std::once_flag g_registerOnce;
int g_registerResult = SQLITE_ERROR;

sqlite3_vfs* Underlying(sqlite3_vfs* vfs) {
    return static_cast<sqlite3_vfs*>(vfs->pAppData);
}

int VfsOpen(sqlite3_vfs* vfs, const char* name, sqlite3_file* output,
            int flags, int* outFlags) {
    std::memset(output, 0, static_cast<size_t>(vfs->szOsFile));
    auto* wrapped = new(output) EncryptedFile();
    wrapped->real = reinterpret_cast<sqlite3_file*>(
        reinterpret_cast<uint8_t*>(output) + kRealFileOffset);
    sqlite3_vfs* underlying = Underlying(vfs);
    int result = underlying->xOpen(underlying, name, wrapped->real, flags, outFlags);
    if (result != SQLITE_OK) {
        wrapped->~EncryptedFile();
        return result;
    }

    wrapped->domain = DomainFromFlags(flags);
    if (wrapped->domain == FileDomain::RollbackJournal) {
        const int sectorSize = wrapped->real->pMethods->xSectorSize(wrapped->real);
        if (sectorSize >= 512 && sectorSize <= 65536)
            wrapped->journalHeaderSize = sectorSize;
    }
    if (wrapped->domain == FileDomain::MainDatabase) {
        if (!name) result = SQLITE_CANTOPEN;
        else wrapped->key = LoadKey(std::filesystem::u8path(name));
        if (!wrapped->key) result = SQLITE_CANTOPEN;
    } else if (wrapped->domain == FileDomain::Wal ||
               wrapped->domain == FileDomain::RollbackJournal) {
        sqlite3_file* mainFile = name ? sqlite3_database_file_object(name) : nullptr;
        if (!mainFile || mainFile->pMethods != &kIoMethods)
            result = SQLITE_CANTOPEN;
        else
            wrapped->key = AsEncrypted(mainFile)->key;
    }

    if (result != SQLITE_OK) {
        if (wrapped->real->pMethods) wrapped->real->pMethods->xClose(wrapped->real);
        wrapped->~EncryptedFile();
        output->pMethods = nullptr;
        return result;
    }
    output->pMethods = &kIoMethods;
    return SQLITE_OK;
}

int VfsDelete(sqlite3_vfs* v, const char* z, int sync) { auto* u=Underlying(v); return u->xDelete(u,z,sync); }
int VfsAccess(sqlite3_vfs* v, const char* z, int f, int* out) { auto* u=Underlying(v); return u->xAccess(u,z,f,out); }
int VfsFullPathname(sqlite3_vfs* v, const char* z, int n, char* out) { auto* u=Underlying(v); return u->xFullPathname(u,z,n,out); }
void* VfsDlOpen(sqlite3_vfs* v, const char* z) { auto* u=Underlying(v); return u->xDlOpen(u,z); }
void VfsDlError(sqlite3_vfs* v, int n, char* out) { auto* u=Underlying(v); u->xDlError(u,n,out); }
void (*VfsDlSym(sqlite3_vfs* v, void* p, const char* z))(void) { auto* u=Underlying(v); return u->xDlSym(u,p,z); }
void VfsDlClose(sqlite3_vfs* v, void* p) { auto* u=Underlying(v); u->xDlClose(u,p); }
int VfsRandomness(sqlite3_vfs* v, int n, char* out) { auto* u=Underlying(v); return u->xRandomness(u,n,out); }
int VfsSleep(sqlite3_vfs* v, int us) { auto* u=Underlying(v); return u->xSleep(u,us); }
int VfsCurrentTime(sqlite3_vfs* v, double* out) { auto* u=Underlying(v); return u->xCurrentTime(u,out); }
int VfsGetLastError(sqlite3_vfs* v, int a, char* b) { auto* u=Underlying(v); return u->xGetLastError ? u->xGetLastError(u,a,b) : 0; }
int VfsCurrentTimeInt64(sqlite3_vfs* v, sqlite3_int64* out) { auto* u=Underlying(v); return u->iVersion>=2 && u->xCurrentTimeInt64 ? u->xCurrentTimeInt64(u,out) : SQLITE_OK; }
int VfsSetSystemCall(sqlite3_vfs* v, const char* z, sqlite3_syscall_ptr p) { auto* u=Underlying(v); return u->iVersion>=3 && u->xSetSystemCall ? u->xSetSystemCall(u,z,p) : SQLITE_NOTFOUND; }
sqlite3_syscall_ptr VfsGetSystemCall(sqlite3_vfs* v, const char* z) { auto* u=Underlying(v); return u->iVersion>=3 && u->xGetSystemCall ? u->xGetSystemCall(u,z) : nullptr; }
const char* VfsNextSystemCall(sqlite3_vfs* v, const char* z) { auto* u=Underlying(v); return u->iVersion>=3 && u->xNextSystemCall ? u->xNextSystemCall(u,z) : nullptr; }

bool Exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool IntegrityCheck(sqlite3* db) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    const bool ok = sqlite3_step(statement) == SQLITE_ROW &&
        sqlite3_column_text(statement, 0) &&
        std::strcmp(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)), "ok") == 0;
    sqlite3_finalize(statement);
    return ok;
}

void RemoveSidecars(const std::filesystem::path& path) {
    std::error_code ec;
    for (const wchar_t* suffix : {L"-wal", L"-shm", L"-journal"}) {
        std::filesystem::path sidecar = path;
        sidecar += suffix;
        std::filesystem::remove(sidecar, ec);
        ec.clear();
    }
}

} // namespace

const char* Name() { return kVfsName; }

bool Register(std::string* error) {
    std::call_once(g_registerOnce, [] {
        sqlite3_vfs* underlying = sqlite3_vfs_find(nullptr);
        if (!underlying) { g_registerResult = SQLITE_NOTFOUND; return; }
        g_vfs.iVersion = std::min(underlying->iVersion, 3);
        g_vfs.szOsFile = static_cast<int>(kRealFileOffset + underlying->szOsFile);
        g_vfs.mxPathname = underlying->mxPathname;
        g_vfs.zName = kVfsName;
        g_vfs.pAppData = underlying;
        g_vfs.xOpen = VfsOpen;
        g_vfs.xDelete = VfsDelete;
        g_vfs.xAccess = VfsAccess;
        g_vfs.xFullPathname = VfsFullPathname;
        g_vfs.xDlOpen = VfsDlOpen;
        g_vfs.xDlError = VfsDlError;
        g_vfs.xDlSym = VfsDlSym;
        g_vfs.xDlClose = VfsDlClose;
        g_vfs.xRandomness = VfsRandomness;
        g_vfs.xSleep = VfsSleep;
        g_vfs.xCurrentTime = VfsCurrentTime;
        g_vfs.xGetLastError = VfsGetLastError;
        g_vfs.xCurrentTimeInt64 = VfsCurrentTimeInt64;
        g_vfs.xSetSystemCall = VfsSetSystemCall;
        g_vfs.xGetSystemCall = VfsGetSystemCall;
        g_vfs.xNextSystemCall = VfsNextSystemCall;
        g_registerResult = sqlite3_vfs_register(&g_vfs, 0);
    });
    if (g_registerResult != SQLITE_OK) {
        SetError(error, "Could not register encrypted SQLite VFS (" +
                        std::to_string(g_registerResult) + ")");
        return false;
    }
    return true;
}

std::filesystem::path KeyPath(const std::filesystem::path& databasePath) {
    std::filesystem::path path = databasePath;
    path += L".key";
    return path;
}

bool HasKey(const std::filesystem::path& databasePath) {
    std::error_code ec;
    return std::filesystem::is_regular_file(KeyPath(databasePath), ec);
}

bool CreateKey(const std::filesystem::path& databasePath, std::string* error) {
    if (HasKey(databasePath)) return true;
    std::array<uint8_t, kRawKeySize> raw{};
    if (BCryptGenRandom(nullptr, raw.data(), static_cast<ULONG>(raw.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        SetError(error, "Could not generate the SQLite encryption key");
        return false;
    }
    std::vector<uint8_t> plaintext(raw.begin(), raw.end());
    std::vector<uint8_t> protectedBytes;
    const bool protectedOk = DpapiProtection::Protect(plaintext, protectedBytes);
    SecureZeroMemory(raw.data(), raw.size());
    SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!protectedOk) {
        SetError(error, "Could not protect the SQLite encryption key with DPAPI");
        return false;
    }
    std::vector<uint8_t> envelope(kKeyMagic.begin(), kKeyMagic.end());
    AppendU32(envelope, kKeyVersion);
    AppendU32(envelope, static_cast<uint32_t>(protectedBytes.size()));
    envelope.insert(envelope.end(), protectedBytes.begin(), protectedBytes.end());
    if (!WriteFileAtomically(KeyPath(databasePath), envelope)) {
        SetError(error, "Could not write the SQLite encryption key file");
        return false;
    }
    return true;
}

bool RemoveKey(const std::filesystem::path& databasePath) {
    std::error_code ec;
    return std::filesystem::remove(KeyPath(databasePath), ec) || !ec;
}

int Open(const std::filesystem::path& databasePath, sqlite3** database,
         int flags, std::string* error) {
    if (!database) return SQLITE_MISUSE;
    *database = nullptr;
    if (!Register(error)) return SQLITE_CANTOPEN;
    const int result = sqlite3_open_v2(databasePath.u8string().c_str(), database,
                                       flags, Name());
    if (result != SQLITE_OK) {
        const std::string message = *database && sqlite3_errmsg(*database)
            ? sqlite3_errmsg(*database) : "unknown SQLite open error";
        SetError(error, message);
    }
    return result;
}

bool MigratePlaintextDatabase(const std::filesystem::path& databasePath,
                              std::string* error) {
    if (!Register(error)) return false;
    std::error_code ec;
    std::filesystem::path temporary = databasePath;
    temporary += L".encrypted-migration";
    std::filesystem::path backup = databasePath;
    backup += L".plaintext-migration-backup";

    // Recover the only destructive migration window: the plaintext source has
    // been renamed to its rollback path but the encrypted database/key pair was
    // not completely installed or verified before the process stopped.
    if (std::filesystem::exists(backup, ec)) {
        bool installedValid = false;
        if (HasKey(databasePath) && std::filesystem::exists(databasePath, ec)) {
            sqlite3* verify = nullptr;
            installedValid = Open(
                databasePath, &verify,
                SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) == SQLITE_OK &&
                IntegrityCheck(verify);
            if (verify) sqlite3_close(verify);
        }
        if (installedValid) {
            std::filesystem::remove(backup, ec);
            std::filesystem::remove(temporary, ec);
            RemoveKey(temporary);
            return true;
        }

        RemoveSidecars(databasePath);
        std::filesystem::remove(databasePath, ec);
        RemoveKey(databasePath);
        if (!MoveFileExW(backup.c_str(), databasePath.c_str(),
                         MOVEFILE_WRITE_THROUGH)) {
            SetError(error, "Could not recover the interrupted plaintext migration rollback");
            return false;
        }
        std::filesystem::remove(temporary, ec);
        RemoveKey(temporary);
    }

    if (HasKey(databasePath)) return true;
    if (!std::filesystem::exists(databasePath, ec) ||
        std::filesystem::file_size(databasePath, ec) == 0)
        return CreateKey(databasePath, error);

    sqlite3* source = nullptr;
    if (sqlite3_open_v2(databasePath.u8string().c_str(), &source,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        SetError(error, source ? sqlite3_errmsg(source) : "Could not open plaintext database");
        if (source) sqlite3_close(source);
        return false;
    }
    if (!Exec(source, "PRAGMA wal_checkpoint(TRUNCATE);") ||
        !Exec(source, "PRAGMA journal_mode=DELETE;")) {
        SetError(error, "Could not checkpoint the plaintext database before migration");
        sqlite3_close(source);
        return false;
    }

    std::filesystem::remove(temporary, ec);
    RemoveKey(temporary);
    std::filesystem::remove(backup, ec);
    if (!CreateKey(temporary, error)) { sqlite3_close(source); return false; }

    sqlite3* destination = nullptr;
    if (Open(temporary, &destination,
             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
             error) != SQLITE_OK) {
        sqlite3_close(source);
        RemoveKey(temporary);
        return false;
    }
    Exec(destination, "PRAGMA page_size=4096;");
    // The destination is a private throw-away file until the copy has passed
    // integrity verification and is atomically installed. Avoid generating a
    // potentially very large rollback journal while sqlite3_backup populates it.
    // Normal application opens select WAL mode after migration.
    Exec(destination, "PRAGMA journal_mode=OFF;");
    sqlite3_backup* backupHandle = sqlite3_backup_init(destination, "main", source, "main");
    const int backupStep = backupHandle ? sqlite3_backup_step(backupHandle, -1) : SQLITE_ERROR;
    const int backupFinish = backupHandle ? sqlite3_backup_finish(backupHandle) : SQLITE_ERROR;
    const bool integrityOk = backupStep == SQLITE_DONE &&
                             backupFinish == SQLITE_OK && IntegrityCheck(destination);
    bool ok = integrityOk;
    sqlite3_close(destination);
    sqlite3_close(source);
    if (!ok) {
        SetError(error, "Encrypted SQLite migration copy failed (step=" +
                        std::to_string(backupStep) + ", finish=" +
                        std::to_string(backupFinish) + ")");
        std::filesystem::remove(temporary, ec);
        RemoveKey(temporary);
        return false;
    }

    RemoveSidecars(databasePath);
    if (!MoveFileExW(databasePath.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH)) {
        SetError(error, "Could not create the plaintext migration rollback file");
        return false;
    }
    const bool movedDatabase = MoveFileExW(temporary.c_str(), databasePath.c_str(),
                                           MOVEFILE_WRITE_THROUGH) != FALSE;
    const bool movedKey = movedDatabase &&
        MoveFileExW(KeyPath(temporary).c_str(), KeyPath(databasePath).c_str(),
                    MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!movedDatabase || !movedKey) {
        std::filesystem::remove(databasePath, ec);
        RemoveKey(databasePath);
        MoveFileExW(backup.c_str(), databasePath.c_str(), MOVEFILE_WRITE_THROUGH);
        SetError(error, "Could not install the encrypted database; plaintext database restored");
        return false;
    }

    sqlite3* verify = nullptr;
    ok = Open(databasePath, &verify, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
              error) == SQLITE_OK && IntegrityCheck(verify);
    if (verify) sqlite3_close(verify);
    if (!ok) {
        std::filesystem::remove(databasePath, ec);
        RemoveKey(databasePath);
        MoveFileExW(backup.c_str(), databasePath.c_str(), MOVEFILE_WRITE_THROUGH);
        SetError(error, "Encrypted database failed final verification; plaintext database restored");
        return false;
    }
    std::filesystem::remove(backup, ec);
    return true;
}

bool BackupEncryptedDatabase(const std::filesystem::path& sourcePath,
                             const std::filesystem::path& destinationPath,
                             std::string* error) {
    std::error_code ec;
    if (!HasKey(sourcePath) || !std::filesystem::exists(sourcePath, ec)) {
        SetError(error, "Encrypted SQLite backup source does not exist");
        return false;
    }
    if (std::filesystem::exists(destinationPath, ec) ||
        HasKey(destinationPath)) {
        SetError(error, "Encrypted SQLite backup destination already exists");
        return false;
    }
    std::filesystem::create_directories(destinationPath.parent_path(), ec);
    if (ec || !CreateKey(destinationPath, error))
        return false;

    sqlite3* source = nullptr;
    sqlite3* destination = nullptr;
    bool ok = Open(sourcePath, &source,
                   SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                   error) == SQLITE_OK &&
              Open(destinationPath, &destination,
                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                   SQLITE_OPEN_FULLMUTEX, error) == SQLITE_OK;
    int backupStep = SQLITE_ERROR;
    int backupFinish = SQLITE_ERROR;
    if (ok) {
        Exec(destination, "PRAGMA journal_mode=OFF;");
        sqlite3_backup* handle = sqlite3_backup_init(
            destination, "main", source, "main");
        backupStep = handle ? sqlite3_backup_step(handle, -1) : SQLITE_ERROR;
        backupFinish = handle ? sqlite3_backup_finish(handle) : SQLITE_ERROR;
        ok = backupStep == SQLITE_DONE && backupFinish == SQLITE_OK &&
             IntegrityCheck(destination);
    }
    if (destination) sqlite3_close(destination);
    if (source) sqlite3_close(source);
    if (!ok) {
        std::filesystem::remove(destinationPath, ec);
        RemoveSidecars(destinationPath);
        RemoveKey(destinationPath);
        SetError(error, "Encrypted SQLite backup failed (step=" +
                        std::to_string(backupStep) + ", finish=" +
                        std::to_string(backupFinish) + ")");
        return false;
    }
    return true;
}

} // namespace EncryptedSqliteVfs
