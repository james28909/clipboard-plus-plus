#pragma once

#include <cstdint>
#include <vector>

namespace DpapiProtection {

bool Protect(const std::vector<uint8_t>& plaintext,
             std::vector<uint8_t>& protectedData,
             uint32_t* error = nullptr);
bool Unprotect(const std::vector<uint8_t>& protectedData,
               std::vector<uint8_t>& plaintext,
               uint32_t* error = nullptr);

} // namespace DpapiProtection
