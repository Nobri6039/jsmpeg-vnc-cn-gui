#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>  // 添加ShellExecute需要的头文件
#include <tchar.h>
#include <strsafe.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <vector>
#include <winbase.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")  // 添加shell32.lib链接

#define ID_EDIT_PORT       101
#define ID_EDIT_WINDOW     102
#define ID_EDIT_BITRATE    103
#define ID_EDIT_SIZE       104
#define ID_EDIT_FPS        105
#define ID_EDIT_CROP       106
#define ID_CHECK_INPUT     107
#define ID_CHECK_MOUSELOCK 108
#define ID_BUTTON_START    109
#define ID_BUTTON_STOP     110
#define ID_STATUS_TEXT     111
#define ID_LOG_EDIT        112
#define ID_URL_DISPLAY     113

#define WM_APPEND_LOG (WM_USER + 100)

HINSTANCE hInst;
HWND hEditPort, hEditWindow, hEditBitrate, hEditSize, hEditFps, hEditCrop;
HWND hCheckInput, hCheckMouseLock, hUrlDisplay;
HWND hButtonStart, hButtonStop, hStatusText, hLogEdit;
HFONT hFontNormal = NULL;      // 普通字体
HFONT hFontBold = NULL;        // 粗体字体
HFONT hFontLog = NULL;         // 日志字体
HBRUSH hBgBrush = NULL;        // 背景画刷
HCURSOR hHandCursor = NULL;    // 手型光标
PROCESS_INFORMATION procInfo = { 0 };
bool isRunning = false;
HANDLE hJobObject = NULL;
HANDLE hReadPipe = NULL;
HANDLE hWritePipe = NULL;
std::atomic<bool> stopReading{ false };
std::thread readThread;

// 初始化UI字体和样式
void InitUIFonts() {
    // 创建黑体字体（使用系统黑体）
    hFontNormal = CreateFontW(
        16,                         // 字体高度
        0,                          // 平均宽度
        0,                          // 角度
        0,                          // 基线角度
        FW_NORMAL,                  // 正常粗细
        FALSE,                      // 不斜体
        FALSE,                      // 无下划线
        FALSE,                      // 无删除线
        DEFAULT_CHARSET,            // 字符集
        OUT_DEFAULT_PRECIS,         // 输出精度
        CLIP_DEFAULT_PRECIS,        // 裁剪精度
        CLEARTYPE_QUALITY,          // 清晰类型质量（关键！消除锯齿）
        DEFAULT_PITCH | FF_DONTCARE,// 间距和家族
        L"Microsoft YaHei UI"       // 微软雅黑UI字体（Windows 10/11自带）
    );

    hFontBold = CreateFontW(
        18,                         // 稍大一点
        0,
        0,
        0,
        FW_BOLD,                    // 粗体
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI"
    );

    hFontLog = CreateFontW(
        14,                         // 日志字体稍小
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Consolas"                 // 等宽字体，适合日志
    );

    // 创建白色背景画刷
    hBgBrush = CreateSolidBrush(RGB(255, 255, 255));
}

// 初始化光标
void InitCursors() {
    hHandCursor = LoadCursorW(NULL, IDC_HAND);  // 加载手型光标
}

// 清理UI资源
void CleanupUIResources() {
    if (hFontNormal) DeleteObject(hFontNormal);
    if (hFontBold) DeleteObject(hFontBold);
    if (hFontLog) DeleteObject(hFontLog);
    if (hBgBrush) DeleteObject(hBgBrush);
    // 注意：系统光标不需要删除
}

// 线程安全日志
void AppendLog(LPCTSTR text) {
    if (!hLogEdit) return;
    LPTSTR strCopy = _tcsdup(text);
    PostMessage(hLogEdit, WM_APPEND_LOG, 0, (LPARAM)strCopy);
}

void Log(const TCHAR* fmt, ...) {
    if (!hLogEdit) return;

    TCHAR buf[2048];
    va_list args;
    va_start(args, fmt);
    _vstprintf_s(buf, fmt, args);
    va_end(args);

    size_t len = _tcslen(buf);
    if (len > 0 && buf[len - 1] != _T('\n')) {
        _tcscat_s(buf, _T("\r\n"));
    }

    int textLen = GetWindowTextLength(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, textLen, textLen);
    SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)buf);
    SendMessage(hLogEdit, EM_SCROLLCARET, 0, 0);
}

// 读取子进程输出线程
void ReadOutputThread() {
    char buffer[4096];
    DWORD bytesRead;

    while (!stopReading) {
        BOOL success = ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL);

        if (!success || bytesRead == 0) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                AppendLog(_T("[管道已断开]"));
                break;
            }
            Sleep(10);
            continue;
        }

        buffer[bytesRead] = '\0';

        // 转换到Unicode (UTF-8 -> UTF-16)
#ifdef UNICODE
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
        if (wideLen > 0) {
            std::vector<wchar_t> wstr(wideLen);
            MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wstr.data(), wideLen);
            AppendLog(wstr.data());
        }
#else
        AppendLog(buffer);
#endif
    }
}

// 终止进程树
BOOL TerminateProcessTree(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return FALSE;

    PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };
    if (Process32First(snapshot, &pe)) {
        do {
            if (pe.th32ParentProcessID == pid) {
                TerminateProcessTree(pe.th32ProcessID);
            }
        } while (Process32Next(snapshot, &pe));
    }

    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) {
        TerminateProcess(hProc, 0);
        CloseHandle(hProc);
    }

    CloseHandle(snapshot);
    return TRUE;
}

void UpdateUrlDisplay() {
    if (!hUrlDisplay || !hEditPort) return;

    wchar_t port[32];
    GetWindowTextW(hEditPort, port, 32);

    std::wstring url = L"http://localhost:";
    url += port;
    url += L"/";

    if (SendMessage(hCheckMouseLock, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        url += L"?mouselock";
    }

    SetWindowTextW(hUrlDisplay, url.c_str());
}

void StopServer() {
    if (!isRunning && !procInfo.hProcess) return;

    Log(_T("正在停止服务..."));

    // 停止读取线程
    stopReading = true;

    // 关闭写端，触发读取线程退出
    if (hWritePipe) {
        CloseHandle(hWritePipe);
        hWritePipe = NULL;
    }

    // 等待读取线程结束
    if (readThread.joinable()) {
        readThread.join();
    }

    // 终止进程树
    if (procInfo.dwProcessId != 0) {
        Log(_T("终止进程树 PID=%lu"), procInfo.dwProcessId);
        TerminateProcessTree(procInfo.dwProcessId);
    }

    // 清理作业对象
    if (hJobObject) {
        CloseHandle(hJobObject);
        hJobObject = NULL;
    }

    // 清理进程句柄
    if (procInfo.hThread) {
        CloseHandle(procInfo.hThread);
        procInfo.hThread = NULL;
    }
    if (procInfo.hProcess) {
        CloseHandle(procInfo.hProcess);
        procInfo.hProcess = NULL;
    }

    // 清理读管道
    if (hReadPipe) {
        CloseHandle(hReadPipe);
        hReadPipe = NULL;
    }

    ZeroMemory(&procInfo, sizeof(procInfo));
    isRunning = false;
    SetWindowText(hStatusText, _T("状态: 已停止"));
    EnableWindow(hButtonStart, TRUE);
    EnableWindow(hButtonStop, FALSE);
    Log(_T("服务已停止"));
}

void StartServer() {
    if (isRunning) return;

    // 获取程序目录
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = 0;

    // Bin目录
    std::wstring binDir = exePath;
    binDir += L"\\Bin";

    // jsmpeg-vnc.exe路径
    std::wstring vncExe = binDir + L"\\jsmpeg-vnc.exe";

    if (GetFileAttributesW(vncExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(_T("错误: 找不到 %s"), vncExe.c_str());
        return;
    }

    // 创建管道
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        Log(_T("错误: 创建管道失败"));
        return;
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // 获取参数
    wchar_t port[32], window[256], bitrate[32], size[32], fps[32], crop[128];
    GetWindowTextW(hEditPort, port, 32);
    GetWindowTextW(hEditWindow, window, 256);
    GetWindowTextW(hEditBitrate, bitrate, 32);
    GetWindowTextW(hEditSize, size, 32);
    GetWindowTextW(hEditFps, fps, 32);
    GetWindowTextW(hEditCrop, crop, 128);

    // 构建命令行 - 使用std::wstring避免运算符问题
    std::wstringstream cmdStream;
    cmdStream << L"\"" << vncExe << L"\"";

    if (wcslen(bitrate) > 0) {
        cmdStream << L" -b " << bitrate;
    }
    if (wcslen(size) > 0) {
        cmdStream << L" -s " << size;
    }
    if (wcslen(fps) > 0) {
        cmdStream << L" -f " << fps;
    }
    if (wcslen(port) > 0) {
        cmdStream << L" -p " << port;
    }
    if (wcslen(crop) > 0) {
        cmdStream << L" -c " << crop;
    }

    if (SendMessage(hCheckInput, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        cmdStream << L" -i 1";
    }
    else {
        cmdStream << L" -i 0";
    }

    if (wcslen(window) > 0) {
        cmdStream << L" " << window;
    }

    std::wstring cmdLine = cmdStream.str();

    Log(_T("执行: %s"), cmdLine.c_str());
    Log(_T("工作目录: %s"), binDir.c_str());

    // 创建作业对象
    hJobObject = CreateJobObjectW(NULL, NULL);
    if (hJobObject) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = { 0 };
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJobObject, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));
    }

    // 启动信息
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    DWORD flags = CREATE_NO_WINDOW;
    if (hJobObject) flags |= CREATE_SUSPENDED;

    // 使用CreateProcessW，所有参数都是宽字符
    if (CreateProcessW(
        vncExe.c_str(),           // lpApplicationName
        &cmdLine[0],              // lpCommandLine
        NULL, NULL, TRUE, flags, NULL, binDir.c_str(), &si, &procInfo
    )) {
        isRunning = true;
        SetWindowText(hStatusText, _T("状态: 运行中"));
        EnableWindow(hButtonStart, FALSE);
        EnableWindow(hButtonStop, TRUE);

        Log(_T("服务启动成功! PID=%lu"), procInfo.dwProcessId);

        // 加入作业对象
        if (hJobObject) {
            AssignProcessToJobObject(hJobObject, procInfo.hProcess);
            ResumeThread(procInfo.hThread);
        }

        // 启动读取线程
        stopReading = false;
        readThread = std::thread(ReadOutputThread);

        UpdateUrlDisplay();

        wchar_t url[256];
        GetWindowTextW(hUrlDisplay, url, 256);
        Log(_T("访问地址: %s"), url);
        Log(_T("--- 开始接收输出 ---"));
    }
    else {
        DWORD err = GetLastError();
        Log(_T("启动失败! 错误码: %lu"), err);
        SetWindowText(hStatusText, _T("状态: 启动失败"));

        if (hReadPipe) CloseHandle(hReadPipe);
        if (hWritePipe) CloseHandle(hWritePipe);
        if (hJobObject) CloseHandle(hJobObject);
        hReadPipe = hWritePipe = hJobObject = NULL;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        InitUIFonts();

        int y = 20;
        const int labelW = 95, editW = 190, smallW = 85, h = 26, gap = 32;

        // 端口
        HWND lblPort = CreateWindowW(L"STATIC", L"端口:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblPort, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hEditPort = CreateWindowW(L"EDIT", L"8080", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, labelW + 20, y, smallW - 25, h, hwnd, (HMENU)ID_EDIT_PORT, hInst, NULL);
        SendMessage(hEditPort, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap;

        // 窗口名
        HWND lblWindow = CreateWindowW(L"STATIC", L"窗口名:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblWindow, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hEditWindow = CreateWindowW(L"EDIT", L"desktop", WS_CHILD | WS_VISIBLE | WS_BORDER, labelW + 20, y, editW, h, hwnd, (HMENU)ID_EDIT_WINDOW, hInst, NULL);
        SendMessage(hEditWindow, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap;

        // 比特率
        HWND lblBitrate = CreateWindowW(L"STATIC", L"比特率(kbps):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblBitrate, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hEditBitrate = CreateWindowW(L"EDIT", L"3072", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, labelW + 20, y, smallW - 25, h, hwnd, (HMENU)ID_EDIT_BITRATE, hInst, NULL);
        SendMessage(hEditBitrate, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap;

        // 尺寸
        HWND lblSize = CreateWindowW(L"STATIC", L"分辨率:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblSize, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hEditSize = CreateWindowW(L"EDIT", L"1920x1080", WS_CHILD | WS_VISIBLE | WS_BORDER, labelW + 20, y, smallW, h, hwnd, (HMENU)ID_EDIT_SIZE, hInst, NULL);
        SendMessage(hEditSize, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        HWND hintSize = CreateWindowW(L"STATIC", L"(例: 1920x1080)", WS_CHILD | WS_VISIBLE, labelW + 20 + smallW + 10, y, 130, h, hwnd, NULL, hInst, NULL);
        SendMessage(hintSize, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap;

        // 帧率
        HWND lblFps = CreateWindowW(L"STATIC", L"帧率(FPS):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblFps, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hEditFps = CreateWindowW(L"EDIT", L"60", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, labelW + 20, y, smallW - 55, h, hwnd, (HMENU)ID_EDIT_FPS, hInst, NULL);
        SendMessage(hEditFps, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap;

        // 裁剪
        HWND lblCrop = CreateWindowW(L"STATIC", L"裁剪区域:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblCrop, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hEditCrop = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, labelW + 20, y, editW, h, hwnd, (HMENU)ID_EDIT_CROP, hInst, NULL);
        SendMessage(hEditCrop, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        HWND hintCrop = CreateWindowW(L"STATIC", L"(例: 100,50,800,600)", WS_CHILD | WS_VISIBLE, labelW + 20 + editW + 10, y, 160, h, hwnd, NULL, hInst, NULL);
        SendMessage(hintCrop, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap;

        // 远程输入
        HWND lblInput = CreateWindowW(L"STATIC", L"远程输入:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblInput, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hCheckInput = CreateWindowW(L"BUTTON", L"启用", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, labelW + 20, y, 70, h, hwnd, (HMENU)ID_CHECK_INPUT, hInst, NULL);
        SendMessage(hCheckInput, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessage(hCheckInput, BM_SETCHECK, BST_CHECKED, 0);
        y += gap;

        // 鼠标锁定
        HWND lblMouseLock = CreateWindowW(L"STATIC", L"鼠标锁定:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblMouseLock, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        hCheckMouseLock = CreateWindowW(L"BUTTON", L"URL添加 ?mouselock", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, labelW + 20, y, 180, h, hwnd, (HMENU)ID_CHECK_MOUSELOCK, hInst, NULL);
        SendMessage(hCheckMouseLock, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += gap + 8;

        // URL显示 - 添加SS_NOTIFY样式使其可点击
        HWND lblUrl = CreateWindowW(L"STATIC", L"访问地址:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, y, labelW, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblUrl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        hUrlDisplay = CreateWindowW(L"STATIC", L"http://localhost:8080/",
            WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_ENDELLIPSIS,  // 添加SS_NOTIFY和SS_ENDELLIPSIS样式
            labelW + 20, y, 320, h, hwnd, (HMENU)ID_URL_DISPLAY, hInst, NULL);
        SendMessage(hUrlDisplay, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        y += gap + 12;

        // 按钮
        hButtonStart = CreateWindowW(L"BUTTON", L"启动服务", WS_CHILD | WS_VISIBLE | BS_FLAT, 80, y, 100, 36, hwnd, (HMENU)ID_BUTTON_START, hInst, NULL);
        SendMessage(hButtonStart, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        hButtonStop = CreateWindowW(L"BUTTON", L"停止服务", WS_CHILD | WS_VISIBLE | BS_FLAT, 200, y, 100, 36, hwnd, (HMENU)ID_BUTTON_STOP, hInst, NULL);
        SendMessage(hButtonStop, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        EnableWindow(hButtonStop, FALSE);
        y += 50;

        // 状态
        hStatusText = CreateWindowW(L"STATIC", L"● 状态: 就绪", WS_CHILD | WS_VISIBLE, 15, y, 350, h, hwnd, (HMENU)ID_STATUS_TEXT, hInst, NULL);
        SendMessage(hStatusText, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        y += 35;

        // 日志
        HWND lblLog = CreateWindowW(L"STATIC", L"运行日志:", WS_CHILD | WS_VISIBLE, 15, y, 100, h, hwnd, NULL, hInst, NULL);
        SendMessage(lblLog, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        y += 24;
        hLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            15, y, 470, 200, hwnd, (HMENU)ID_LOG_EDIT, hInst, NULL);
        SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
        break;
    }

    case WM_SETCURSOR: {
        // 当鼠标在URL显示区域上时，显示手型光标
        if ((HWND)wp == hUrlDisplay && LOWORD(lp) == HTCLIENT) {
            if (hHandCursor) {
                SetCursor(hHandCursor);
                return TRUE;
            }
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND hCtrl = (HWND)lp;

        // 设置文本背景为透明
        SetBkMode(hdc, TRANSPARENT);

        // 根据控件ID设置不同的文本颜色
        if (hCtrl == hStatusText) {
            SetTextColor(hdc, RGB(0, 120, 215)); // 蓝色状态文本
            return (LRESULT)hBgBrush;
        }
        else if (hCtrl == hUrlDisplay) {
            SetTextColor(hdc, RGB(0, 102, 204)); // 更亮的蓝色URL，更像链接
            return (LRESULT)hBgBrush;
        }
        else if (hCtrl == hLogEdit) {
            SetTextColor(hdc, RGB(40, 40, 40)); // 深灰色日志
            return (LRESULT)hBgBrush;
        }
        else {
            SetTextColor(hdc, RGB(0, 0, 0)); // 黑色文本
            return (LRESULT)hBgBrush;
        }
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0)); // 黑色文本
        return (LRESULT)hBgBrush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)hBgBrush;
    }

    case WM_APPEND_LOG: {
        LPTSTR txt = (LPTSTR)lp;
        if (txt) {
            Log(_T("%s"), txt);
            free(txt);
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_EDIT_PORT:
            if (HIWORD(wp) == EN_CHANGE) UpdateUrlDisplay();
            break;
        case ID_CHECK_MOUSELOCK:
            UpdateUrlDisplay();
            break;
        case ID_BUTTON_START:
            StartServer();
            break;
        case ID_BUTTON_STOP:
            StopServer();
            break;
        case ID_URL_DISPLAY:
            if (HIWORD(wp) == STN_CLICKED) {
                // 获取当前URL
                wchar_t url[256];
                GetWindowTextW(hUrlDisplay, url, 256);

                // 使用默认浏览器打开URL
                HINSTANCE result = ShellExecuteW(
                    hwnd,                   // 父窗口句柄
                    L"open",               // 操作：打开
                    url,                   // URL
                    NULL,                  // 参数
                    NULL,                  // 工作目录
                    SW_SHOWNORMAL          // 显示方式
                );

                // 检查是否成功
                if ((INT_PTR)result <= 32) {
                    Log(_T("无法打开浏览器，错误码: %ld"), (INT_PTR)result);
                    MessageBoxW(hwnd, L"无法打开浏览器，请手动复制URL访问。", L"错误", MB_ICONERROR);
                }
                else {
                    Log(_T("已在浏览器中打开: %s"), url);
                }
            }
            break;
        }
        break;

    case WM_DESTROY:
        StopServer();
        CleanupUIResources();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nShow) {
    hInst = hInstance;

    // 初始化通用控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 初始化光标
    InitCursors();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)hBgBrush; // 使用白色背景
    wc.lpszClassName = L"JSMPEGVNC";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"JSMPEGVNC", L"jsmpeg-vnc GUI 控制面板",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, // 禁止最大化和调整大小
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 680, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopServer();
    CleanupUIResources();
    return 0;
}