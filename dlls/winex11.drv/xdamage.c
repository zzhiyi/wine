/*
 * Wine X11DRV XDamage interface
 *
 * Copyright 2021 Zhiyi Zhang for CodeWeavers
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

#if 0
#pragma makedep unix
#endif

#include "config.h"
#include <stdarg.h>
#include <dlfcn.h>

#include "windef.h"
#include "winbase.h"
#include "x11drv.h"
#include "xdamage.h"
#include "xrender.h"

#ifdef SONAME_LIBXDAMAGE

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(xdamage);

#define MAKE_FUNCPTR(f) typeof(f) * p##f;

MAKE_FUNCPTR(XDamageCreate)
MAKE_FUNCPTR(XDamageDestroy)
MAKE_FUNCPTR(XDamageQueryExtension)
MAKE_FUNCPTR(XDamageSubtract)

#undef MAKE_FUNCPTR

BOOL X11DRV_DamageNotify( HWND hwnd, XEvent *xev )
{
    Drawable src_drawable, dst_drawable;
    struct x11drv_win_data *data = NULL;
    Picture src_picture, dst_picture;
    XRenderPictureAttributes pa;
    XTransform transform = {0};
    XRenderPictFormat *format;
    XWindowAttributes wa;
    Display *display;
    unsigned int dpi;
    Damage damage;
    double scale;

    TRACE("hwnd %p, event %p.\n", hwnd, xev);

    if (hwnd == NtUserGetDesktopWindow())
    {
        display = thread_display();
        damage = root_window_damage;
        src_drawable = root_window_drawable;
        dst_drawable = root_window;
    }
    else
    {
        if (!(data = get_win_data(hwnd)))
            return FALSE;

        display = data->display;
        damage = data->damage;
        src_drawable = data->whole_drawable;
        dst_drawable = data->whole_window;
    }

    if (!damage)
    {
        /* Damage was destroyed before event arrival */
        if (data)
            release_win_data( data );
        return FALSE;
    }

    pXDamageSubtract(display, damage, None, None);

    pa.subwindow_mode = IncludeInferiors;
    XGetWindowAttributes(display, src_drawable, &wa);
    format = pXRenderFindVisualFormat(display, wa.visual);
    src_picture = pXRenderCreatePicture(display, src_drawable, format, CPSubwindowMode, &pa);
    dst_picture = pXRenderCreatePicture(display, dst_drawable, format, 0, &pa);

    dpi = get_effective_dpi();
    scale = (double)dpi / USER_DEFAULT_SCREEN_DPI;

    /* scaling matrix */
    transform.matrix[0][0] = XDoubleToFixed(1);
    transform.matrix[1][1] = XDoubleToFixed(1);
    transform.matrix[2][2] = XDoubleToFixed(scale);
    pXRenderSetPictureTransform(display, src_picture, &transform);

    /* !! COPY only changed parts */
//    pXRenderComposite(data->display, PictOpSrc, src_picture, None, dst_picture, 0, 0, 0, 0, 0, 0,
//                      wa.width, wa.height);

    pXRenderComposite(display, PictOpSrc, src_picture, None, dst_picture, 0, 0, 0, 0, 0, 0,
                      muldiv(wa.width, dpi, USER_DEFAULT_SCREEN_DPI),
                      muldiv(wa.height, dpi, USER_DEFAULT_SCREEN_DPI));

    pXRenderFreePicture(display, src_picture);
    pXRenderFreePicture(display, dst_picture);

    if (data)
        release_win_data( data );
    return TRUE;
}

void X11DRV_XDamage_Init(void)
{
    int event_base, error_base;
    void *xdamage_handle;

    xdamage_handle = dlopen(SONAME_LIBXDAMAGE, RTLD_NOW);
    if (!xdamage_handle)
    {
        ERR("Unable to open %s. XDamage is disabled.\n", SONAME_LIBXDAMAGE);
        return;
    }

#define LOAD_FUNCPTR(f)                             \
    if ((p##f = dlsym(xdamage_handle, #f)) == NULL) \
        goto failed;

    LOAD_FUNCPTR(XDamageCreate)
    LOAD_FUNCPTR(XDamageDestroy)
    LOAD_FUNCPTR(XDamageQueryExtension)
    LOAD_FUNCPTR(XDamageSubtract)

#undef LOAD_FUNCPTR

    if (!pXDamageQueryExtension(gdi_display, &event_base, &error_base))
    {
        ERR("XDamage extension could not be queried. XDamage is disabled.\n");
        goto failed;
    }
    TRACE("XDamage is up, event base %d, error_base %d.\n", event_base, error_base);
    X11DRV_register_event_handler(event_base + XDamageNotify, X11DRV_DamageNotify, "XDamageNotify");
    usexdamage = TRUE;
    return;

failed:
    ERR("Unable to load function pointers from %s. XDamage is disabled.\n", SONAME_LIBXDAMAGE);
    dlclose(xdamage_handle);
    xdamage_handle = NULL;
    usexdamage = FALSE;
}

#endif /* defined(SONAME_LIBXDAMAGE) */
