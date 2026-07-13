#pragma once

#include <windows.h>

namespace StartupRegistration {

bool IsEnabled();
bool SetEnabled(bool enabled, LSTATUS* error = nullptr);

} // namespace StartupRegistration
