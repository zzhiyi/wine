/*
 * Wine X11drv display settings functions
 *
 * Copyright 2003 Alexander James Pasadyn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include <string.h>
#include <stdio.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "x11drv.h"

#include "windef.h"
#include "winreg.h"
#include "wingdi.h"
#include "wine/debug.h"
#include "wine/heap.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(x11settings);

static struct x11drv_mode_info *dd_modes = NULL;
static unsigned int dd_mode_count = 0;
static unsigned int dd_max_modes = 0;
/* All Windows drivers seen so far either support 32 bit depths, or 24 bit depths, but never both. So if we have
 * a 32 bit framebuffer, report 32 bit bpps, otherwise 24 bit ones.
 */
static const unsigned int depths_24[]  = {8, 16, 24};
static const unsigned int depths_32[]  = {8, 16, 32};
const DWORD *depths;

/* pointers to functions that actually do the hard stuff */
static int (*pGetCurrentMode)(void);
static LONG (*pSetCurrentMode)(int mode);
static const char *handler_name;

/*
 * Set the handlers for resolution changing functions
 * and initialize the master list of modes
 */
struct x11drv_mode_info *X11DRV_Settings_SetHandlers(const char *name,
                                                     int (*pNewGCM)(void),
                                                     LONG (*pNewSCM)(int),
                                                     unsigned int nmodes,
                                                     int reserve_depths)
{
    handler_name = name;
    pGetCurrentMode = pNewGCM;
    pSetCurrentMode = pNewSCM;
    TRACE("Resolution settings now handled by: %s\n", name);
    if (reserve_depths)
        /* leave room for other depths */
        dd_max_modes = (3+1)*(nmodes);
    else 
        dd_max_modes = nmodes;

    if (dd_modes) 
    {
        TRACE("Destroying old display modes array\n");
        HeapFree(GetProcessHeap(), 0, dd_modes);
    }
    dd_modes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dd_modes) * dd_max_modes);
    dd_mode_count = 0;
    TRACE("Initialized new display modes array\n");
    return dd_modes;
}

/* Add one mode to the master list */
void X11DRV_Settings_AddOneMode(unsigned int width, unsigned int height, unsigned int bpp, unsigned int freq)
{
    struct x11drv_mode_info *info = &dd_modes[dd_mode_count];
    DWORD dwBpp = screen_bpp;
    if (dd_mode_count >= dd_max_modes)
    {
        ERR("Maximum modes (%d) exceeded\n", dd_max_modes);
        return;
    }
    if (bpp == 0) bpp = dwBpp;
    info->width         = width;
    info->height        = height;
    info->refresh_rate  = freq;
    info->bpp           = bpp;
    TRACE("initialized mode %d: %dx%dx%d @%d Hz (%s)\n", 
          dd_mode_count, width, height, bpp, freq, handler_name);
    dd_mode_count++;
}

/* copy all the current modes using the other color depths */
void X11DRV_Settings_AddDepthModes(void)
{
    int i, j;
    int existing_modes = dd_mode_count;
    DWORD dwBpp = screen_bpp;
    const DWORD *depths = screen_bpp == 32 ? depths_32 : depths_24;

    for (j=0; j<3; j++)
    {
        if (depths[j] != dwBpp)
        {
            for (i=0; i < existing_modes; i++)
            {
                X11DRV_Settings_AddOneMode(dd_modes[i].width, dd_modes[i].height,
                                           depths[j], dd_modes[i].refresh_rate);
            }
        }
    }
}

/* return the number of modes that are initialized */
unsigned int X11DRV_Settings_GetModeCount(void)
{
    return dd_mode_count;
}

/* TODO: Remove the old handler interface usage once all backends are migrated to the new interface */
static struct x11drv_settings_handler handler;

/* Display modes for a device, protected by modes_section. Only cache one device at a time */
static WCHAR cached_device_name[CCHDEVICENAME];
static DWORD cached_flags;
static DEVMODEW *cached_modes;
static INT cached_mode_count;
static CRITICAL_SECTION modes_section;
static CRITICAL_SECTION_DEBUG modes_critsect_debug =
{
    0, 0, &modes_section,
    {&modes_critsect_debug.ProcessLocksList, &modes_critsect_debug.ProcessLocksList},
     0, 0, {(DWORD_PTR)(__FILE__ ": modes_section")}
};
static CRITICAL_SECTION modes_section = {&modes_critsect_debug, -1, 0, 0, 0, 0};

static const char *const orientation_text[] = {"DMDO_DEFAULT", "DMDO_90", "DMDO_180", "DMDO_270"};

void X11DRV_Settings_SetHandler(const struct x11drv_settings_handler *new_handler)
{
    if (new_handler->priority > handler.priority)
    {
        handler = *new_handler;
        TRACE("Resolution settings are now handled by: %s\n", handler.name);
    }
}

/***********************************************************************
 * Default handlers if resolution switching is not enabled
 *
 */
static int X11DRV_nores_GetCurrentMode(void)
{
    return 0;
}

static LONG X11DRV_nores_SetCurrentMode(int mode)
{
    if (mode == 0) return DISP_CHANGE_SUCCESSFUL;
    TRACE("Ignoring mode change request mode=%d\n", mode);
    return DISP_CHANGE_FAILED;
}

/* default handler only gets the current X desktop resolution */
void X11DRV_Settings_Init(void)
{
    RECT primary = get_host_primary_monitor_rect();

    depths = screen_bpp == 32 ? depths_32 : depths_24;

    X11DRV_Settings_SetHandlers("NoRes", 
                                X11DRV_nores_GetCurrentMode, 
                                X11DRV_nores_SetCurrentMode, 
                                1, 0);
    X11DRV_Settings_AddOneMode( primary.right - primary.left, primary.bottom - primary.top, 0, 60);
}

static BOOL get_display_device_reg_key(const WCHAR *device_name, WCHAR *key, unsigned len)
{
    static const WCHAR displayW[] = {'\\','\\','.','\\','D','I','S','P','L','A','Y'};
    static const WCHAR video_value_fmt[] = {'\\','D','e','v','i','c','e','\\',
                                            'V','i','d','e','o','%','d',0};
    static const WCHAR video_key[] = {'H','A','R','D','W','A','R','E','\\',
                                      'D','E','V','I','C','E','M','A','P','\\',
                                      'V','I','D','E','O','\\',0};
    WCHAR key_nameW[MAX_PATH], bufferW[MAX_PATH], *end_ptr;
    DWORD adapter_index, size;

    /* Device name has to be \\.\DISPLAY%d */
    if (strncmpiW( device_name, displayW, ARRAY_SIZE(displayW) ))
        return FALSE;

    /* Parse \\.\DISPLAY* */
    adapter_index = strtolW(device_name + ARRAY_SIZE(displayW), &end_ptr, 10) - 1;
    if (*end_ptr)
        return FALSE;

    /* Open \Device\Video* in HKLM\HARDWARE\DEVICEMAP\VIDEO\ */
    sprintfW(key_nameW, video_value_fmt, adapter_index);
    size = sizeof(bufferW);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, video_key, key_nameW, RRF_RT_REG_SZ, NULL, bufferW, &size))
        return FALSE;

    if (len < lstrlenW(bufferW + 18) + 1)
        return FALSE;

    /* Skip \Registry\Machine\ prefix */
    lstrcpyW(key, bufferW + 18);
    TRACE("display device key %s\n", wine_dbgstr_w(key));
    return TRUE;
}

static BOOL read_registry_settings(const WCHAR *device_name, DEVMODEW *dm)
{
    WCHAR wine_x11_reg_key[MAX_PATH];
    HANDLE mutex;
    HKEY hkey;
    DWORD type, size;
    BOOL ret = TRUE;

    dm->dmFields = 0;

    mutex = get_display_device_init_mutex();
    ret = get_display_device_reg_key(device_name, wine_x11_reg_key, ARRAY_SIZE(wine_x11_reg_key));
    release_display_device_init_mutex(mutex);

    if (!ret)
        return FALSE;

    if (RegOpenKeyExW(HKEY_CURRENT_CONFIG, wine_x11_reg_key, 0, KEY_READ, &hkey))
        return FALSE;

#define query_value(name, data) \
    size = sizeof(DWORD); \
    if (RegQueryValueExA(hkey, name, 0, &type, (LPBYTE)(data), &size) || \
        type != REG_DWORD || size != sizeof(DWORD)) \
        ret = FALSE

    query_value("DefaultSettings.BitsPerPel", &dm->dmBitsPerPel);
    dm->dmFields |= DM_BITSPERPEL;
    query_value("DefaultSettings.XResolution", &dm->dmPelsWidth);
    dm->dmFields |= DM_PELSWIDTH;
    query_value("DefaultSettings.YResolution", &dm->dmPelsHeight);
    dm->dmFields |= DM_PELSHEIGHT;
    query_value("DefaultSettings.VRefresh", &dm->dmDisplayFrequency);
    dm->dmFields |= DM_DISPLAYFREQUENCY;
    query_value("DefaultSettings.Flags", &dm->u2.dmDisplayFlags);
    dm->dmFields |= DM_DISPLAYFLAGS;
    query_value("DefaultSettings.XPanning", &dm->u1.s2.dmPosition.x);
    query_value("DefaultSettings.YPanning", &dm->u1.s2.dmPosition.y);
    dm->dmFields |= DM_POSITION;
    query_value("DefaultSettings.Orientation", &dm->u1.s2.dmDisplayOrientation);
    dm->dmFields |= DM_DISPLAYORIENTATION;
    query_value("DefaultSettings.FixedOutput", &dm->u1.s2.dmDisplayFixedOutput);

#undef query_value

    RegCloseKey(hkey);
    return ret;
}

static BOOL write_registry_settings(const WCHAR *device_name, const DEVMODEW *dm)
{
    WCHAR wine_x11_reg_key[MAX_PATH];
    HANDLE mutex;
    HKEY hkey;
    BOOL ret = TRUE;

    mutex = get_display_device_init_mutex();
    ret = get_display_device_reg_key(device_name, wine_x11_reg_key, ARRAY_SIZE(wine_x11_reg_key));
    release_display_device_init_mutex(mutex);

    if (!ret)
        return FALSE;

    if (RegCreateKeyExW(HKEY_CURRENT_CONFIG, wine_x11_reg_key, 0, NULL,
                        REG_OPTION_VOLATILE, KEY_WRITE, NULL, &hkey, NULL))
        return FALSE;

#define set_value(name, data) \
    if (RegSetValueExA(hkey, name, 0, REG_DWORD, (const BYTE*)(data), sizeof(DWORD))) \
        ret = FALSE

    set_value("DefaultSettings.BitsPerPel", &dm->dmBitsPerPel);
    set_value("DefaultSettings.XResolution", &dm->dmPelsWidth);
    set_value("DefaultSettings.YResolution", &dm->dmPelsHeight);
    set_value("DefaultSettings.VRefresh", &dm->dmDisplayFrequency);
    set_value("DefaultSettings.Flags", &dm->u2.dmDisplayFlags);
    set_value("DefaultSettings.XPanning", &dm->u1.s2.dmPosition.x);
    set_value("DefaultSettings.YPanning", &dm->u1.s2.dmPosition.y);
    set_value("DefaultSettings.Orientation", &dm->u1.s2.dmDisplayOrientation);
    set_value("DefaultSettings.FixedOutput", &dm->u1.s2.dmDisplayFixedOutput);

#undef set_value

    RegCloseKey(hkey);
    return ret;
}

BOOL get_primary_adapter(WCHAR *name)
{
    DISPLAY_DEVICEW dd;
    DWORD i;

    dd.cb = sizeof(dd);
    for (i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); ++i)
    {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            lstrcpyW(name, dd.DeviceName);
            return TRUE;
        }
    }

    return FALSE;
}

/* Initialize display device registry settings when new devices are added.
 * This function should only be called in the desktop thread */
void init_display_registry_settings(void)
{
    DISPLAY_DEVICEW dd;
    DEVMODEW dm;
    DWORD i = 0;

    dd.cb = sizeof(dd);
    dm.dmSize = sizeof(dm);
    while (EnumDisplayDevicesW(NULL, i++, &dd, 0))
    {
        /* Skip if the device already has registry settings */
        if (EnumDisplaySettingsExW(dd.DeviceName, ENUM_REGISTRY_SETTINGS, &dm, 0))
            continue;

        if (!EnumDisplaySettingsExW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0))
        {
            ERR("Failed to query current settings for %s\n", wine_dbgstr_w(dd.DeviceName));
            continue;
        }

        TRACE("Device %s current display mode %ux%u %uBits %uHz at %d,%d\n",
              wine_dbgstr_w(dd.DeviceName), dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel,
              dm.dmDisplayFrequency, dm.u1.s2.dmPosition.x, dm.u1.s2.dmPosition.y);

        if (ChangeDisplaySettingsExW(dd.DeviceName, &dm, 0,
                                     CDS_GLOBAL | CDS_NORESET | CDS_UPDATEREGISTRY, 0))
        {
            ERR("Failed to save registry settings for %s\n", wine_dbgstr_w(dd.DeviceName));
            continue;
        }
    }
}

/***********************************************************************
 *		EnumDisplaySettingsEx  (X11DRV.@)
 *
 */
BOOL CDECL X11DRV_EnumDisplaySettingsEx( LPCWSTR name, DWORD n, LPDEVMODEW devmode, DWORD flags)
{
    static const WCHAR dev_name[CCHDEVICENAME] =
        { 'W','i','n','e',' ','X','1','1',' ','d','r','i','v','e','r',0 };
    DEVMODEW *modes, *mode;
    INT mode_count;
    ULONG_PTR id;

    /* Use new interface if possible */
    if (!handler.name)
        goto old_interface;

    if (n == ENUM_REGISTRY_SETTINGS)
    {
        TRACE("Getting %s default settings\n", wine_dbgstr_w(name));
        if (!read_registry_settings(name, devmode))
            return FALSE;
        goto done;
    }

    if (n == ENUM_CURRENT_SETTINGS)
    {
        TRACE("Getting %s current settings\n", wine_dbgstr_w(name));
        if (!handler.get_id(name, &id) || !handler.get_current_settings(id, devmode))
            return FALSE;
        goto done;
    }

    /* Refresh cache when necessary */
    EnterCriticalSection(&modes_section);
    if (n == 0 || lstrcmpiW(cached_device_name, name) || cached_flags != flags)
    {
        if (!handler.get_id(name, &id) || !handler.get_modes(id, flags, &modes, &mode_count))
        {
            LeaveCriticalSection(&modes_section);
            return FALSE;
        }

        if (cached_modes)
            handler.free_modes(cached_modes);
        lstrcpyW(cached_device_name, name);
        cached_flags = flags;
        cached_modes = modes;
        cached_mode_count = mode_count;
    }

    if (n >= cached_mode_count)
    {
        LeaveCriticalSection(&modes_section);
        WARN("handler:%s device:%s mode index:%d not found\n", handler.name, wine_dbgstr_w(name), n);
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }

    mode = (DEVMODEW *)((BYTE *)cached_modes + (sizeof(*cached_modes) + cached_modes[0].dmDriverExtra) * n);
    *devmode = *mode;
    LeaveCriticalSection(&modes_section);

done:
    /* Set generic fields */
    devmode->dmSize = FIELD_OFFSET(DEVMODEW, dmICMMethod);
    devmode->dmDriverExtra = 0;
    devmode->dmSpecVersion = DM_SPECVERSION;
    devmode->dmDriverVersion = DM_SPECVERSION;
    memcpy(devmode->dmDeviceName, dev_name, sizeof(dev_name));
    TRACE("handler:%s device:%s mode index:%d position:(%d,%d) resolution:%dx%d frequency:%dHz depth:%dbits orientation:%s\n",
          handler.name, wine_dbgstr_w(name), n, devmode->u1.s2.dmPosition.x, devmode->u1.s2.dmPosition.y,
          devmode->dmPelsWidth, devmode->dmPelsHeight, devmode->dmDisplayFrequency, devmode->dmBitsPerPel,
          orientation_text[devmode->u1.s2.dmDisplayOrientation]);
    return TRUE;

old_interface:
    devmode->dmSize = FIELD_OFFSET(DEVMODEW, dmICMMethod);
    devmode->dmSpecVersion = DM_SPECVERSION;
    devmode->dmDriverVersion = DM_SPECVERSION;
    memcpy(devmode->dmDeviceName, dev_name, sizeof(dev_name));
    devmode->dmDriverExtra = 0;
    devmode->u2.dmDisplayFlags = 0;
    devmode->dmDisplayFrequency = 0;
    devmode->u1.s2.dmPosition.x = 0;
    devmode->u1.s2.dmPosition.y = 0;
    devmode->u1.s2.dmDisplayOrientation = 0;
    devmode->u1.s2.dmDisplayFixedOutput = 0;

    if (n == ENUM_CURRENT_SETTINGS)
    {
        TRACE("mode %d (current) -- getting current mode (%s)\n", n, handler_name);
        n = pGetCurrentMode();
    }
    if (n == ENUM_REGISTRY_SETTINGS)
    {
        TRACE("mode %d (registry) -- getting default mode (%s)\n", n, handler_name);
        return read_registry_settings(name, devmode);
    }
    if (n < dd_mode_count)
    {
        devmode->dmPelsWidth = dd_modes[n].width;
        devmode->dmPelsHeight = dd_modes[n].height;
        devmode->dmBitsPerPel = dd_modes[n].bpp;
        devmode->dmDisplayFrequency = dd_modes[n].refresh_rate;
        devmode->dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL |
                            DM_DISPLAYFLAGS;
        if (devmode->dmDisplayFrequency)
        {
            devmode->dmFields |= DM_DISPLAYFREQUENCY;
            TRACE("mode %d -- %dx%dx%dbpp @%d Hz (%s)\n", n,
                  devmode->dmPelsWidth, devmode->dmPelsHeight, devmode->dmBitsPerPel,
                  devmode->dmDisplayFrequency, handler_name);
        }
        else
        {
            TRACE("mode %d -- %dx%dx%dbpp (%s)\n", n,
                  devmode->dmPelsWidth, devmode->dmPelsHeight, devmode->dmBitsPerPel, 
                  handler_name);
        }
        return TRUE;
    }
    TRACE("mode %d -- not present (%s)\n", n, handler_name);
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL is_detached_mode(const DEVMODEW *mode)
{
    return mode->dmFields & DM_POSITION &&
           mode->dmFields & DM_PELSWIDTH &&
           mode->dmFields & DM_PELSHEIGHT &&
           mode->dmPelsWidth == 0 &&
           mode->dmPelsHeight == 0;
}

static DEVMODEW *get_full_mode(DEVMODEW *driver_modes, INT driver_mode_count, DEVMODEW *user_mode)
{
    DEVMODEW *found = NULL;
    INT i;

    for (i = 0; i < driver_mode_count; ++i)
    {
        found = (DEVMODEW *)((BYTE *)driver_modes + (sizeof(*driver_modes) + driver_modes[0].dmDriverExtra) * i);

        if (user_mode->dmFields & DM_BITSPERPEL && found->dmBitsPerPel != user_mode->dmBitsPerPel)
            continue;
        if (user_mode->dmFields & DM_PELSWIDTH && found->dmPelsWidth != user_mode->dmPelsWidth)
            continue;
        if (user_mode->dmFields & DM_PELSHEIGHT && found->dmPelsHeight != user_mode->dmPelsHeight)
            continue;
        if (user_mode->dmFields & DM_DISPLAYFREQUENCY && user_mode->dmDisplayFrequency && found->dmDisplayFrequency
            && user_mode->dmDisplayFrequency != found->dmDisplayFrequency)
            continue;

        break;
    }

    if (i == driver_mode_count)
        return NULL;

    if (user_mode->dmFields & DM_POSITION)
    {
        found->dmFields |= DM_POSITION;
        found->u1.s2.dmPosition = user_mode->u1.s2.dmPosition;
    }

    return found;
}

/***********************************************************************
 *		ChangeDisplaySettingsEx  (X11DRV.@)
 *
 */
LONG CDECL X11DRV_ChangeDisplaySettingsEx( LPCWSTR devname, LPDEVMODEW devmode,
                                           HWND hwnd, DWORD flags, LPVOID lpvoid )
{
    WCHAR primary_adapter[CCHDEVICENAME];
    char bpp_buffer[16], freq_buffer[18];
    DEVMODEW *driver_modes = NULL, *full_mode;
    DEVMODEW default_mode;
    INT driver_mode_count;
    ULONG_PTR id;
    BOOL ret;
    INT i;

    /* Use new interface if possible */
    if (!handler.name)
        goto old_interface;

    if (!get_primary_adapter(primary_adapter))
        return DISP_CHANGE_FAILED;

    if (!devname && !devmode)
    {
        default_mode.dmSize = sizeof(default_mode);
        if (!EnumDisplaySettingsExW(primary_adapter, ENUM_REGISTRY_SETTINGS, &default_mode, 0))
        {
            ERR("Default mode not found for %s!\n", wine_dbgstr_w(primary_adapter));
            return DISP_CHANGE_BADMODE;
        }

        devname = primary_adapter;
        devmode = &default_mode;
    }

    if (flags & CDS_UPDATEREGISTRY && !write_registry_settings(devname, devmode))
        return DISP_CHANGE_NOTUPDATED;

    if (flags & (CDS_TEST | CDS_NORESET))
        return DISP_CHANGE_SUCCESSFUL;

    if (!handler.get_id(devname, &id))
        goto fail;

    if (is_detached_mode(devmode))
    {
        INT attached_count = 0;
        DISPLAY_DEVICEW dd;

        dd.cb = sizeof(dd);
        for (i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); ++i)
        {
            if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
                ++attached_count;
        }

        /* Can't detach the only attached adapter */
        if (attached_count <= 1)
            return DISP_CHANGE_SUCCESSFUL;

        full_mode = devmode;
    }
    else
    {
        /* Find a mode that has all the required fields */
        if (!handler.get_modes(id, 0, &driver_modes, &driver_mode_count))
            goto fail;

        full_mode = get_full_mode(driver_modes, driver_mode_count, devmode);
        if (!full_mode)
        {
            handler.free_modes(driver_modes);
            goto fail;
        }

        if (!(devmode->dmFields & DM_POSITION))
        {
            DEVMODEW current_mode;

            current_mode.dmSize = sizeof(current_mode);
            if (!EnumDisplaySettingsExW(devname, ENUM_CURRENT_SETTINGS, &current_mode, 0))
            {
                handler.free_modes(driver_modes);
                goto fail;
            }

            full_mode->dmFields |= DM_POSITION;
            full_mode->u1.s2.dmPosition = current_mode.u1.s2.dmPosition;
        }
    }

    TRACE("handler:%s device:%s to position:(%d,%d) resolution:%dx%d frequency:%dHz depth:%dbits orientation:%s\n",
          handler.name, wine_dbgstr_w(devname), full_mode->u1.s2.dmPosition.x, full_mode->u1.s2.dmPosition.y,
          full_mode->dmPelsWidth, full_mode->dmPelsHeight, full_mode->dmDisplayFrequency, full_mode->dmBitsPerPel,
          orientation_text[full_mode->u1.s2.dmDisplayOrientation]);

    ret = handler.set_current_settings(id, full_mode);
    if (driver_modes)
        handler.free_modes(driver_modes);
    if (ret == DISP_CHANGE_SUCCESSFUL)
        X11DRV_DisplayDevices_Update(TRUE);
    return ret;

old_interface:
    if (!get_primary_adapter(primary_adapter))
        return DISP_CHANGE_FAILED;

    if (!devname && !devmode)
    {
        default_mode.dmSize = sizeof(default_mode);
        if (!EnumDisplaySettingsExW(primary_adapter, ENUM_REGISTRY_SETTINGS, &default_mode, 0))
        {
            ERR("Default mode not found for %s!\n", wine_dbgstr_w(primary_adapter));
            return DISP_CHANGE_BADMODE;
        }

        devname = primary_adapter;
        devmode = &default_mode;
    }

    if (is_detached_mode(devmode))
    {
        FIXME("Detaching adapter is currently unsupported.\n");
        return DISP_CHANGE_SUCCESSFUL;
    }

    for (i = 0; i < dd_mode_count; i++)
    {
        if (devmode->dmFields & DM_BITSPERPEL)
        {
            if (devmode->dmBitsPerPel != dd_modes[i].bpp)
                continue;
        }
        if (devmode->dmFields & DM_PELSWIDTH)
        {
            if (devmode->dmPelsWidth != dd_modes[i].width)
                continue;
        }
        if (devmode->dmFields & DM_PELSHEIGHT)
        {
            if (devmode->dmPelsHeight != dd_modes[i].height)
                continue;
        }
        if ((devmode->dmFields & DM_DISPLAYFREQUENCY) && (dd_modes[i].refresh_rate != 0) &&
            devmode->dmDisplayFrequency != 0)
        {
            if (devmode->dmDisplayFrequency != dd_modes[i].refresh_rate)
                continue;
        }
        /* we have a valid mode */
        TRACE("Requested display settings match mode %d (%s)\n", i, handler_name);

        if (flags & CDS_UPDATEREGISTRY)
            write_registry_settings(devname, devmode);

        if (lstrcmpiW(primary_adapter, devname))
        {
            FIXME("Changing non-primary adapter %s settings is currently unsupported.\n",
                  wine_dbgstr_w(devname));
            return DISP_CHANGE_SUCCESSFUL;
        }

        if (!(flags & (CDS_TEST | CDS_NORESET)))
            return pSetCurrentMode(i);

        return DISP_CHANGE_SUCCESSFUL;
    }

fail:
    /* no valid modes found, only print the fields we were trying to matching against */
    bpp_buffer[0] = freq_buffer[0] = 0;
    if (devmode->dmFields & DM_BITSPERPEL)
        sprintf(bpp_buffer, "bpp=%u ",  devmode->dmBitsPerPel);
    if ((devmode->dmFields & DM_DISPLAYFREQUENCY) && (devmode->dmDisplayFrequency != 0))
        sprintf(freq_buffer, "freq=%u ", devmode->dmDisplayFrequency);
    ERR("No matching mode found: width=%d height=%d %s%s(%s)\n",
        devmode->dmPelsWidth, devmode->dmPelsHeight, bpp_buffer, freq_buffer, handler.name ? handler.name: handler_name );

    return DISP_CHANGE_BADMODE;
}
