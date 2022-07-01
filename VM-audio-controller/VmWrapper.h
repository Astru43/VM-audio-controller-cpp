#pragma once

#include "VoicemeeterRemote.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>

class VmWrapper {
    HMODULE vmLib = NULL;
    bool connected = false;
    
    T_VBVMR_Login _Login = NULL;
    T_VBVMR_Logout _Logout = NULL;
    
    int Loadfunctions();

  public:
    VmWrapper();
    ~VmWrapper();

    int Login();
    void Logout();

    T_VBVMR_GetParameterFloat GetParameterFloat = NULL;
    T_VBVMR_SetParameterFloat SetParameterFloat = NULL;
    T_VBVMR_IsParametersDirty IsParametersDirty = NULL;

    int status = 0;
};
