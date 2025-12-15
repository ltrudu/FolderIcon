#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
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
#pragma comment(lib, "uuid.lib")
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
#define ID_TIMER_CLICK_ANIM 2
#define CLICK_ANIM_DURATION_MS 2000
#define CLICK_ANIM_INTERVAL_MS 200

#define IDM_OPEN_FOLDER 2001
#define IDM_REGISTER_CONTEXT_MENU 2002
#define IDM_UNREGISTER_CONTEXT_MENU 2003

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
static int g_hoverIndex = -1;
static int g_clickedIndex = -1;
static int g_clickAnimAlpha = 255;
static BOOL g_clickAnimFading = TRUE;
static DWORD g_clickAnimStartTime = 0;

// Colors
static COLORREF g_bgColor;
static COLORREF g_headerBgColor;
static COLORREF g_textColor;
static COLORREF g_statusTextColor;
static COLORREF g_borderColor;
static COLORREF g_hoverBgColor;

static WCHAR g_exePath[MAX_PATH] = {0};

static void GetExePath(void) {
    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);
}

static void ShowNotification(const WCHAR* title, const WCHAR* message, BOOL isError) {
    // Create a temporary invisible window for the notification
    HWND hwndNotify = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                       HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);

    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwndNotify;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    nid.dwInfoFlags = isError ? NIIF_ERROR : NIIF_INFO;
    wcscpy_s(nid.szTip, 64, L"FolderIcon");
    wcscpy_s(nid.szInfoTitle, 64, title);
    wcscpy_s(nid.szInfo, 256, message);

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Wait briefly for notification to show, then remove icon
    Sleep(100);
    Shell_NotifyIconW(NIM_DELETE, &nid);

    DestroyWindow(hwndNotify);
}

static BOOL IsRunningAsAdmin(void) {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

static void RelaunchAsAdmin(const WCHAR* args) {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = g_exePath;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

static BOOL IsContextMenuRegistered(void) {
    HKEY hKey;

    // Check HKEY_CURRENT_USER first
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\Directory\\shell\\AddAsFolderIcon", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }

    // Check HKEY_CLASSES_ROOT
    result = RegOpenKeyExW(HKEY_CLASSES_ROOT,
        L"Directory\\shell\\AddAsFolderIcon", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }

    return FALSE;
}

static BOOL RegisterContextMenuForKey(HKEY hRoot, const WCHAR* basePath) {
    HKEY hKey, hCommandKey;
    LONG result;
    WCHAR keyPath[MAX_PATH];
    WCHAR commandKeyPath[MAX_PATH];

    swprintf_s(keyPath, MAX_PATH, L"%s\\shell\\AddAsFolderIcon", basePath);
    swprintf_s(commandKeyPath, MAX_PATH, L"%s\\shell\\AddAsFolderIcon\\command", basePath);

    // Create the main key
    result = RegCreateKeyExW(hRoot, keyPath, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS) return FALSE;

    // Set display name
    const WCHAR* displayName = L"Add as FolderIcon";
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)displayName,
                   (DWORD)((wcslen(displayName) + 1) * sizeof(WCHAR)));

    // Set icon to FolderIcon.exe
    RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (BYTE*)g_exePath,
                   (DWORD)((wcslen(g_exePath) + 1) * sizeof(WCHAR)));

    RegCloseKey(hKey);

    // Create command subkey
    result = RegCreateKeyExW(hRoot, commandKeyPath, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hCommandKey, NULL);
    if (result != ERROR_SUCCESS) return FALSE;

    // Set command
    WCHAR command[MAX_PATH * 2];
    swprintf_s(command, MAX_PATH * 2, L"\"%s\" --add-to-taskbar \"%%1\"", g_exePath);
    RegSetValueExW(hCommandKey, NULL, 0, REG_SZ, (BYTE*)command,
                   (DWORD)((wcslen(command) + 1) * sizeof(WCHAR)));

    RegCloseKey(hCommandKey);
    return TRUE;
}

static BOOL RegisterContextMenu(void) {
    BOOL success = FALSE;

    // Try HKEY_CURRENT_USER first (no admin required)
    if (RegisterContextMenuForKey(HKEY_CURRENT_USER, L"Software\\Classes\\Directory")) {
        success = TRUE;
    }

    // Also try HKEY_CLASSES_ROOT if running as admin (system-wide)
    if (IsRunningAsAdmin()) {
        if (RegisterContextMenuForKey(HKEY_CLASSES_ROOT, L"Directory")) {
            success = TRUE;
        }
    }

    return success;
}

static BOOL UnregisterContextMenu(void) {
    BOOL success = FALSE;

    // Delete from HKEY_CURRENT_USER
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\AddAsFolderIcon\\command");
    if (RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\AddAsFolderIcon") == ERROR_SUCCESS) {
        success = TRUE;
    }

    // Delete from HKEY_CLASSES_ROOT if running as admin
    if (IsRunningAsAdmin()) {
        RegDeleteKeyW(HKEY_CLASSES_ROOT, L"Directory\\shell\\AddAsFolderIcon\\command");
        if (RegDeleteKeyW(HKEY_CLASSES_ROOT, L"Directory\\shell\\AddAsFolderIcon") == ERROR_SUCCESS) {
            success = TRUE;
        }
    }

    return success;
}

static BOOL CreateFolderIconShortcut(const WCHAR* folderPath) {
    if (!folderPath || !folderPath[0] || !g_exePath[0]) {
        return FALSE;
    }

    IShellLinkW* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;
    BOOL success = FALSE;

    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void**)&pShellLink);
    if (FAILED(hr)) return FALSE;

    // Set shortcut target to FolderIcon.exe
    pShellLink->lpVtbl->SetPath(pShellLink, g_exePath);

    // Set arguments to the folder path
    WCHAR args[MAX_PATH + 3];
    swprintf_s(args, MAX_PATH + 3, L"\"%s\"", folderPath);
    pShellLink->lpVtbl->SetArguments(pShellLink, args);

    // Set working directory to exe directory
    WCHAR exeDir[MAX_PATH];
    wcscpy_s(exeDir, MAX_PATH, g_exePath);
    WCHAR* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    pShellLink->lpVtbl->SetWorkingDirectory(pShellLink, exeDir);

    // Set icon to the folder's icon
    pShellLink->lpVtbl->SetIconLocation(pShellLink, folderPath, 0);

    // Get folder name for description
    WCHAR folderName[MAX_PATH];
    const WCHAR* nameStart = wcsrchr(folderPath, L'\\');
    if (nameStart && *(nameStart + 1)) {
        wcscpy_s(folderName, MAX_PATH, nameStart + 1);
    } else {
        wcscpy_s(folderName, MAX_PATH, L"FolderIcon");
    }
    pShellLink->lpVtbl->SetDescription(pShellLink, folderName);

    // Create shortcut path: same location as folder with "-shortcut" suffix
    // e.g., C:\Users\Name\MyFolder -> C:\Users\Name\MyFolder-shortcut.lnk
    WCHAR shortcutPath[MAX_PATH];
    swprintf_s(shortcutPath, MAX_PATH, L"%s-shortcut.lnk", folderPath);

    // Save shortcut
    hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile, (void**)&pPersistFile);
    if (SUCCEEDED(hr)) {
        hr = pPersistFile->lpVtbl->Save(pPersistFile, shortcutPath, TRUE);
        if (SUCCEEDED(hr)) {
            success = TRUE;
            WCHAR msg[MAX_PATH];
            swprintf_s(msg, MAX_PATH, L"Shortcut created: %s-shortcut.lnk", folderName);
            ShowNotification(L"FolderIcon", msg, FALSE);
        } else {
            ShowNotification(L"FolderIcon", L"Failed to create shortcut", TRUE);
        }
        pPersistFile->lpVtbl->Release(pPersistFile);
    } else {
        ShowNotification(L"FolderIcon", L"Failed to create shortcut", TRUE);
    }

    pShellLink->lpVtbl->Release(pShellLink);
    return success;
}

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
        g_hoverBgColor = RGB(65, 65, 65);
    } else {
        g_bgColor = RGB(243, 243, 243);
        g_headerBgColor = RGB(255, 255, 255);
        g_textColor = RGB(26, 26, 26);
        g_statusTextColor = RGB(102, 102, 102);
        g_borderColor = RGB(229, 229, 229);
        g_hoverBgColor = RGB(225, 225, 225);
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

static BOOL IsShortcut(const WCHAR* path) {
    const WCHAR* ext = wcsrchr(path, L'.');
    return ext && _wcsicmp(ext, L".lnk") == 0;
}

static BOOL ResolveShortcut(const WCHAR* shortcutPath, WCHAR* targetPath, int targetPathSize) {
    BOOL success = FALSE;
    IShellLinkW* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;

    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void**)&pShellLink);
    if (SUCCEEDED(hr)) {
        hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile, (void**)&pPersistFile);
        if (SUCCEEDED(hr)) {
            hr = pPersistFile->lpVtbl->Load(pPersistFile, shortcutPath, STGM_READ);
            if (SUCCEEDED(hr)) {
                hr = pShellLink->lpVtbl->GetPath(pShellLink, targetPath, targetPathSize, NULL, 0);
                if (SUCCEEDED(hr) && targetPath[0] != L'\0') {
                    success = TRUE;
                }
            }
            pPersistFile->lpVtbl->Release(pPersistFile);
        }
        pShellLink->lpVtbl->Release(pShellLink);
    }

    return success;
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

            // Get icon - for shortcuts, get the target's icon without overlay arrow
            WCHAR iconPath[MAX_PATH];
            wcscpy_s(iconPath, MAX_PATH, item->szPath);

            if (IsShortcut(item->szPath)) {
                WCHAR targetPath[MAX_PATH];
                if (ResolveShortcut(item->szPath, targetPath, MAX_PATH)) {
                    wcscpy_s(iconPath, MAX_PATH, targetPath);
                }
            }

            SHFILEINFOW sfi = {0};
            SHGetFileInfoW(iconPath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);

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
        // Start the application
        ShellExecuteW(NULL, L"open", g_items[index].szPath, NULL, NULL, SW_SHOWNORMAL);

        // Start click animation
        g_clickedIndex = index;
        g_clickAnimAlpha = 255;
        g_clickAnimFading = TRUE;
        g_clickAnimStartTime = GetTickCount();
        SetTimer(g_hwndMain, ID_TIMER_CLICK_ANIM, CLICK_ANIM_INTERVAL_MS, NULL);
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
    switch (msg) {
        case WM_MOUSEMOVE: {
            // Track mouse to get WM_MOUSELEAVE
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);

            LVHITTESTINFO ht = {0};
            ht.pt.x = LOWORD(lParam);
            ht.pt.y = HIWORD(lParam);
            int index = ListView_HitTest(hwnd, &ht);

            if (index != g_hoverIndex) {
                int oldIndex = g_hoverIndex;
                g_hoverIndex = index;
                UpdateTooltip(hwnd, index);

                // Invalidate old and new items to trigger repaint
                if (oldIndex >= 0) {
                    RECT oldRect;
                    ListView_GetItemRect(hwnd, oldIndex, &oldRect, LVIR_BOUNDS);
                    InvalidateRect(hwnd, &oldRect, TRUE);
                }
                if (index >= 0) {
                    RECT newRect;
                    ListView_GetItemRect(hwnd, index, &newRect, LVIR_BOUNDS);
                    InvalidateRect(hwnd, &newRect, TRUE);
                }
            }
            break;
        }

        case WM_MOUSELEAVE: {
            if (g_hoverIndex >= 0) {
                RECT oldRect;
                ListView_GetItemRect(hwnd, g_hoverIndex, &oldRect, LVIR_BOUNDS);
                g_hoverIndex = -1;
                InvalidateRect(hwnd, &oldRect, TRUE);
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

        case WM_RBUTTONUP: {
            LVHITTESTINFO ht = {0};
            ht.pt.x = LOWORD(lParam);
            ht.pt.y = HIWORD(lParam);
            int index = ListView_HitTest(hwnd, &ht);

            // Show context menu only if not clicking on an icon
            if (index < 0) {
                POINT screenPt = ht.pt;
                ClientToScreen(hwnd, &screenPt);

                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDM_OPEN_FOLDER, L"Open folder in Explorer");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

                BOOL isRegistered = IsContextMenuRegistered();
                if (isRegistered) {
                    AppendMenuW(hMenu, MF_STRING, IDM_UNREGISTER_CONTEXT_MENU, L"Unregister Explorer context menu");
                } else {
                    AppendMenuW(hMenu, MF_STRING, IDM_REGISTER_CONTEXT_MENU, L"Register Explorer context menu");
                }

                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                         screenPt.x, screenPt.y, 0, g_hwndMain, NULL);
                DestroyMenu(hMenu);

                if (cmd == IDM_OPEN_FOLDER) {
                    ShellExecuteW(NULL, L"open", g_folderPath, NULL, NULL, SW_SHOWNORMAL);
                } else if (cmd == IDM_REGISTER_CONTEXT_MENU) {
                    if (RegisterContextMenu()) {
                        ShowNotification(L"FolderIcon", L"Context menu registered.\nUse 'Show more options' in Explorer.", FALSE);
                    } else {
                        ShowNotification(L"FolderIcon", L"Failed to register context menu", TRUE);
                    }
                } else if (cmd == IDM_UNREGISTER_CONTEXT_MENU) {
                    if (UnregisterContextMenu()) {
                        ShowNotification(L"FolderIcon", L"Context menu unregistered", FALSE);
                    } else {
                        ShowNotification(L"FolderIcon", L"Failed to unregister context menu", TRUE);
                    }
                }
                return 0;
            }
            break;
        }

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
            } else if (wParam == ID_TIMER_CLICK_ANIM) {
                DWORD elapsed = GetTickCount() - g_clickAnimStartTime;

                // Check if animation duration is over
                if (elapsed >= CLICK_ANIM_DURATION_MS) {
                    KillTimer(hwnd, ID_TIMER_CLICK_ANIM);
                    g_clickedIndex = -1;
                    g_isClosing = TRUE;
                    SetTimer(hwnd, ID_TIMER_FADE, 10, NULL);
                } else {
                    // Animate alpha: 255 -> 30 -> 255 -> 30 ...
                    int step = 45;
                    if (g_clickAnimFading) {
                        g_clickAnimAlpha -= step;
                        if (g_clickAnimAlpha <= 30) {
                            g_clickAnimAlpha = 30;
                            g_clickAnimFading = FALSE;
                        }
                    } else {
                        g_clickAnimAlpha += step;
                        if (g_clickAnimAlpha >= 255) {
                            g_clickAnimAlpha = 255;
                            g_clickAnimFading = TRUE;
                        }
                    }

                    // Invalidate clicked item to redraw
                    if (g_clickedIndex >= 0) {
                        RECT itemRect;
                        ListView_GetItemRect(g_hwndListView, g_clickedIndex, &itemRect, LVIR_BOUNDS);
                        InflateRect(&itemRect, 4, 4);
                        InvalidateRect(g_hwndListView, &itemRect, TRUE);
                    }
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

        case WM_NOTIFY: {
            NMHDR* nmhdr = (NMHDR*)lParam;
            if (nmhdr->hwndFrom == g_hwndListView && nmhdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* lvcd = (NMLVCUSTOMDRAW*)lParam;
                switch (lvcd->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;

                    case CDDS_ITEMPREPAINT:
                        return CDRF_NOTIFYPOSTPAINT;

                    case CDDS_ITEMPOSTPAINT: {
                        int itemIndex = (int)lvcd->nmcd.dwItemSpec;
                        BOOL isClicked = (itemIndex == g_clickedIndex && g_clickedIndex >= 0);
                        BOOL isHovered = (itemIndex == g_hoverIndex && g_hoverIndex >= 0 && !isClicked);

                        if (isClicked || isHovered) {
                            // Draw rounded border outline
                            RECT itemRect;
                            ListView_GetItemRect(g_hwndListView, itemIndex, &itemRect, LVIR_BOUNDS);

                            HDC hdc = lvcd->nmcd.hdc;
                            HBRUSH oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

                            // Calculate pen color with alpha blending for clicked items
                            COLORREF penColor = g_hoverBgColor;
                            if (isClicked) {
                                // Blend hover color with background based on alpha
                                int alpha = g_clickAnimAlpha;
                                int r = (GetRValue(g_hoverBgColor) * alpha + GetRValue(g_bgColor) * (255 - alpha)) / 255;
                                int g = (GetGValue(g_hoverBgColor) * alpha + GetGValue(g_bgColor) * (255 - alpha)) / 255;
                                int b = (GetBValue(g_hoverBgColor) * alpha + GetBValue(g_bgColor) * (255 - alpha)) / 255;
                                penColor = RGB(r, g, b);
                            }

                            HPEN hPen = CreatePen(PS_SOLID, 2, penColor);
                            HPEN oldPen = SelectObject(hdc, hPen);

                            RoundRect(hdc, itemRect.left + 3, itemRect.top + 3,
                                      itemRect.right - 3, itemRect.bottom - 3, 8, 8);

                            SelectObject(hdc, oldBrush);
                            SelectObject(hdc, oldPen);
                            DeleteObject(hPen);
                        }
                        return CDRF_DODEFAULT;
                    }
                }
            }
            break;
        }

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
    GetExePath();

    // Handle special command line arguments
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"--register") == 0) {
                if (RegisterContextMenu()) {
                    ShowNotification(L"FolderIcon", L"Context menu registered.\nUse 'Show more options' in Explorer.", FALSE);
                } else {
                    ShowNotification(L"FolderIcon", L"Failed to register context menu", TRUE);
                }
                LocalFree(argv);
                CoUninitialize();
                return 0;
            } else if (wcscmp(argv[i], L"--unregister") == 0) {
                if (UnregisterContextMenu()) {
                    ShowNotification(L"FolderIcon", L"Context menu unregistered", FALSE);
                } else {
                    ShowNotification(L"FolderIcon", L"Failed to unregister context menu", TRUE);
                }
                LocalFree(argv);
                CoUninitialize();
                return 0;
            } else if (wcscmp(argv[i], L"--add-to-taskbar") == 0 && i + 1 < argc) {
                CreateFolderIconShortcut(argv[i + 1]);
                LocalFree(argv);
                CoUninitialize();
                return 0;
            }
        }
        LocalFree(argv);
    }

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
