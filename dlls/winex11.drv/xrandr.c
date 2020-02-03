/*
 * Wine X11drv Xrandr interface
 *
 * Copyright 2003 Alexander James Pasadyn
 * Copyright 2012 Henri Verbeet for CodeWeavers
 * Copyright 2019 Zhiyi Zhang for CodeWeavers
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
#include "wine/port.h"

#define NONAMELESSSTRUCT
#define NONAMELESSUNION

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(xrandr);
#ifdef HAVE_XRRGETPROVIDERRESOURCES
WINE_DECLARE_DEBUG_CHANNEL(winediag);
#endif

#ifdef SONAME_LIBXRANDR

#include <assert.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#ifdef HAVE_X11_XLIB_XCB_H
#include <X11/Xlib-xcb.h>
#endif
#ifdef HAVE_XCB_DRI3_H
#include <xcb/dri3.h>
#endif
#ifdef HAVE_XF86DRM_H
#include <xf86drm.h>
#endif
#include "x11drv.h"

#include "wine/heap.h"
#include "wine/library.h"
#include "wine/unicode.h"

static void *xrandr_handle;

#define MAKE_FUNCPTR(f) static typeof(f) * p##f;
MAKE_FUNCPTR(XRRConfigCurrentConfiguration)
MAKE_FUNCPTR(XRRConfigCurrentRate)
MAKE_FUNCPTR(XRRFreeScreenConfigInfo)
MAKE_FUNCPTR(XRRGetScreenInfo)
MAKE_FUNCPTR(XRRQueryExtension)
MAKE_FUNCPTR(XRRQueryVersion)
MAKE_FUNCPTR(XRRRates)
MAKE_FUNCPTR(XRRSetScreenConfig)
MAKE_FUNCPTR(XRRSetScreenConfigAndRate)
MAKE_FUNCPTR(XRRSizes)

#ifdef HAVE_XRRGETPROVIDERRESOURCES
MAKE_FUNCPTR(XRRFreeCrtcInfo)
MAKE_FUNCPTR(XRRFreeOutputInfo)
MAKE_FUNCPTR(XRRFreeScreenResources)
MAKE_FUNCPTR(XRRGetCrtcInfo)
MAKE_FUNCPTR(XRRGetOutputInfo)
MAKE_FUNCPTR(XRRGetScreenSizeRange)
MAKE_FUNCPTR(XRRGetScreenResources)
MAKE_FUNCPTR(XRRGetScreenResourcesCurrent)
MAKE_FUNCPTR(XRRSetCrtcConfig)
MAKE_FUNCPTR(XRRSetScreenSize)
MAKE_FUNCPTR(XRRSelectInput)
MAKE_FUNCPTR(XRRGetOutputPrimary)
MAKE_FUNCPTR(XRRGetProviderResources)
MAKE_FUNCPTR(XRRFreeProviderResources)
MAKE_FUNCPTR(XRRGetProviderInfo)
MAKE_FUNCPTR(XRRFreeProviderInfo)
#endif

#if defined(SONAME_LIBX11_XCB) && defined(SONAME_LIBXCB_DRI3)
MAKE_FUNCPTR(XGetXCBConnection)
MAKE_FUNCPTR(xcb_dri3_id)
MAKE_FUNCPTR(xcb_dri3_open)
MAKE_FUNCPTR(xcb_dri3_open_reply)
MAKE_FUNCPTR(xcb_dri3_open_reply_fds)
MAKE_FUNCPTR(xcb_get_extension_data)
static void *x11_xcb_handle;
static void *xcb_dri3_handle;
static BOOL dri3_loaded;
#endif

#ifdef SONAME_LIBDRM
MAKE_FUNCPTR(drmFreeDevice)
MAKE_FUNCPTR(drmGetDevice)
static void *drm_handle;
static BOOL drm_loaded;
#endif

#undef MAKE_FUNCPTR

static struct x11drv_mode_info *dd_modes;
static SizeID *xrandr10_modes;
static unsigned int xrandr_mode_count;
static int xrandr_current_mode = -1;

static int load_xrandr(void)
{
    int r = 0;

    if (wine_dlopen(SONAME_LIBXRENDER, RTLD_NOW|RTLD_GLOBAL, NULL, 0) &&
        (xrandr_handle = wine_dlopen(SONAME_LIBXRANDR, RTLD_NOW, NULL, 0)))
    {

#define LOAD_SYMBOL(library, symbol) \
        if((p##symbol = wine_dlsym(library##_handle, #symbol, NULL, 0)) == NULL) \
            goto sym_not_found;

        LOAD_SYMBOL(xrandr, XRRConfigCurrentConfiguration)
        LOAD_SYMBOL(xrandr, XRRConfigCurrentRate)
        LOAD_SYMBOL(xrandr, XRRFreeScreenConfigInfo)
        LOAD_SYMBOL(xrandr, XRRGetScreenInfo)
        LOAD_SYMBOL(xrandr, XRRQueryExtension)
        LOAD_SYMBOL(xrandr, XRRQueryVersion)
        LOAD_SYMBOL(xrandr, XRRRates)
        LOAD_SYMBOL(xrandr, XRRSetScreenConfig)
        LOAD_SYMBOL(xrandr, XRRSetScreenConfigAndRate)
        LOAD_SYMBOL(xrandr, XRRSizes)
        r = 1;

#ifdef HAVE_XRRGETPROVIDERRESOURCES
        LOAD_SYMBOL(xrandr, XRRFreeCrtcInfo)
        LOAD_SYMBOL(xrandr, XRRFreeOutputInfo)
        LOAD_SYMBOL(xrandr, XRRFreeScreenResources)
        LOAD_SYMBOL(xrandr, XRRGetCrtcInfo)
        LOAD_SYMBOL(xrandr, XRRGetOutputInfo)
        LOAD_SYMBOL(xrandr, XRRGetScreenSizeRange)
        LOAD_SYMBOL(xrandr, XRRGetScreenResources)
        LOAD_SYMBOL(xrandr, XRRGetScreenResourcesCurrent)
        LOAD_SYMBOL(xrandr, XRRSetCrtcConfig)
        LOAD_SYMBOL(xrandr, XRRSetScreenSize)
        LOAD_SYMBOL(xrandr, XRRSelectInput)
        LOAD_SYMBOL(xrandr, XRRGetOutputPrimary)
        LOAD_SYMBOL(xrandr, XRRGetProviderResources)
        LOAD_SYMBOL(xrandr, XRRFreeProviderResources)
        LOAD_SYMBOL(xrandr, XRRGetProviderInfo)
        LOAD_SYMBOL(xrandr, XRRFreeProviderInfo)
        r = 4;
#endif

#if defined(SONAME_LIBX11_XCB) && defined(SONAME_LIBXCB_DRI3)
        if ((x11_xcb_handle = wine_dlopen(SONAME_LIBX11_XCB, RTLD_NOW, NULL, 0))
            && (xcb_dri3_handle = wine_dlopen(SONAME_LIBXCB_DRI3, RTLD_NOW, NULL, 0)))
        {
            LOAD_SYMBOL(x11_xcb, XGetXCBConnection)
            LOAD_SYMBOL(xcb_dri3, xcb_dri3_id)
            LOAD_SYMBOL(xcb_dri3, xcb_dri3_open)
            LOAD_SYMBOL(xcb_dri3, xcb_dri3_open_reply)
            LOAD_SYMBOL(xcb_dri3, xcb_dri3_open_reply_fds)
            LOAD_SYMBOL(xcb_dri3, xcb_get_extension_data)
            dri3_loaded = TRUE;
        }
#endif

#ifdef SONAME_LIBDRM
        if ((drm_handle = wine_dlopen(SONAME_LIBDRM, RTLD_NOW, NULL, 0)))
        {
            LOAD_SYMBOL(drm, drmFreeDevice)
            LOAD_SYMBOL(drm, drmGetDevice)
            drm_loaded = TRUE;
        }
#endif

#undef LOAD_SYMBOL
    }

sym_not_found:
    if (!r)
        TRACE("Unable to load function ptrs from XRandR library\n");
    return r;
}

static int XRandRErrorHandler(Display *dpy, XErrorEvent *event, void *arg)
{
    return 1;
}

static int xrandr10_get_current_mode(void)
{
    SizeID size;
    Rotation rot;
    XRRScreenConfiguration *sc;
    short rate;
    unsigned int i;
    int res = -1;

    if (xrandr_current_mode != -1)
        return xrandr_current_mode;

    sc = pXRRGetScreenInfo (gdi_display, DefaultRootWindow( gdi_display ));
    size = pXRRConfigCurrentConfiguration (sc, &rot);
    rate = pXRRConfigCurrentRate (sc);
    pXRRFreeScreenConfigInfo(sc);

    for (i = 0; i < xrandr_mode_count; ++i)
    {
        if (xrandr10_modes[i] == size && dd_modes[i].refresh_rate == rate)
        {
            res = i;
            break;
        }
    }
    if (res == -1)
    {
        ERR("In unknown mode, returning default\n");
        return 0;
    }

    xrandr_current_mode = res;
    return res;
}

static LONG xrandr10_set_current_mode( int mode )
{
    SizeID size;
    Rotation rot;
    Window root;
    XRRScreenConfiguration *sc;
    Status stat;
    short rate;

    root = DefaultRootWindow( gdi_display );
    sc = pXRRGetScreenInfo (gdi_display, root);
    pXRRConfigCurrentConfiguration (sc, &rot);
    mode = mode % xrandr_mode_count;

    TRACE("Changing Resolution to %dx%d @%d Hz\n",
          dd_modes[mode].width,
          dd_modes[mode].height,
          dd_modes[mode].refresh_rate);

    size = xrandr10_modes[mode];
    rate = dd_modes[mode].refresh_rate;

    if (rate)
        stat = pXRRSetScreenConfigAndRate( gdi_display, sc, root, size, rot, rate, CurrentTime );
    else
        stat = pXRRSetScreenConfig( gdi_display, sc, root, size, rot, CurrentTime );

    pXRRFreeScreenConfigInfo(sc);

    if (stat == RRSetConfigSuccess)
    {
        xrandr_current_mode = mode;
        X11DRV_DisplayDevices_Update( TRUE );
        return DISP_CHANGE_SUCCESSFUL;
    }

    ERR("Resolution change not successful -- perhaps display has changed?\n");
    return DISP_CHANGE_FAILED;
}

static void xrandr10_init_modes(void)
{
    XRRScreenSize *sizes;
    int sizes_count;
    int i, j, nmodes = 0;

    sizes = pXRRSizes( gdi_display, DefaultScreen(gdi_display), &sizes_count );
    if (sizes_count <= 0) return;

    TRACE("XRandR: found %d sizes.\n", sizes_count);
    for (i = 0; i < sizes_count; ++i)
    {
        int rates_count;
        short *rates;

        rates = pXRRRates( gdi_display, DefaultScreen(gdi_display), i, &rates_count );
        TRACE("- at %d: %dx%d (%d rates):", i, sizes[i].width, sizes[i].height, rates_count);
        if (rates_count)
        {
            nmodes += rates_count;
            for (j = 0; j < rates_count; ++j)
            {
                if (j > 0)
                    TRACE(",");
                TRACE("  %d", rates[j]);
            }
        }
        else
        {
            ++nmodes;
            TRACE(" <default>");
        }
        TRACE(" Hz\n");
    }

    TRACE("XRandR modes: count=%d\n", nmodes);

    if (!(xrandr10_modes = HeapAlloc( GetProcessHeap(), 0, sizeof(*xrandr10_modes) * nmodes )))
    {
        ERR("Failed to allocate xrandr mode info array.\n");
        return;
    }

    dd_modes = X11DRV_Settings_SetHandlers( "XRandR 1.0",
                                            xrandr10_get_current_mode,
                                            xrandr10_set_current_mode,
                                            nmodes, 1 );

    xrandr_mode_count = 0;
    for (i = 0; i < sizes_count; ++i)
    {
        int rates_count;
        short *rates;

        rates = pXRRRates( gdi_display, DefaultScreen(gdi_display), i, &rates_count );

        if (rates_count)
        {
            for (j = 0; j < rates_count; ++j)
            {
                X11DRV_Settings_AddOneMode( sizes[i].width, sizes[i].height, 0, rates[j] );
                xrandr10_modes[xrandr_mode_count++] = i;
            }
        }
        else
        {
            X11DRV_Settings_AddOneMode( sizes[i].width, sizes[i].height, 0, 0 );
            xrandr10_modes[xrandr_mode_count++] = i;
        }
    }

    X11DRV_Settings_AddDepthModes();
    nmodes = X11DRV_Settings_GetModeCount();

    TRACE("Available DD modes: count=%d\n", nmodes);
    TRACE("Enabling XRandR\n");
}

#ifdef HAVE_XRRGETPROVIDERRESOURCES

static XRRScreenResources *xrandr_get_screen_resources(void)
{
    XRRScreenResources *resources = pXRRGetScreenResourcesCurrent( gdi_display, root_window );
    if (resources && !resources->ncrtc)
    {
        pXRRFreeScreenResources( resources );
        resources = pXRRGetScreenResources( gdi_display, root_window );
    }

    if (!resources)
        ERR("Failed to get screen resources.\n");
    return resources;
}

/* Some (304.64, possibly earlier) versions of the nvidia driver only
 * report a DFP's native mode through RandR 1.2 / 1.3. Standard DMT modes
 * are only listed through RandR 1.0 / 1.1. This is completely useless,
 * but NVIDIA considers this a feature, so it's unlikely to change. The
 * best we can do is to fall back to RandR 1.0 and encourage users to
 * consider more cooperative driver vendors when we detect such a
 * configuration. */
static BOOL is_broken_driver(void)
{
    XRRScreenResources *screen_resources;
    XRROutputInfo *output_info;
    XRRCrtcInfo *crtc_info;
    INT major, event, error;
    BOOL ret = TRUE;
    INT output;

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        return ret;

    for (output = 0; output < screen_resources->noutput; ++output)
    {
        output_info = pXRRGetOutputInfo( gdi_display, screen_resources, screen_resources->outputs[output] );
        if (!output_info)
            continue;

        if (output_info->connection != RR_Connected || !output_info->crtc)
        {
            pXRRFreeOutputInfo( output_info );
            continue;
        }

        crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, output_info->crtc );
        if (!crtc_info)
        {
            pXRRFreeOutputInfo( output_info );
            continue;
        }

        if (!crtc_info->mode)
        {
            pXRRFreeCrtcInfo( crtc_info );
            pXRRFreeOutputInfo( output_info );
            continue;
        }

        if (output_info->nmode > 1)
        {
            pXRRFreeCrtcInfo( crtc_info );
            pXRRFreeOutputInfo( output_info );
            pXRRFreeScreenResources( screen_resources );
            return FALSE;
        }

        pXRRFreeCrtcInfo( crtc_info );
        pXRRFreeOutputInfo( output_info );
        break;
    }
    pXRRFreeScreenResources( screen_resources );

    ret = XQueryExtension( gdi_display, "NV-CONTROL", &major, &event, &error );
    if (ret)
    {
        ERR_(winediag)("Broken NVIDIA RandR detected, falling back to RandR 1.0. "
                       "Please consider using the Nouveau driver instead.\n");
    }

    return ret;
}

static void get_screen_size( XRRScreenResources *resources, INT *width, INT *height )
{
    INT max_width, max_height;
    XRRCrtcInfo *crtc_info;
    int i;

    pXRRGetScreenSizeRange( gdi_display, root_window, width, height, &max_width, &max_height );

    for (i = 0; i < resources->ncrtc; ++i)
    {
        if (!(crtc_info = pXRRGetCrtcInfo( gdi_display, resources, resources->crtcs[i] )))
            continue;

        if (crtc_info->mode != None)
        {
            *width = max(*width, crtc_info->x + (INT)crtc_info->width);
            *height = max(*height, crtc_info->y + (INT)crtc_info->height);
        }

        pXRRFreeCrtcInfo( crtc_info );
    }
}

static DWORD get_frequency( const XRRModeInfo *mode )
{
    if (mode->hTotal && mode->vTotal)
    {
        double v_total = mode->vTotal;

        if (mode->modeFlags & RR_DoubleScan)
            v_total *= 2;
        if (mode->modeFlags & RR_Interlace)
            v_total /= 2;

        /* Adding 0.05 instead of 0.5 to round so that common frequencies like
         * 59.94Hz and 23.976Hz become 59Hz and 24Hz. Using 0.5 would make
         * 59.94Hz become 60Hz and would make it seem like there are two 60Hz modes */
        return mode->dotClock / (mode->hTotal * v_total) + 0.05;
    }

    return 0;
}

static RECT get_primary_rect( XRRScreenResources *resources )
{
    XRROutputInfo *output_info = NULL;
    XRRCrtcInfo *crtc_info = NULL;
    RROutput primary_output;
    RECT primary_rect = {0};
    RECT first_rect = {0};
    INT i;

    primary_output = pXRRGetOutputPrimary( gdi_display, root_window );
    if (!primary_output)
        goto fallback;

    output_info = pXRRGetOutputInfo( gdi_display, resources, primary_output );
    if (!output_info || output_info->connection != RR_Connected || !output_info->crtc)
        goto fallback;

    crtc_info = pXRRGetCrtcInfo( gdi_display, resources, output_info->crtc );
    if (!crtc_info || !crtc_info->mode)
        goto fallback;

    SetRect( &primary_rect, crtc_info->x, crtc_info->y, crtc_info->x + crtc_info->width, crtc_info->y + crtc_info->height );
    pXRRFreeCrtcInfo( crtc_info );
    pXRRFreeOutputInfo( output_info );
    return primary_rect;

/* Fallback when XRandR primary output is a disconnected output.
 * Try to find a crtc with (x, y) being (0, 0). If it's found then get the primary rect from that crtc,
 * otherwise use the first active crtc to get the primary rect */
fallback:
    if (crtc_info)
        pXRRFreeCrtcInfo( crtc_info );
    if (output_info)
        pXRRFreeOutputInfo( output_info );

    WARN("Primary is set to a disconnected XRandR output.\n");
    for (i = 0; i < resources->ncrtc; ++i)
    {
        crtc_info = pXRRGetCrtcInfo( gdi_display, resources, resources->crtcs[i] );
        if (!crtc_info)
            continue;

        if (!crtc_info->mode)
        {
            pXRRFreeCrtcInfo( crtc_info );
            continue;
        }

        if (!crtc_info->x && !crtc_info->y)
        {
            SetRect( &primary_rect, 0, 0, crtc_info->width, crtc_info->height );
            pXRRFreeCrtcInfo( crtc_info );
            break;
        }

        if (IsRectEmpty( &first_rect ))
            SetRect( &first_rect, crtc_info->x, crtc_info->y,
                     crtc_info->x + crtc_info->width, crtc_info->y + crtc_info->height );

        pXRRFreeCrtcInfo( crtc_info );
    }

    return IsRectEmpty( &primary_rect ) ? first_rect : primary_rect;
}

static BOOL is_crtc_primary( RECT primary, const XRRCrtcInfo *crtc )
{
    return crtc &&
           crtc->mode &&
           crtc->x == primary.left &&
           crtc->y == primary.top &&
           crtc->x + crtc->width == primary.right &&
           crtc->y + crtc->height == primary.bottom;
}

static int get_drm_device_from_provider( RRProvider provider )
{
#if defined(SONAME_LIBX11_XCB) && defined(SONAME_LIBXCB_DRI3)
    const xcb_query_extension_reply_t *extension;
    xcb_dri3_open_cookie_t cookie;
    xcb_dri3_open_reply_t *reply;
    xcb_connection_t *connection;
    int *fds, fd;

    if (!dri3_loaded)
        return -1;

    connection = pXGetXCBConnection( gdi_display );
    extension = pxcb_get_extension_data( connection, pxcb_dri3_id );
    if (!extension || !extension->present)
    {
        WARN("DRI3 is unsupported.\n");
        return -1;
    }

    cookie = pxcb_dri3_open( connection, DefaultRootWindow( gdi_display ), provider );
    reply = pxcb_dri3_open_reply( connection, cookie, NULL );

    if (!reply)
        return -1;

    if (reply->nfd != 1)
    {
        free( reply );
        return -1;
    }

    fds = pxcb_dri3_open_reply_fds( connection, reply );
    fd = fds[0];
    free( reply );
    fcntl( fd, F_SETFD, FD_CLOEXEC );
    return fd;
#endif /* defined(SONAME_LIBX11_XCB) && defined(SONAME_LIBXCB_DRI3) */

    WARN("DRI3 support not compiled in. Finding a DRM device with a RandR provider won't work!\n");
    return -1;
}

/* Fallback when DRI3 is unavailable. For example, GPUs using NVIDIA proprietary drivers.
 * This functions may not get the correct device when there are multiple GPUs present */
static int get_drm_device_from_index( int gpu_index )
{
#ifdef __linux__
    char device_path[MAX_PATH];
    int fd;

    sprintf( device_path, "/dev/dri/card%d", gpu_index );
    fd = open( device_path, O_RDONLY );
    if (fd < 0)
        return -1;

    fcntl( fd, F_SETFD, FD_CLOEXEC );
    return fd;
#endif /* __linux__ */

    return -1;
}

#ifdef __linux__
static unsigned int read_id( const char *device_name, const char *id_name )
{
    char filename[MAX_PATH];
    unsigned int id = 0;
    FILE *file;

    sprintf( filename, "%s/%s", device_name, id_name );
    file = fopen( filename, "r" );
    if (!file)
        return 0;

    fscanf( file, "%x", &id );
    fclose( file );
    return id;
}
#endif /* __linux__ */

static BOOL get_gpu_pci_id( struct x11drv_gpu *gpu, RRProvider provider, int gpu_index )
{
    int fd = get_drm_device_from_provider( provider );

    if (fd < 0)
        fd = get_drm_device_from_index( gpu_index );

    if (fd < 0)
    {
        WARN("Failed to get DRM device.\n");
        return FALSE;
    }

#ifdef SONAME_LIBDRM
    {
        drmDevice *device;
        int ret;

        if (!drm_loaded)
        {
            close( fd );
            return FALSE;
        }

        ret = pdrmGetDevice( fd, &device );
        close( fd );

        if (ret != 0)
            return FALSE;

        if (device->bustype != DRM_BUS_PCI)
        {
            pdrmFreeDevice( &device );
            return FALSE;
        }

        gpu->vendor_id = device->deviceinfo.pci->vendor_id;
        gpu->device_id = device->deviceinfo.pci->device_id;
        gpu->subsys_id = (UINT)device->deviceinfo.pci->subdevice_id << 16 | device->deviceinfo.pci->subvendor_id;
        gpu->revision_id = device->deviceinfo.pci->revision_id;
        pdrmFreeDevice( &device );
        return TRUE;
    }
#endif /* SONAME_LIBDRM */

    /* Fallback on Linux when libdrm is too old to have drmGetDevice() */
#ifdef __linux__
    {
        char fd_path[MAX_PATH], link[MAX_PATH], device_path[128], node_name[64];
        char *subsystem_name, subsystem_path[MAX_PATH];
        int ret;

        /* Get DRM device path from fd */
        snprintf( fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd );
        ret = readlink( fd_path, link, sizeof(link) - 1 );
        close( fd );

        if (ret < 0)
            return FALSE;

        link[ret] = 0;
        if (sscanf( link, "/dev/dri/%63s", node_name ) != 1)
            return FALSE;

        snprintf( device_path, sizeof(device_path), "/sys/class/drm/%s/device", node_name );
        snprintf( subsystem_path, sizeof(subsystem_path), "%s/subsystem", device_path );

        /* Check if device is using PCI */
        ret = readlink( subsystem_path, link, sizeof(link) - 1 );
        if (ret < 0)
            return FALSE;

        link[ret] = 0;
        subsystem_name = strrchr( link, '/' );
        if (!subsystem_name)
            return FALSE;

        if (strncmp( subsystem_name + 1, "pci", 3 ))
            return FALSE;

        /* Read IDs */
        gpu->vendor_id = read_id( device_path, "vendor" );
        gpu->device_id = read_id( device_path, "device" );
        gpu->subsys_id = read_id( device_path, "subsystem_device" ) << 16 | read_id( device_path, "subsystem_vendor" );
        gpu->revision_id = read_id( device_path, "revision" );
        return TRUE;
    }
#endif /* __linux__ */

    close( fd );
    WARN("DRM support not compiled in. No valid PCI ID will be reported for GPUs.\n");
    return FALSE;
}

static BOOL xrandr14_get_gpus( struct x11drv_gpu **new_gpus, int *count )
{
    static const WCHAR wine_adapterW[] = {'W','i','n','e',' ','A','d','a','p','t','e','r',0};
    struct x11drv_gpu *gpus = NULL;
    XRRScreenResources *screen_resources = NULL;
    XRRProviderResources *provider_resources = NULL;
    XRRProviderInfo *provider_info = NULL;
    XRRCrtcInfo *crtc_info = NULL;
    INT primary_provider = -1;
    RECT primary_rect;
    BOOL ret = FALSE;
    INT i, j;

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        goto done;

    provider_resources = pXRRGetProviderResources( gdi_display, root_window );
    if (!provider_resources)
        goto done;

    gpus = heap_calloc( provider_resources->nproviders ? provider_resources->nproviders : 1, sizeof(*gpus) );
    if (!gpus)
        goto done;

    /* Some XRandR implementations don't support providers.
     * In this case, report a fake one to try searching adapters in screen resources */
    if (!provider_resources->nproviders)
    {
        WARN("XRandR implementation doesn't report any providers, faking one.\n");
        lstrcpyW( gpus[0].name, wine_adapterW );
        *new_gpus = gpus;
        *count = 1;
        ret = TRUE;
        goto done;
    }

    primary_rect = get_primary_rect( screen_resources );
    for (i = 0; i < provider_resources->nproviders; ++i)
    {
        provider_info = pXRRGetProviderInfo( gdi_display, screen_resources, provider_resources->providers[i] );
        if (!provider_info)
            goto done;

        /* Find primary provider */
        for (j = 0; primary_provider == -1 && j < provider_info->ncrtcs; ++j)
        {
            crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, provider_info->crtcs[j] );
            if (!crtc_info)
                continue;

            if (is_crtc_primary( primary_rect, crtc_info ))
            {
                primary_provider = i;
                pXRRFreeCrtcInfo( crtc_info );
                break;
            }

            pXRRFreeCrtcInfo( crtc_info );
        }

        gpus[i].id = provider_resources->providers[i];
        MultiByteToWideChar( CP_UTF8, 0, provider_info->name, -1, gpus[i].name, ARRAY_SIZE(gpus[i].name) );
        pXRRFreeProviderInfo( provider_info );

        if (!get_gpu_pci_id( &gpus[i], provider_resources->providers[i], i ))
            WARN("Failed to get PCI ID for GPU %s\n", wine_dbgstr_w(gpus[i].name));

        TRACE("name:%s vendor id:%#x device id:%#x subsystem id:%#x revision id:%#x\n", wine_dbgstr_w(gpus[i].name),
              gpus[i].vendor_id, gpus[i].device_id, gpus[i].subsys_id, gpus[i].revision_id);
    }

    /* Make primary GPU the first */
    if (primary_provider > 0)
    {
        struct x11drv_gpu tmp = gpus[0];
        gpus[0] = gpus[primary_provider];
        gpus[primary_provider] = tmp;
    }

    *new_gpus = gpus;
    *count = provider_resources->nproviders;
    ret = TRUE;
done:
    if (provider_resources)
        pXRRFreeProviderResources( provider_resources );
    if (screen_resources)
        pXRRFreeScreenResources( screen_resources );
    if (!ret)
    {
        heap_free( gpus );
        ERR("Failed to get gpus\n");
    }
    return ret;
}

static void xrandr14_free_gpus( struct x11drv_gpu *gpus )
{
    heap_free( gpus );
}

static BOOL xrandr14_get_adapters( ULONG_PTR gpu_id, struct x11drv_adapter **new_adapters, int *count )
{
    struct x11drv_adapter *adapters = NULL;
    XRRScreenResources *screen_resources = NULL;
    XRRProviderInfo *provider_info = NULL;
    XRRCrtcInfo *enum_crtc_info, *crtc_info = NULL;
    XRROutputInfo *output_info = NULL;
    RROutput *outputs;
    INT crtc_count, output_count;
    INT primary_adapter = 0;
    INT adapter_count = 0;
    BOOL mirrored, detached;
    RECT primary_rect;
    BOOL ret = FALSE;
    INT i, j;

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        goto done;

    if (gpu_id)
    {
        provider_info = pXRRGetProviderInfo( gdi_display, screen_resources, gpu_id );
        if (!provider_info)
            goto done;

        crtc_count = provider_info->ncrtcs;
        output_count = provider_info->noutputs;
        outputs = provider_info->outputs;
    }
    /* Fake provider id, search adapters in screen resources */
    else
    {
        crtc_count = screen_resources->ncrtc;
        output_count = screen_resources->noutput;
        outputs = screen_resources->outputs;
    }

    /* Actual adapter count could be less */
    adapters = heap_calloc( crtc_count, sizeof(*adapters) );
    if (!adapters)
        goto done;

    primary_rect = get_primary_rect( screen_resources );
    for (i = 0; i < output_count; ++i)
    {
        output_info = pXRRGetOutputInfo( gdi_display, screen_resources, outputs[i] );
        if (!output_info)
            goto done;

        /* Only connected output are considered as monitors */
        if (output_info->connection != RR_Connected)
        {
            pXRRFreeOutputInfo( output_info );
            output_info = NULL;
            continue;
        }

        /* Connected output doesn't mean the output is attached to a crtc */
        detached = FALSE;
        if (output_info->crtc)
        {
            crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, output_info->crtc );
            if (!crtc_info)
                goto done;
        }

        if (!output_info->crtc || !crtc_info->mode)
            detached = TRUE;

        /* Ignore crtc mirroring slaves because mirrored monitors are under the same adapter */
        mirrored = FALSE;
        if (!detached)
        {
            for (j = 0; j < screen_resources->ncrtc; ++j)
            {
                enum_crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, screen_resources->crtcs[j] );
                if (!enum_crtc_info)
                    goto done;

                /* Some crtcs on different providers may have the same coordinates, aka mirrored.
                 * Choose the crtc with the lowest value as primary and the rest will then be slaves
                 * in a mirroring set */
                if (crtc_info->x == enum_crtc_info->x &&
                    crtc_info->y == enum_crtc_info->y &&
                    crtc_info->width == enum_crtc_info->width &&
                    crtc_info->height == enum_crtc_info->height &&
                    output_info->crtc > screen_resources->crtcs[j])
                {
                    mirrored = TRUE;
                    pXRRFreeCrtcInfo( enum_crtc_info );
                    break;
                }

                pXRRFreeCrtcInfo( enum_crtc_info );
            }
        }

        if (!mirrored || detached)
        {
            /* Use RROutput as adapter id. The reason of not using RRCrtc is that we need to detect inactive but
             * attached monitors */
            adapters[adapter_count].id = outputs[i];
            if (!detached)
                adapters[adapter_count].state_flags |= DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
            if (is_crtc_primary( primary_rect, crtc_info ))
            {
                adapters[adapter_count].state_flags |= DISPLAY_DEVICE_PRIMARY_DEVICE;
                primary_adapter = adapter_count;
            }

            ++adapter_count;
        }

        pXRRFreeOutputInfo( output_info );
        output_info = NULL;
        if (crtc_info)
        {
            pXRRFreeCrtcInfo( crtc_info );
            crtc_info = NULL;
        }
    }

    /* Make primary adapter the first */
    if (primary_adapter)
    {
        struct x11drv_adapter tmp = adapters[0];
        adapters[0] = adapters[primary_adapter];
        adapters[primary_adapter] = tmp;
    }

    *new_adapters = adapters;
    *count = adapter_count;
    ret = TRUE;
done:
    if (screen_resources)
        pXRRFreeScreenResources( screen_resources );
    if (provider_info)
        pXRRFreeProviderInfo( provider_info );
    if (output_info)
        pXRRFreeOutputInfo( output_info );
    if (crtc_info)
        pXRRFreeCrtcInfo( crtc_info );
    if (!ret)
    {
        heap_free( adapters );
        ERR("Failed to get adapters\n");
    }
    return ret;
}

static void xrandr14_free_adapters( struct x11drv_adapter *adapters )
{
    heap_free( adapters );
}

static BOOL xrandr14_get_monitors( ULONG_PTR adapter_id, struct x11drv_monitor **new_monitors, int *count )
{
    static const WCHAR generic_nonpnp_monitorW[] = {
        'G','e','n','e','r','i','c',' ',
        'N','o','n','-','P','n','P',' ','M','o','n','i','t','o','r',0};
    struct x11drv_monitor *realloc_monitors, *monitors = NULL;
    XRRScreenResources *screen_resources = NULL;
    XRROutputInfo *output_info = NULL, *enum_output_info = NULL;
    XRRCrtcInfo *crtc_info = NULL, *enum_crtc_info;
    INT primary_index = 0, monitor_count = 0, capacity;
    RECT work_rect, primary_rect;
    BOOL ret = FALSE;
    INT i;

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        goto done;

    /* First start with a 2 monitors, should be enough for most cases */
    capacity = 2;
    monitors = heap_calloc( capacity, sizeof(*monitors) );
    if (!monitors)
        goto done;

    output_info = pXRRGetOutputInfo( gdi_display, screen_resources, adapter_id );
    if (!output_info)
        goto done;

    if (output_info->crtc)
    {
        crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, output_info->crtc );
        if (!crtc_info)
            goto done;
    }

    /* Inactive but attached monitor, no need to check for mirrored/slave monitors */
    if (!output_info->crtc || !crtc_info->mode)
    {
        lstrcpyW( monitors[monitor_count].name, generic_nonpnp_monitorW );
        monitors[monitor_count].state_flags = DISPLAY_DEVICE_ATTACHED;
        monitor_count = 1;
    }
    /* Active monitors, need to find other monitors with the same coordinates as mirrored */
    else
    {
        query_work_area( &work_rect );
        primary_rect = get_primary_rect( screen_resources );

        for (i = 0; i < screen_resources->noutput; ++i)
        {
            enum_output_info = pXRRGetOutputInfo( gdi_display, screen_resources, screen_resources->outputs[i] );
            if (!enum_output_info)
                goto done;

            /* Detached outputs don't count */
            if (enum_output_info->connection != RR_Connected)
            {
                pXRRFreeOutputInfo( enum_output_info );
                enum_output_info = NULL;
                continue;
            }

            /* Allocate more space if needed */
            if (monitor_count >= capacity)
            {
                capacity *= 2;
                realloc_monitors = heap_realloc( monitors, capacity * sizeof(*monitors) );
                if (!realloc_monitors)
                    goto done;
                monitors = realloc_monitors;
            }

            if (enum_output_info->crtc)
            {
                enum_crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, enum_output_info->crtc );
                if (!enum_crtc_info)
                    goto done;

                if (enum_crtc_info->x == crtc_info->x &&
                    enum_crtc_info->y == crtc_info->y &&
                    enum_crtc_info->width == crtc_info->width &&
                    enum_crtc_info->height == crtc_info->height)
                {
                    /* FIXME: Read output EDID property and parse the data to get the correct name */
                    lstrcpyW( monitors[monitor_count].name, generic_nonpnp_monitorW );

                    SetRect( &monitors[monitor_count].rc_monitor, crtc_info->x, crtc_info->y,
                             crtc_info->x + crtc_info->width, crtc_info->y + crtc_info->height );
                    if (!IntersectRect( &monitors[monitor_count].rc_work, &work_rect, &monitors[monitor_count].rc_monitor ))
                        monitors[monitor_count].rc_work = monitors[monitor_count].rc_monitor;

                    monitors[monitor_count].state_flags = DISPLAY_DEVICE_ATTACHED;
                    if (!IsRectEmpty( &monitors[monitor_count].rc_monitor ))
                        monitors[monitor_count].state_flags |= DISPLAY_DEVICE_ACTIVE;

                    if (is_crtc_primary( primary_rect, crtc_info ))
                        primary_index = monitor_count;
                    monitor_count++;
                }

                pXRRFreeCrtcInfo( enum_crtc_info );
            }

            pXRRFreeOutputInfo( enum_output_info );
            enum_output_info = NULL;
        }

        /* Make sure the first monitor is the primary */
        if (primary_index)
        {
            struct x11drv_monitor tmp = monitors[0];
            monitors[0] = monitors[primary_index];
            monitors[primary_index] = tmp;
        }

        /* Make sure the primary monitor origin is at (0, 0) */
        for (i = 0; i < monitor_count; i++)
        {
            OffsetRect( &monitors[i].rc_monitor, -primary_rect.left, -primary_rect.top );
            OffsetRect( &monitors[i].rc_work, -primary_rect.left, -primary_rect.top );
        }
    }

    *new_monitors = monitors;
    *count = monitor_count;
    ret = TRUE;
done:
    if (screen_resources)
        pXRRFreeScreenResources( screen_resources );
    if (output_info)
        pXRRFreeOutputInfo( output_info);
    if (crtc_info)
        pXRRFreeCrtcInfo( crtc_info );
    if (enum_output_info)
        pXRRFreeOutputInfo( enum_output_info );
    if (!ret)
    {
        heap_free( monitors );
        ERR("Failed to get monitors\n");
    }
    return ret;
}

static void xrandr14_free_monitors( struct x11drv_monitor *monitors )
{
    heap_free( monitors );
}

static BOOL xrandr14_device_change_handler( HWND hwnd, XEvent *event )
{
    if (hwnd == GetDesktopWindow() && GetWindowThreadProcessId( hwnd, NULL ) == GetCurrentThreadId())
    {
        /* Don't send WM_DISPLAYCHANGE message here because this event may be a result from ChangeDisplaySettings().
         * Otherwise, ChangeDisplaySettings() would send multiple WM_DISPLAYCHANGE messages instead of just one */
        X11DRV_DisplayDevices_Update( FALSE );

        init_display_registry_settings();
    }
    return FALSE;
}

static void xrandr14_register_event_handlers(void)
{
    Display *display = thread_init_display();
    int event_base, error_base;

    if (!pXRRQueryExtension( display, &event_base, &error_base ))
        return;

    pXRRSelectInput( display, root_window,
                     RRCrtcChangeNotifyMask | RROutputChangeNotifyMask | RRProviderChangeNotifyMask );
    X11DRV_register_event_handler( event_base + RRNotify_CrtcChange, xrandr14_device_change_handler,
                                   "XRandR CrtcChange" );
    X11DRV_register_event_handler( event_base + RRNotify_OutputChange, xrandr14_device_change_handler,
                                   "XRandR OutputChange" );
    X11DRV_register_event_handler( event_base + RRNotify_ProviderChange, xrandr14_device_change_handler,
                                   "XRandR ProviderChange" );
}

/* XRandR 1.4 settings handler */
static BOOL xrandr14_get_id( const WCHAR *device_name, ULONG_PTR *id )
{
    static const WCHAR displayW[] = {'\\','\\','.','\\','D','I','S','P','L','A','Y'};
    INT gpu_count, adapter_count, display_count = 0;
    INT gpu_index, adapter_index, display_index;
    struct x11drv_adapter *adapters;
    struct x11drv_gpu *gpus;
    WCHAR *end;

    if (strncmpiW( device_name, displayW, ARRAY_SIZE(displayW) ))
        return FALSE;

    display_index = strtolW( device_name + ARRAY_SIZE(displayW), &end, 10 ) - 1;
    if (*end)
        return FALSE;

    if (!xrandr14_get_gpus( &gpus, &gpu_count ))
        return FALSE;

    for (gpu_index = 0; gpu_index < gpu_count; ++gpu_index)
    {
        if (!xrandr14_get_adapters( gpus[gpu_index].id, &adapters, &adapter_count ))
        {
            xrandr14_free_gpus( gpus );
            return FALSE;
        }

        adapter_index = display_index - display_count;
        if (adapter_index < adapter_count)
        {
            *id = adapters[adapter_index].id;
            xrandr14_free_adapters( adapters );
            xrandr14_free_gpus( gpus );
            return TRUE;
        }

        display_count += adapter_count;
        xrandr14_free_adapters( adapters );
    }
    xrandr14_free_gpus( gpus );
    return FALSE;
}

static void add_xrandr14_mode( DEVMODEW *mode, XRRModeInfo *info, DWORD depth, DWORD frequency )
{
    mode->dmSize = sizeof(*mode);
    mode->dmDriverExtra = sizeof(RRMode);
    mode->dmFields = DM_DISPLAYORIENTATION | DM_BITSPERPEL | DM_PELSWIDTH |
            DM_PELSHEIGHT | DM_DISPLAYFLAGS;
    if (frequency)
        mode->dmFields |= DM_DISPLAYFREQUENCY;
    mode->u1.s2.dmDisplayOrientation = DMDO_DEFAULT;
    mode->dmBitsPerPel = depth;
    mode->dmPelsWidth = info->width;
    mode->dmPelsHeight = info->height;
    mode->u2.dmDisplayFlags = 0;
    mode->dmDisplayFrequency = frequency;
    *((RRMode *)((BYTE *)mode + sizeof(*mode))) = info->id;
}

static BOOL xrandr14_get_modes( ULONG_PTR id, DWORD flags, DEVMODEW **new_modes, INT *mode_count )
{
    XRRScreenResources *screen_resources = NULL;
    XRROutputInfo *output_info = NULL;
    INT depth_index, mode_index = 0;
    RROutput output = (RROutput)id;
    XRRModeInfo *mode_info;
    BOOL ret = FALSE;
    DEVMODEW *modes;
    DWORD frequency;
    INT i, j;

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        goto done;

    output_info = pXRRGetOutputInfo( gdi_display, screen_resources, output );
    if (!output_info)
        goto done;

    /* Allocate space for reported modes in different color depths.
     * Store a RRMode at the end of each DEVMODEW as private driver data */
    modes = heap_calloc( output_info->nmode * DEPTH_COUNT, sizeof(*modes) + sizeof(RRMode) );
    if (!modes)
        goto done;

    for (i = 0; i < output_info->nmode; ++i)
    {
        for (j = 0; j < screen_resources->nmode; ++j)
        {
            if (output_info->modes[i] != screen_resources->modes[j].id)
                continue;

            mode_info = &screen_resources->modes[j];
            frequency = get_frequency( mode_info );

            for (depth_index = 0; depth_index < DEPTH_COUNT; ++depth_index)
            {
                DEVMODEW *mode = (DEVMODEW *)((BYTE *)modes + (sizeof(*mode) + sizeof(RRMode)) * mode_index++);
                add_xrandr14_mode( mode, mode_info, depths[depth_index], frequency );
            }

            break;
        }
    }

    ret = TRUE;
    *new_modes = modes;
    *mode_count = mode_index;
done:
    if (output_info)
        pXRRFreeOutputInfo( output_info );
    if (screen_resources)
        pXRRFreeScreenResources( screen_resources );
    return ret;
}

static void xrandr14_free_modes( DEVMODEW *modes )
{
    heap_free( modes );
}

static BOOL xrandr14_get_current_settings( ULONG_PTR id, DEVMODEW *mode )
{
    XRRScreenResources *screen_resources = NULL;
    XRROutputInfo *output_info = NULL;
    RROutput output = (RROutput)id;
    XRRModeInfo *mode_info = NULL;
    XRRCrtcInfo *crtc_info = NULL;
    BOOL ret = FALSE;
    INT mode_index;
    RECT primary;

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        goto done;

    output_info = pXRRGetOutputInfo( gdi_display, screen_resources, output );
    if (!output_info)
        goto done;

    /* Detached */
    if (!output_info->crtc)
    {
        mode->dmDriverExtra = 0;
        mode->dmFields = DM_DISPLAYORIENTATION | DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS
                         | DM_DISPLAYFREQUENCY | DM_POSITION;
        mode->u1.s2.dmDisplayOrientation = DMDO_DEFAULT;
        mode->dmBitsPerPel = 0;
        mode->dmPelsWidth = 0;
        mode->dmPelsHeight = 0;
        mode->u2.dmDisplayFlags = 0;
        mode->dmDisplayFrequency = 0;
        mode->u1.s2.dmPosition.x = 0;
        mode->u1.s2.dmPosition.y = 0;

        ret = TRUE;
        goto done;
    }

    /* Attached */
    crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, output_info->crtc );
    if (!crtc_info)
        goto done;

    for (mode_index = 0; mode_index < screen_resources->nmode; ++mode_index)
    {
        if (crtc_info->mode != screen_resources->modes[mode_index].id)
            continue;

        mode_info = &screen_resources->modes[mode_index];
        break;
    }

    if (!mode_info)
        goto done;

    mode->dmDriverExtra = 0;
    mode->dmFields = DM_DISPLAYORIENTATION | DM_BITSPERPEL | DM_PELSWIDTH |
            DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY | DM_POSITION;
    mode->u1.s2.dmDisplayOrientation = DMDO_DEFAULT;
    mode->dmBitsPerPel = screen_bpp;
    mode->dmPelsWidth = mode_info->width;
    mode->dmPelsHeight = mode_info->height;
    mode->u2.dmDisplayFlags = 0;
    mode->dmDisplayFrequency = get_frequency( mode_info );
    /* Convert RandR coordinates to virtual screen coordinates */
    primary = get_primary_rect( screen_resources );
    mode->u1.s2.dmPosition.x = crtc_info->x - primary.left;
    mode->u1.s2.dmPosition.y = crtc_info->y - primary.top;

    ret = TRUE;
done:
    if (crtc_info)
        pXRRFreeCrtcInfo( crtc_info );
    if (output_info)
        pXRRFreeOutputInfo( output_info );
    if (screen_resources)
        pXRRFreeScreenResources( screen_resources );
    return ret;
}

static LONG xrandr14_set_current_settings( ULONG_PTR id, DEVMODEW *mode )
{
    XRRScreenResources *screen_resources = NULL;
    RROutput output = (RROutput)id, *outputs;
    INT screen_width = 0, screen_height = 0;
    XRROutputInfo *output_info = NULL;
    XRRCrtcInfo *crtc_info = NULL;
    LONG ret = DISP_CHANGE_FAILED;
    Rotation rotation;
    RECT primary_rect;
    INT output_count;
    RRCrtc crtc = 0;
    Status status;
    RRMode rrmode;
    INT x, y, i;

    if (mode->dmFields & DM_BITSPERPEL && mode->dmBitsPerPel != screen_bpp)
        WARN("Cannot change screen color depth from %ubits to %ubits!\n", screen_bpp, mode->dmBitsPerPel);

    screen_resources = xrandr_get_screen_resources();
    if (!screen_resources)
        return ret;

    XGrabServer( gdi_display );

    output_info = pXRRGetOutputInfo( gdi_display, screen_resources, output );
    if (!output_info || output_info->connection != RR_Connected)
        goto done;

    /* Attached */
    if (output_info->crtc)
    {
        crtc = output_info->crtc;
    }
    /* Detached, need to find a free CRTC */
    else
    {
        for (i = 0; i < output_info->ncrtc; ++i)
        {
            crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, output_info->crtcs[i] );
            if (!crtc_info)
                goto done;

            if (!crtc_info->noutput)
            {
                crtc = output_info->crtcs[i];
                pXRRFreeCrtcInfo( crtc_info );
                crtc_info = NULL;
                break;
            }

            pXRRFreeCrtcInfo( crtc_info );
            crtc_info = NULL;
        }

        /* Failed to find a free CRTC */
        if (i == output_info->ncrtc)
            goto done;
    }

    crtc_info = pXRRGetCrtcInfo( gdi_display, screen_resources, crtc );
    if (!crtc_info)
        goto done;

    /* Convert virtual screen coordinates to RandR coordinates */
    primary_rect = get_primary_rect( screen_resources );
    x = mode->u1.s2.dmPosition.x + primary_rect.left;
    y = mode->u1.s2.dmPosition.y + primary_rect.top;

    assert(mode->dmDriverExtra == sizeof(RRMode));
    rrmode = *((RRMode *)((BYTE *)mode + sizeof(*mode)));

    if (crtc_info->noutput)
    {
        outputs = crtc_info->outputs;
        output_count = crtc_info->noutput;
        rotation = crtc_info->rotation;
    }
    else
    {
        outputs = &output;
        output_count = 1;
        rotation = RR_Rotate_0;
    }

    get_screen_size( screen_resources, &screen_width, &screen_height );
    screen_width = max(screen_width, x + (INT)mode->dmPelsWidth);
    screen_height = max(screen_height, y + (INT)mode->dmPelsHeight);

    pXRRSetScreenSize( gdi_display, root_window, screen_width, screen_height,
                       screen_width * DisplayWidthMM(gdi_display, default_visual.screen)
                               / DisplayWidth(gdi_display, default_visual.screen),
                       screen_height * DisplayHeightMM(gdi_display, default_visual.screen)
                               / DisplayHeight(gdi_display, default_visual.screen) );

    status = pXRRSetCrtcConfig( gdi_display, screen_resources, crtc, CurrentTime, x, y, rrmode, rotation,
                                outputs, output_count );
    if (status == RRSetConfigSuccess)
        ret = DISP_CHANGE_SUCCESSFUL;

done:
    XUngrabServer( gdi_display );
    XSync( gdi_display, FALSE );
    if (crtc_info)
        pXRRFreeCrtcInfo( crtc_info );
    if (output_info)
        pXRRFreeOutputInfo( output_info );
    pXRRFreeScreenResources( screen_resources );
    return ret;
}

#endif

void X11DRV_XRandR_Init(void)
{
    struct x11drv_display_device_handler display_handler;
    struct x11drv_settings_handler settings_handler;
    int event_base, error_base, minor, ret;
    static int major;
    Bool ok;

    if (major) return; /* already initialized? */
    if (!usexrandr) return; /* disabled in config */
    if (is_virtual_desktop()) return;
    if (!(ret = load_xrandr())) return;  /* can't load the Xrandr library */

    /* see if Xrandr is available */
    if (!pXRRQueryExtension( gdi_display, &event_base, &error_base )) return;
    X11DRV_expect_error( gdi_display, XRandRErrorHandler, NULL );
    ok = pXRRQueryVersion( gdi_display, &major, &minor );
    if (X11DRV_check_error() || !ok) return;

    TRACE("Found XRandR %d.%d.\n", major, minor);

    xrandr10_init_modes();

#ifdef HAVE_XRRGETPROVIDERRESOURCES
    if (ret >= 4 && (major > 1 || (major == 1 && minor >= 4)))
    {
        display_handler.name = "XRandR 1.4";
        display_handler.priority = 200;
        display_handler.get_gpus = xrandr14_get_gpus;
        display_handler.get_adapters = xrandr14_get_adapters;
        display_handler.get_monitors = xrandr14_get_monitors;
        display_handler.free_gpus = xrandr14_free_gpus;
        display_handler.free_adapters = xrandr14_free_adapters;
        display_handler.free_monitors = xrandr14_free_monitors;
        display_handler.register_event_handlers = xrandr14_register_event_handlers;
        X11DRV_DisplayDevices_SetHandler( &display_handler );

        if (is_broken_driver())
            return;

        settings_handler.name = "XRandR 1.4";
        settings_handler.priority = 300;
        settings_handler.get_id = xrandr14_get_id;
        settings_handler.get_modes = xrandr14_get_modes;
        settings_handler.free_modes = xrandr14_free_modes;
        settings_handler.get_current_settings = xrandr14_get_current_settings;
        settings_handler.set_current_settings = xrandr14_set_current_settings;
        X11DRV_Settings_SetHandler( &settings_handler );
    }
#endif
}

#else /* SONAME_LIBXRANDR */

void X11DRV_XRandR_Init(void)
{
    TRACE("XRandR support not compiled in.\n");
}

#endif /* SONAME_LIBXRANDR */
