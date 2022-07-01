
#include "main.h"

#include <format>
#include <iostream>
#include <shellapi.h>
#include <strsafe.h>
#include <vector>
#include <wincodec.h>

#define HOOK_DATA WM_APP + 1

DWORD WINAPI workerMain(LPVOID);
LRESULT CALLBACK hookProc(int, WPARAM, LPARAM);
BOOL CALLBACK con_handler(DWORD);
int loadImage();

DWORD mainThreadId = NULL;
DWORD workerThreadId = NULL;
HHOOK hookHandle = NULL;
VmWrapper vm;

// BOOL enumTypes(HMODULE hModule, LPTSTR lpType, LONG_PTR lParam) {
//
// }

int main(char *argv, char **argc) {
    if (vm.status < 0)
        return -9999;
    int status = vm.Login();
    if (status < 0) {
        return -9999;
    }

    EnumResourceTypes(
        GetModuleHandle(NULL),
        [](HMODULE hModule, LPTSTR lpType, LONG_PTR lParam) -> BOOL {
            if (!IS_INTRESOURCE(lpType))
                std::wcout << "String resource: " << (TCHAR *)lpType << '\n';
            else
                std::cout << "Int resource: " << (USHORT)lpType << '\n';
            return TRUE;
        },
        0);
    HICON icon =
        (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(ICON_VB_MOD), IMAGE_ICON, 0, 0, LR_SHARED);
    if (icon == NULL)
        std::cout << "Failed to load icon: " << GetLastError() << std::endl;
    NOTIFYICONDATA nidata = {0};
    nidata.cbSize = sizeof(NOTIFYICONDATA);
    nidata.uFlags = NIF_GUID | NIF_TIP | NIF_ICON;
    if (StringCbCopy(nidata.szTip, sizeof(nidata.szTip), TEXT("VM Remote")) != S_OK)
        return -1;
    nidata.guidItem = notifyIconGuid;
    nidata.hIcon = icon;

    if (Shell_NotifyIcon(NIM_ADD, &nidata) == FALSE)
        std::cout << "Failed to add icon" << std::endl;

    mainThreadId = GetCurrentThreadId();
    SetConsoleCtrlHandler(con_handler, TRUE);

    hookHandle = SetWindowsHookEx(WH_KEYBOARD_LL, hookProc, GetModuleHandle(NULL), 0);
    if (hookHandle == NULL) {
        std::cout << "Hooking for input failed with: " << GetLastError() << std::endl;
        return -2;
    }

    HANDLE workerThread = CreateThread(NULL, 0, workerMain, NULL, 0, &workerThreadId);

    if (workerThread == NULL) {
        std::cout << "Failed to create worker thread" << std::endl;
        UnhookWindowsHookEx(hookHandle);
        return -3;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Stopping..." << std::endl;
    PostThreadMessage(workerThreadId, WM_QUIT, 0, 0);
    DWORD ret;
    if (ret = WaitForSingleObject(workerThread, 5000) == WAIT_TIMEOUT)
        std::cout << "Thread did't stop in 5 seconds, continuing..." << std::endl;
    CloseHandle(workerThread);
    UnhookWindowsHookEx(hookHandle);
    Shell_NotifyIcon(NIM_DELETE, &nidata);
    vm.Logout();
}

int loadImage() {
    HRESULT hr = S_OK;
    CoInitialize(NULL);
    IWICImagingFactory *pFactory;

    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr))
        return -9999;

    HRSRC imageResHandle = FindResource(NULL, MAKEINTRESOURCE(ICON_VB_MOD), TEXT("Image"));
    hr = (imageResHandle ? S_OK : E_FAIL);

    if (FAILED(hr))
        return -1;

    HGLOBAL imageDateHandle = LoadResource(NULL, imageResHandle);
    hr = (imageDateHandle ? S_OK : E_FAIL);

    if (FAILED(hr))
        return -2;

    void *pImageFile = LockResource(imageDateHandle);
    hr = (pImageFile ? S_OK : E_FAIL);

    if (FAILED(hr))
        return -3;

    DWORD imageFileSize = SizeofResource(NULL, imageResHandle);
    hr = (imageFileSize ? S_OK : E_FAIL);

    if (FAILED(hr))
        return -4;

    IWICStream *pImageStream;
    hr = pFactory->CreateStream(&pImageStream);

    if (FAILED(hr))
        return -5;

    hr = pImageStream->InitializeFromMemory(reinterpret_cast<BYTE *>(pImageFile), imageFileSize);
    if (FAILED(hr))
        return -6;

    IWICBitmapDecoder *pDecoder;
    hr = pFactory->CreateDecoderFromStream(pImageStream, NULL, WICDecodeMetadataCacheOnLoad, &pDecoder);

    if (FAILED(hr))
        return -7;

    IWICBitmapFrameDecode *pFrame;
    hr = pDecoder->GetFrame(0, &pFrame);

    if (FAILED(hr))
        return -8


}

bool selfUpdate = false;
float volume = 0;
void onTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (vm.IsParametersDirty() > 0 && !selfUpdate) {
        std::cout << "Refreshing params\n";
        vm.GetParameterFloat((char *)"Bus[4].Gain", &volume);
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
                    std::cout << "MEDIA_VOLUME_UP" << '\n';
                    selfUpdate = true;
                    if (volume < 12.0f)
                        volume += 1;
                    vm.SetParameterFloat((char *)"Bus[4].Gain", volume);
                    break;
                case VK_VOLUME_DOWN:
                    std::cout << "MEDIA_VOLUME_DOWN" << '\n';
                    selfUpdate = true;
                    if (volume > -60.0f)
                        volume -= 1;
                    vm.SetParameterFloat((char *)"Bus[4].Gain", volume);
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
    CopyMemory(keyInfo, (void *)lParam, sizeof(KBDLLHOOKSTRUCT));

    if (wParam == WM_KEYDOWN) {
        switch (keyInfo->vkCode) {
        case VK_VOLUME_UP:
            PostThreadMessage(workerThreadId, HOOK_DATA, 0, (WPARAM)keyInfo);
            return 1;
        case VK_VOLUME_DOWN:
            PostThreadMessage(workerThreadId, HOOK_DATA, 0, (WPARAM)keyInfo);
            return 1;
        case VK_VOLUME_MUTE:
            PostThreadMessage(mainThreadId, WM_QUIT, 0, 0);
            return 1;
        }
    }

    return CallNextHookEx(hookHandle, nCode, wParam, lParam);
}

BOOL CALLBACK con_handler(DWORD) {
    PostThreadMessage(mainThreadId, WM_QUIT, 0, 0);
    return TRUE;
}