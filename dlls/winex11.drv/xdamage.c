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

#ifdef SONAME_LIBXDAMAGE

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(xdamage);

#define MAKE_FUNCPTR(f) typeof(f) * p##f;

MAKE_FUNCPTR(XDamageCreate)
MAKE_FUNCPTR(XDamageDestroy)
MAKE_FUNCPTR(XDamageQueryExtension)
MAKE_FUNCPTR(XDamageSubtract)

#undef MAKE_FUNCPTR

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
    usexdamage = TRUE;
    return;

failed:
    ERR("Unable to load function pointers from %s. XDamage is disabled.\n", SONAME_LIBXDAMAGE);
    dlclose(xdamage_handle);
    xdamage_handle = NULL;
    usexdamage = FALSE;
}

#endif /* defined(SONAME_LIBXDAMAGE) */
