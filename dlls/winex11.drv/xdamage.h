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
#ifndef __WINE_XDAMAGE_H
#define __WINE_XDAMAGE_H

#ifndef __WINE_CONFIG_H
#error You must include config.h to use this header
#endif

#ifdef SONAME_LIBXDAMAGE

#include <X11/extensions/Xdamage.h>

#define MAKE_FUNCPTR(f) extern typeof(f) * p##f DECLSPEC_HIDDEN;

MAKE_FUNCPTR(XDamageCreate)
MAKE_FUNCPTR(XDamageDestroy)
MAKE_FUNCPTR(XDamageQueryExtension)
MAKE_FUNCPTR(XDamageSubtract)

#undef MAKE_FUNCPTR

#endif /* defined(SONAME_LIBXDAMAGE) */
#endif /* __WINE_XDAMAGE_H */
