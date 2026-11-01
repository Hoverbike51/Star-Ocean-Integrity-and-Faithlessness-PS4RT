// SPDX-License-Identifier: GPL-2.0-or-later
#include "../launcher/log_console.h"
#include <cassert>
#include <cstdio>
std::wstring Selected(HWND window) {
    CHARRANGE range{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&range));
    std::wstring text(size_t(range.cpMax-range.cpMin)+1,L'\0');
    const auto count=SendMessageW(window,EM_GETSELTEXT,0,reinterpret_cast<LPARAM>(text.data()));
    text.resize(size_t(count));return text;
}
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};assert(InitCommonControlsEx(&controls));
    const auto parent=CreateWindowExW(0,L"STATIC",L"Console fixture",WS_OVERLAPPED,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(parent);{
        LogConsole console;assert(console.Create(parent));const auto w=console.Handle();
        console.Place(10,20,600,200);assert(GetWindowLongPtrW(w,GWL_STYLE)&ES_READONLY);
        console.Update({L"First log line",L"Second line",L"GPU: Résumé / 中文"});
        CHARRANGE selection{0,5};SendMessageW(w,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));assert(Selected(w)==L"First");
        SendMessageW(w,WM_CHAR,L'X',0);assert(Selected(w)==L"First");
        console.Update({L"First log line",L"Second line",L"GPU: Résumé / 中文",L"New output"});assert(Selected(w)==L"First");
        SendMessageW(w,EM_SETSEL,0,-1);const auto all=Selected(w);assert(all.find(L"中文")!=std::wstring::npos && all.find(L"New output")!=std::wstring::npos);
        console.Update({L"First log line",L"Second line",L"Third"});
        CHARRANGE second{15,21};SendMessageW(w,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&second));assert(Selected(w)==L"Second");
        console.Update({L"Second line",L"Third",L"Fourth"});assert(Selected(w)==L"Second");
        console.Update({L"Cleared"});SendMessageW(w,EM_SETSEL,0,-1);assert(Selected(w)==L"Cleared");
        console.Update({});SendMessageW(w,EM_SETSEL,0,-1);assert(Selected(w).empty());
        std::puts("PASS: native read-only console, Unicode selection, live append preserves selection and clear; clipboard untouched");
    }DestroyWindow(parent);
}
