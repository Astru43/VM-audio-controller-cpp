#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include "VmWrapper.h"
#include "resource.h"
#include <Windows.h>

// {BE2CFE76-08DD-4296-969B-B218607BBFAC}
static const GUID notifyIconGuid = {
    0xbe2cfe76, 0x8dd, 0x4296, {0x96, 0x9b, 0xb2, 0x18, 0x60, 0x7b, 0xbf, 0xac}};
