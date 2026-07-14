#pragma once

#include <cstdint>

inline bool IsSelfGeneratedClipboardUpdate(bool writeInProgress,
                                           bool matchingWriteToken,
                                           uint32_t sequence,
                                           uint32_t completedWriteSequence) {
    return writeInProgress || matchingWriteToken ||
           (completedWriteSequence != 0 && sequence == completedWriteSequence);
}
