#include "IconPatcher.h"
#include <vector>
#include <fstream>
#include <shlobj.h>

// ICO file on-disk layout
#pragma pack(push, 1)
struct IcoDir {
    WORD reserved;
    WORD type;
    WORD count;
};
struct IcoDirEntry {
    BYTE  width;
    BYTE  height;
    BYTE  colorCount;
    BYTE  reserved;
    WORD  planes;
    WORD  bitCount;
    DWORD bytesInRes;
    DWORD imageOffset;
};
struct GrpIconDir {
    WORD reserved;
    WORD type;
    WORD count;
};
struct GrpIconDirEntry {
    BYTE  width;
    BYTE  height;
    BYTE  colorCount;
    BYTE  reserved;
    WORD  planes;
    WORD  bitCount;
    DWORD bytesInRes;
    WORD  id;
};
#pragma pack(pop)

bool IconPatcher::ExtractExeIcon(const std::wstring& exePath, const std::wstring& outIcoPath) {
    HMODULE hMod = LoadLibraryExW(exePath.c_str(), nullptr,
                                   LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!hMod) return false;

    HRSRC hGrpRes = FindResourceW(hMod, MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    if (!hGrpRes) { FreeLibrary(hMod); return false; }
    DWORD grpSize = SizeofResource(hMod, hGrpRes);
    HGLOBAL hGrpData = LoadResource(hMod, hGrpRes);
    if (!hGrpData || grpSize < sizeof(GrpIconDir)) { FreeLibrary(hMod); return false; }
    const auto* grpPtr = static_cast<const char*>(LockResource(hGrpData));
    if (!grpPtr) { FreeLibrary(hMod); return false; }

    const auto* grpDir = reinterpret_cast<const GrpIconDir*>(grpPtr);
    const WORD count = grpDir->count;
    if (grpSize < sizeof(GrpIconDir) + count * sizeof(GrpIconDirEntry)) {
        FreeLibrary(hMod); return false;
    }
    const auto* rawEntries = reinterpret_cast<const GrpIconDirEntry*>(grpPtr + sizeof(GrpIconDir));

    // Copy entries before any chance of FreeLibrary invalidating the pointer
    std::vector<GrpIconDirEntry> entries(rawEntries, rawEntries + count);

    // Read each RT_ICON image while module is still loaded
    std::vector<std::vector<char>> images(count);
    for (WORD i = 0; i < count; ++i) {
        HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(entries[i].id), RT_ICON);
        if (!hRes) { FreeLibrary(hMod); return false; }
        DWORD sz = SizeofResource(hMod, hRes);
        HGLOBAL hData = LoadResource(hMod, hRes);
        const void* ptr = hData ? LockResource(hData) : nullptr;
        if (!ptr || sz == 0) { FreeLibrary(hMod); return false; }
        images[i].resize(sz);
        memcpy(images[i].data(), ptr, sz);
    }

    FreeLibrary(hMod);

    // Build .ico file in memory
    size_t totalSize = sizeof(IcoDir) + count * sizeof(IcoDirEntry);
    for (WORD i = 0; i < count; ++i) totalSize += images[i].size();

    std::vector<char> icoFile(totalSize);
    auto* icoDir = reinterpret_cast<IcoDir*>(icoFile.data());
    icoDir->reserved = 0; icoDir->type = 1; icoDir->count = count;

    auto* icoEntries = reinterpret_cast<IcoDirEntry*>(icoFile.data() + sizeof(IcoDir));
    size_t dataOffset = sizeof(IcoDir) + count * sizeof(IcoDirEntry);
    for (WORD i = 0; i < count; ++i) {
        icoEntries[i].width       = entries[i].width;
        icoEntries[i].height      = entries[i].height;
        icoEntries[i].colorCount  = entries[i].colorCount;
        icoEntries[i].reserved    = 0;
        icoEntries[i].planes      = entries[i].planes;
        icoEntries[i].bitCount    = entries[i].bitCount;
        icoEntries[i].bytesInRes  = static_cast<DWORD>(images[i].size());
        icoEntries[i].imageOffset = static_cast<DWORD>(dataOffset);
        memcpy(icoFile.data() + dataOffset, images[i].data(), images[i].size());
        dataOffset += images[i].size();
    }

    FILE* fp = _wfopen(outIcoPath.c_str(), L"wb");
    if (!fp) return false;
    bool ok = fwrite(icoFile.data(), 1, icoFile.size(), fp) == icoFile.size();
    fclose(fp);
    return ok;
}

bool IconPatcher::PatchExeIcon(const std::wstring& exePath, const std::wstring& icoPath,
                               DWORD* outError) {
    auto fail = [&](DWORD err) -> bool {
        if (outError) *outError = err;
        return false;
    };

    // Windows will not allow BeginUpdateResource or MoveFileEx(REPLACE_EXISTING) on a
    // running process's own image - even from a child process running the same exe.
    // Solution:
    //   1. CopyFile exe → AppData temp (writable, no permission issues)
    //   2. Patch the temp copy with UpdateResource (copy was never loaded - always works)
    //   3. Spawn powershell.exe (a different image) to do the final copy+replace after we exit,
    //      then call SHChangeNotify so Explorer refreshes the icon immediately.

    wchar_t appData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
        return fail(GetLastError());
    const std::wstring tmp = std::wstring(appData) + L"\\Clipboard++\\clipboardpp_patch_tmp.exe";

    if (!CopyFileW(exePath.c_str(), tmp.c_str(), FALSE))
        return fail(GetLastError());

    // Read and validate the .ico file
    std::ifstream f(icoPath, std::ios::binary | std::ios::ate);
    if (!f) {
        DWORD err = GetLastError(); DeleteFileW(tmp.c_str()); return fail(err);
    }
    const auto fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<char> ico(fileSize);
    if (!f.read(ico.data(), static_cast<std::streamsize>(fileSize))) {
        DeleteFileW(tmp.c_str()); return fail(ERROR_READ_FAULT);
    }
    f.close();

    if (fileSize < sizeof(IcoDir)) { DeleteFileW(tmp.c_str()); return fail(ERROR_INVALID_DATA); }
    const auto* dir = reinterpret_cast<const IcoDir*>(ico.data());
    if (dir->reserved != 0 || dir->type != 1 || dir->count == 0) {
        DeleteFileW(tmp.c_str()); return fail(ERROR_INVALID_DATA);
    }
    const WORD count = dir->count;
    const size_t entriesEnd = sizeof(IcoDir) + static_cast<size_t>(count) * sizeof(IcoDirEntry);
    if (fileSize < entriesEnd) { DeleteFileW(tmp.c_str()); return fail(ERROR_INVALID_DATA); }
    const auto* entries = reinterpret_cast<const IcoDirEntry*>(ico.data() + sizeof(IcoDir));
    for (WORD i = 0; i < count; ++i) {
        const size_t end = static_cast<size_t>(entries[i].imageOffset) + entries[i].bytesInRes;
        if (end > fileSize || entries[i].bytesInRes == 0) {
            DeleteFileW(tmp.c_str()); return fail(ERROR_INVALID_DATA);
        }
    }

    // Patch the temp copy (never been loaded - BeginUpdateResource always succeeds on it)
    const WORD langId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    HANDLE hUpdate = BeginUpdateResourceW(tmp.c_str(), FALSE);
    if (!hUpdate) {
        DWORD err = GetLastError(); DeleteFileW(tmp.c_str()); return fail(err);
    }
    for (WORD i = 0; i < count; ++i) {
        void* imgData = ico.data() + entries[i].imageOffset;
        if (!UpdateResourceW(hUpdate, RT_ICON, MAKEINTRESOURCEW(i + 1),
                             langId, imgData, entries[i].bytesInRes)) {
            DWORD err = GetLastError();
            EndUpdateResourceW(hUpdate, TRUE); DeleteFileW(tmp.c_str()); return fail(err);
        }
    }
    const size_t grpSize = sizeof(GrpIconDir) + static_cast<size_t>(count) * sizeof(GrpIconDirEntry);
    std::vector<char> grp(grpSize, '\0');
    auto* grpDir = reinterpret_cast<GrpIconDir*>(grp.data());
    grpDir->reserved = 0; grpDir->type = 1; grpDir->count = count;
    auto* grpEntries = reinterpret_cast<GrpIconDirEntry*>(grp.data() + sizeof(GrpIconDir));
    for (WORD i = 0; i < count; ++i) {
        grpEntries[i] = { entries[i].width, entries[i].height, entries[i].colorCount,
                          entries[i].reserved, entries[i].planes, entries[i].bitCount,
                          entries[i].bytesInRes, static_cast<WORD>(i + 1) };
    }
    if (!UpdateResourceW(hUpdate, RT_GROUP_ICON, MAKEINTRESOURCEW(1),
                         langId, grp.data(), static_cast<DWORD>(grpSize))) {
        DWORD err = GetLastError();
        EndUpdateResourceW(hUpdate, TRUE); DeleteFileW(tmp.c_str()); return fail(err);
    }
    if (!EndUpdateResourceW(hUpdate, FALSE)) {
        DWORD err = GetLastError(); DeleteFileW(tmp.c_str()); return fail(err);
    }

    // Temp copy is fully patched. Spawn powershell.exe (different image) to do the final
    // swap once all clipboardpp processes exit - we cannot replace our own running image.
    // Copy-Item handles cross-volume moves (AppData → exe dir) correctly.
    const std::wstring scriptPath = std::wstring(appData) + L"\\Clipboard++\\patch_finalize.ps1";

    auto escapeSQ = [](std::wstring s) {
        std::wstring out;
        for (wchar_t c : s) { if (c == L'\'') out += L"''"; else out.push_back(c); }
        return out;
    };

    const std::wstring script =
        L"$t='" + escapeSQ(tmp) + L"'\n"
        L"$e='" + escapeSQ(exePath) + L"'\n"
        L"$dl=[datetime]::Now.AddSeconds(15)\n"
        L"while((Get-Process clipboardpp -EA SilentlyContinue) -and ([datetime]::Now -lt $dl)){Start-Sleep -Milliseconds 200}\n"
        L"Start-Sleep -Milliseconds 500\n"
        L"Copy-Item -LiteralPath $t -Destination $e -Force -EA SilentlyContinue\n"
        L"Remove-Item -LiteralPath $t -EA SilentlyContinue\n"
        L"$code='[DllImport(\"shell32.dll\",CharSet=CharSet.Unicode)]public static extern void SHChangeNotify(int e,uint f,string a,string b);'\n"
        L"$sh=Add-Type -PassThru -MemberDefinition $code -Name SH -Namespace W -EA SilentlyContinue\n"
        L"if($sh){$sh::SHChangeNotify(2,5,$e,$null);$sh::SHChangeNotify(0x8000000,0,$null,$null)}\n"
        L"Remove-Item $PSCommandPath -EA SilentlyContinue\n";

    if (FILE* fp = _wfopen(scriptPath.c_str(), L"w,ccs=UTF-8")) {
        fwrite(script.c_str(), sizeof(wchar_t), script.size(), fp);
        fclose(fp);

        std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden "
                           L"-ExecutionPolicy Bypass -File \"" + scriptPath + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(L'\0');
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
                           FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    if (outError) *outError = 0;
    return true;
}
