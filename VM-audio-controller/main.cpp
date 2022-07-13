#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include "VmWrapper.h"
#include "framework.h"
#include "resource.h"
#include <Windows.h>

#include <comdef.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <strsafe.h>
#include <tchar.h>
#include <vector>
#include <wincodec.h>
#include <windowsx.h>

#define HOOK_DATA WM_APP + 1
#define APPWM_ICON WM_APP + 2

using json = nlohmann::json;
namespace fs = std::filesystem;

inline void throwOnFail(HRESULT hr) {
    if (FAILED(hr))
        throw _com_error(hr);
}

// Forward declarations
DWORD WINAPI workerMain(LPVOID);
LRESULT CALLBACK hookProc(int nCode, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK con_handler(DWORD);
HWND createWindow();
Channel selectChannel(WPARAM wParam, LPARAM lParam);
void updateMenu(WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void createMenu();
BOOL loadIcon(HICON *pRet);
void loadConfig(std::string config);
void saveConfig(std::string config);

// TODO: Refactor and reduce global variables.
// Global variables
HMENU hMenu = nullptr;
struct HSUBMENU {
    HMENU hBusMenu;
    HMENU hStripMenu;
} hSubMenu;

struct LastItem {
    HMENU hMenu = NULL;
    UINT index = NULL;
} lastItem;

struct Modifiers {
    bool ctrl = false;
    bool shift = false;
} modifiers;

HICON hIcon = nullptr;
DWORD mainThreadId = NULL;
DWORD workerThreadId = NULL;
HHOOK hookHandle = nullptr;
VmWrapper vm;
Channel channel;
bool selfUpdate = false;
bool hide = true;

int main(int argc, char **argv) {
    std::vector<std::string> args(argv, argv + argc);
    if (argc > 1) {
        if (std::find(args.begin(), args.end(), "-debug") != args.end()) {
            hide = false;
        }
    }
    if (hide) {
        HWND hConsole = GetConsoleWindow();
        ShowWindow(hConsole, SW_HIDE);
    }

    loadConfig("config.json");

    NOTIFYICONDATA nidata = {0};
    if (vm.status < 0)
        return -9999;
    if (vm.Login() < 0) {
        return -9999;
    }

    HWND hWnd = createWindow();
    if (!loadIcon(&hIcon) || hWnd == NULL)
        std::cout << "Failed to load icon: " << GetLastError() << std::endl;
    else {
        try {
            nidata.cbSize = sizeof(NOTIFYICONDATA);
            nidata.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE;
            throwOnFail(StringCbCopy(nidata.szTip, sizeof(nidata.szTip), TEXT("VM Remote")));
            nidata.uID = 1;
            nidata.hIcon = hIcon;
            nidata.hWnd = hWnd;
            nidata.uCallbackMessage = APPWM_ICON;
            nidata.uVersion = NOTIFYICON_VERSION_4;

            if (Shell_NotifyIcon(NIM_ADD, &nidata) == FALSE)
                std::cout << "Failed to add icon" << std::endl;
            else
                Shell_NotifyIcon(NIM_SETVERSION, &nidata);
        } catch (_com_error err) {
            std::cout << "Error copying string\n ";
        }
    }

    mainThreadId = GetCurrentThreadId();
    SetConsoleCtrlHandler(con_handler, TRUE);

    hookHandle = SetWindowsHookEx(WH_KEYBOARD_LL, hookProc, GetModuleHandle(NULL), 0);
    if (hookHandle == NULL) {
        std::cout << "Hooking for input failed with: " << GetLastError() << std::endl;
        return -1;
    }

    HANDLE workerThread = CreateThread(NULL, 0, workerMain, NULL, 0, &workerThreadId);

    if (workerThread == NULL) {
        std::cout << "Failed to create worker thread" << std::endl;
        UnhookWindowsHookEx(hookHandle);
        return -2;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Stopping..." << std::endl;
    PostThreadMessage(workerThreadId, WM_QUIT, 0, 0);
    if (WaitForSingleObject(workerThread, 5000) == WAIT_TIMEOUT)
        std::cout << "Thread did't stop in 5 seconds, continuing..." << std::endl;
    CloseHandle(workerThread);
    UnhookWindowsHookEx(hookHandle);
    Shell_NotifyIcon(NIM_DELETE, &nidata);
    if (hWnd != NULL)
        DestroyWindow(hWnd);
    vm.Logout();
    saveConfig("config.json");
}

BOOL loadIcon(HICON *pRet) {
    try {
        CComPtr<IWICImagingFactory> pFactory;
        CComPtr<IWICStream> pImageStream;
        CComPtr<IWICBitmapDecoder> pDecoder;
        CComPtr<IWICBitmapFrameDecode> pFrame;

        // Initialize the com for later use
        // --------------------------------------------------
        throwOnFail(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
        // hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        // IID_PPV_ARGS(&pFactory));
        throwOnFail(pFactory.CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER));
        // --------------------------------------------------

        // Get the wanted image from resources.
        // --------------------------------------------------
        HRSRC imageResHandle = FindResource(NULL, MAKEINTRESOURCE(ICON_VB_MOD), TEXT("png"));
        throwOnFail(imageResHandle ? S_OK : E_FAIL);

        HGLOBAL imageDateHandle = LoadResource(NULL, imageResHandle);
        throwOnFail(imageDateHandle ? S_OK : E_FAIL);

        LPVOID pImageFile = LockResource(imageDateHandle);
        throwOnFail(pImageFile ? S_OK : E_FAIL);

        DWORD imageFileSize = SizeofResource(NULL, imageResHandle);
        throwOnFail(imageFileSize ? S_OK : E_FAIL);
        // --------------------------------------------------

        throwOnFail(pFactory->CreateStream(&pImageStream));
        throwOnFail(pImageStream->InitializeFromMemory(reinterpret_cast<BYTE *>(pImageFile), imageFileSize));

        throwOnFail(
            pFactory->CreateDecoderFromStream(pImageStream, NULL, WICDecodeMetadataCacheOnLoad, &pDecoder));
        throwOnFail(pDecoder->GetFrame(0, &pFrame));

        // Get Image information from loaded image frame.
        // --------------------------------------------------
        WICPixelFormatGUID pixelFormat;
        CComPtr<IWICComponentInfo> componentInfo;
        CComPtr<IWICPixelFormatInfo> pixelFormatInfo;

        throwOnFail(pFrame->GetPixelFormat(&pixelFormat));
        throwOnFail(pFactory->CreateComponentInfo(pixelFormat, &componentInfo));

        // hr = componentInfo->QueryInterface(IID_PPV_ARGS(&pixelFormatInfo));
        throwOnFail(componentInfo.QueryInterface(&pixelFormatInfo));

        UINT width, height;
        throwOnFail(pFrame->GetSize(&width, &height));

        UINT bitsPerPixel;
        throwOnFail(pixelFormatInfo->GetBitsPerPixel(&bitsPerPixel));

        UINT colorChanels;
        throwOnFail(pixelFormatInfo->GetChannelCount(&colorChanels));

        float totalPixels = static_cast<float>(bitsPerPixel * width);
        UINT stride = static_cast<UINT>(totalPixels / 8);
        // --------------------------------------------------

        // Create bitmap for image.
        // --------------------------------------------------
        UINT bufferSize = stride * height;
        BYTE *pixels = NULL;

        BITMAPINFO bmpInfo = {0};
        bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmpInfo.bmiHeader.biWidth = width;
        bmpInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
        bmpInfo.bmiHeader.biPlanes = 1;
        bmpInfo.bmiHeader.biBitCount = bitsPerPixel;
        bmpInfo.bmiHeader.biCompression = BI_RGB;

        HDC hdc = GetDC(NULL);
        HBITMAP hBitmap =
            CreateDIBSection(hdc, &bmpInfo, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), NULL, 0);
        if (hBitmap == NULL) {
            ReleaseDC(NULL, hdc);
            throw std::invalid_argument("hBitmap is NULL");
        }

        // Copy pixels from disk to memory bitmap.
        throwOnFail(pFrame->CopyPixels(NULL, stride, bufferSize, pixels));
        // --------------------------------------------------

        // Create empty mask bitmap.
        HBITMAP bmpMask = CreateCompatibleBitmap(hdc, width, height);
        ReleaseDC(NULL, hdc);

        // Create icon from bitmaps and free resources.
        // --------------------------------------------------
        ICONINFO icon = {0};
        icon.fIcon = TRUE;
        icon.hbmColor = hBitmap;
        icon.hbmMask = bmpMask;
        *pRet = CreateIconIndirect(&icon);
        DeleteObject(bmpMask);
        DeleteObject(hBitmap);
        CoUninitialize();
        // --------------------------------------------------

        if (*pRet == NULL)
            throw std::invalid_argument("hIcon is NULL");
        return TRUE;
    } catch (_com_error err) {
        CoUninitialize();
        std::cout << "Error in loading icon\n";
        return FALSE;
    } catch (std::invalid_argument err) {
        CoUninitialize();
        std::cout << "Error in creating \n";
        return FALSE;
    }
}

void onTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (vm.IsParametersDirty() > 0 && !selfUpdate) {
        std::cout << "Refreshing params\n";
        vm.GetParameterFloat(channel.gain(), &channel.volume);
    } else
        selfUpdate = false;
}

DWORD WINAPI workerMain(LPVOID) {
    UINT_PTR timer = SetTimer(NULL, 0, 10, onTimer);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == HOOK_DATA) {
            KBDLLHOOKSTRUCT *data = (KBDLLHOOKSTRUCT *)msg.lParam;
            if (data != nullptr) {
                switch (data->vkCode) {
                case VK_VOLUME_UP:
                    selfUpdate = true;
                    channel.incVol(1);
                    vm.SetParameterFloat(channel.gain(), channel.volume);
                    break;
                case VK_VOLUME_DOWN:
                    selfUpdate = true;
                    channel.decVol(1);
                    vm.SetParameterFloat(channel.gain(), channel.volume);
                    break;
                case VK_RCONTROL:
                case VK_LCONTROL:
                    if (msg.wParam == WM_KEYDOWN && !modifiers.ctrl) {
                        modifiers.ctrl = true;
                    } else if (msg.wParam == WM_KEYUP && modifiers.ctrl) {
                        modifiers.ctrl = false;
                    }
                    break;
                case VK_RSHIFT:
                case VK_LSHIFT:
                    if (msg.wParam == WM_KEYDOWN && !modifiers.shift) {
                        modifiers.shift = true;
                    } else if (msg.wParam == WM_KEYUP && modifiers.shift) {
                        modifiers.shift = false;
                    }
                    break;
                case VK_VOLUME_MUTE:
                    if (modifiers.ctrl && modifiers.shift) {
                        HWND hConsole = GetConsoleWindow();
                        if (hConsole) {
                            if (hide) {
                                hide = false;
                                ShowWindow(hConsole, SW_SHOW);
                            } else {
                                hide = true;
                                ShowWindow(hConsole, SW_HIDE);
                            }
                        }
                    } else {
                        PostThreadMessage(mainThreadId, WM_QUIT, 0, 0);
                    }
                    break;
                }
                delete data;
            }
        } else {
            DispatchMessage(&msg);
        }
    }
    KillTimer(NULL, timer);
    return 0;
}

LRESULT CALLBACK hookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(hookHandle, nCode, wParam, lParam);
    KBDLLHOOKSTRUCT *keyInfo = new KBDLLHOOKSTRUCT;
    CopyMemory(keyInfo, reinterpret_cast<void *>(lParam), sizeof(KBDLLHOOKSTRUCT));

    if (wParam == WM_KEYDOWN) {
        switch (keyInfo->vkCode) {
        case VK_VOLUME_UP:
        case VK_VOLUME_DOWN:
        case VK_VOLUME_MUTE:
            PostThreadMessage(workerThreadId, HOOK_DATA, 0, reinterpret_cast<LPARAM>(keyInfo));
            return 1;
        }
    }

    switch (keyInfo->vkCode) {
    case VK_RCONTROL:
    case VK_LCONTROL:
    case VK_RSHIFT:
    case VK_LSHIFT:
        PostThreadMessage(workerThreadId, HOOK_DATA, wParam, reinterpret_cast<LPARAM>(keyInfo));
        return 0;
    }

    delete keyInfo;
    return CallNextHookEx(hookHandle, nCode, wParam, lParam);
}

BOOL CALLBACK con_handler(DWORD) {
    PostThreadMessage(mainThreadId, WM_QUIT, 0, 0);
    return TRUE;
}

HWND createWindow() {
    static bool REGISTERED = false;
    if (!REGISTERED) {
        WNDCLASS wndClass = {0};
        wndClass.lpszClassName = TEXT("ICON_WINDOW");
        wndClass.hInstance = GetModuleHandle(NULL);
        wndClass.lpfnWndProc = wndProc;
        RegisterClass(&wndClass);
        REGISTERED = true;
    }

    return CreateWindow(TEXT("ICON_WINDOW"), TEXT(""), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, GetModuleHandle(NULL), NULL);
}

Channel selectChannel(WPARAM wParam, LPARAM lParam) {
    Channel ret;
    HMENU hSelectedMenu = reinterpret_cast<HMENU>(lParam);
    if (hSelectedMenu == hSubMenu.hBusMenu) {
        ret.type = Channel::BUS;
        ret.index = static_cast<UINT>(wParam);
        ret.name = std::format("Bus[{:d}]", wParam);
        return ret;
    } else if (hSelectedMenu == hSubMenu.hStripMenu) {
        ret.type = Channel::STRIP;
        ret.index = static_cast<UINT>(wParam);
        ret.name = std::format("Strip[{:d}]", wParam);
        return ret;
    }
    ret.index = 0;
    ret.name = std::string();
    return ret;
}

void updateMenu(WPARAM wParam, LPARAM lParam) {
    HMENU hSelectedMenu = reinterpret_cast<HMENU>(lParam);
    if (lastItem.hMenu == hSelectedMenu && lastItem.index == wParam)
        return;

    if (lastItem.hMenu != NULL) {
        MENUITEMINFO lastItemInfo = {0};
        lastItemInfo.cbSize = sizeof(MENUITEMINFO);
        lastItemInfo.fMask = MIIM_STATE;
        lastItemInfo.fState = MFS_UNCHECKED;

        SetMenuItemInfo(lastItem.hMenu, lastItem.index, TRUE, &lastItemInfo);
    }

    MENUITEMINFO currentItemInfo = {0};
    currentItemInfo.cbSize = sizeof(MENUITEMINFO);
    currentItemInfo.fMask = MIIM_STATE;
    currentItemInfo.fState = MFS_CHECKED;

    SetMenuItemInfo(hSelectedMenu, static_cast<UINT>(wParam), TRUE, &currentItemInfo);
    lastItem.hMenu = hSelectedMenu;
    lastItem.index = static_cast<UINT>(wParam);
}

LRESULT CALLBACK wndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_MENUCOMMAND: {
        if (reinterpret_cast<HMENU>(lParam) == hMenu) {
            HWND hConsole = GetConsoleWindow();
            MENUITEMINFO info = {0};
            info.cbSize = sizeof(MENUITEMINFO);
            info.fMask = MIIM_STATE;
            if (hide) {
                info.fState = MFS_CHECKED;
                SetMenuItemInfo(hMenu, wParam, TRUE, &info);
                hide = false;
                ShowWindow(hConsole, SW_SHOW);
            } else {
                info.fState = MFS_UNCHECKED;
                SetMenuItemInfo(hMenu, wParam, TRUE, &info);
                hide = true;
                ShowWindow(hConsole, SW_HIDE);
            }
        } else {
            channel = selectChannel(wParam, lParam);
            updateMenu(wParam, lParam);
            vm.GetParameterFloat(channel.gain(), &channel.volume);
        }
        return 0;
    }
    case APPWM_ICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            PostThreadMessage(mainThreadId, WM_QUIT, NULL, NULL);
            return 0;
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU:
            if (hMenu == NULL)
                createMenu();
            SetForegroundWindow(hWnd);
            if (hMenu != NULL)
                TrackPopupMenuEx(
                    hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_HORIZONTAL | TPM_VERTICAL,
                    GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam), hWnd, NULL);
            break;
        default:
            break;
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

void createMenu() {
    hMenu = CreatePopupMenu();
    HMENU hBusMenu = CreatePopupMenu();
    HMENU hStripMenu = CreatePopupMenu();

    MENUINFO menuInfo = {0};
    menuInfo.cbSize = sizeof(MENUINFO);
    menuInfo.fMask = MIM_STYLE;
    menuInfo.dwStyle = MNS_NOTIFYBYPOS;
    SetMenuInfo(hMenu, &menuInfo);

    TCHAR bus[] = TEXT("Bus");
    TCHAR strip[] = TEXT("Strip");

    MENUITEMINFO busMenuItem = {0};
    busMenuItem.cbSize = sizeof(MENUITEMINFO);
    busMenuItem.fMask = MIIM_SUBMENU | MIIM_STRING;
    busMenuItem.hSubMenu = hBusMenu;
    busMenuItem.dwTypeData = bus;

    MENUITEMINFO stripMenuItem = {0};
    stripMenuItem.cbSize = sizeof(MENUITEMINFO);
    stripMenuItem.fMask = MIIM_SUBMENU | MIIM_STRING;
    stripMenuItem.hSubMenu = hStripMenu;
    stripMenuItem.dwTypeData = strip;

    int pos = 0;
    for (UINT i = 0; i < vm.BUS_LENGTH; i++) {
        TCHAR str[10];
        StringCchCopy(str, 10, std::format(TEXT("BUS {:d}"), i).c_str());

        MENUITEMINFO menuItemInfo = {0};
        menuItemInfo.cbSize = sizeof(MENUITEMINFO);
        menuItemInfo.fMask = MIIM_STRING | MIIM_FTYPE;
        menuItemInfo.fType = MFT_RADIOCHECK;
        menuItemInfo.dwTypeData = str;

        InsertMenuItem(hBusMenu, i, TRUE, &menuItemInfo);
    }

    for (UINT i = 0; i < vm.STRIP_LENGTH; i++) {
        TCHAR str[10];
        StringCchCopy(str, 10, std::format(TEXT("STRIP {:d}"), i).c_str());

        MENUITEMINFO menuItemInfo = {0};
        menuItemInfo.cbSize = sizeof(MENUITEMINFO);
        menuItemInfo.fMask = MIIM_STRING | MIIM_FTYPE;
        menuItemInfo.fType = MFT_RADIOCHECK;
        menuItemInfo.dwTypeData = str;

        InsertMenuItem(hStripMenu, i, TRUE, &menuItemInfo);
    }

    MENUITEMINFO menuItemInfo = {0};
    menuItemInfo.cbSize = sizeof(MENUITEMINFO);
    menuItemInfo.fMask = MIIM_STATE;
    menuItemInfo.fState = MFS_CHECKED;
    if (channel.type == Channel::BUS) {
        SetMenuItemInfo(hBusMenu, channel.index, TRUE, &menuItemInfo);
        lastItem.hMenu = hBusMenu;
        lastItem.index = channel.index;
    } else {
        SetMenuItemInfo(hStripMenu, channel.index, TRUE, &menuItemInfo);
        lastItem.hMenu = hStripMenu;
        lastItem.index = channel.index;
    }

    hSubMenu.hBusMenu = hBusMenu;
    hSubMenu.hStripMenu = hStripMenu;

    InsertMenuItem(hMenu, pos++, TRUE, &busMenuItem);
    InsertMenuItem(hMenu, pos++, TRUE, &stripMenuItem);

    menuItemInfo = {0};
    menuItemInfo.cbSize = sizeof(MENUITEMINFO);
    menuItemInfo.fMask = MIIM_STRING | MIIM_STATE;
    TCHAR str[] = TEXT("Show");
    menuItemInfo.dwTypeData = str;
    menuItemInfo.fState = hide ? MFS_UNCHECKED : MFS_CHECKED;

    InsertMenuItem(hMenu, pos++, TRUE, &menuItemInfo);
}

void loadConfig(std::string config) {
    auto file = fs::absolute(config);
    if (fs::exists(file)) {
        std::cout << "Loading config\n";
        auto jsonFile = std::ifstream(file);
        json config;

        try {
            jsonFile >> config;
            channel.name = config["channel"]["name"];
            channel.type = config["channel"]["type"];
            channel.index = config["channel"]["index"];

            std::cout << config.dump(4) << std::endl;
        } catch (json::parse_error &err) {
            std::cerr << "Invalid json: " << err.what();
        }
    }
}

void saveConfig(std::string config) {
    using std::ios;
    auto file = fs::absolute(config);
    if (fs::exists(file)) {
        auto jsonFile = std::fstream(file, ios::in | ios::out);
        json config;
        try {
            jsonFile >> config;
        } catch (json::parse_error &) {
            config.clear();
        }

        config["channel"] = {
            {"name", channel.name},
            {"type", channel.type},
            {"index", channel.index},
        };
        fs::resize_file(file, 0);
        jsonFile.seekp(0);
        jsonFile << config;
    } else {
        auto jsonFile = std::ofstream(file);
        json config;
        config["channel"] = {
            {"name", channel.name},
            {"type", channel.type},
            {"index", channel.index},
        };
        jsonFile << config;
    }
}
