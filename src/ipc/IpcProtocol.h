#pragma once

#include <windows.h>

#include <cstdint>

constexpr ULONG_PTR CD_CLIPBOARD_TEXT = 0x43505031;     // "CPP1"
constexpr ULONG_PTR CD_HISTORY_MUTATION = 0x43505032;   // "CPP2"

struct ClipboardTextCommand {
    int position; // 0=top, -1=bottom, 1..kMaxClipboardHistoryItems=one-based history slot
    BOOL setSystemClipboard;
};

enum class HistoryMutationOperation : uint32_t {
    Delete = 1,
    Pin = 2,
    Unpin = 3,
    Clear = 4,
};

struct HistoryMutationCommand {
    uint32_t version;
    uint32_t operation;
    uint64_t itemId;       // ignored for Clear
};

constexpr uint32_t kHistoryMutationVersion = 1;
