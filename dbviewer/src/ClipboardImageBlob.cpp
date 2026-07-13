#include "ClipboardImageBlob.h"
#include "../../src/security/DpapiProtection.h"

namespace ClipboardImageBlob {

DecodeResult Decode(int protectionVersion,
                    const std::vector<uint8_t>& storedBytes,
                    std::vector<uint8_t>& imageBytes) {
    imageBytes.clear();
    if (protectionVersion == 0) {
        imageBytes = storedBytes;
        return DecodeResult::Plaintext;
    }
    if (protectionVersion != 1)
        return DecodeResult::UnsupportedVersion;
    if (!DpapiProtection::Unprotect(storedBytes, imageBytes))
        return DecodeResult::DecryptionFailed;
    return DecodeResult::Decrypted;
}

} // namespace ClipboardImageBlob
