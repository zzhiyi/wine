/*
 * Server-side display device management
 *
 * Copyright (C) 2021 Zhiyi Zhang for CodeWeavers
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

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"

#include "request.h"
#include "user.h"

static struct list monitor_list = LIST_INIT(monitor_list);
static unsigned int monitor_count;

/* retrieve a pointer to a monitor from its handle */
static struct monitor *get_monitor( user_handle_t handle )
{
    struct monitor *monitor;

    if (!(monitor = get_user_object( handle, USER_MONITOR )))
        set_win32_error( ERROR_INVALID_MONITOR_HANDLE );
    return monitor;
}

/* create a monitor */
static void create_monitor( const struct update_monitor_entry *entry )
{
    struct monitor *monitor;

    if (!(monitor = mem_alloc( sizeof(*monitor) )))
        return;

    if (!(monitor->adapter_name = memdup( entry->adapter_name, entry->adapter_name_len )))
        goto failed;
    monitor->adapter_name_len = entry->adapter_name_len;

    if (!(monitor->handle = alloc_user_handle( monitor, USER_MONITOR )))
        goto failed;

    monitor->monitor_rect = entry->monitor_rect;
    monitor->work_rect = entry->work_rect;
    list_add_tail( &monitor_list, &monitor->entry );
    ++monitor_count;
    return;

failed:
    if (monitor->adapter_name)
        free( monitor->adapter_name );
    free( monitor );
}

/* modify the list of monitors */
DECL_HANDLER(update_monitors)
{
    const struct update_monitor_entry *entries;
    struct monitor *monitor, *monitor2;
    unsigned int entry_count, i;

    LIST_FOR_EACH_ENTRY_SAFE(monitor, monitor2, &monitor_list, struct monitor, entry)
    {
        list_remove( &monitor->entry );
        free_user_handle( monitor->handle );
        free( monitor->adapter_name );
        free( monitor );
    }
    monitor_count = 0;

    entries = get_req_data();
    entry_count = get_req_data_size() / sizeof(*entries);
    for (i = 0; i < entry_count; ++i)
        create_monitor( &entries[i] );
}

/* get information about a monitor */
DECL_HANDLER(get_monitor_info)
{
    struct monitor *monitor;

    if (!(monitor = get_monitor( req->handle )))
        return;

    reply->monitor_rect = monitor->monitor_rect;
    reply->work_rect = monitor->work_rect;
    set_reply_data( monitor->adapter_name, min(monitor->adapter_name_len, get_reply_max_size()) );
}

/* enumerate monitors */
DECL_HANDLER(enum_monitors)
{
    struct enum_monitor_entry *entries;
    unsigned int size, i = 0;
    struct monitor *monitor;

    reply->count = monitor_count;
    size = reply->count * sizeof(*entries);
    if (size > get_reply_max_size())
    {
        set_error( STATUS_BUFFER_TOO_SMALL );
        return;
    }

    if (!(entries = set_reply_data_size( size )))
        return;

    LIST_FOR_EACH_ENTRY(monitor, &monitor_list, struct monitor, entry)
    {
        entries[i].handle = monitor->handle;
        entries[i].monitor_rect = monitor->monitor_rect;
        ++i;
    }
}
