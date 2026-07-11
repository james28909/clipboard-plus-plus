#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class ScreenshotTracker {
public:
    static ScreenshotTracker& Instance();

    void NoteHotkey(const std::string& hint);
    std::filesystem::path FindRecentScreenshotFile(int imageW, int imageH, uint64_t pixelHash);
    static uint64_t PixelHashFromImageBytes(const std::vector<uint8_t>& bytes, bool isPngOrEncoded);
    std::string LastHint() const;

private:
    ScreenshotTracker();

    void RefreshFolders();
    bool IsCandidateImage(const std::filesystem::path& path) const;

    mutable std::string m_lastHint;
    std::chrono::system_clock::time_point m_lastHotkeyTime{};
    std::vector<std::filesystem::path> m_folders;
};
