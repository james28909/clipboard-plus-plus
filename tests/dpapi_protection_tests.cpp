#include "../src/security/DpapiProtection.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
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
    const std::string secret = "Clipboard++ DPAPI test: unicode \xF0\x9F\x94\x92";
    const std::vector<uint8_t> plaintext(secret.begin(), secret.end());
    std::vector<uint8_t> encrypted;
    std::vector<uint8_t> decrypted;
    uint32_t error{};
    bool ok = true;

    ok &= Expect(DpapiProtection::Protect(plaintext, encrypted, &error),
                 "protect succeeds");
    ok &= Expect(!encrypted.empty(), "protected blob is non-empty");
    ok &= Expect(std::search(encrypted.begin(), encrypted.end(),
                            plaintext.begin(), plaintext.end()) == encrypted.end(),
                 "plaintext is not embedded in protected blob");
    ok &= Expect(DpapiProtection::Unprotect(encrypted, decrypted, &error),
                 "unprotect succeeds");
    ok &= Expect(decrypted == plaintext, "round trip preserves bytes");

    if (!encrypted.empty())
        encrypted[encrypted.size() / 2] ^= 0x5a;
    decrypted.clear();
    ok &= Expect(!DpapiProtection::Unprotect(encrypted, decrypted, &error),
                 "tampered blob is rejected");

    if (!ok)
        return 1;
    std::cout << "DPAPI protection tests passed\n";
    return 0;
}
