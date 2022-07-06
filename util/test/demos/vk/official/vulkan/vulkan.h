#ifndef VULKAN_H_
#define VULKAN_H_ 1

/*
** Copyright 2015-2022 The Khronos Group Inc.
**
** SPDX-License-Identifier: Apache-2.0
*/

#include "vk_platform.h"
#include "vulkan_core.h"

#ifdef VK_USE_PLATFORM_ANDROID_KHR
#include "vulkan_android.h"
#endif

#ifdef VK_USE_PLATFORM_FUCHSIA
#include <zircon/types.h>
#include "vulkan_fuchsia.h"
#endif

#ifdef VK_USE_PLATFORM_IOS_MVK
#include "vulkan_ios.h"
#endif


#ifdef VK_USE_PLATFORM_MACOS_MVK
#include "vulkan_macos.h"
#endif

#ifdef VK_USE_PLATFORM_METAL_EXT
#include "vulkan_metal.h"
#endif

#ifdef VK_USE_PLATFORM_VI_NN
#include "vulkan_vi.h"
#endif


#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#include "vulkan_wayland.h"
#endif


#ifdef VK_USE_PLATFORM_WIN32_KHR

// RenderDoc modification
// Want to allow building this on linux
//#include <windows.h>
typedef unsigned long DWORD;
typedef wchar_t WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef void *HANDLE;
struct HINSTANCE__; typedef struct HINSTANCE__ *HINSTANCE;
struct HMONITOR__; typedef struct HMONITOR__ *HMONITOR;
struct HWND__; typedef struct HWND__ *HWND;
struct _SECURITY_ATTRIBUTES; typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

#include "vulkan_win32.h"
#endif


#ifdef VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include "vulkan_xcb.h"
#endif


#ifdef VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#include "vulkan_xlib.h"
#endif


#ifdef VK_USE_PLATFORM_DIRECTFB_EXT
#include <directfb.h>
#include "vulkan_directfb.h"
#endif


#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
#include <X11/Xlib.h>

// RenderDoc modification
// Don't want to depend on Xrandr for this
//#include <X11/extensions/Xrandr.h>

typedef unsigned int RROutput;

#include "vulkan_xlib_xrandr.h"
#endif


#ifdef VK_USE_PLATFORM_GGP
#include <ggp_c/vulkan_types.h>
#include "vulkan_ggp.h"
#endif


#ifdef VK_USE_PLATFORM_SCREEN_QNX
#include <screen/screen.h>
#include "vulkan_screen.h"
#endif

#ifdef VK_ENABLE_BETA_EXTENSIONS
#include "vulkan_beta.h"
#endif

#endif // VULKAN_H_
