#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../hotkeys/HotkeyManager.h"
#include "../util/Win32Util.h"
#include "PasteDiagnostics.h"
#include "ToastWindow.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static PopupWindow* g_trackingPopup = nullptr;

static HGLOBAL BuildUnicodeTextGlobal(const std::wstring& text) {
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem)
        return nullptr;

    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        return nullptr;
    }

    std::memcpy(data, text.c_str(), text.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(data)[text.size()] = L'\0';
    GlobalUnlock(mem);
    return mem;
}

static HGLOBAL BuildFileDropGlobal(const std::vector<std::wstring>& paths) {
    if (paths.empty())
        return nullptr;

    size_t chars = 1;
    for (const std::wstring& path : paths)
        chars += path.size() + 1;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                              sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (!mem)
        return nullptr;

    auto* drop = static_cast<DROPFILES*>(GlobalLock(mem));
    if (!drop) {
        GlobalFree(mem);
        return nullptr;
    }

    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;

    wchar_t* out = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    for (const std::wstring& path : paths) {
        std::memcpy(out, path.c_str(), path.size() * sizeof(wchar_t));
        out += path.size() + 1;
    }

    GlobalUnlock(mem);
    return mem;
}

static std::wstring WithTrailingCrlf(std::wstring text) {
    if (text.empty())
        return text;
    if (text.back() == L'\n' || text.back() == L'\r')
        return text;
    text += L"\r\n";
    return text;
}

static void PLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[512]{};
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    PasteDiagnostics::Log("%s", msg);
}

static std::string WH(HWND h) {
    if (!h) return "null";
    char t[64]{};
    GetWindowTextA(h, t, sizeof(t));
    char b[100];
    snprintf(b, sizeof(b), "0x%llX(\"%s\")",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)), t[0] ? t : "?");
    return b;
}

static const char* CtName(ContentType ct) {
    switch (ct) {
    case ContentType::Text:      return "Text";
    case ContentType::Html:      return "Html";
    case ContentType::RichText:  return "RichText";
    case ContentType::Image:     return "Image";
    case ContentType::FilePaths: return "FilePaths";
    default:                     return "Unknown";
    }
}

static bool SetForegroundWindowRobust(HWND target) {
    if (!target || !IsWindow(target))
        return false;

    if (IsIconic(target))
        ShowWindow(target, SW_RESTORE);

    HWND foreground = GetForegroundWindow();
    if (GetAncestor(foreground, GA_ROOT) == target)
        return true;

    DWORD currentThread = GetCurrentThreadId();
    DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;

    if (targetThread)
        AttachThreadInput(currentThread, targetThread, TRUE);
    if (foregroundThread && foregroundThread != targetThread)
        AttachThreadInput(currentThread, foregroundThread, TRUE);

    BringWindowToTop(target);
    BOOL ok = SetForegroundWindow(target);

    if (foregroundThread && foregroundThread != targetThread)
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    if (targetThread)
        AttachThreadInput(currentThread, targetThread, FALSE);

    return ok != FALSE || GetAncestor(GetForegroundWindow(), GA_ROOT) == target;
}

bool PopupWindow::IsValidPasteTarget(HWND hwnd) const {
    if (!hwnd || !IsWindow(hwnd))
        return false;

    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root || !IsWindow(root))
        return false;

    if (root == m_hwnd)
        return false;

    if (Application* app = Application::Get()) {
        if (root == app->GetHwnd())
            return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    if (pid == GetCurrentProcessId())
        return false;

    return IsWindowVisible(root) != FALSE;
}

void PopupWindow::NotePasteTarget(HWND hwnd, const char* reason) {
    HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    if (!IsValidPasteTarget(root)) {
        PLog("[TARGET] ignored reason=%s hwnd=%s", reason ? reason : "unknown", WH(hwnd).c_str());
        return;
    }

    if (root != m_activePasteTarget)
        PLog("[TARGET] active reason=%s %s -> %s",
             reason ? reason : "unknown",
             WH(m_activePasteTarget).c_str(),
             WH(root).c_str());

    m_activePasteTarget = root;
    m_prevForeground = root;

    if (Application* app = Application::Get())
        app->SyncClipboardForWindow(root);
}

void PopupWindow::StartPasteTargetTracking() {
    if (m_foregroundHook)
        return;

    g_trackingPopup = this;
    m_foregroundHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, ForegroundWinEventProc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    PLog("[TARGET] foreground hook %s", m_foregroundHook ? "started" : "FAILED");
}

void PopupWindow::StopPasteTargetTracking() {
    if (m_foregroundHook) {
        UnhookWinEvent(m_foregroundHook);
        m_foregroundHook = nullptr;
        PLog("[TARGET] foreground hook stopped");
    }
    if (g_trackingPopup == this)
        g_trackingPopup = nullptr;
}

void CALLBACK PopupWindow::ForegroundWinEventProc(HWINEVENTHOOK, DWORD event,
                                                  HWND hwnd, LONG objectId,
                                                  LONG childId, DWORD, DWORD) {
    if (event != EVENT_SYSTEM_FOREGROUND || objectId != OBJID_WINDOW || childId != CHILDID_SELF)
        return;
    if (g_trackingPopup && g_trackingPopup->m_visible)
        g_trackingPopup->NotePasteTarget(hwnd, "foreground");
}

void PopupWindow::NoteExternalMouseDown(POINT screenPoint) {
    RECT popupRect{};
    if (GetWindowRect(m_hwnd, &popupRect) &&
        PtInRect(&popupRect, screenPoint)) {
        PLog("[MOUSE-EXT] click=(%ld,%ld) INSIDE popup -> no change (kbCapture=%d)",
             screenPoint.x, screenPoint.y, m_keyboardCapture);
        return;
    }

    PLog("[MOUSE-EXT] click=(%ld,%ld) OUTSIDE popup rect=(%ld,%ld,%ld,%ld) -> kbCapture=false",
         screenPoint.x, screenPoint.y,
         popupRect.left, popupRect.top, popupRect.right, popupRect.bottom);
    ReleaseSearchCapture();
    m_keyboardCapture = false;

    HWND clicked = WindowFromPoint(screenPoint);
    if (clicked) clicked = GetAncestor(clicked, GA_ROOT);
    NotePasteTarget(clicked, "mouse");

    Application* app = Application::Get();
    if (app && app->GetHidePopupOnOutsideClick())
        Hide();
}

void PopupWindow::PasteHistorySlot(int slot, HWND targetWindow) {
    if (slot < 0) return;

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetRegularCopy(static_cast<size_t>(slot), item)) return;

    PLog("[PASTE-HISTORY] slot=%d type=%s target=%s text=%.50s",
         slot, CtName(item.type), WH(targetWindow).c_str(), item.text.c_str());
    m_prevForeground = targetWindow;
    NotePasteTarget(targetWindow, "hidden-history");
    WriteToClipboard(item, targetWindow);
    RestoreFocusAndPaste(targetWindow);
    hist->MoveItemById(item.id, m_pasteMoveTarget);
}

void PopupWindow::PastePinnedSlot(int slot, HWND targetWindow) {
    if (slot < 0) return;

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetPinnedCopy(static_cast<size_t>(slot), item)) return;

    m_prevForeground = targetWindow;
    NotePasteTarget(targetWindow, "hidden-pinned");
    WriteToClipboard(item, targetWindow);
    RestoreFocusAndPaste(targetWindow);
    hist->MoveItemById(item.id, ClipboardHistory::MoveTarget::None);
}

void PopupWindow::PasteVisibleSlot(int slot) {
    const std::vector<size_t> regular = BuildVisibleHistoryIndices(false);
    if (slot < 0 || static_cast<size_t>(slot) >= regular.size()) {
        PLog("[PASTE-SLOT] slot=%d INVALID (visible count=%zu)", slot, regular.size());
        return;
    }
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetCopy(regular[static_cast<size_t>(slot)], item)) {
        PLog("[PASTE-SLOT] slot=%d GetCopy FAILED", slot);
        return;
    }

    PLog("[PASTE-SLOT] slot=%d type=%s kbCapture=%d txtEntry=%d text=%.50s",
         slot, CtName(item.type), m_keyboardCapture, IsTextEntryActive(), item.text.c_str());
    PasteItemKeepOpen(item);
    hist->MoveItemById(item.id, m_pasteMoveTarget);
}

void PopupWindow::PasteItemKeepOpen(const ClipboardItem& item) {
    PLog("[PASTE-ITEM] type=%s fg=%s text=%.50s",
         CtName(item.type), WH(GetForegroundWindow()).c_str(), item.text.c_str());
    HWND target = ResolvePasteTarget();
    if (!target) {
        ActivateKeyboardCapture();
        return;
    }
    WriteToClipboard(item, target);
    RestoreFocusAndPaste(target);
    ActivateKeyboardCapture(); // re-arm so slot keys keep working after paste
}

void PopupWindow::PasteSelectedItemsInOrder() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    std::vector<uint64_t> ids = m_itemSelection.Ids();
    if (ids.empty())
        return;

    HWND pasteTarget = ResolvePasteTarget();
    if (!pasteTarget) {
        ActivateKeyboardCapture();
        return;
    }
    PLog("[PASTE-SELECTED] count=%zu lockedTarget=%s", ids.size(), WH(pasteTarget).c_str());
    Hide();

    for (size_t qi = 0; qi < ids.size(); ++qi) {
        ClipboardItem item;
        if (!hist->GetByIdCopy(ids[qi], item)) continue;
        WriteToClipboard(item, pasteTarget);
        RestoreFocusAndPaste(pasteTarget);
        hist->MoveItemById(item.id, m_pasteMoveTarget);
        if (qi + 1 < ids.size() && m_multiPasteDelayMs > 0)
            Sleep(static_cast<DWORD>(m_multiPasteDelayMs));
    }
}

void PopupWindow::PasteSelectedItems() {
    if (!m_itemSelection.HasMultiple())
        return;
    ActivateKeyboardCapture();
    PasteSelectedItemsInOrder();
}

void PopupWindow::WriteToClipboard(const ClipboardItem& item, HWND targetWindow) const {
    PLog("[WRITE-CB] type=%s len=%zu text=%.60s",
         CtName(item.type), item.text.size(), item.text.c_str());

    if (Application::Get() && Application::Get()->GetMonitor())
        Application::Get()->GetMonitor()->SuppressNextUpdate();

    std::string text = item.text;
    const bool isFileDrop = item.type == ContentType::FilePaths || (item.tags & TAG_PATH) != 0;
    if (isFileDrop) {
        std::vector<std::wstring> filePaths = win32util::ExistingPathListUtf8(text);
        if (!filePaths.empty()) {
            if (!OpenClipboard(nullptr)) {
                PLog("[WRITE-CB] OpenClipboard FAILED for FileDrop+text (GLE=%lu)", GetLastError());
                ToastWindow::Show(L"Paste failed: clipboard busy");
                return;
            }
            EmptyClipboard();

            bool wroteAny = false;
            const std::wstring textPaths = WithTrailingCrlf(win32util::Utf8ToWide(text));
            if (!textPaths.empty()) {
                if (HGLOBAL hm = BuildUnicodeTextGlobal(textPaths)) {
                    if (SetClipboardData(CF_UNICODETEXT, hm)) {
                        wroteAny = true;
                        PLog("[WRITE-CB] CF_UNICODETEXT fallback paths set OK");
                    } else {
                        GlobalFree(hm);
                        PLog("[WRITE-CB] CF_UNICODETEXT fallback paths set FAILED");
                    }
                }
            }

            if (HGLOBAL drop = BuildFileDropGlobal(filePaths)) {
                if (SetClipboardData(CF_HDROP, drop)) {
                    wroteAny = true;
                    PLog("[WRITE-CB] CF_HDROP set OK (%zu paths)", filePaths.size());
                } else {
                    GlobalFree(drop);
                    PLog("[WRITE-CB] CF_HDROP set FAILED (%zu paths)", filePaths.size());
                }
            }

            CloseClipboard();
            if (!wroteAny)
                ToastWindow::Show(L"Paste failed: could not write file paths");
            return;
        }
        PLog("[WRITE-CB] FileDrop FAILED (paths=%zu), falling through to text", filePaths.size());
    }

    if (item.type == ContentType::Image && !item.sourceFilePath.empty()) {
        if (!OpenClipboard(nullptr)) {
            PLog("[WRITE-CB] OpenClipboard FAILED for image+path (GLE=%lu)", GetLastError());
            ToastWindow::Show(L"Paste failed: clipboard busy");
            return;
        }
        EmptyClipboard();

        bool wroteAny = false;
        const std::wstring path = win32util::Utf8ToWide(item.sourceFilePath);
        if (!path.empty()) {
            if (HGLOBAL hm = BuildUnicodeTextGlobal(path)) {
                if (SetClipboardData(CF_UNICODETEXT, hm)) {
                    wroteAny = true;
                    PLog("[WRITE-CB] CF_UNICODETEXT path set OK");
                } else {
                    GlobalFree(hm);
                    PLog("[WRITE-CB] CF_UNICODETEXT path set FAILED");
                }
            }
            if (HGLOBAL drop = BuildFileDropGlobal({path})) {
                if (SetClipboardData(CF_HDROP, drop)) {
                    wroteAny = true;
                    PLog("[WRITE-CB] CF_HDROP set OK path=%s", item.sourceFilePath.c_str());
                } else {
                    GlobalFree(drop);
                    PLog("[WRITE-CB] CF_HDROP set FAILED path=%s", item.sourceFilePath.c_str());
                }
            }
        }

        if (!item.imageStoreId.empty()) {
            Application* app = Application::Get();
            ImageStore* store = app ? app->GetImageStore() : nullptr;
            if (store) {
                HGLOBAL hDib = store->GetDibForPaste(item.imageStoreId);
                if (hDib && SetClipboardData(CF_DIB, hDib)) {
                    wroteAny = true;
                    PLog("[WRITE-CB] CF_DIB set OK id=%s", item.imageStoreId.c_str());
                } else if (hDib) {
                    GlobalFree(hDib);
                    PLog("[WRITE-CB] CF_DIB set FAILED id=%s", item.imageStoreId.c_str());
                }
            }
        }

        CloseClipboard();
        if (!wroteAny)
            ToastWindow::Show(L"Paste failed: image data unavailable");
        return;
    }

    if (m_appendNewlineAfterPaste && !isFileDrop)
        text += "\r\n";

    if (!OpenClipboard(nullptr)) {
        PLog("[WRITE-CB] OpenClipboard FAILED (GLE=%lu)", GetLastError());
        ToastWindow::Show(L"Paste failed: clipboard busy");
        return;
    }
    EmptyClipboard();

    if (item.type == ContentType::Image && !item.imageStoreId.empty()) {
        Application* app = Application::Get();
        ImageStore* store = app ? app->GetImageStore() : nullptr;
        if (store) {
            HGLOBAL hDib = store->GetDibForPaste(item.imageStoreId);
            if (hDib) {
                SetClipboardData(CF_DIB, hDib);
                PLog("[WRITE-CB] CF_DIB set OK id=%s", item.imageStoreId.c_str());
            } else {
                PLog("[WRITE-CB] GetDibForPaste FAILED id=%s", item.imageStoreId.c_str());
                ToastWindow::Show(L"Paste failed: image data unavailable");
            }
        }
    } else {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        text.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE,
                                      static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
            if (hm) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1,
                                    static_cast<wchar_t*>(GlobalLock(hm)), wlen);
                GlobalUnlock(hm);
                HANDLE res = SetClipboardData(CF_UNICODETEXT, hm);
                PLog("[WRITE-CB] CF_UNICODETEXT set %s (wlen=%d)", res ? "OK" : "FAILED", wlen);
                if (!res) ToastWindow::Show(L"Paste failed: could not write to clipboard");
            } else {
                PLog("[WRITE-CB] GlobalAlloc FAILED");
                ToastWindow::Show(L"Paste failed: out of memory");
            }
        } else {
            PLog("[WRITE-CB] MultiByteToWideChar returned 0 (empty?)");
        }
    }
    CloseClipboard();
}

HWND PopupWindow::ResolvePasteTarget() const {
    HWND fg = GetForegroundWindow();

    if (IsValidPasteTarget(m_activePasteTarget)) {
        PLog("[RESOLVE-TARGET] active=%s fg=%s -> use active",
             WH(m_activePasteTarget).c_str(), WH(fg).c_str());
        return m_activePasteTarget;
    }

    if (IsValidPasteTarget(fg)) {
        PLog("[RESOLVE-TARGET] fg=%s active=%s -> use fg",
             WH(fg).c_str(), WH(m_activePasteTarget).c_str());
        return GetAncestor(fg, GA_ROOT);
    }

    if (IsValidPasteTarget(m_prevForeground)) {
        PLog("[RESOLVE-TARGET] fg=%s active=%s prevFg=%s -> use prevFg",
             WH(fg).c_str(), WH(m_activePasteTarget).c_str(), WH(m_prevForeground).c_str());
        return GetAncestor(m_prevForeground, GA_ROOT);
    }

    PLog("[RESOLVE-TARGET] fg=%s active=%s prevFg=%s -> NO TARGET",
         WH(fg).c_str(), WH(m_activePasteTarget).c_str(), WH(m_prevForeground).c_str());
    ToastWindow::Show(L"No paste target selected");
    return nullptr;
}

bool PopupWindow::WaitForForeground(HWND target, DWORD timeoutMs) const {
    if (!target) return false;

    const DWORD started = GetTickCount();
    while (GetAncestor(GetForegroundWindow(), GA_ROOT) != target) {
        if (GetTickCount() - started >= timeoutMs) {
            PLog("[WAIT-FG] TIMEOUT after %ums target=%s actual_fg=%s",
                 timeoutMs, WH(target).c_str(), WH(GetForegroundWindow()).c_str());
            return false;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
    PLog("[WAIT-FG] OK (%ums) target=%s", GetTickCount() - started, WH(target).c_str());
    return true;
}

void PopupWindow::RestoreFocusAndPaste(HWND preferredTarget) {
    HWND target = preferredTarget ? GetAncestor(preferredTarget, GA_ROOT) : ResolvePasteTarget();
    HWND curFg = GetForegroundWindow();

    PLog("[RESTORE] target=%s curFg=%s preferredTarget=%s",
         WH(target).c_str(), WH(curFg).c_str(),
         preferredTarget ? WH(preferredTarget).c_str() : "(none)");

    if (target && IsWindow(target)) {
        if (GetAncestor(curFg, GA_ROOT) != target) {
            PLog("[RESTORE] SetForegroundWindowRobust(%s)", WH(target).c_str());
            bool sfwOk = SetForegroundWindowRobust(target);
            PLog("[RESTORE] SetForegroundWindowRobust returned %d", sfwOk ? 1 : 0);
            WaitForForeground(target, 150);
        } else {
            PLog("[RESTORE] already foreground - skip SetForegroundWindow");
        }
    } else {
        PLog("[RESTORE] ABORT: target=%s IsWindow=%d",
             WH(target).c_str(), target ? IsWindow(target) : 0);
        ToastWindow::Show(L"No paste target selected");
        return;
    }

    // All injected events carry kClipboardPasteMagic so our LL hook ignores them.
    const bool ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    const bool altDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

    INPUT in[12]{};
    for (auto& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.dwExtraInfo = kClipboardPasteMagic;
    }

    int n = 0;
    if (altDown) {
        in[n].ki.wVk = VK_MENU;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (shiftDown) {
        in[n].ki.wVk = VK_SHIFT;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (ctrlDown) {
        in[n].ki.wVk = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }

    in[n].ki.wVk = VK_CONTROL;
    ++n;
    in[n].ki.wVk = 'V';
    ++n;
    in[n].ki.wVk = 'V';
    in[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;
    in[n].ki.wVk = VK_CONTROL;
    in[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;

    PLog("[RESTORE] SendInput n=%d ctrl=%d shift=%d alt=%d fg_at_send=%s",
         n, ctrlDown, shiftDown, altDown, WH(GetForegroundWindow()).c_str());
    UINT sent = SendInput(static_cast<UINT>(n), in, sizeof(INPUT));
    PLog("[RESTORE] SendInput sent=%u/%d", sent, n);
}
