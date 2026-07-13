#pragma once

#include <cstdint>
#include <vector>

namespace ClipboardImageBlob {

enum class DecodeResult {
    Plaintext,
    Decrypted,
    UnsupportedVersion,
    DecryptionFailed,
};

DecodeResult Decode(int protectionVersion,
                    const std::vector<uint8_t>& storedBytes,
                    std::vector<uint8_t>& imageBytes);

} // namespace ClipboardImageBlob
