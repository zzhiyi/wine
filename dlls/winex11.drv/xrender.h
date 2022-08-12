/*
 * Wine X11DRV XRender interface
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
#ifndef __WINE_XRENDER_H
#define __WINE_XRENDER_H

#ifndef __WINE_CONFIG_H
#error You must include config.h to use this header
#endif

#ifdef SONAME_LIBXRENDER

#include <X11/extensions/Xrender.h>

#define MAKE_FUNCPTR(f) extern typeof(f) * p##f DECLSPEC_HIDDEN;

MAKE_FUNCPTR(XRenderAddGlyphs)
MAKE_FUNCPTR(XRenderChangePicture)
MAKE_FUNCPTR(XRenderComposite)
MAKE_FUNCPTR(XRenderCompositeText16)
MAKE_FUNCPTR(XRenderCreateGlyphSet)
MAKE_FUNCPTR(XRenderCreatePicture)
MAKE_FUNCPTR(XRenderFillRectangle)
MAKE_FUNCPTR(XRenderFindFormat)
MAKE_FUNCPTR(XRenderFindVisualFormat)
MAKE_FUNCPTR(XRenderFreeGlyphSet)
MAKE_FUNCPTR(XRenderFreePicture)
MAKE_FUNCPTR(XRenderSetPictureClipRectangles)
#ifdef HAVE_XRENDERCREATELINEARGRADIENT
MAKE_FUNCPTR(XRenderCreateLinearGradient)
#endif
#ifdef HAVE_XRENDERSETPICTURETRANSFORM
MAKE_FUNCPTR(XRenderSetPictureTransform)
#endif
MAKE_FUNCPTR(XRenderQueryExtension)

#undef MAKE_FUNCPTR

#endif /* defined(SONAME_LIBXRENDER) */
#endif /* __WINE_XRENDER_H */
