/*
 * Copyright 2014 Austin English
 * Copyright 2020 Vijay Kiran Kamuju
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


#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "msasn1.h"

#include "wine/heap.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msasn1);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{

    switch (reason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer native version */
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            break;
    }

    return TRUE;
}

ASN1module_t WINAPI ASN1_CreateModule(ASN1uint32_t ver, ASN1encodingrule_e rule, ASN1uint32_t flags,
                                      ASN1uint32_t pdu, const ASN1GenericFun_t encoder[],
                                      const ASN1GenericFun_t decoder[], const ASN1FreeFun_t freemem[],
                                      const ASN1uint32_t size[], ASN1magic_t magic)
{
    ASN1module_t module = NULL;

    TRACE("(%08x %08x %08x %u %p %p %p %p %u)\n", ver, rule, flags, pdu, encoder, decoder, freemem, size, magic);

    if (!encoder || !decoder || !freemem || !size)
        return module;

    module = heap_alloc(sizeof(module));
    if (module)
    {
        module->nModuleName = magic;
        module->eRule = rule;
        module->dwFlags = flags;
        module->cPDUs = pdu;
        module->apfnFreeMemory = freemem;
        module->acbStructSize = size;

        if (rule & ASN1_PER_RULE)
        {
            module->PER.apfnEncoder = (ASN1PerEncFun_t *)encoder;
            module->PER.apfnDecoder = (ASN1PerDecFun_t *)decoder;
        }
        else if (rule & ASN1_BER_RULE)
        {
            module->BER.apfnEncoder = (ASN1BerEncFun_t *)encoder;
            module->BER.apfnDecoder = (ASN1BerDecFun_t *)decoder;
        }
        else
        {
            module->PER.apfnEncoder = NULL;
            module->PER.apfnDecoder = NULL;
        }
    }

    return module;
}

void WINAPI ASN1_CloseModule(ASN1module_t module)
{
    TRACE("(%p)\n", module);

    heap_free(module);
}

ASN1error_e WINAPI ASN1_CreateEncoder(ASN1module_t module, ASN1encoding_t *encoder, ASN1octet_t *buf,
                                      ASN1uint32_t bufsize, ASN1encoding_t parent)
{
    ASN1encoding_t enc;

    TRACE("(%p %p %p %u %p)\n", module, encoder, buf, bufsize, parent);

    if (!module || !encoder)
        return ASN1_ERR_BADARGS;

    enc = heap_alloc(sizeof(enc));
    if (!enc)
    {
        return ASN1_ERR_MEMORY;
    }

    if (parent)
      FIXME("parent not implemented.\n");

    enc->magic = 0x44434e45;
    enc->version = 0;
    enc->module = module;
    enc->buf = 0;
    enc->size = 0;
    enc->len = 0;
    enc->err = ASN1_SUCCESS;
    enc->bit = 0;
    enc->pos = 0;
    enc->cbExtraHeader = 0;
    enc->eRule = module->eRule;
    enc->dwFlags = module->dwFlags;

    if (buf && bufsize)
    {
        enc->buf = buf;
        enc->pos = buf;
        enc->size = bufsize;
        enc->dwFlags |= ASN1ENCODE_SETBUFFER;
    }

    *encoder = enc;

    return ASN1_SUCCESS;
}
