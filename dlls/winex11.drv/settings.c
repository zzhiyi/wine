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
#include <stdlib.h>
#include <math.h>
#include <stdlib.h>

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

/* All Windows drivers seen so far either support 32 bit depths, or 24 bit depths, but never both. So if we have
 * a 32 bit framebuffer, report 32 bit bpps, otherwise 24 bit ones.
 */
static const unsigned int depths_24[]  = {8, 16, 24};
static const unsigned int depths_32[]  = {8, 16, 32};
const DWORD *depths;

static int currentMode = -1, realMode = -1;

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
double fs_hack_user_to_real_w = 1., fs_hack_user_to_real_h = 1.;
double fs_hack_real_to_user_w = 1., fs_hack_real_to_user_h = 1.;
static int offs_x = 0, offs_y = 0;
static int fs_width = 0, fs_height = 0;

BOOL fs_hack_enabled(void)
{
    return currentMode >= 0 &&
        currentMode != realMode;
}

BOOL fs_hack_mapping_required(void)
{
    /* steamcompmgr does our mapping for us */
    return !wm_is_steamcompmgr(NULL) &&
        currentMode >= 0 &&
        currentMode != realMode;
}

BOOL fs_hack_is_integer(void)
{
    static int is_int = -1;
    if(is_int < 0)
    {
        const char *e = getenv("WINE_FULLSCREEN_INTEGER_SCALING");
        is_int = e && strcmp(e, "0");
    }
    return is_int;
}

BOOL fs_hack_matches_current_mode(int w, int h)
{
    return fs_hack_enabled() &&
        (w == dd_modes[currentMode].width &&
         h == dd_modes[currentMode].height);
}

BOOL fs_hack_matches_real_mode(int w, int h)
{
    return fs_hack_enabled() &&
        (w == dd_modes[realMode].width &&
         h == dd_modes[realMode].height);
}

BOOL fs_hack_matches_last_mode(int w, int h)
{
    return w == fs_width && h == fs_height;
}

void fs_hack_scale_user_to_real(POINT *pos)
{
    if(fs_hack_mapping_required()){
        TRACE("from %d,%d\n", pos->x, pos->y);
        pos->x = lround(pos->x * fs_hack_user_to_real_w);
        pos->y = lround(pos->y * fs_hack_user_to_real_h);
        TRACE("to %d,%d\n", pos->x, pos->y);
    }
}

void fs_hack_scale_real_to_user(POINT *pos)
{
    if(fs_hack_mapping_required()){
        TRACE("from %d,%d\n", pos->x, pos->y);
        pos->x = lround(pos->x * fs_hack_real_to_user_w);
        pos->y = lround(pos->y * fs_hack_real_to_user_h);
        TRACE("to %d,%d\n", pos->x, pos->y);
    }
}

POINT fs_hack_get_scaled_screen_size(void)
{
    POINT p = { dd_modes[currentMode].width,
        dd_modes[currentMode].height };
    fs_hack_scale_user_to_real(&p);
    return p;
}

void fs_hack_user_to_real(POINT *pos)
{
    if(fs_hack_mapping_required()){
        TRACE("from %d,%d\n", pos->x, pos->y);
        fs_hack_scale_user_to_real(pos);
        pos->x += offs_x;
        pos->y += offs_y;
        TRACE("to %d,%d\n", pos->x, pos->y);
    }
}

void fs_hack_real_to_user(POINT *pos)
{
    if(fs_hack_mapping_required()){
        TRACE("from %d,%d\n", pos->x, pos->y);

        if(pos->x <= offs_x)
            pos->x = 0;
        else
            pos->x -= offs_x;

        if(pos->y <= offs_y)
            pos->y = 0;
        else
            pos->y -= offs_y;

        if(pos->x >= fs_width)
            pos->x = fs_width - 1;
        if(pos->y >= fs_height)
            pos->y = fs_height - 1;

        fs_hack_scale_real_to_user(pos);

        TRACE("to %d,%d\n", pos->x, pos->y);
    }
}

void fs_hack_rect_user_to_real(RECT *rect)
{
    fs_hack_user_to_real((POINT *)&rect->left);
    fs_hack_user_to_real((POINT *)&rect->right);
}

/* this is for clipping */
void fs_hack_rgndata_user_to_real(RGNDATA *data)
{
    unsigned int i;
    XRectangle *xrect;

    if (data && fs_hack_mapping_required())
    {
        xrect = (XRectangle *)data->Buffer;
        for (i = 0; i < data->rdh.nCount; i++)
        {
            POINT p;
            p.x = xrect[i].x;
            p.y = xrect[i].y;
            fs_hack_user_to_real(&p);
            xrect[i].x = p.x;
            xrect[i].y = p.y;
            xrect[i].width  *= fs_hack_user_to_real_w;
            xrect[i].height *= fs_hack_user_to_real_h;
        }
    }
}

POINT fs_hack_current_mode(void)
{
    POINT ret = { dd_modes[currentMode].width,
        dd_modes[currentMode].height };
    return ret;
}

POINT fs_hack_real_mode(void)
{
    POINT ret = { dd_modes[realMode].width,
        dd_modes[realMode].height };
    return ret;
}

static BOOL nores_get_id(const WCHAR *device_name, ULONG_PTR *id)
{
    static const WCHAR displayW[] = {'\\', '\\', '.', '\\', 'D', 'I', 'S', 'P', 'L', 'A', 'Y'};
    WCHAR primary_adapter[CCHDEVICENAME];
    WCHAR *end_ptr;

    /* Device name has to be \\.\DISPLAY%d */
    if (strncmpiW(device_name, displayW, ARRAY_SIZE(displayW)))
        return FALSE;

    strtolW(device_name + ARRAY_SIZE(displayW), &end_ptr, 10);
    if (*end_ptr)
        return FALSE;

    if (!get_primary_adapter(primary_adapter))
        return FALSE;

    *id = !lstrcmpiW(device_name, primary_adapter) ? 1 : 0;
    return TRUE;
}

static BOOL nores_get_modes(ULONG_PTR id, DWORD flags, DEVMODEW **new_modes, INT *mode_count)
{
    RECT primary = get_host_primary_monitor_rect();
    DEVMODEW *modes;

    modes = heap_calloc(1, sizeof(*modes));
    if (!modes)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    modes[0].dmSize = sizeof(*modes);
    modes[0].dmDriverExtra = 0;
    modes[0].dmFields = DM_DISPLAYORIENTATION | DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS
                        | DM_DISPLAYFREQUENCY;
    modes[0].u1.s2.dmDisplayOrientation = DMDO_DEFAULT;
    modes[0].dmBitsPerPel = screen_bpp;
    modes[0].dmPelsWidth = primary.right;
    modes[0].dmPelsHeight = primary.bottom;
    modes[0].u2.dmDisplayFlags = 0;
    modes[0].dmDisplayFrequency = 60;

    *new_modes = modes;
    *mode_count = 1;
    return TRUE;
}

static void nores_free_modes(DEVMODEW *modes)
{
    heap_free(modes);
}

static BOOL nores_get_current_settings(ULONG_PTR id, DEVMODEW *mode)
{
    RECT primary = get_host_primary_monitor_rect();

    mode->dmDriverExtra = 0;
    mode->dmFields = DM_DISPLAYORIENTATION | DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS
                     | DM_DISPLAYFREQUENCY | DM_POSITION;
    mode->u1.s2.dmDisplayOrientation = DMDO_DEFAULT;
    mode->u2.dmDisplayFlags = 0;
    mode->u1.s2.dmPosition.x = 0;
    mode->u1.s2.dmPosition.y = 0;

    if (!id)
    {
        FIXME("Non-primary adapters are unsupported.\n");
        mode->dmBitsPerPel = 0;
        mode->dmPelsWidth = 0;
        mode->dmPelsHeight = 0;
        mode->dmDisplayFrequency = 0;
        return TRUE;
    }

    mode->dmBitsPerPel = screen_bpp;
    mode->dmPelsWidth = primary.right;
    mode->dmPelsHeight = primary.bottom;
    mode->dmDisplayFrequency = 60;
    return TRUE;
}

static LONG nores_set_current_settings(ULONG_PTR id, DEVMODEW *mode)
{
    WARN("NoRes settings handler, ignoring mode change request.\n");
    return DISP_CHANGE_SUCCESSFUL;
}

void X11DRV_Settings_Init(void)
{
    struct x11drv_settings_handler nores_handler;

    depths = screen_bpp == 32 ? depths_32 : depths_24;

    nores_handler.name = "NoRes";
    nores_handler.priority = 0;
    nores_handler.get_id = nores_get_id;
    nores_handler.get_modes = nores_get_modes;
    nores_handler.free_modes = nores_free_modes;
    nores_handler.get_current_settings = nores_get_current_settings;
    nores_handler.set_current_settings = nores_set_current_settings;
    nores_handler.convert_coordinates = NULL;
    X11DRV_Settings_SetHandler(&nores_handler);
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

static int mode_compare(const void *p1, const void *p2)
{
    const DEVMODEW *a = p1, *b = p2;

    if (a->dmBitsPerPel != b->dmBitsPerPel)
        return b->dmBitsPerPel - a->dmBitsPerPel;

    if (a->dmPelsWidth != b->dmPelsWidth)
        return a->dmPelsWidth - b->dmPelsWidth;

    if (a->dmPelsHeight != b->dmPelsHeight)
        return a->dmPelsHeight - b->dmPelsHeight;

    return a->dmDisplayFrequency - b->dmDisplayFrequency;
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

        qsort(modes, mode_count, sizeof(*modes) + modes[0].dmDriverExtra, mode_compare);

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

static INT offset_length(POINT offset)
{
    return offset.x * offset.x + offset.y * offset.y;
}

static BOOL next_to_rect(const RECT *center, const RECT *side)
{
    /* Left/Right side */
    if ((side->right == center->left || side->left == center->right) &&
        side->top >= center->bottom && side->bottom <= center->top)
        return TRUE;

    /* Top/Bottom side */
    if ((side->bottom == center->top || side->top == center->bottom) &&
        side->left <= center->right && side->right >= center->left)
        return TRUE;

    return FALSE;
}

/* Check if a rect overlaps with placed adapters */
static BOOL overlap_placed_adapters(const struct x11drv_adapter_setting *adapters, INT count, const RECT *rect)
{
    RECT intersect;
    INT i;

    for (i = 0; i < count; ++i)
    {
        if (!adapters[i].placed)
            continue;

        if (IntersectRect(&intersect, &adapters[i].new_rect, rect))
            return TRUE;
    }

    return FALSE;
}

/* Get the offset with minimum length to place an adapter next to the placed adapters without extra space and overlapping */
static POINT get_placement_offset(const struct x11drv_adapter_setting *adapters, INT count, INT placing_index,
                                  INT changing_index)
{
    INT left_most = INT_MAX, right_most = INT_MIN;
    INT top_most = INT_MAX, bottom_most = INT_MIN;
    INT adapter_index, point_index, corner_index;
    POINT points[4], offset, min_offset = {0, 0};
    BOOL has_placed = FALSE, first = TRUE;
    INT width, height;
    RECT rect;

    /* If the adapter to be placed is detached, no offset is needed to place it */
    if (IsRectEmpty(&adapters[placing_index].old_rect))
        return min_offset;

    /* If there is no placed and attached adapter, place this adapter as it was */
    for (adapter_index = 0; adapter_index < count; ++adapter_index)
    {
        if (adapters[adapter_index].placed && !IsRectEmpty(&adapters[adapter_index].new_rect))
        {
            has_placed = TRUE;
            break;
        }
    }

    if (!has_placed)
        return min_offset;

    /* If the old_rect of the adapter to be placed is next to a placed adapter and
     * there is no overlapping with the placed adapters, then place this adapter as it was */
    for (adapter_index = 0; adapter_index < count; ++adapter_index)
    {
        if (adapters[adapter_index].placed
            && next_to_rect(&adapters[adapter_index].new_rect, &adapters[placing_index].old_rect)
            && !overlap_placed_adapters(adapters, count, &adapters[placing_index].old_rect))
            return min_offset;
    }

    /* Try to place this adapter next to the placed adapter it was next to if possible */
    for (adapter_index = 0; adapter_index < count; ++adapter_index)
    {
        /* Was next to the changing adapter doesn't count */
        if (changing_index == adapter_index)
            continue;

        /* If this adapter was next to a placed adapter before the adapter was placed */
        if (adapters[adapter_index].placed && !IsRectEmpty(&adapters[adapter_index].old_rect)
            && next_to_rect(&adapters[adapter_index].old_rect, &adapters[placing_index].old_rect))
        {
            /* Try to place this adapter with the same offset used by the placed monitor */
            offset.x = adapters[adapter_index].new_rect.left - adapters[adapter_index].old_rect.left;
            offset.y = adapters[adapter_index].new_rect.top - adapters[adapter_index].old_rect.top;
            rect = adapters[placing_index].old_rect;
            OffsetRect(&rect, offset.x, offset.y);

            /* Check if this offset will cause overlapping */
            if (!overlap_placed_adapters(adapters, count, &rect))
                return offset;
        }
    }

    /* Try to place this adapter with each of its four corners to every corner of the placed adapters
     * and see which combination has the minimum offset length */
    width = adapters[placing_index].old_rect.right - adapters[placing_index].old_rect.left;
    height = adapters[placing_index].old_rect.bottom - adapters[placing_index].old_rect.top;

    for (adapter_index = 0; adapter_index < count; ++adapter_index)
    {
        if (!adapters[adapter_index].placed || IsRectEmpty(&adapters[adapter_index].new_rect))
            continue;

        /* Get four points of the placed adapter rectangle */
        points[0].x = adapters[adapter_index].new_rect.left;
        points[0].y = adapters[adapter_index].new_rect.top;
        points[1].x = adapters[adapter_index].new_rect.left;
        points[1].y = adapters[adapter_index].new_rect.bottom;
        points[2].x = adapters[adapter_index].new_rect.right;
        points[2].y = adapters[adapter_index].new_rect.top;
        points[3].x = adapters[adapter_index].new_rect.right;
        points[3].y = adapters[adapter_index].new_rect.bottom;

        /* Try each of the four points */
        for (point_index = 0; point_index < 4; ++point_index)
        {
            /* Try moving each of the four corners of the current adapter rectangle to the point */
            for (corner_index = 0; corner_index < 4; ++corner_index)
            {
                switch (corner_index)
                {
                case 0:
                    offset.x = points[point_index].x - width;
                    offset.y = points[point_index].y - height;
                    break;
                case 1:
                    offset.x = points[point_index].x;
                    offset.y = points[point_index].y - height;
                    break;
                case 2:
                    offset.x = points[point_index].x - width;
                    offset.y = points[point_index].y;
                    break;
                case 3:
                    offset.x = points[point_index].x;
                    offset.y = points[point_index].y;
                    break;
                }

                offset.x -= adapters[placing_index].old_rect.left;
                offset.y -= adapters[placing_index].old_rect.top;

                rect = adapters[placing_index].old_rect;
                OffsetRect(&rect, offset.x, offset.y);

                if (!overlap_placed_adapters(adapters, count, &rect))
                {
                    if (first)
                    {
                        min_offset = offset;
                        first = FALSE;
                    }

                    if (offset_length(offset) < offset_length(min_offset))
                        min_offset = offset;
                }
            }
        }
    }

    /* Finally, try to move straight in four directions. They may get an offset with smaller movement */
    for (adapter_index = 0; adapter_index < count; ++adapter_index)
    {
        if (!adapters[adapter_index].placed || IsRectEmpty(&adapters[adapter_index].new_rect))
            continue;

        /* Check that this placed adapter is directly at the left/right/top/bottom side of the current adapter */
        points[0].x = max(adapters[placing_index].old_rect.left, adapters[adapter_index].new_rect.left);
        points[1].x = min(adapters[placing_index].old_rect.right, adapters[adapter_index].new_rect.right);
        points[0].y = max(adapters[placing_index].old_rect.top, adapters[adapter_index].new_rect.top);
        points[1].y = min(adapters[placing_index].old_rect.bottom, adapters[adapter_index].new_rect.bottom);

        if (points[0].x <= points[1].x)
        {
            top_most = min(top_most, adapters[adapter_index].new_rect.top);
            bottom_most = max(bottom_most, adapters[adapter_index].new_rect.bottom);
        }

        if (points[0].y <= points[1].y)
        {
            left_most = min(left_most, adapters[adapter_index].new_rect.left);
            right_most = max(right_most, adapters[adapter_index].new_rect.right);
        }
    }

    offset.x = INT_MAX;
    offset.y = INT_MAX;

    /* Move to the left side if movement is smaller than moving to the right side */
    if (left_most != INT_MAX && right_most != INT_MIN)
    {
        if (abs(left_most - width - adapters[placing_index].old_rect.left)
            <= abs(right_most - adapters[placing_index].old_rect.left))
            offset.x = left_most - width - adapters[placing_index].old_rect.left;
        else
            offset.x = right_most - adapters[placing_index].old_rect.left;
    }


    /* Move to the top side if movement is smaller than moving to the bottom side */
    if (top_most != INT_MAX && bottom_most != INT_MIN)
    {
        if (abs(top_most - height - adapters[placing_index].old_rect.top)
            <= abs(bottom_most - adapters[placing_index].old_rect.top))
            offset.y = top_most - height - adapters[placing_index].old_rect.top;
        else
            offset.y = bottom_most - adapters[placing_index].old_rect.top;
    }

    /* Move horizontally if movement is smaller than moving vertically */
    if (abs(offset.x) <= abs(offset.y))
        offset.y = 0;
    else
        offset.x = 0;

    if (offset.x != INT_MAX && offset.y != INT_MAX && offset_length(offset) < offset_length(min_offset))
        min_offset = offset;

    return min_offset;
}

static LONG load_adapter_settings(struct x11drv_adapter_setting **new_adapters, INT *new_adapter_count,
                                  const WCHAR *device_name, DEVMODEW *user_mode, BOOL from_registry,
                                  INT *changing_index)
{
    DEVMODEW *modes = NULL, *full_mode, current_mode;
    INT adapter_index, adapter_count = 0, mode_count;
    struct x11drv_adapter_setting *adapters;
    DISPLAY_DEVICEW ddW;

    ddW.cb = sizeof(ddW);
    for (adapter_index = 0; EnumDisplayDevicesW(NULL, adapter_index, &ddW, 0); ++adapter_index)
        ++adapter_count;

    adapters = heap_calloc(adapter_count, sizeof(*adapters));
    if (!adapters)
        goto done;

    for (adapter_index = 0; adapter_index < adapter_count; ++adapter_index)
    {
        if (!EnumDisplayDevicesW(NULL, adapter_index, &ddW, 0))
            goto done;

        if (!handler.get_id(ddW.DeviceName, &adapters[adapter_index].id))
            goto done;

        if (from_registry)
        {
            adapters[adapter_index].mode.dmSize = sizeof(adapters[adapter_index].mode);
            if (!EnumDisplaySettingsExW(ddW.DeviceName, ENUM_REGISTRY_SETTINGS, &adapters[adapter_index].mode, 0))
                goto done;
        }
        else if (!lstrcmpiW(device_name, ddW.DeviceName))
        {
            if (is_detached_mode(user_mode))
            {
                full_mode = user_mode;
            }
            else
            {
                if (!handler.get_modes(adapters[adapter_index].id, 0, &modes, &mode_count))
                    goto done;

                full_mode = get_full_mode(modes, mode_count, user_mode);
                if (!full_mode)
                {
                    handler.free_modes(modes);
                    return DISP_CHANGE_BADMODE;
                }

                if (!(user_mode->dmFields & DM_POSITION))
                {
                    current_mode.dmSize = sizeof(current_mode);
                    if (!EnumDisplaySettingsExW(ddW.DeviceName, ENUM_CURRENT_SETTINGS, &current_mode, 0))
                    {
                        handler.free_modes(modes);
                        goto done;
                    }

                    full_mode->dmFields |= DM_POSITION;
                    full_mode->u1.s2.dmPosition = current_mode.u1.s2.dmPosition;
                }
            }

            adapters[adapter_index].mode = *full_mode;
            *changing_index = adapter_index;
            if (modes)
            {
                handler.free_modes(modes);
                modes = NULL;
            }
        }
        else
        {
            adapters[adapter_index].mode.dmSize = sizeof(adapters[adapter_index].mode);
            if (!EnumDisplaySettingsExW(ddW.DeviceName, ENUM_CURRENT_SETTINGS, &adapters[adapter_index].mode, 0))
                goto done;
        }

        adapters[adapter_index].old_rect.left = adapters[adapter_index].mode.u1.s2.dmPosition.x;
        adapters[adapter_index].old_rect.top = adapters[adapter_index].mode.u1.s2.dmPosition.y;
        adapters[adapter_index].old_rect.right = adapters[adapter_index].old_rect.left + adapters[adapter_index].mode.dmPelsWidth;
        adapters[adapter_index].old_rect.bottom = adapters[adapter_index].old_rect.top + adapters[adapter_index].mode.dmPelsHeight;
    }

    *new_adapters = adapters;
    *new_adapter_count = adapter_count;
    return DISP_CHANGE_SUCCESSFUL;

done:
    heap_free(adapters);
    return DISP_CHANGE_FAILED;
}

static BOOL all_detached_settings(const struct x11drv_adapter_setting *adapters, INT adapter_count)
{
    INT attached_count = 0;
    INT i;

    for (i = 0; i < adapter_count; ++i)
    {
        if (!is_detached_mode(&adapters->mode))
            ++attached_count;
    }

    return attached_count == 0;
}

static void place_changing_adapter(struct x11drv_adapter_setting *changing_adapter, DEVMODEW *user_mode)
{
    if (user_mode->dmFields & DM_POSITION)
    {
        changing_adapter->new_rect.left = user_mode->u1.s2.dmPosition.x;
        changing_adapter->new_rect.top = user_mode->u1.s2.dmPosition.y;
    }
    else
    {
        changing_adapter->new_rect.left = changing_adapter->old_rect.left;
        changing_adapter->new_rect.top = changing_adapter->old_rect.top;
    }

    changing_adapter->new_rect.right = changing_adapter->new_rect.left + user_mode->dmPelsWidth;
    changing_adapter->new_rect.bottom = changing_adapter->new_rect.top + user_mode->dmPelsHeight;
    changing_adapter->placed = TRUE;
}

static void place_all_adapters(struct x11drv_adapter_setting *adapters, INT adapter_count, INT changing_index)
{
    POINT min_offset, offset;
    INT placing_index;
    INT adapter_index;

    /* Place all other adapters with no extra space between them and no overlapping */
    while (1)
    {
        /* Place the unplaced adapter with the minimum offset length first */
        placing_index = -1;
        for (adapter_index = 0; adapter_index < adapter_count; ++adapter_index)
        {
            if (adapters[adapter_index].placed)
                continue;

            offset = get_placement_offset(adapters, adapter_count, adapter_index, changing_index);
            if (placing_index == -1 || offset_length(offset) < offset_length(min_offset))
            {
                min_offset = offset;
                placing_index = adapter_index;
            }
        }

        /* If all adapters are placed */
        if (placing_index == -1)
            break;

        adapters[placing_index].new_rect = adapters[placing_index].old_rect;
        OffsetRect(&adapters[placing_index].new_rect, min_offset.x, min_offset.y);
        adapters[placing_index].placed = TRUE;
    }

    for (adapter_index = 0; adapter_index < adapter_count; ++adapter_index)
    {
        adapters[adapter_index].mode.u1.s2.dmPosition.x = adapters[adapter_index].new_rect.left;
        adapters[adapter_index].mode.u1.s2.dmPosition.y = adapters[adapter_index].new_rect.top;
    }

    if (handler.convert_coordinates)
        handler.convert_coordinates(adapters, adapter_count);
}

static LONG apply_adapter_settings(struct x11drv_adapter_setting *adapters, INT count, BOOL dettach)
{
    DEVMODEW *driver_modes = NULL, *full_mode;
    INT driver_mode_count;
    LONG ret;
    INT i;

    for (i = 0; i < count; ++i)
    {
        if (is_detached_mode(&adapters[i].mode) != dettach)
            continue;

        if (dettach)
        {
            full_mode = &adapters[i].mode;
        }
        else
        {
            /* Find a mode that has all the required fields */
            if (!handler.get_modes(adapters[i].id, 0, &driver_modes, &driver_mode_count))
                return DISP_CHANGE_FAILED;

            full_mode = get_full_mode(driver_modes, driver_mode_count, &adapters[i].mode);
            if (!full_mode)
            {
                handler.free_modes(driver_modes);
                return DISP_CHANGE_FAILED;
            }
        }

        TRACE("handler:%s change adapter:%d to position:(%d,%d) resolution:%dx%d frequency:%dHz "
              "depth:%dbits orientation:%s\n", handler.name, i, full_mode->u1.s2.dmPosition.x,
              full_mode->u1.s2.dmPosition.y, full_mode->dmPelsWidth, full_mode->dmPelsHeight,
              full_mode->dmDisplayFrequency, full_mode->dmBitsPerPel,
              orientation_text[full_mode->u1.s2.dmDisplayOrientation]);

        ret = handler.set_current_settings(adapters[i].id, full_mode);
        if (driver_modes)
        {
            handler.free_modes(driver_modes);
            driver_modes = NULL;
        }
        if (ret != DISP_CHANGE_SUCCESSFUL)
            return ret;
    }

    return DISP_CHANGE_SUCCESSFUL;
}

/***********************************************************************
 *		ChangeDisplaySettingsEx  (X11DRV.@)
 *
 */
LONG CDECL X11DRV_ChangeDisplaySettingsEx( LPCWSTR devname, LPDEVMODEW devmode,
                                           HWND hwnd, DWORD flags, LPVOID lpvoid )
{
    struct x11drv_adapter_setting *adapters;
    INT changing_index = -1;
    BOOL from_registry;
    INT adapter_count;
    LONG ret;

    from_registry = !devname && !devmode;

    if (!from_registry && flags & CDS_UPDATEREGISTRY && !write_registry_settings(devname, devmode))
        return DISP_CHANGE_NOTUPDATED;

    if (flags & (CDS_TEST | CDS_NORESET))
        return DISP_CHANGE_SUCCESSFUL;

    ret = load_adapter_settings(&adapters, &adapter_count, devname, devmode, from_registry, &changing_index);
    if (ret != DISP_CHANGE_SUCCESSFUL)
        return ret;

    /* Can't detach all adapters */
    if (all_detached_settings(adapters, adapter_count))
    {
        heap_free(adapters);
        return DISP_CHANGE_SUCCESSFUL;
    }

    if (!from_registry)
        place_changing_adapter(&adapters[changing_index], devmode);
    place_all_adapters(adapters, adapter_count, changing_index);

    /* Detach adapter first to free up CRTC */
    ret = apply_adapter_settings(adapters, adapter_count, TRUE);
    if (ret == DISP_CHANGE_SUCCESSFUL)
        ret = apply_adapter_settings(adapters, adapter_count, FALSE);

    heap_free(adapters);
    if (ret == DISP_CHANGE_SUCCESSFUL)
        X11DRV_DisplayDevices_Update(TRUE);
    return ret;
}
