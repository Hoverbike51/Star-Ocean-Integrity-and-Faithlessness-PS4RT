#pragma once
#include <string>

// Returns a bundled, reviewed UI translation. Unknown technical diagnostics deliberately
// fall back to English instead of contacting an online service from the user's computer.
const wchar_t* ps4rt_localize(const std::wstring& language, const wchar_t* english);
