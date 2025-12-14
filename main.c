#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <wchar.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

// DWM constants for Windows 11 (if not already defined)
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MAX_ITEMS 256
#define ICON_SIZE 32
#define ICON_PADDING 8
#define ICON_CELL_SIZE (ICON_SIZE + ICON_PADDING * 2)
#define HEADER_HEIGHT 36
#define STATUS_HEIGHT 24
#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 400

#define IDC_LISTVIEW 1001
#define ID_TIMER_FADE 1

typedef struct FolderEntry {
    WCHAR szName[MAX_PATH];
    WCHAR szPath[MAX_PATH];
    BOOL bIsDirectory;
    int nIconIndex;
} FolderEntry;

static WCHAR g_folderPath[MAX_PATH] = {0};
static WCHAR g_folderName[MAX_PATH] = {0};
static FolderEntry g_items[MAX_ITEMS];
static int g_itemCount = 0;
static BOOL g_isDarkMode = FALSE;
static HIMAGELIST g_imageList = NULL;
static HWND g_hwndMain = NULL;
static HWND g_hwndListView = NULL;
static HWND g_hwndTooltip = NULL;
static BYTE g_opacity = 0;
static BOOL g_isClosing = FALSE;

// Colors
static COLORREF g_bgColor;
static COLORREF g_headerBgColor;
static COLORREF g_textColor;
static COLORREF g_statusTextColor;
static COLORREF g_borderColor;

static BOOL IsDarkModeEnabled(void) {
    HKEY hKey;
    DWORD value = 1;
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
        RegCloseKey(hKey);
    }
    return value == 0;
}

static void InitializeColors(void) {
    g_isDarkMode = IsDarkModeEnabled();

    if (g_isDarkMode) {
        g_bgColor = RGB(45, 45, 45);
        g_headerBgColor = RGB(56, 56, 56);
        g_textColor = RGB(255, 255, 255);
        g_statusTextColor = RGB(157, 157, 157);
        g_borderColor = RGB(61, 61, 61);
    } else {
        g_bgColor = RGB(243, 243, 243);
        g_headerBgColor = RGB(255, 255, 255);
        g_textColor = RGB(26, 26, 26);
        g_statusTextColor = RGB(102, 102, 102);
        g_borderColor = RGB(229, 229, 229);
    }
}

static void ParseCommandLine(void) {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv) {
        for (int i = 1; i < argc; i++) {
            if ((wcscmp(argv[i], L"--folder") == 0 || wcscmp(argv[i], L"-f") == 0) && i + 1 < argc) {
                wcscpy_s(g_folderPath, MAX_PATH, argv[++i]);
            } else if (argv[i][0] != L'-' && GetFileAttributesW(argv[i]) & FILE_ATTRIBUTE_DIRECTORY) {
                wcscpy_s(g_folderPath, MAX_PATH, argv[i]);
            }
        }
        LocalFree(argv);
    }

    if (g_folderPath[0] == 0) {
        SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, g_folderPath);
    }

    // Extract folder name
    WCHAR* lastSlash = wcsrchr(g_folderPath, L'\\');
    if (lastSlash && *(lastSlash + 1)) {
        wcscpy_s(g_folderName, MAX_PATH, lastSlash + 1);
    } else {
        wcscpy_s(g_folderName, MAX_PATH, g_folderPath);
    }
}

static int CompareItems(const void* a, const void* b) {
    const FolderEntry* itemA = (const FolderEntry*)a;
    const FolderEntry* itemB = (const FolderEntry*)b;

    if (itemA->bIsDirectory != itemB->bIsDirectory) {
        return itemB->bIsDirectory - itemA->bIsDirectory;
    }
    return _wcsicmp(itemA->szName, itemB->szName);
}

static void LoadFolderContents(void) {
    g_itemCount = 0;

    if (g_imageList) {
        ImageList_Destroy(g_imageList);
    }
    g_imageList = ImageList_Create(ICON_SIZE, ICON_SIZE, ILC_COLOR32 | ILC_MASK, 50, 50);

    WCHAR searchPath[MAX_PATH];
    swprintf_s(searchPath, MAX_PATH, L"%s\\*", g_folderPath);

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath, &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            if (g_itemCount >= MAX_ITEMS) break;

            FolderEntry* item = &g_items[g_itemCount];
            wcscpy_s(item->szName, MAX_PATH, findData.cFileName);
            swprintf_s(item->szPath, MAX_PATH, L"%s\\%s", g_folderPath, findData.cFileName);
            item->bIsDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            // Get icon
            SHFILEINFOW sfi = {0};
            SHGetFileInfoW(item->szPath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);

            if (sfi.hIcon) {
                item->nIconIndex = ImageList_AddIcon(g_imageList, sfi.hIcon);
                DestroyIcon(sfi.hIcon);
            } else {
                item->nIconIndex = -1;
            }

            g_itemCount++;
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    qsort(g_items, g_itemCount, sizeof(FolderEntry), CompareItems);
}

static void PositionWindow(HWND hwnd) {
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    HMONITOR hMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMonitor, &mi);

    RECT work = mi.rcWork;
    RECT bounds = mi.rcMonitor;

    int left = cursorPos.x;
    int top = cursorPos.y;

    // Determine taskbar position
    if (work.top > bounds.top) {
        top = work.top;
    } else if (work.bottom < bounds.bottom) {
        top = work.bottom - WINDOW_HEIGHT;
    } else if (work.left > bounds.left) {
        left = work.left;
    } else if (work.right < bounds.right) {
        left = work.right - WINDOW_WIDTH;
    } else {
        top = work.bottom - WINDOW_HEIGHT;
    }

    // Clamp to screen
    if (left + WINDOW_WIDTH > work.right) left = work.right - WINDOW_WIDTH;
    if (left < work.left) left = work.left;
    if (top + WINDOW_HEIGHT > work.bottom) top = work.bottom - WINDOW_HEIGHT;
    if (top < work.top) top = work.top;

    SetWindowPos(hwnd, NULL, left, top, WINDOW_WIDTH, WINDOW_HEIGHT, SWP_NOZORDER);
}

static void OpenItem(int index) {
    if (index >= 0 && index < g_itemCount) {
        ShellExecuteW(NULL, L"open", g_items[index].szPath, NULL, NULL, SW_SHOWNORMAL);
        g_isClosing = TRUE;
        SetTimer(g_hwndMain, ID_TIMER_FADE, 10, NULL);
    }
}

static void CreateTooltip(HWND hwndParent) {
    g_hwndTooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwndParent, NULL, GetModuleHandle(NULL), NULL);

    SetWindowPos(g_hwndTooltip, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static WCHAR g_tooltipText[MAX_PATH];

static void UpdateTooltip(HWND hwndLV, int index) {
    TOOLINFOW ti = { sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_hwndMain;
    ti.uId = (UINT_PTR)hwndLV;

    SendMessageW(g_hwndTooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);

    if (index >= 0 && index < g_itemCount) {
        wcscpy_s(g_tooltipText, MAX_PATH, g_items[index].szName);

        // Remove extension for files (not folders)
        if (!g_items[index].bIsDirectory) {
            WCHAR* dot = wcsrchr(g_tooltipText, L'.');
            if (dot && dot != g_tooltipText) {
                *dot = L'\0';
            }
        }

        ti.lpszText = g_tooltipText;
        SendMessageW(g_hwndTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(g_hwndTooltip, TTM_ACTIVATE, TRUE, 0);
    }
}

static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    static int lastHoverIndex = -1;

    switch (msg) {
        case WM_MOUSEMOVE: {
            LVHITTESTINFO ht = {0};
            ht.pt.x = LOWORD(lParam);
            ht.pt.y = HIWORD(lParam);
            int index = ListView_HitTest(hwnd, &ht);

            if (index != lastHoverIndex) {
                lastHoverIndex = index;
                UpdateTooltip(hwnd, index);
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            LVHITTESTINFO ht = {0};
            ht.pt.x = LOWORD(lParam);
            ht.pt.y = HIWORD(lParam);
            int index = ListView_HitTest(hwnd, &ht);
            if (index >= 0) {
                OpenItem(index);
                return 0;
            }
            break;
        }

        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ListViewSubclassProc, uIdSubclass);
            break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void CreateListView(HWND hwndParent) {
    RECT rc;
    GetClientRect(hwndParent, &rc);

    g_hwndListView = CreateWindowExW(
        0, WC_LISTVIEWW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_SINGLESEL | LVS_AUTOARRANGE,
        0, HEADER_HEIGHT,
        rc.right, rc.bottom - HEADER_HEIGHT - STATUS_HEIGHT,
        hwndParent, (HMENU)IDC_LISTVIEW, GetModuleHandle(NULL), NULL);

    ListView_SetImageList(g_hwndListView, g_imageList, LVSIL_NORMAL);
    ListView_SetIconSpacing(g_hwndListView, ICON_CELL_SIZE, ICON_CELL_SIZE);
    ListView_SetExtendedListViewStyle(g_hwndListView, LVS_EX_DOUBLEBUFFER);

    // Set colors
    ListView_SetBkColor(g_hwndListView, g_bgColor);
    ListView_SetTextBkColor(g_hwndListView, g_bgColor);
    ListView_SetTextColor(g_hwndListView, g_bgColor); // Hide text, show in tooltip only

    // Populate items
    for (int i = 0; i < g_itemCount; i++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_IMAGE | LVIF_PARAM;
        lvi.iItem = i;
        lvi.iImage = g_items[i].nIconIndex;
        lvi.lParam = i;
        ListView_InsertItem(g_hwndListView, &lvi);
    }

    SetWindowSubclass(g_hwndListView, ListViewSubclassProc, 0, 0);
    CreateTooltip(hwndParent);
}

static void PaintWindow(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Create memory DC for double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBitmap = SelectObject(memDC, memBitmap);

    // Fill background
    HBRUSH bgBrush = CreateSolidBrush(g_bgColor);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Draw header
    RECT headerRect = { 0, 0, rc.right, HEADER_HEIGHT };
    HBRUSH headerBrush = CreateSolidBrush(g_headerBgColor);
    FillRect(memDC, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    // Draw header text
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, g_textColor);
    HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = SelectObject(memDC, hFont);

    RECT textRect = { 12, 0, rc.right - 36, HEADER_HEIGHT };
    DrawTextW(memDC, g_folderName, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    // Draw status bar
    RECT statusRect = { 0, rc.bottom - STATUS_HEIGHT, rc.right, rc.bottom };
    HBRUSH statusBrush = CreateSolidBrush(g_isDarkMode ? RGB(37, 37, 37) : RGB(249, 249, 249));
    FillRect(memDC, &statusRect, statusBrush);
    DeleteObject(statusBrush);

    // Draw status text
    SetTextColor(memDC, g_statusTextColor);
    HFONT statusFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(memDC, statusFont);

    int folders = 0, files = 0;
    for (int i = 0; i < g_itemCount; i++) {
        if (g_items[i].bIsDirectory) folders++;
        else files++;
    }

    WCHAR statusText[64];
    if (folders > 0 && files > 0) {
        swprintf_s(statusText, 64, L"%d folder%s, %d file%s",
            folders, folders == 1 ? L"" : L"s",
            files, files == 1 ? L"" : L"s");
    } else if (folders > 0) {
        swprintf_s(statusText, 64, L"%d folder%s", folders, folders == 1 ? L"" : L"s");
    } else if (files > 0) {
        swprintf_s(statusText, 64, L"%d file%s", files, files == 1 ? L"" : L"s");
    } else {
        wcscpy_s(statusText, 64, L"Empty folder");
    }

    RECT statusTextRect = { 12, rc.bottom - STATUS_HEIGHT, rc.right - 12, rc.bottom };
    DrawTextW(memDC, statusText, -1, &statusTextRect, DT_SINGLELINE | DT_VCENTER);

    SelectObject(memDC, oldFont);
    DeleteObject(hFont);
    DeleteObject(statusFont);

    // Copy to screen
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Set rounded corners (Windows 11)
            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

            // Dark mode title bar
            BOOL darkMode = g_isDarkMode;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

            LoadFolderContents();
            CreateListView(hwnd);
            PositionWindow(hwnd);

            // Show immediately (fade-out only)
            g_opacity = 255;
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
            return 0;
        }

        case WM_TIMER:
            if (wParam == ID_TIMER_FADE && g_isClosing) {
                if (g_opacity <= 25) {
                    KillTimer(hwnd, ID_TIMER_FADE);
                    DestroyWindow(hwnd);
                } else {
                    g_opacity -= 25;
                    SetLayeredWindowAttributes(hwnd, 0, g_opacity, LWA_ALPHA);
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintWindow(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && !g_isClosing) {
                g_isClosing = TRUE;
                SetTimer(hwnd, ID_TIMER_FADE, 10, NULL);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            if (pt.y < HEADER_HEIGHT) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            if (pt.y < HEADER_HEIGHT) {
                ShellExecuteW(NULL, L"open", g_folderPath, NULL, NULL, SW_SHOWNORMAL);
                g_isClosing = TRUE;
                SetTimer(hwnd, ID_TIMER_FADE, 10, NULL);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_isClosing = TRUE;
                SetTimer(hwnd, ID_TIMER_FADE, 10, NULL);
            }
            return 0;

        case WM_DESTROY:
            if (g_imageList) {
                ImageList_Destroy(g_imageList);
                g_imageList = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    InitializeColors();
    ParseCommandLine();

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"FolderIconClass";
    RegisterClassExW(&wc);

    g_hwndMain = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"FolderIconClass", L"FolderIcon",
        WS_POPUP,
        0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_hwndMain, SW_SHOW);
    UpdateWindow(g_hwndMain);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
