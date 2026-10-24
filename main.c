#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>

#ifndef SHCNE_ASSOCCHANGED
#define SHCNE_ASSOCCHANGED 0x08000000L
#endif
#ifndef SHCNF_IDLIST
#define SHCNF_IDLIST 0x0000
#endif

#define APP_NAME        L"XDH映像劫持器"
#define APP_VERSION     L"1.0.1"
#define APP_PUBLISHER   L"XDH"
#define INSTALL_DIR     L"C:\\Program Files\\XDHImageHijack"
#define MAIN_EXE_PATH   L"C:\\Program Files\\XDHImageHijack\\XDHImageHijack.exe"
#define BLOCKER_EXE     L"C:\\Program Files\\XDHImageHijack\\XDHBlocker.exe"
#define UNINSTALL_KEY   L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\XDHImageHijack"
#define IFEO_BASE       L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options"
#define CMDSTORE        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell"

static void MsgOk(const wchar_t* t)  { MessageBoxW(NULL, t, APP_NAME, MB_OK|MB_ICONINFORMATION); }
static void MsgErr(const wchar_t* t) { MessageBoxW(NULL, t, APP_NAME, MB_OK|MB_ICONERROR); }

static BOOL IsAdmin(void) {
    BOOL b = FALSE; PSID g = NULL;
    SID_IDENTIFIER_AUTHORITY a = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&a,2,SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&g)) {
        CheckTokenMembership(NULL,g,&b); FreeSid(g);
    }
    return b;
}

static void ElevateSelf(LPWSTR lpCmdLine) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t params[4096] = {0};
    if (lpCmdLine && lpCmdLine[0]) {
        if (wcsstr(lpCmdLine, L"--elevated")) return;
        wcscpy(params, lpCmdLine);
    }
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = params[0] ? params : NULL;
    sei.nShow = SW_NORMAL;
    ShellExecuteExW(&sei);
}

// ---------- 劫持核心 ----------
static BOOL DoHijack(const wchar_t* exePath, int mode, const wchar_t* hijackTo) {
    const wchar_t* name = wcsrchr(exePath, L'\\');
    name = name ? name+1 : exePath;
    if (!name[0]) return FALSE;

    wchar_t sub[1024];
    swprintf(sub, 1024, L"%s\\%s", IFEO_BASE, name);

    HKEY hk;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub, 0, NULL, 0,
            KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
        return FALSE;

    BOOL ok = FALSE;
    if (mode == 0) { // 取消
        ok = RegSetValueExW(hk, L"debugger", 0, REG_SZ,
                (const BYTE*)L"", sizeof(wchar_t)) == ERROR_SUCCESS;
    } else if (mode == 1) { // 阻止运行（改为调用拦截器）
        ok = RegSetValueExW(hk, L"debugger", 0, REG_SZ,
                (const BYTE*)BLOCKER_EXE,
                (DWORD)((wcslen(BLOCKER_EXE)+1)*sizeof(wchar_t))) == ERROR_SUCCESS;
    } else if (mode == 2 && hijackTo && hijackTo[0]) {
        ok = RegSetValueExW(hk, L"debugger", 0, REG_SZ,
                (const BYTE*)hijackTo,
                (DWORD)((wcslen(hijackTo)+1)*sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    RegCloseKey(hk);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return ok;
}

// ---------- 稳定的 GUI 输入框 ----------
typedef struct {
    wchar_t* out;
    DWORD cch;
} InputBoxCtx;

static LRESULT CALLBACK IBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    InputBoxCtx* ctx = (InputBoxCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        ctx = (InputBoxCtx*)cs->lpCreateParams;

        CreateWindowExW(0, L"STATIC", L"请输入要劫持到的程序名（如 calc.exe）：", 
            WS_CHILD | WS_VISIBLE, 20, 20, 400, 40, hwnd, NULL, NULL, NULL);

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            20, 70, 400, 28, hwnd, (HMENU)1001, NULL, NULL);
        SendMessageW(edit, EM_SETLIMITTEXT, (WPARAM)ctx->cch - 1, 0);
        
        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            220, 115, 90, 30, hwnd, (HMENU)IDOK, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            320, 115, 90, 30, hwnd, (HMENU)IDCANCEL, NULL, NULL);
            
        return 0;
    }
    
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        if (id == IDOK) {
            if (ctx) GetDlgItemTextW(hwnd, 1001, ctx->out, ctx->cch);
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL InputBox(HWND owner, const wchar_t* title, const wchar_t* prompt,
                     wchar_t* out, DWORD cch) {
    out[0] = 0;
    InputBoxCtx ctx = { out, cch };

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = IBoxWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"XDHIBoxWnd_V3";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassExW(&wc);

    int w=440, h=180;
    int x=(GetSystemMetrics(SM_CXSCREEN)-w)/2;
    int y=(GetSystemMetrics(SM_CYSCREEN)-h)/2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        wc.lpszClassName, title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, NULL, wc.hInstance, &ctx);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetFocus(GetDlgItem(dlg, 1001));

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0)) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (owner) EnableWindow(owner, TRUE);
    return (out[0] != 0);
}

// ---------- 注册表 ----------
static BOOL RegSetStr(HKEY root, const wchar_t* sub, const wchar_t* name, const wchar_t* val) {
    HKEY hk;
    if (RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL)!=ERROR_SUCCESS)
        return FALSE;
    BOOL ok = RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val,
        (DWORD)((wcslen(val)+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
    RegCloseKey(hk);
    return ok;
}

static BOOL IsInstalled(void) {
    HKEY hk;
    BOOL r = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"exefile\\shell\\debugger",
        0, KEY_READ, &hk)==ERROR_SUCCESS;
    if (r) RegCloseKey(hk);
    return r;
}

static BOOL InstallApp(void) {
    CreateDirectoryW(L"C:\\Program Files", NULL);
    CreateDirectoryW(INSTALL_DIR, NULL);

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);

    wchar_t dest[MAX_PATH];
    swprintf(dest, MAX_PATH, L"%s\\XDHImageHijack.exe", INSTALL_DIR);

    // 复制两份本体（主程序 + 拦截器）
    if (_wcsicmp(self, dest) != 0) {
        if (!CopyFileW(self, dest, FALSE) || !CopyFileW(self, BLOCKER_EXE, FALSE)) {
            MsgErr(L"复制程序到 C 盘失败！请检查杀毒软件是否拦截。");
            return FALSE;
        }
    }

    wchar_t icon[MAX_PATH];
    GetWindowsDirectoryW(icon, MAX_PATH);
    wcscat(icon, L"\\system32\\imageres.dll,102");

    RegSetStr(HKEY_CLASSES_ROOT, L"exefile\\shell\\debugger", L"MUIVerb", L"映像劫持");
    RegSetStr(HKEY_CLASSES_ROOT, L"exefile\\shell\\debugger", L"SubCommands",
              L"XDHDynHijack;XDHDynCancel;XDHDynCustom");
    RegSetStr(HKEY_CLASSES_ROOT, L"exefile\\shell\\debugger", L"icon", icon);

    wchar_t sub[512], cmd[2048];

    swprintf(sub,512,L"%s\\XDHDynHijack",CMDSTORE);
    RegSetStr(HKEY_LOCAL_MACHINE, sub, NULL, L"劫持此程序(&Y)");
    swprintf(sub,512,L"%s\\XDHDynHijack\\Command",CMDSTORE);
    swprintf(cmd,2048,L"\"%s\" --hijack \"%%1\"", dest);
    RegSetStr(HKEY_LOCAL_MACHINE, sub, NULL, cmd);

    swprintf(sub,512,L"%s\\XDHDynCancel",CMDSTORE);
    RegSetStr(HKEY_LOCAL_MACHINE, sub, NULL, L"取消映像劫持(&N)");
    swprintf(sub,512,L"%s\\XDHDynCancel\\Command",CMDSTORE);
    swprintf(cmd,2048,L"\"%s\" --cancel \"%%1\"", dest);
    RegSetStr(HKEY_LOCAL_MACHINE, sub, NULL, cmd);

    swprintf(sub,512,L"%s\\XDHDynCustom",CMDSTORE);
    RegSetStr(HKEY_LOCAL_MACHINE, sub, NULL, L"自定义劫持(&Z)");
    swprintf(sub,512,L"%s\\XDHDynCustom\\Command",CMDSTORE);
    swprintf(cmd,2048,L"\"%s\" --custom \"%%1\"", dest);
    RegSetStr(HKEY_LOCAL_MACHINE, sub, NULL, cmd);

    HKEY hk;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, NULL, 0,
            KEY_SET_VALUE, NULL, &hk, NULL)==ERROR_SUCCESS) {
        wchar_t v[512];
        swprintf(v,512,L"%s",APP_NAME);
        RegSetValueExW(hk,L"DisplayName",0,REG_SZ,(BYTE*)v,(DWORD)((wcslen(v)+1)*2));
        swprintf(v,512,L"%s",APP_VERSION);
        RegSetValueExW(hk,L"DisplayVersion",0,REG_SZ,(BYTE*)v,(DWORD)((wcslen(v)+1)*2));
        swprintf(v,512,L"%s",APP_PUBLISHER);
        RegSetValueExW(hk,L"Publisher",0,REG_SZ,(BYTE*)v,(DWORD)((wcslen(v)+1)*2));
        swprintf(v,512,L"%s",INSTALL_DIR);
        RegSetValueExW(hk,L"InstallLocation",0,REG_SZ,(BYTE*)v,(DWORD)((wcslen(v)+1)*2));
        
        swprintf(v,2048,L"\"%s\" --uninstall", dest);
        RegSetValueExW(hk,L"UninstallString",0,REG_SZ,(BYTE*)v,(DWORD)((wcslen(v)+1)*2));
        swprintf(v,2048,L"\"%s\" --uninstall /S", dest);
        RegSetValueExW(hk,L"QuietUninstallString",0,REG_SZ,(BYTE*)v,(DWORD)((wcslen(v)+1)*2));
        DWORD one=1;
        RegSetValueExW(hk,L"NoModify",0,REG_DWORD,(BYTE*)&one,sizeof(one));
        RegSetValueExW(hk,L"NoRepair",0,REG_DWORD,(BYTE*)&one,sizeof(one));
        RegCloseKey(hk);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return TRUE;
}

static BOOL UninstallApp(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"exefile\\shell", 0, KEY_ALL_ACCESS, &hk)==ERROR_SUCCESS) {
        RegDeleteTreeW(hk, L"debugger"); RegCloseKey(hk);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, CMDSTORE, 0, KEY_ALL_ACCESS, &hk)==ERROR_SUCCESS) {
        RegDeleteTreeW(hk, L"XDHDynHijack");
        RegDeleteTreeW(hk, L"XDHDynCancel");
        RegDeleteTreeW(hk, L"XDHDynCustom");
        RegCloseKey(hk);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, KEY_ALL_ACCESS, &hk)==ERROR_SUCCESS) {
        RegCloseKey(hk);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return TRUE;
}

static void ParsePath(LPWSTR p, wchar_t* out, DWORD cch) {
    out[0] = 0;
    while (*p==L' ') p++;
    if (*p==L'"') {
        p++;
        const wchar_t* e = wcschr(p, L'"');
        if (e) { DWORD n=(DWORD)(e-p); if(n>=cch)n=cch-1; wcsncpy(out,p,n); out[n]=0; }
    } else {
        wcsncpy(out, p, cch-1); out[cch-1]=0;
    }
    wchar_t* t = out + wcslen(out);
    while (t>out && (*(t-1)==L' '||*(t-1)==L'\r'||*(t-1)==L'\n')) *--t=0;
}

static void SelfDelete(void) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    
    // 如果当前运行的在安装目录，顺便把整个文件夹删了
    if (_wcsicmp(self, MAIN_EXE_PATH) == 0 || _wcsicmp(self, BLOCKER_EXE) == 0) {
        wchar_t cmd[2048];
        swprintf(cmd, 2048, L"cmd.exe /c ping 127.0.0.1 -n 2 >nul & del /f /q \"%s\" & del /f /q \"%s\" & rd /s /q \"%s\"", MAIN_EXE_PATH, BLOCKER_EXE, INSTALL_DIR);
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    }
}

int wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInst;(void)hPrev;(void)nCmdShow;

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);

    // 【核心改动】如果当前运行的是 XDHBlocker.exe，说明是用户启动了被劫持的程序
    if (_wcsicmp(self, BLOCKER_EXE) == 0) {
        wchar_t msg[1024];
        // 提取 IFEO 的路径，它会被作为启动参数传进来
        swprintf(msg, 1024, L"该程序已被 XDHImageHijack 拦截并阻止运行！");
        MessageBoxW(NULL, msg, L"XDH 映像劫持拦截器", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return 0;
    }

    if (!IsAdmin()) {
        ElevateSelf(lpCmdLine);
        return 0;
    }

    if (wcsstr(lpCmdLine, L"--uninstall")) {
        UninstallApp();
        if (!wcsstr(lpCmdLine, L"/S")) MsgOk(L"卸载完成。");
        SelfDelete();
        return 0;
    }

    int op = -1;
    LPWSTR p = lpCmdLine;
    while (*p==L' ') p++;
    if (wcsncmp(p,L"--hijack",8)==0) { op=1; p+=8; }
    else if (wcsncmp(p,L"--cancel",8)==0) { op=0; p+=8; }
    else if (wcsncmp(p,L"--custom",8)==0) { op=2; p+=8; }

    if (op>=0) {
        wchar_t exePath[MAX_PATH];
        ParsePath(p, exePath, MAX_PATH);
        if (!exePath[0]) { MsgErr(L"未指定程序路径。"); return 1; }

        if (op==1) {
            if (DoHijack(exePath,1,NULL)) MsgOk(L"已成功阻止该程序运行！");
            else MsgErr(L"劫持失败。");
        } else if (op==0) {
            if (DoHijack(exePath,0,NULL)) MsgOk(L"已取消该程序的映像劫持。");
            else MsgErr(L"取消失败。");
        } else if (op==2) {
            wchar_t target[512]={0};
            if (InputBox(NULL,L"自定义映像劫持",
                    L"请输入要劫持到的程序名（如 calc.exe）：",target,512)) {
                if (DoHijack(exePath,2,target)) MsgOk(L"已自定义劫持。");
                else MsgErr(L"自定义劫持失败。");
            }
        }
        return 0;
    }

    // 修复这里：补全 MessageBoxW 的逗号，并加入卸载逻辑
    if (IsInstalled()) {
        int r = MessageBoxW(NULL,
            L"XDH映像劫持器 已安装！\n\n是否卸载？",
            APP_NAME, MB_YESNO|MB_ICONQUESTION);
        if (r == IDYES) {
            UninstallApp();
            MsgOk(L"卸载完成。");
            SelfDelete();
        }
    } else {
        int r = MessageBoxW(NULL,
            L"是否安装 XDH映像劫持器？\n\n"
            L"安装后右键任意 exe 文件将出现『映像劫持』菜单。\n",
            APP_NAME, MB_YESNO|MB_ICONQUESTION);
        if (r==IDYES) {
            if (InstallApp()) MsgOk(L"安装成功！");
            else MsgErr(L"安装失败。");
        }
    }
    return 0;
}