// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/arch.h"
#include "common/signal_context.h"

#include <windows.h>

namespace Common {

void* GetRip(void* ctx) {
    return (void*)((EXCEPTION_POINTERS*)ctx)->ContextRecord->Rip;
}

bool IsWriteError(void* ctx) {
    return ((EXCEPTION_POINTERS*)ctx)->ExceptionRecord->ExceptionInformation[0] == 1;
}

} // namespace Common
