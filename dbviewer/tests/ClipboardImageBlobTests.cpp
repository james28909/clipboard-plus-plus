#include "../src/ClipboardImageBlob.h"
#include "../../src/security/DpapiProtection.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool Expect(bool value, const char* name) {
    if (value)
        return true;
    std::cerr << "FAILED: " << name << '\n';
    return false;
}

} // namespace

int main() {
    const std::vector<uint8_t> imageBytes = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        'c', 'l', 'i', 'p', 'b', 'o', 'a', 'r', 'd', '+', '+'
    };
    bool ok = true;

    std::vector<uint8_t> protectedBytes;
    ok &= Expect(DpapiProtection::Protect(imageBytes, protectedBytes),
                 "test image is protected");
    ok &= Expect(protectedBytes != imageBytes,
                 "protected image differs from plaintext");

    std::vector<uint8_t> decoded;
    ok &= Expect(ClipboardImageBlob::Decode(1, protectedBytes, decoded) ==
                     ClipboardImageBlob::DecodeResult::Decrypted,
                 "version 1 image decrypts");
    ok &= Expect(decoded == imageBytes, "decryption preserves image bytes");

    decoded.clear();
    ok &= Expect(ClipboardImageBlob::Decode(0, imageBytes, decoded) ==
                     ClipboardImageBlob::DecodeResult::Plaintext,
                 "legacy plaintext image remains supported");
    ok &= Expect(decoded == imageBytes, "plaintext image bytes are preserved");

    decoded = imageBytes;
    ok &= Expect(ClipboardImageBlob::Decode(2, protectedBytes, decoded) ==
                     ClipboardImageBlob::DecodeResult::UnsupportedVersion,
                 "unknown protection version is rejected");
    ok &= Expect(decoded.empty(), "rejected version returns no plaintext");

    protectedBytes.back() ^= 0x5a;
    ok &= Expect(ClipboardImageBlob::Decode(1, protectedBytes, decoded) ==
                     ClipboardImageBlob::DecodeResult::DecryptionFailed,
                 "corrupt protected image is rejected");
    ok &= Expect(decoded.empty(), "failed decryption returns no plaintext");

    if (!ok)
        return 1;
    std::cout << "SQLite editor Clipboard++ image decryption tests passed\n";
    return 0;
}
