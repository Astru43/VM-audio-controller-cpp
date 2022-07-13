#pragma once

#include "VoicemeeterRemote.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include <Windows.h>
#include <string>

template <class F, typename T> class ReadOnly {
    friend F;
    T data;
    ReadOnly(T value) : data(value){};
    T &operator=(const T &value) {
        data = value;
        return data;
    };

  public:
    ReadOnly(ReadOnly<F, T> &) = delete;
    operator const T &() const { return data; };
};

class VmWrapper {
    HMODULE vmLib = NULL;
    bool connected = false;

    T_VBVMR_Login _Login = NULL;
    T_VBVMR_Logout _Logout = NULL;
    T_VBVMR_GetVoicemeeterType _GetVoicemeeterType = NULL;

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

    ReadOnly<VmWrapper, UINT> BUS_LENGTH;
    enum Bus {
        BUS0,
        BUS1,
        BUS2,
        BUS3,
        BUS4,
        BUS5,
        BUS6,
        BUS7,
    };
    ReadOnly<VmWrapper, UINT> STRIP_LENGTH;
    enum Strip {
        STRIP0,
        STRIP1,
        STRIP2,
        STRIP3,
        STRIP4,
        STRIP5,
        STRIP6,
        STRIP7,
    };
};

struct Channel {
    enum CHANNELTYPE { BUS, STRIP } type = BUS;
    UINT index = 4;
    std::string name = "Bus[4]";
    float volume = 0;

    char *gain() const {
        tmp = name + ".Gain";
        return tmp.data();
    }

    void incVol(const float &val) {
        if (volume < 12.0f)
            volume += val;
    }
    void decVol(const float &val) {
        if (volume > -60.0f)
            volume -= val;
    }

  private:
    mutable std::string tmp;
};