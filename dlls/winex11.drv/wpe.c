/*
 * Copyright 2026, enderice2
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

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#ifdef HAVE_LIBXSHAPE
#include <X11/extensions/shape.h>
#endif /* HAVE_LIBXSHAPE */
#ifdef HAVE_X11_EXTENSIONS_XINPUT2_H
#include <X11/extensions/XInput2.h>
#endif

/* avoid conflict with field names in included win32 headers */
#undef Status

#include "x11drv.h"

#include "wine/debug.h"
#include "wine/server.h"

WINE_DEFAULT_DEBUG_CHANNEL(x11drv);

static const WCHAR expected[] =
    {'W','P','E','D','e','s','k','t','o','p','D','X','1','1','W','i','n','d','o','w','\0'};
static const WCHAR wpe_host_title[] =
    {'W','E',' ','W','a','l','l','p','a','p','e','r',' ','H','o','s','t','\0'};

BOOL is_wpe_renderer(HWND hwnd)
{
    WCHAR class_name[128];
    UNICODE_STRING name = {0};
    INT length;

    name.Buffer = class_name;
    name.MaximumLength = sizeof(class_name);

    length = NtUserGetClassName(hwnd, FALSE, &name);

    return length == ARRAY_SIZE(expected) - 1 &&
           !memcmp(class_name, expected, sizeof(expected));
}

static void set_wpe_renderer_class(Display *display, Window window)
{
    static const char class_hint[] = "wpe-renderer\0wpe-renderer";
    Atom wm_class = XInternAtom(display, "WM_CLASS", False);

    XChangeProperty(display, window, wm_class, XA_STRING, 8,
                    PropModeReplace, (const unsigned char *)class_hint,
                    sizeof(class_hint));
}

static void set_wpe_renderer_type(Display *display, Window window)
{
    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XChangeProperty(display, window, type, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&desktop, 1);
}

static void set_wpe_desktop_type(Display *display, Window window)
{
    Atom property = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    XChangeProperty(display, window, property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&desktop, 1);
}

static void set_wpe_skip_taskbar(Display *display, Window window)
{
    Atom state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skip_pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom states[] = {skip_taskbar, skip_pager};

    XChangeProperty(display, window, state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states, ARRAY_SIZE(states));
}

void configure_wpe_renderer(Display *gdi_display, struct x11drv_win_data *data)
{
    XWMHints hints = {0};
    set_wpe_renderer_class(gdi_display, data->client_window);
    set_wpe_renderer_type(gdi_display, data->client_window);
    set_wpe_skip_taskbar(gdi_display, data->client_window);

    hints.flags = StateHint;
    hints.initial_state = IconicState;
    XSetWMHints(gdi_display, data->client_window, &hints);

    TRACE("renderer=%lx\n",  data->client_window);
}

void detect_wallpaper_host(Display *display, Window win, HWND hwnd, LPCWSTR text)
{
    if (lstrlenW(text) == ARRAY_SIZE(wpe_host_title) - 1 &&
        !memcmp(text, wpe_host_title, sizeof(wpe_host_title)))
    {
        set_wpe_desktop_type(display, win);

        TRACE("WPE host detected: hwnd=%p xwin=%lx\n", hwnd, win);
    }
}
