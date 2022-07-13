#include "VmWrapper.h"

#include <filesystem>
#include <iostream>
#include <vector>

VmWrapper::VmWrapper() : BUS_LENGTH(0), STRIP_LENGTH(0) {
    namespace fs = std::filesystem;
    LPCTSTR path = TEXT("SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:"
                        "Voicemeeter {17359A74-1236-5467}");
    HKEY key;
    LSTATUS ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);

    if (ret != ERROR_SUCCESS) {
        std::cout << "Failed to find voicemeeter" << std::endl;
        status = -1;
        return;
    }

    std::vector<TCHAR> vmUninstallString;
    DWORD size = 512;
    vmUninstallString.resize(size);
    ret =
        RegGetValue(key, NULL, TEXT("UninstallString"), RRF_RT_REG_SZ, NULL, vmUninstallString.data(), &size);
    if (ret == ERROR_MORE_DATA) {
        std::cout << size << " :New size" << '\n';
        vmUninstallString.resize(size);
        ret = RegGetValue(key, NULL, TEXT("UninstallString"), RRF_RT_REG_SZ, NULL, vmUninstallString.data(),
                          &size);
        if (ret == ERROR_MORE_DATA) {
            std::cout << "Failed to find voicemeeter" << std::endl;
            status = -2;
            return;
        }
    }
    vmUninstallString.shrink_to_fit();
    fs::path vmPath(vmUninstallString.data());
    vmPath.remove_filename();

    if (sizeof(void *) == 8)
        vmPath.append("voicemeeterRemote64.dll");
    else
        vmPath.append("voicemeeterRemote.dll");

    vmLib = LoadLibrary(vmPath.c_str());
    if (vmLib == NULL) {
        std::cout << "Error loading voicemeeter" << std::endl;
        status = -3;
        return;
    }
    int err = Loadfunctions();
    if (err < 0) {
        std::cout << "Failed to get voicemeeter functions: " << err << std::endl;
        status = -4;
        return;
    }

    return;
}

VmWrapper::~VmWrapper() {
    // if (connected)
    //     Logout();
    HMODULE
    FreeLibrary(vmLib);
    vmLib = NULL;
}

int VmWrapper::Login() {
    if (_Login != NULL) {
        int ret;
        if (ret = _Login() < 0)
            connected = false;
        else {
            long vmType;
            _GetVoicemeeterType(&vmType);
            switch (vmType) {
            case 1:
                BUS_LENGTH = 2;
                STRIP_LENGTH = 3;
                break;
            case 2:
                BUS_LENGTH = STRIP_LENGTH = 5;
                break;
            case 3:
                BUS_LENGTH = STRIP_LENGTH = 8;
                break;
            }
            connected = true;
        }
        return ret;
    }
    return -9999;
}

void VmWrapper::Logout() {
    if (connected) {
        _Logout();
        connected = false;
    }
}

int VmWrapper::Loadfunctions() {
    if (vmLib != NULL) {
        _Login = (T_VBVMR_Login)GetProcAddress(vmLib, "VBVMR_Login");
        _Logout = (T_VBVMR_Logout)GetProcAddress(vmLib, "VBVMR_Logout");
        if (_Login == NULL || _Logout == NULL)
            return -1;

        GetParameterFloat = (T_VBVMR_GetParameterFloat)GetProcAddress(vmLib, "VBVMR_GetParameterFloat");
        SetParameterFloat = (T_VBVMR_SetParameterFloat)GetProcAddress(vmLib, "VBVMR_SetParameterFloat");
        if (GetParameterFloat == NULL || SetParameterFloat == NULL)
            return -2;

        IsParametersDirty = (T_VBVMR_IsParametersDirty)GetProcAddress(vmLib, "VBVMR_IsParametersDirty");
        if (IsParametersDirty == NULL)
            return -3;

        _GetVoicemeeterType = (T_VBVMR_GetVoicemeeterType)GetProcAddress(vmLib, "VBVMR_GetVoicemeeterType");
        if (_GetVoicemeeterType == NULL)
            return -4;

    } else
        return -9999;
    return 0;
}
