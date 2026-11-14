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
void LightText(HWND w) {
    CHARFORMAT2W cf{};cf.cbSize=sizeof(cf);
    SendMessageW(w,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&cf));
    assert((cf.dwMask&CFM_COLOR) && !(cf.dwEffects&CFE_AUTOCOLOR));
    assert(cf.crTextColor==RGB(158,204,193));
    SendMessageW(w,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&cf));
    assert((cf.dwMask&CFM_COLOR) && !(cf.dwEffects&CFE_AUTOCOLOR));
    assert(cf.crTextColor==RGB(158,204,193));
}
int main() {
    So5Log::RetainedDiagnostics history;
    history.Keep(L"Runtime: semaphore id=1578");assert(history.lines.empty());
    for(unsigned i=0;i<400;++i) history.Keep(L"[23:01:06] Scene preset refused: "+std::to_wstring(i));
    assert(history.lines.size()==history.Capacity);
    assert(history.lines.front().ends_with(L": 0") && history.lines[63].ends_with(L": 63"));
    assert(history.lines.back().ends_with(L": 399"));
    history.Clear();assert(history.lines.empty());
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};assert(InitCommonControlsEx(&controls));
    const auto parent=CreateWindowExW(0,L"STATIC",L"Console fixture",WS_OVERLAPPED,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(parent);{
        LogConsole console;assert(console.Create(parent));const auto w=console.Handle();
        SYSTEMTIME time{};time.wYear=2026;time.wMonth=11;time.wDay=13;time.wHour=9;time.wMinute=7;time.wSecond=3;time.wMilliseconds=42;
        const auto stamped=So5Log::Stamp(L"GPU: Résumé / 中文",time);
        assert(stamped==L"[2026-11-13 09:07:03.0420000] GPU: Résumé / 中文");
        assert(So5Log::Stamp(L"Precision",time,1234567)==L"[2026-11-13 09:07:03.1234567] Precision");
        const auto live=So5Log::Stamp(L"Clock");
        assert(live.size()==35 && live[11]==L' ' && live[20]==L'.' && live[28]==L']');
        LightText(w);
        console.Update({stamped});SendMessageW(w,EM_SETSEL,0,-1);assert(Selected(w)==stamped);
        LightText(w);console.SetScale(1.5f);LightText(w);
        time.wSecond=4;console.Update({stamped,So5Log::Stamp(L"New output",time)});
        SendMessageW(w,EM_SETSEL,0,-1);assert(Selected(w).find(stamped)==0);
        console.Place(10,20,600,200);assert(GetWindowLongPtrW(w,GWL_STYLE)&ES_READONLY);
        console.Update({L"First log line",L"Second line",L"GPU: Résumé / 中文"});
        CHARRANGE selection{0,5};SendMessageW(w,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));assert(Selected(w)==L"First");
        SendMessageW(w,WM_CHAR,L'X',0);assert(Selected(w)==L"First");
        console.Update({L"First log line",L"Second line",L"GPU: Résumé / 中文",L"New output"});assert(Selected(w)==L"First");
        SendMessageW(w,EM_SETSEL,0,-1);const auto all=Selected(w);assert(all.find(L"中文")!=std::wstring::npos && all.find(L"New output")!=std::wstring::npos);
        console.Update({L"First log line",L"Second line",L"Third"});
        CHARRANGE second{15,21};SendMessageW(w,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&second));assert(Selected(w)==L"Second");
        console.Update({L"Second line",L"Third",L"Fourth"});assert(Selected(w)==L"Second");
        SendMessageW(w,WM_THEMECHANGED,0,0);LightText(w);assert(Selected(w)==L"Second");
        console.Update({L"Cleared"});SendMessageW(w,EM_SETSEL,0,-1);assert(Selected(w)==L"Cleared");
        console.Update({});SendMessageW(w,EM_SETSEL,0,-1);assert(Selected(w).empty());LightText(w);
        std::puts("PASS: precise dated timestamps, persistent light colors, native read-only console, Unicode selection, live append preserves selection and clear; clipboard untouched");
    }DestroyWindow(parent);
}
