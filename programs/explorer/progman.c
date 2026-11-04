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

#include <stdio.h>

#define COBJMACROS
#define OEMRESOURCE
#include <windows.h>
#include <winternl.h>
#include <rpc.h>
#include <shlobj.h>
#include <ntuser.h>
#include "exdisp.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

extern HWND WINAPI SetProgmanWindow(HWND hwnd);

HWND progman_window;
static HWND wallpaper_workerw_window;
static HWND workerw_window;
static HWND defview_window;
static HWND desktop_listview_window;

/* this should be removed */
static void dump_window(HWND hwnd)
{
    WCHAR class_name[128] = {0};
    RECT rect = {0};
    LONG_PTR style;
    LONG_PTR exstyle;
    HWND parent;
    HWND owner;
    HWND previous;
    HWND next;

    if (!IsWindow(hwnd))
        return;

    GetClassNameW(hwnd, class_name, ARRAY_SIZE(class_name));
    GetWindowRect(hwnd, &rect);

    style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    parent = GetParent(hwnd);
    owner = GetWindow(hwnd, GW_OWNER);
    previous = GetWindow(hwnd, GW_HWNDPREV);
    next = GetWindow(hwnd, GW_HWNDNEXT);

    TRACE("hwnd=%p, class=%s, parent=%p, owner=%p, prev=%p, next=%p, style=%08Ix, exstyle=%08Ix, rect=%ld,%ld %ldx%ld, visible=%u\n",
          hwnd,
          debugstr_w(class_name),
          parent,
          owner,
          previous,
          next,
          style,
          exstyle,
          rect.left,
          rect.top,
          rect.right - rect.left,
          rect.bottom - rect.top,
          IsWindowVisible(hwnd));
}

static HWND create_desktop_listview(HWND defview)
{
    INITCOMMONCONTROLSEX icex;
    DWORD style;
    HWND hwnd;
    int width, height;

    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES;

    if (!InitCommonControlsEx(&icex))
    {
        ERR("InitCommonControlsEx failed\n");
        return NULL;
    }

    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    style = WS_TABSTOP |
            WS_VISIBLE |
            WS_CHILDWINDOW |
            WS_CLIPSIBLINGS |
            WS_CLIPCHILDREN |
            LVS_SHAREIMAGELISTS |
            LVS_EDITLABELS |
            LVS_ALIGNLEFT |
            LVS_AUTOARRANGE |
            LVS_SHOWSELALWAYS |
            LVS_ICON;

    hwnd = CreateWindowExW(
        0,
        WC_LISTVIEWW,
        NULL,
        style,
        0, 0, width, height,
        defview,
        (HMENU)(ULONG_PTR)1,
        GetModuleHandleW(NULL),
        NULL
    );

    if (!hwnd)
    {
        ERR("failed to create SysListView32, error %lu\n", GetLastError());
        return NULL;
    }

    TRACE("SysListView32 created: %p\n", hwnd);

    return hwnd;
}

static HWND create_defview_window(HWND parent)
{
    static const WCHAR class_name[] = L"SHELLDLL_DefView";
    WNDCLASSW wc;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND hwnd;
    int width, height;

    memset(&wc, 0, sizeof(wc));

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd = CreateWindowExW(
        0,
        class_name,
        NULL,
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, width, height,
        parent,
        NULL,
        instance,
        NULL
    );

    if (!hwnd)
        return NULL;

    return hwnd;
}

static LRESULT WINAPI workerw_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND create_workerw_window(HWND progman)
{
    static const WCHAR class_name[] = L"WorkerW";
    WNDCLASSW wc;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND hwnd;
    int x, y, width, height;

    memset(&wc, 0, sizeof(wc));

    wc.lpfnWndProc = workerw_wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;

    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd = CreateWindowExW(
        0,
        class_name,
        NULL,
        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, width, height,
        NULL, NULL, instance, NULL
    );

    if (!hwnd)
        return NULL;

    if (!SetWindowPos(
        hwnd,
        progman,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER))
    {
        DestroyWindow(hwnd);
        return NULL;
    }

    return hwnd;
}

static LRESULT WINAPI progman_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case 0x052c:
    {
        switch (wp)
        {
            case 0x0000:
            {
                if (lp == 0)
                {
                    if (wallpaper_workerw_window)
                        DestroyWindow(wallpaper_workerw_window);
                    wallpaper_workerw_window = create_workerw_window(workerw_window);
                    SetWindowTextW(wallpaper_workerw_window, L"WE Wallpaper Host");
                    dump_window(wallpaper_workerw_window);
                    return 0;
                }
            }
            case 0x000D:
            {
                if (lp == 0 || lp == 1)
                {
                    FIXME("wp=%Ix lp=%Ix\n", wp, lp);
                    return 0;
                }
            }
            default:
                break;
        }
        FIXME("what we do here? wp=%Ix lp=%Ix\n", wp, lp);
        return 0;
    }
    default:
        if (msg >= WM_USER) /* not sure if it's correct */
            FIXME("unhandled Progman message %04x\n", msg);

        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

HWND create_progman_window(void)
{
    static const WCHAR class_name[] = L"Progman";
    WNDCLASSW wc;
    HINSTANCE instance = GetModuleHandleW(NULL);
    DWORD error;
    int x, y, width, height;

    memset(&wc, 0, sizeof(wc));

    wc.lpfnWndProc = progman_wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;

    if (!RegisterClassW(&wc))
    {
        error = GetLastError();

        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            ERR("failed to register Progman class, error %lu\n", error);
            return NULL;
        }
    }

    x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    progman_window = CreateWindowExW(
        0,
        class_name,
        L"Program Manager",
        WS_POPUP,
        x, y, width, height,
        NULL,
        NULL,
        instance,
        NULL
    );

    if (!progman_window)
    {
        ERR("failed to create Progman window, error %lu\n", GetLastError());
        return NULL;
    }

    if (SetProgmanWindow(progman_window) != progman_window)
    {
        ERR("SetProgmanWindow failed for %p\n", progman_window);
        DestroyWindow(progman_window);
        return NULL;
    }

    TRACE("created Progman window %p\n", progman_window);

    if (!workerw_window || !IsWindow(workerw_window))
    {
        workerw_window = create_workerw_window(progman_window);
        TRACE("WorkerW created: %p\n", workerw_window);
    }

    if (!defview_window || !IsWindow(defview_window))
    {
        defview_window = create_defview_window(workerw_window);
        TRACE("SHELLDLL_DefView created: %p\n", defview_window);
    }

    if (!desktop_listview_window || !IsWindow(desktop_listview_window))
    {
        desktop_listview_window = create_desktop_listview(defview_window);
    }

    if (!wallpaper_workerw_window || !IsWindow(wallpaper_workerw_window))
    {
        wallpaper_workerw_window = create_workerw_window(workerw_window);
        
        TRACE("Wallpaper WorkerW created: %p\n", wallpaper_workerw_window);
    }

    dump_window(progman_window);
    dump_window(workerw_window);
    dump_window(defview_window);
    dump_window(desktop_listview_window);
    dump_window(wallpaper_workerw_window);

    SetWindowTextW(progman_window, L"WE Progman");
    SetWindowTextW(workerw_window, L"WE Icons WorkerW");
    SetWindowTextW(defview_window, L"WE DefView");
    SetWindowTextW(desktop_listview_window, L"WE Desktop List View");
    SetWindowTextW(wallpaper_workerw_window, L"WE Wallpaper Host");

    return progman_window;
}
