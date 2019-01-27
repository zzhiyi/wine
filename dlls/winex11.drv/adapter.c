/*
 * Adapter enumeration
 *
 * Copyright 2018 Zhiyi Zhang for CodeWeavers
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

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "winuser.h"
#include "wine/debug.h"
#include "wine/heap.h"
#include "wine/unicode.h"
#include "wine/vulkan.h"
#include "wine/vulkan_driver.h"

#include "initguid.h"
#include "devguid.h"
#include "devpkey.h"
#include "setupapi.h"
#include "x11drv.h"

WINE_DEFAULT_DEBUG_CHANNEL(x11drv);

DEFINE_DEVPROPKEY(DEVPROPKEY_DISPLAY_ADAPTER_LUID, 0x60b193cb, 0x5276, 0x4d0f, 0x96, 0xfc, 0xf1, 0x73, 0xab, 0xad, 0x3e, 0xc6, 2);
/* Private DEVPROPKEY for associating an physical adapter to a vulkan adapter */
DEFINE_DEVPROPKEY(DEVPROPKEY_DISPLAY_ADAPTER_UUID, 0x233a9ef3, 0xafc4, 0x4abd, 0xb5, 0x64, 0xc3, 0x2f, 0x21, 0xf1, 0x53, 0x5b, 2);

struct adapter_desc
{
    uint32_t vendor_id;
    uint32_t device_id;
    GUID uuid;
    WCHAR *device_name;
};

static WCHAR *heap_strdupAtoW(const char *str)
{
    WCHAR *ret;
    INT len;

    len = MultiByteToWideChar(CP_ACP, 0, str, -1, 0, 0);
    ret = heap_alloc(len * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len);

    return ret;
}

static BOOL add_adapter_entry(const struct adapter_desc *adapter, uint32_t index)
{
    static const WCHAR instance_formatW[] = {'P', 'C', 'I',
                                             '\\', 'V', 'E', 'N', '_', '%', '0', '4', 'X',
                                             '&', 'D', 'E', 'V',  '_', '%', '0', '4', 'X',
                                             '&', 'S', 'U', 'B', 'S', 'Y', 'S',  '_', '0', '0', '0', '0', '0', '0', '0', '0',
                                             '&', 'R', 'E',  'V', '_', '0', '0',
                                             /* Fake instance id */
                                             '\\', '3',
                                             '&', '2', '6', '7', 'a', '6', '1', '6', 'a',
                                             '&', '0',
                                             '&', '%','0','2', 'u', 0};
    WCHAR instanceW[ARRAY_SIZE(instance_formatW)];
    HDEVINFO devinfo;
    SP_DEVINFO_DATA devinfo_data = {sizeof(SP_DEVINFO_DATA)};
    LUID luid;
    BOOL ret = FALSE;

    sprintfW(instanceW, instance_formatW, adapter->vendor_id, adapter->device_id, index);

    devinfo = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_DISPLAY, NULL);
    if (devinfo == INVALID_HANDLE_VALUE)
        goto fail;
    if (!SetupDiCreateDeviceInfoW(devinfo, instanceW, &GUID_DEVCLASS_DISPLAY, adapter->device_name, NULL, 0,
                                  &devinfo_data))
        goto fail;
    if (!AllocateLocallyUniqueId(&luid))
        goto fail;
    if (!SetupDiSetDevicePropertyW(devinfo, &devinfo_data, &DEVPROPKEY_DISPLAY_ADAPTER_LUID, DEVPROP_TYPE_UINT64,
                                   (const BYTE *)&luid, sizeof(luid), 0))
        goto fail;
    if (!SetupDiSetDevicePropertyW(devinfo, &devinfo_data, &DEVPROPKEY_DISPLAY_ADAPTER_UUID, DEVPROP_TYPE_UINT64,
                                   (const BYTE *)&adapter->uuid, sizeof(adapter->uuid), 0))
        goto fail;
    if (!SetupDiSetDevicePropertyW(devinfo, &devinfo_data, &DEVPKEY_NAME, DEVPROP_TYPE_STRING,
                                   (const BYTE *)adapter->device_name,
                                   (strlenW(adapter->device_name) + 1) * sizeof(WCHAR), 0))
        goto fail;
    if (!SetupDiRegisterDeviceInfo(devinfo, &devinfo_data, 0, NULL, NULL, NULL))
        goto fail;

    ret = TRUE;
fail:
    SetupDiDestroyDeviceInfoList(devinfo);
    return ret;
}

static BOOL init_vulkan_adapters(void)
{
    static const CHAR *extensions[] = {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    const struct vulkan_funcs *vk_funcs;
    PFN_vkEnumeratePhysicalDevices p_vkEnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties2KHR p_vkGetPhysicalDeviceProperties2KHR;
    VkInstanceCreateInfo create_info = {0};
    VkInstance instance;
    VkPhysicalDevice *devices = NULL;
    VkPhysicalDeviceProperties2 prop2;
    VkPhysicalDeviceIDProperties id;
    VkResult vr;
    struct adapter_desc desc;
    uint32_t i, count = 0;
    BOOL ret = FALSE;

    vk_funcs = get_vulkan_driver(WINE_VULKAN_DRIVER_VERSION);
    if (!vk_funcs)
        return FALSE;

    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.enabledExtensionCount = ARRAY_SIZE(extensions);
    create_info.ppEnabledExtensionNames = extensions;
    vr = vk_funcs->p_vkCreateInstance(&create_info, NULL, &instance);
    if (vr != VK_SUCCESS)
        return FALSE;

    p_vkEnumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)vk_funcs->p_vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    p_vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)vk_funcs->p_vkGetInstanceProcAddr(
        instance, "vkGetPhysicalDeviceProperties2KHR");

    vr = p_vkEnumeratePhysicalDevices(instance, &count, NULL);
    if (vr != VK_SUCCESS || count == 0)
        goto fail;

    devices = heap_alloc(count * sizeof(VkPhysicalDevice));
    if (!devices)
        goto fail;

    vr = p_vkEnumeratePhysicalDevices(instance, &count, devices);
    if (vr != VK_SUCCESS)
        goto fail;

    for (i = 0; i < count; i++)
    {
        prop2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        prop2.pNext = &id;

        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        id.pNext = NULL;

        p_vkGetPhysicalDeviceProperties2KHR(devices[i], &prop2);
        desc.vendor_id = prop2.properties.vendorID;
        desc.device_id = prop2.properties.deviceID;
        desc.uuid = *(GUID *)id.deviceUUID;
        desc.device_name = heap_strdupAtoW(prop2.properties.deviceName);
        ret = add_adapter_entry(&desc, i);
        heap_free(desc.device_name);
        if (!ret)
            goto fail;
    }

fail:
    heap_free(devices);
    vk_funcs->p_vkDestroyInstance(instance, NULL);
    return ret;
}

static void init_default_adpater(void)
{
    WCHAR device_name[] = {'W', 'i', 'n', 'e', ' ', 'X', '1', '1', ' ', 'D', 'r', 'i', 'v', 'e', 'r', 0};
    struct adapter_desc desc = {0, 0, {0}, device_name};
    if (!add_adapter_entry(&desc, 0))
        ERR("Failed to initialize the default adapter\n");
}

static void delete_all_adapters(void)
{
    HDEVINFO devinfo;
    SP_DEVINFO_DATA devinfo_data = {0};
    DWORD i = 0;
    BOOL result;

    devinfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, NULL, NULL, 0);
    if (devinfo == INVALID_HANDLE_VALUE)
        return;

    while (SetupDiEnumDeviceInfo(devinfo, i++, &devinfo_data))
    {
        result = SetupDiRemoveDevice(devinfo, &devinfo_data);
        if (!result)
            ERR("Failed to remove adapter %d\n", i);
    }

    SetupDiDestroyDeviceInfoList(devinfo);
}

void X11DRV_Adapter_Init(void)
{
    delete_all_adapters();
    if (!init_vulkan_adapters())
    {
        WARN("Failed to enumerate adapters via Vulkan, fallling back to using the default adapter\n");
        init_default_adpater();
    }
}