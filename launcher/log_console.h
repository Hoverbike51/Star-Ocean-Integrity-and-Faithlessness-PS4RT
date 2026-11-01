// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <vector>

// Native read-only console: selection, Unicode copying and scrolling are provided
// by Windows Rich Edit. No clipboard data is read by the launcher.
class LogConsole {
    HWND window{};HFONT font{};RECT placed{};std::vector<std::wstring> displayed;
    static LRESULT CALLBACK Events(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(msg==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000)) {
            if(wp=='A') {SendMessageW(w,EM_SETSEL,0,-1);return 0;}
            if(wp=='C') {SendMessageW(w,WM_COPY,0,0);return 0;}
        }
        if(msg==WM_NCDESTROY) RemoveWindowSubclass(w,Events,1);
        return DefSubclassProc(w,msg,wp,lp);
    }
public:
    ~LogConsole() {if(IsWindow(window)) DestroyWindow(window);if(font) DeleteObject(font);}
    HWND Handle() const {return window;}
    bool Create(HWND parent) {
        static const HMODULE rich=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!rich) return false;
        window=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL,0,0,1,1,parent,nullptr,
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE)),nullptr);
        if(!window) return false;
        SetWindowSubclass(window,Events,1,0);
        SetWindowTheme(window,L"DarkMode_Explorer",nullptr);
        SendMessageW(window,EM_SETBKGNDCOLOR,0,RGB(2,10,16));
        font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Consolas");
        SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        CHARFORMAT2W cf{};cf.cbSize=sizeof(cf);cf.dwMask=CFM_COLOR;cf.crTextColor=RGB(158,204,193);
        SendMessageW(window,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&cf));
        SendMessageW(window,EM_EXLIMITTEXT,0,16*1024*1024);
        // A target width of one disables word wrapping. Long diagnostics use the
        // horizontal scrollbar rather than being cut into narrow visual lines.
        SendMessageW(window,EM_SETTARGETDEVICE,0,1);
        SendMessageW(window,EM_SETUNDOLIMIT,0,0);
        return true;
    }
    void Show(bool show) {if(window && bool(IsWindowVisible(window))!=show) ShowWindow(window,show?SW_SHOWNA:SW_HIDE);}
    void Place(int x,int y,int width,int height) {
        if(placed.left!=x || placed.top!=y || placed.right!=width || placed.bottom!=height) {
            SetWindowPos(window,nullptr,x,y,std::max(1,width),std::max(1,height),SWP_NOZORDER|SWP_NOACTIVATE);
            placed=RECT{x,y,width,height};
        }
    }
    void Update(const std::vector<std::wstring>& lines) {
        if(!window || lines==displayed) return;
        CHARRANGE selection{};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        POINT scroll{};SendMessageW(window,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        SCROLLINFO bar{sizeof(bar),SIF_ALL};GetScrollInfo(window,SB_VERT,&bar);
        const bool follow=selection.cpMin==selection.cpMax && bar.nPos>=bar.nMax-int(bar.nPage)+1;
        const bool append=lines.size()>=displayed.size() && std::equal(displayed.begin(),displayed.end(),lines.begin());
        size_t removed=0;
        if(!append && !lines.empty()) for(size_t start=1;start<displayed.size();++start) {
            const size_t overlap=std::min(displayed.size()-start,lines.size());
            if(std::equal(displayed.begin()+start,displayed.begin()+start+overlap,lines.begin())) {removed=start;break;}
        }
        if(removed) {
            LONG chars=0;for(size_t i=0;i<removed;++i) chars+=LONG(displayed[i].size()+1);
            selection.cpMin=std::max<LONG>(0,selection.cpMin-chars);selection.cpMax=std::max<LONG>(0,selection.cpMax-chars);
        }
        const LONG first_line=LONG(SendMessageW(window,EM_GETFIRSTVISIBLELINE,0,0));
        std::wstring text;
        for(size_t i=append?displayed.size():0;i<lines.size();++i) {
            if(i || (!append && !text.empty())) text+=L'\r';
            text+=lines[i];
        }
        SendMessageW(window,WM_SETREDRAW,FALSE,0);
        if(append) {SendMessageW(window,EM_SETSEL,-1,-1);SendMessageW(window,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(text.c_str()));}
        else SetWindowTextW(window,text.c_str());
        if(follow) {SendMessageW(window,EM_SETSEL,-1,-1);SendMessageW(window,EM_SCROLLCARET,0,0);}
        else {
            SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
            SendMessageW(window,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
            if(removed) SendMessageW(window,EM_LINESCROLL,0,std::max<LONG>(0,first_line-LONG(removed))-LONG(SendMessageW(window,EM_GETFIRSTVISIBLELINE,0,0)));
        }
        SendMessageW(window,WM_SETREDRAW,TRUE,0);InvalidateRect(window,nullptr,FALSE);
        displayed=lines;
    }
};
