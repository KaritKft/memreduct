// Mem Reduct
// Copyright (c) 2011-2015 Henry++

#include <windows.h>
#include <algorithm>

#include "main.h"
#include "application.h"
#include "routine.h"

#include "resource.h"

NOTIFYICONDATA nid = {0};
STATIC_DATA data = {0};

CONST UINT WM_TASKBARCREATED = RegisterWindowMessage (L"TaskbarCreated");

CApplication app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_AUTHOR);

std::vector<UINT> limit_vec;
std::vector<UINT> interval_vec;

std::vector<UINT> generate_menu_array (UINT val, UINT multiplier)
{
	std::vector<UINT> result;

	for (UINT i = 1; i < 10; i++)
	{
		result.push_back (i * 10);
	}

	for (UINT i = val - 2; i <= (val + 2); i++)
	{
		if (i >= 5)
		{
			result.push_back (i);
		}
	}

	std::sort (result.begin (), result.end ()); // sort
	result.erase (std::unique (result.begin (), result.end ()), result.end ()); // remove duplicates

	return result;
}

VOID BresenhamCircle (HDC dc, LONG radius, LPPOINT pt, COLORREF clr)
{
	LONG cx = 0, cy = radius, d = 2 - 2 * radius;

	// let's start drawing the circle:
	SetPixel (dc, cx + pt->x, cy + pt->y, clr); // point (0, R);
	SetPixel (dc, cx + pt->x, -cy + pt->y, clr); // point (0, -R);
	SetPixel (dc, cy + pt->x, cx + pt->y, clr); // point (R, 0);
	SetPixel (dc, -cy + pt->x, cx + pt->y, clr); // point (-R, 0);

	while (1)
	{
		if (d > -cy)
		{
			--cy;
			d += 1 - 2 * cy;
		}

		if (d <= cx)
		{
			++cx;
			d += 1 + 2 * cx;
		}

		if (!cy) break; // cy is 0, but these points are already drawn;

		// the actual drawing:
		SetPixel (dc, cx + pt->x, cy + pt->y, clr); // 0-90 degrees
		SetPixel (dc, -cx + pt->x, cy + pt->y, clr); // 90-180 degrees
		SetPixel (dc, -cx + pt->x, -cy + pt->y, clr); // 180-270 degrees
		SetPixel (dc, cx + pt->x, -cy + pt->y, clr); // 270-360 degrees
	}
}

VOID BresenhamLine (HDC dc, INT x0, INT y0, INT x1, INT y1, COLORREF clr)
{
	INT dx = abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
	INT dy = abs (y1 - y0), sy = y0 < y1 ? 1 : -1;
	INT err = (dx > dy ? dx : -dy) / 2, e2;

	while (1)
	{
		SetPixel (dc, x0, y0, clr);

		if (x0 == x1 && y0 == y1) break;

		e2 = err;

		if (e2 > -dx)
		{
			err -= dy; x0 += sx;
		}
		if (e2 < dy)
		{
			err += dx; y0 += sy;
		}
	}
}

DWORD _Memory_GetStatus (MEMORYINFO* m)
{
	MEMORYSTATUSEX msex = {0};
	SYSTEM_CACHE_INFORMATION sci = {0};

	msex.dwLength = sizeof (msex);

	if (GlobalMemoryStatusEx (&msex) && m) // WARNING!!! don't touch "m"!
	{
		m->percent_phys = msex.dwMemoryLoad;

		m->free_phys = msex.ullAvailPhys;
		m->total_phys = msex.ullTotalPhys;

		m->percent_page = (DWORD)ROUTINE_PERCENT_OF (msex.ullTotalPageFile - msex.ullAvailPageFile, msex.ullTotalPageFile);

		m->free_page = msex.ullAvailPageFile;
		m->total_page = msex.ullTotalPageFile;
	}

	if (m && NtQuerySystemInformation (SystemFileCacheInformation, &sci, sizeof (sci), nullptr) >= 0)
	{
		m->percent_ws = (DWORD)ROUTINE_PERCENT_OF (sci.CurrentSize, sci.PeakSize);

		m->free_ws = (sci.PeakSize - sci.CurrentSize);
		m->total_ws = sci.PeakSize;
	}

	return msex.dwMemoryLoad;
}

DWORD _Memory_Clean (HWND hwnd, DWORD mask)
{
	SYSTEM_MEMORY_LIST_COMMAND smlc;

	if (!data.is_admin || (hwnd && _r_msg (app.GetHWND (), MB_YESNO | MB_ICONQUESTION, APP_NAME, I18N (&app, IDS_QUESTION, 0)) == IDNO))
	{
		return FALSE;
	}

	data.reduct_before = _Memory_GetStatus (nullptr);

	// Working set
	if (data.is_supported_os && (mask & REDUCT_WORKING_SET) != 0)
	{
		smlc = MemoryEmptyWorkingSets;
		NtSetSystemInformation (SystemMemoryListInformation, &smlc, sizeof (smlc));
	}

	// System working set
	if ((mask & REDUCT_SYSTEM_WORKING_SET) != 0)
	{
		SYSTEM_CACHE_INFORMATION sci = {0};

		sci.MinimumWorkingSet = (ULONG)-1;
		sci.MaximumWorkingSet = (ULONG)-1;

		NtSetSystemInformation (SystemFileCacheInformation, &sci, sizeof (sci));
	}

	// Standby priority-0 list
	if (data.is_supported_os && (mask & REDUCT_STANDBY_PRIORITY0_LIST) != 0)
	{
		smlc = MemoryPurgeLowPriorityStandbyList;
		NtSetSystemInformation (SystemMemoryListInformation, &smlc, sizeof (smlc));
	}

	// Standby list
	if (data.is_supported_os && (mask & REDUCT_STANDBY_LIST) != 0)
	{
		smlc = MemoryPurgeStandbyList;
		NtSetSystemInformation (SystemMemoryListInformation, &smlc, sizeof (smlc));
	}

	// Modified list
	if (data.is_supported_os && (mask & REDUCT_MODIFIED_LIST) != 0)
	{
		smlc = MemoryFlushModifiedList;
		NtSetSystemInformation (SystemMemoryListInformation, &smlc, sizeof (smlc));
	}

	data.reduct_after = _Memory_GetStatus (nullptr);

	// Statistic
	data.statistic_last_reduct = _r_unixtime_now ();
	app.ConfigSet (L"StatisticLastReduct", (DWORD)data.statistic_last_reduct); // time of last cleaning

	// Show result
	if (data.reduct_before > data.reduct_after)
	{
		_r_tray_balloontip (&nid, NIIF_INFO, APP_NAME, _r_fmt (I18N (&app, IDS_STATUS_CLEANED, 0), _r_fmt_size64 ((DWORDLONG)ROUTINE_PERCENT_VAL (data.reduct_before - data.reduct_after, data.ms.total_phys))));
	}

	return data.reduct_after;
}

HICON _Application_DrawIcon ()
{
	COLORREF color = app.ConfigGet (L"TrayColorText", TRAY_COLOR_TEXT);
	HBRUSH bg_brush = data.bg_brush;
	BOOL is_transparent = app.ConfigGet (L"TrayUseTransparency", 0);
	BOOL is_round = app.ConfigGet (L"TrayRoundCorners", 0);

	BOOL has_danger = data.ms.percent_phys >= app.ConfigGet (L"TrayLevelDanger", 90);
	BOOL has_warning = has_danger || data.ms.percent_phys >= app.ConfigGet (L"TrayLevelWarning", 60);

	if (has_danger || has_warning)
	{
		if (app.ConfigGet (L"TrayChangeBg", 1))
		{
			bg_brush = has_danger ? data.bg_brush_danger : data.bg_brush_warning;
			is_transparent = FALSE;
		}
		else
		{
			if (has_danger)
			{
				color = app.ConfigGet (L"TrayColorDanger", TRAY_COLOR_DANGER);
			}
			else
			{
				color = app.ConfigGet (L"TrayColorWarning", TRAY_COLOR_WARNING);
			}
		}
	}

	// select bitmap
	HBITMAP old_bitmap = (HBITMAP)SelectObject (data.cdc1, data.bitmap);

	// draw transparent mask
	COLORREF clr_prev = SetBkColor (data.cdc1, TRAY_COLOR_MASK);
	ExtTextOut (data.cdc1, 0, 0, ETO_OPAQUE, &data.rc, nullptr, 0, nullptr);
	SetBkColor (data.cdc1, clr_prev);

	// draw background
	if (!is_transparent)
	{
		HGDIOBJ prev_pen = SelectObject (data.cdc1, GetStockObject (NULL_PEN));
		HGDIOBJ prev_brush = SelectObject (data.cdc1, bg_brush);

		RoundRect (data.cdc1, 0, 0, data.rc.right, data.rc.bottom, is_round ? (data.rc.right / 2) : 0, is_round ? (data.rc.right / 2) : 0);

		SelectObject (data.cdc1, prev_pen);
		SelectObject (data.cdc1, prev_brush);
	}

	// draw border
	if (app.ConfigGet (L"TrayShowBorder", 0))
	{
		if (is_round)
		{
			POINT pt = {0};

			pt.x = ((data.rc.left + data.rc.right) / 2) - 1;
			pt.y = ((data.rc.top + data.rc.bottom) / 2) - 1;

			for (LONG i = 1; i < data.scale + 1; i++)
			{
				BresenhamCircle (data.cdc1, ((data.rc.left + data.rc.right) / 2) - (i), &pt, color);
			}
		}
		else
		{
			for (LONG i = 0; i < data.scale; i++)
			{
				BresenhamLine (data.cdc1, i, 0, i, data.rc.bottom, color); // left
				BresenhamLine (data.cdc1, i, i, data.rc.right, i, color); // top
				BresenhamLine (data.cdc1, (data.rc.right - 1) - i, 0, (data.rc.right - 1) - i, data.rc.bottom, color); // right
				BresenhamLine (data.cdc1, 0, (data.rc.bottom - 1) - i, data.rc.right, (data.rc.bottom - 1) - i, color); // bottom
			}
		}
	}

	// draw text
	SetTextColor (data.cdc1, color);
	SetBkMode (data.cdc1, TRANSPARENT);

	CString buffer = _r_fmt (L"%d", data.ms.percent_phys);

	SelectObject (data.cdc1, data.font);
	DrawTextEx (data.cdc1, buffer.GetBuffer (), buffer.GetLength (), &data.rc, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOCLIP, nullptr);

	// draw transparent mask
	HGDIOBJ old_mask = SelectObject (data.cdc2, data.bitmap_mask);

	SetBkColor (data.cdc1, TRAY_COLOR_MASK);
	BitBlt (data.cdc2, 0, 0, data.rc.right, data.rc.bottom, data.cdc1, 0, 0, SRCCOPY);

	SelectObject (data.cdc2, old_mask);
	SelectObject (data.cdc1, old_bitmap);

	// finalize icon
	ICONINFO ii = {0};

	ii.fIcon = TRUE;
	ii.hbmColor = data.bitmap;
	ii.hbmMask = data.bitmap_mask;

	return CreateIconIndirect (&ii);
}

VOID CALLBACK _Memory_TimerCallback (HWND hwnd, UINT, UINT_PTR, DWORD)
{
	_Memory_GetStatus (&data.ms);

	if (data.ms_prev != data.ms.percent_phys)
	{
		// destroy previous icon
		if (nid.hIcon)
		{
			DestroyIcon (nid.hIcon);
		}

		// create new icon
		nid.uFlags = NIF_SHOWTIP | NIF_TIP | NIF_ICON;
		nid.hIcon = _Application_DrawIcon ();

		StringCchPrintf (nid.szTip, _countof (nid.szTip), I18N (&app, IDS_TOOLTIP, 0), data.ms.percent_phys, data.ms.percent_page, data.ms.percent_ws);
		Shell_NotifyIcon (NIM_MODIFY, &nid);

		// autoreduct
		if (data.is_admin)
		{
			if ((app.ConfigGet (L"AutoreductEnable", 1) && data.ms.percent_phys >= app.ConfigGet (L"AutoreductValue", 90)) ||
				(app.ConfigGet (L"AutoreductIntervalEnable", 0) && ((_r_unixtime_now () - data.statistic_last_reduct) >= app.ConfigGet (L"AutoreductIntervalValue", 30) * 60)))
			{
				_Memory_Clean (nullptr, data.reduct_mask);
			}
		}

		data.ms_prev = data.ms.percent_phys; // store last percentage value (required!)
	}

	if (IsWindowVisible (hwnd))
	{
		// Physical memory
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt (L"%d%%", data.ms.percent_phys), 0, 1, -1, -1, data.ms.percent_phys);
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt_size64 (data.ms.free_phys), 1, 1, -1, -1, data.ms.percent_phys);
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt_size64 (data.ms.total_phys), 2, 1, -1, -1, data.ms.percent_phys);

		// Page file
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt (L"%d%%", data.ms.percent_page), 3, 1, -1, -1, data.ms.percent_page);
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt_size64 (data.ms.free_page), 4, 1, -1, -1, data.ms.percent_page);
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt_size64 (data.ms.total_page), 5, 1, -1, -1, data.ms.percent_page);

		// System working set
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt (L"%d%%", data.ms.percent_ws), 6, 1, -1, -1, data.ms.percent_ws);
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt_size64 (data.ms.free_ws), 7, 1, -1, -1, data.ms.percent_ws);
		_r_listview_additem (hwnd, IDC_LISTVIEW, _r_fmt_size64 (data.ms.total_ws), 8, 1, -1, -1, data.ms.percent_ws);

		SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_REDRAWITEMS, 0, 8); // redraw (required!)
	}
}

VOID _Application_Unitialize (HWND hwnd, BOOL deletetrayicon)
{
	if (nid.hIcon)
	{
		DestroyIcon (nid.hIcon);
	}

	if (deletetrayicon)
	{
		Shell_NotifyIcon (NIM_DELETE, &nid);
	}

	UnregisterHotKey (hwnd, UID);
	KillTimer (hwnd, UID);

	DeleteObject (data.font);

	DeleteObject (data.bg_brush);
	DeleteObject (data.bg_brush_warning);
	DeleteObject (data.bg_brush_danger);

	DeleteObject (data.bitmap);
	DeleteObject (data.bitmap_mask);

	DeleteDC (data.cdc1);
	DeleteDC (data.cdc2);
	DeleteDC (data.dc);
}

VOID _Application_Initialize (HWND hwnd, BOOL createtrayicon)
{
	_Application_Unitialize (hwnd, createtrayicon);

	data.ms_prev = 0;

	data.scale = app.ConfigGet (L"TrayUseAntialiasing", 0) ? 16 : 1;

	// init configuration
	data.statistic_last_reduct = app.ConfigGet (L"StatisticLastReduct", 0);

	data.reduct_mask = app.ConfigGet (L"ReductMask", MASK_DEFAULT);

	data.rc.right = GetSystemMetrics (SM_CXSMICON) * data.scale;
	data.rc.bottom = GetSystemMetrics (SM_CYSMICON) * data.scale;

	// init device context
	data.dc = GetDC (nullptr);

	data.cdc1 = CreateCompatibleDC (data.dc);
	data.cdc2 = CreateCompatibleDC (data.dc);

	ReleaseDC (nullptr, data.dc);

	// init bitmap
	BITMAPINFO bmi = {0};

	bmi.bmiHeader.biSize = sizeof (bmi.bmiHeader);
	bmi.bmiHeader.biWidth = data.rc.right;
	bmi.bmiHeader.biHeight = data.rc.bottom;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	data.bitmap = CreateDIBSection (data.dc, &bmi, DIB_RGB_COLORS, 0, nullptr, 0);
	data.bitmap_mask = CreateBitmap (data.rc.right, data.rc.bottom, 1, 1, nullptr);

	// init brush
	data.bg_brush = CreateSolidBrush (app.ConfigGet (L"TrayColorBg", TRAY_COLOR_BG));
	data.bg_brush_warning = CreateSolidBrush (app.ConfigGet (L"TrayColorWarning", TRAY_COLOR_WARNING));
	data.bg_brush_danger = CreateSolidBrush (app.ConfigGet (L"TrayColorDanger", TRAY_COLOR_DANGER));

	// init font
	SecureZeroMemory (&data.lf, sizeof (data.lf));

	data.lf.lfQuality = NONANTIALIASED_QUALITY;
	data.lf.lfCharSet = DEFAULT_CHARSET;
	data.lf.lfPitchAndFamily = FF_DONTCARE;
	data.lf.lfWeight = app.ConfigGet (L"TrayFontWeight", FW_NORMAL);
	data.lf.lfHeight = -MulDiv (app.ConfigGet (L"TrayFontSize", FONT_SIZE), GetDeviceCaps (data.cdc1, LOGPIXELSY), 72) * data.scale;

	StringCchCopy (data.lf.lfFaceName, LF_FACESIZE, app.ConfigGet (L"TrayFontName", FONT_NAME));

	data.font = CreateFontIndirect (&data.lf);

	// init hotkey
	UINT hk = app.ConfigGet (L"HotkeyClean", MAKEWORD (VK_F1, HOTKEYF_CONTROL));

	if (data.is_admin && hk && app.ConfigGet (L"HotkeyCleanEnable", 1))
	{
		RegisterHotKey (hwnd, UID, (HIBYTE (hk) & 2) | ((HIBYTE (hk) & 4) >> 2) | ((HIBYTE (hk) & 1) << 2), LOBYTE (hk));
	}

	// init tray icon
	if (createtrayicon)
	{
		nid.cbSize = data.is_supported_os ? sizeof (nid) : NOTIFYICONDATA_V3_SIZE;
		nid.uVersion = data.is_supported_os ? NOTIFYICON_VERSION_4 : NOTIFYICON_VERSION;
		nid.hWnd = hwnd;
		nid.uID = UID;
		nid.uFlags = NIF_MESSAGE | NIF_ICON;
		nid.uCallbackMessage = WM_TRAYICON;
		nid.hIcon = _Application_DrawIcon ();

		Shell_NotifyIcon (NIM_ADD, &nid);
		Shell_NotifyIcon (NIM_SETVERSION, &nid);
	}

	// init timer
	SetTimer (hwnd, UID, TIMER, _Memory_TimerCallback);
	_Memory_TimerCallback (hwnd, 0, 0, 0);
}

INT_PTR WINAPI PagesDlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			// localize
			{
				SetDlgItemText (hwnd, IDC_TITLE_1, I18N (&app, IDS_TITLE_1, 0));
				SetDlgItemText (hwnd, IDC_TITLE_2, I18N (&app, IDS_TITLE_2, 0));
				SetDlgItemText (hwnd, IDC_TITLE_3, I18N (&app, IDS_TITLE_3, 0));
				SetDlgItemText (hwnd, IDC_TITLE_4, I18N (&app, IDS_TITLE_4, 0));
				SetDlgItemText (hwnd, IDC_TITLE_5, I18N (&app, IDS_TITLE_5, 0));
				SetDlgItemText (hwnd, IDC_TITLE_6, I18N (&app, IDS_TITLE_6, 0));
				SetDlgItemText (hwnd, IDC_TITLE_7, I18N (&app, IDS_TITLE_7, 0));
				SetDlgItemText (hwnd, IDC_TITLE_8, I18N (&app, IDS_TITLE_8, 0));

				SetDlgItemText (hwnd, IDC_ALWAYSONTOP_CHK, I18N (&app, IDS_ALWAYSONTOP_CHK, 0));
				SetDlgItemText (hwnd, IDC_LOADONSTARTUP_CHK, I18N (&app, IDS_LOADONSTARTUP_CHK, 0));
				SetDlgItemText (hwnd, IDC_STARTMINIMIZED_CHK, I18N (&app, IDS_STARTMINIMIZED_CHK, 0));
				SetDlgItemText (hwnd, IDC_SKIPUACWARNING_CHK, I18N (&app, IDS_SKIPUACWARNING_CHK, 0));
				SetDlgItemText (hwnd, IDC_CHECKUPDATES_CHK, I18N (&app, IDS_CHECKUPDATES_CHK, 0));

				SetDlgItemText (hwnd, IDC_LANGUAGE_HINT, I18N (&app, IDS_LANGUAGE_HINT, 0));

				SetDlgItemText (hwnd, IDC_WORKINGSET_CHK, I18N (&app, IDS_WORKINGSET_CHK, 0));
				SetDlgItemText (hwnd, IDC_SYSTEMWORKINGSET_CHK, I18N (&app, IDS_SYSTEMWORKINGSET_CHK, 0));
				SetDlgItemText (hwnd, IDC_STANDBYLISTPRIORITY0_CHK, I18N (&app, IDS_STANDBYLISTPRIORITY0_CHK, 0));
				SetDlgItemText (hwnd, IDC_STANDBYLIST_CHK, I18N (&app, IDS_STANDBYLIST_CHK, 0));
				SetDlgItemText (hwnd, IDC_MODIFIEDLIST_CHK, I18N (&app, IDS_MODIFIEDLIST_CHK, 0));

				SetDlgItemText (hwnd, IDC_AUTOREDUCTENABLE_CHK, I18N (&app, IDS_AUTOREDUCTENABLE_CHK, 0));
				SetDlgItemText (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, I18N (&app, IDS_AUTOREDUCTINTERVALENABLE_CHK, 0));

				SetDlgItemText (hwnd, IDC_HOTKEY_CLEAN_CHK, I18N (&app, IDS_HOTKEY_CLEAN_CHK, 0));

				SetDlgItemText (hwnd, IDC_TRAYUSETRANSPARENCY_CHK, I18N (&app, IDS_TRAYUSETRANSPARENCY_CHK, 0));
				SetDlgItemText (hwnd, IDC_TRAYSHOWBORDER_CHK, I18N (&app, IDS_TRAYSHOWBORDER_CHK, 0));
				SetDlgItemText (hwnd, IDC_TRAYROUNDCORNERS_CHK, I18N (&app, IDS_TRAYROUNDCORNERS_CHK, 0));
				SetDlgItemText (hwnd, IDC_TRAYCHANGEBG_CHK, I18N (&app, IDS_TRAYCHANGEBG_CHK, 0));
				SetDlgItemText (hwnd, IDC_TRAYUSEANTIALIASING_CHK, I18N (&app, IDS_TRAYUSEANTIALIASING_CHK, 0));

				SetDlgItemText (hwnd, IDC_FONT_HINT, I18N (&app, IDS_FONT_HINT, 0));
				SetDlgItemText (hwnd, IDC_COLOR_TEXT_HINT, I18N (&app, IDS_COLOR_TEXT_HINT, 0));
				SetDlgItemText (hwnd, IDC_COLOR_BACKGROUND_HINT, I18N (&app, IDS_COLOR_BACKGROUND_HINT, 0));
				SetDlgItemText (hwnd, IDC_COLOR_WARNING_HINT, I18N (&app, IDS_COLOR_WARNING_HINT, 0));
				SetDlgItemText (hwnd, IDC_COLOR_DANGER_HINT, I18N (&app, IDS_COLOR_DANGER_HINT, 0));

				SetDlgItemText (hwnd, IDC_TRAYLEVELWARNING_HINT, I18N (&app, IDS_TRAYLEVELWARNING_HINT, 0));
				SetDlgItemText (hwnd, IDC_TRAYLEVELDANGER_HINT, I18N (&app, IDS_TRAYLEVELDANGER_HINT, 0));

				SetDlgItemText (hwnd, IDC_TRAYACTIONDC_HINT, I18N (&app, IDS_TRAYACTIONDC_HINT, 0));
				SetDlgItemText (hwnd, IDC_TRAYACTIONMC_HINT, I18N (&app, IDS_TRAYACTIONMC_HINT, 0));
			}

			if (lparam == 0)
			{
				if (!data.is_supported_os || !data.is_admin)
				{
					EnableWindow (GetDlgItem (hwnd, IDC_SKIPUACWARNING_CHK), FALSE);
				}

				CheckDlgButton (hwnd, IDC_ALWAYSONTOP_CHK, app.ConfigGet (L"AlwaysOnTop", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_LOADONSTARTUP_CHK, app.AutorunIsPresent () ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_STARTMINIMIZED_CHK, app.ConfigGet (L"StartMinimized", 1) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_SKIPUACWARNING_CHK, app.SkipUacIsPresent (FALSE) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_CHECKUPDATES_CHK, app.ConfigGet (L"CheckUpdates", 1) ? BST_CHECKED : BST_UNCHECKED);

				SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_INSERTSTRING, 0, (LPARAM)L"English (default)");
				SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_SETCURSEL, 0, NULL);

				app.LocaleEnum (hwnd, IDC_LANGUAGE);

				SetProp (app.GetHWND (), L"language", (HANDLE)SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, NULL)); // check on save
			}
			else if (lparam == 1)
			{
				if (!data.is_supported_os || !data.is_admin)
				{
					EnableWindow (GetDlgItem (hwnd, IDC_WORKINGSET_CHK), FALSE);
					EnableWindow (GetDlgItem (hwnd, IDC_STANDBYLISTPRIORITY0_CHK), FALSE);
					EnableWindow (GetDlgItem (hwnd, IDC_STANDBYLIST_CHK), FALSE);
					EnableWindow (GetDlgItem (hwnd, IDC_MODIFIEDLIST_CHK), FALSE);

					if (!data.is_admin)
					{
						EnableWindow (GetDlgItem (hwnd, IDC_SYSTEMWORKINGSET_CHK), FALSE);
						EnableWindow (GetDlgItem (hwnd, IDC_AUTOREDUCTENABLE_CHK), FALSE);
						EnableWindow (GetDlgItem (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK), FALSE);
						EnableWindow (GetDlgItem (hwnd, IDC_HOTKEY_CLEAN_CHK), FALSE);
					}
				}

				CheckDlgButton (hwnd, IDC_WORKINGSET_CHK, ((data.reduct_mask & REDUCT_WORKING_SET) != 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_SYSTEMWORKINGSET_CHK, ((data.reduct_mask & REDUCT_SYSTEM_WORKING_SET) != 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_STANDBYLISTPRIORITY0_CHK, ((data.reduct_mask & REDUCT_STANDBY_PRIORITY0_LIST) != 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_STANDBYLIST_CHK, ((data.reduct_mask & REDUCT_STANDBY_LIST) != 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_MODIFIEDLIST_CHK, ((data.reduct_mask & REDUCT_MODIFIED_LIST) != 0) ? BST_CHECKED : BST_UNCHECKED);

				CheckDlgButton (hwnd, IDC_AUTOREDUCTENABLE_CHK, app.ConfigGet (L"AutoreductEnable", 1) ? BST_CHECKED : BST_UNCHECKED);

				SendDlgItemMessage (hwnd, IDC_AUTOREDUCTVALUE, UDM_SETRANGE32, 10, 99);
				SendDlgItemMessage (hwnd, IDC_AUTOREDUCTVALUE, UDM_SETPOS32, 0, app.ConfigGet (L"AutoreductValue", 90));

				CheckDlgButton (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK, app.ConfigGet (L"AutoreductIntervalEnable", 0) ? BST_CHECKED : BST_UNCHECKED);

				SendDlgItemMessage (hwnd, IDC_AUTOREDUCTINTERVALVALUE, UDM_SETRANGE32, 5, 1440);
				SendDlgItemMessage (hwnd, IDC_AUTOREDUCTINTERVALVALUE, UDM_SETPOS32, 0, app.ConfigGet (L"AutoreductIntervalValue", 30));

				CheckDlgButton (hwnd, IDC_HOTKEY_CLEAN_CHK, app.ConfigGet (L"HotkeyEnable", 1) ? BST_CHECKED : BST_UNCHECKED);

				SendDlgItemMessage (hwnd, IDC_HOTKEY_CLEAN, HKM_SETHOTKEY, app.ConfigGet (L"Hotkey", MAKEWORD (VK_F1, HOTKEYF_CONTROL)), NULL);

				SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_AUTOREDUCTENABLE_CHK, 0), NULL);
				SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_AUTOREDUCTINTERVALENABLE_CHK, 0), NULL);
				SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_HOTKEY_CLEAN_CHK, 0), NULL);
			}
			else if (lparam == 2)
			{
				CheckDlgButton (hwnd, IDC_TRAYUSETRANSPARENCY_CHK, app.ConfigGet (L"TrayUseTransparency", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_TRAYSHOWBORDER_CHK, app.ConfigGet (L"TrayShowBorder", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_TRAYROUNDCORNERS_CHK, app.ConfigGet (L"TrayRoundCorners", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_TRAYCHANGEBG_CHK, app.ConfigGet (L"TrayChangeBg", 1) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton (hwnd, IDC_TRAYUSEANTIALIASING_CHK, app.ConfigGet (L"TrayUseAntialiasing", 0) ? BST_CHECKED : BST_UNCHECKED);

				SetProp (GetDlgItem (hwnd, IDC_COLOR_TEXT), L"color", (HANDLE)app.ConfigGet (L"TrayColorText", TRAY_COLOR_TEXT));
				SetProp (GetDlgItem (hwnd, IDC_COLOR_BACKGROUND), L"color", (HANDLE)app.ConfigGet (L"TrayColorBg", TRAY_COLOR_BG));
				SetProp (GetDlgItem (hwnd, IDC_COLOR_WARNING), L"color", (HANDLE)app.ConfigGet (L"TrayColorWarning", TRAY_COLOR_WARNING));
				SetProp (GetDlgItem (hwnd, IDC_COLOR_DANGER), L"color", (HANDLE)app.ConfigGet (L"TrayColorDanger", TRAY_COLOR_DANGER));

				SetDlgItemText (hwnd, IDC_FONT, _r_fmt (L"%s, %dpx", app.ConfigGet (L"TrayFontName", FONT_NAME), app.ConfigGet (L"TrayFontSize", FONT_SIZE)));
			}
			else if (lparam == 3)
			{
				SendDlgItemMessage (hwnd, IDC_TRAYLEVELWARNING, UDM_SETRANGE32, 10, 99);
				SendDlgItemMessage (hwnd, IDC_TRAYLEVELWARNING, UDM_SETPOS32, 0, app.ConfigGet (L"TrayLevelWarning", 60));

				SendDlgItemMessage (hwnd, IDC_TRAYLEVELDANGER, UDM_SETRANGE32, 10, 99);
				SendDlgItemMessage (hwnd, IDC_TRAYLEVELDANGER, UDM_SETPOS32, 0, app.ConfigGet (L"TrayLevelDanger", 90));

				for (INT i = 0; i < 3; i++)
				{
					CString item = I18N (&app, IDS_TRAY_ACTION_1 + i, _r_fmt (L"IDS_TRAY_ACTION_%d", i + 1));

					SendDlgItemMessage (hwnd, IDC_TRAYACTIONMC, CB_INSERTSTRING, i, (LPARAM)item.GetString ());
					SendDlgItemMessage (hwnd, IDC_TRAYACTIONDC, CB_INSERTSTRING, i, (LPARAM)item.GetString ());
				}

				SendDlgItemMessage (hwnd, IDC_TRAYACTIONDC, CB_SETCURSEL, app.ConfigGet (L"TrayActionDc", 0), NULL);
				SendDlgItemMessage (hwnd, IDC_TRAYACTIONMC, CB_SETCURSEL, app.ConfigGet (L"TrayActionMc", 1), NULL);
			}

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			//if (nmlp->idFrom == IDC_COLORS)
			{
				switch (nmlp->code)
				{
					case NM_CUSTOMDRAW:
					{
						LPNMCUSTOMDRAW lpnmcd = (LPNMCUSTOMDRAW)lparam;

						if (nmlp->idFrom >= IDC_COLOR_TEXT && nmlp->idFrom <= IDC_COLOR_DANGER)
						{
							lpnmcd->rc.left += 3;
							lpnmcd->rc.top += 3;
							lpnmcd->rc.right -= 3;
							lpnmcd->rc.bottom -= 3;

							COLORREF clr_prev = SetBkColor (lpnmcd->hdc, (COLORREF)GetProp (nmlp->hwndFrom, L"color"));
							ExtTextOut (lpnmcd->hdc, 0, 0, ETO_OPAQUE, &lpnmcd->rc, nullptr, 0, nullptr);
							SetBkColor (lpnmcd->hdc, clr_prev);

							return CDRF_DODEFAULT | CDRF_DOERASE;
						}

						break;
					}
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch (LOWORD (wparam))
			{
				case IDC_AUTOREDUCTENABLE_CHK:
				case IDC_AUTOREDUCTINTERVALENABLE_CHK:
				{
					UINT ctrl1 = LOWORD (wparam);
					UINT ctrl2 = LOWORD (wparam) + 1;

					BOOL is_enabled = IsWindowEnabled (GetDlgItem (hwnd, ctrl1)) && (IsDlgButtonChecked (hwnd, ctrl1) == BST_CHECKED);

					EnableWindow (GetDlgItem (hwnd, ctrl2), is_enabled);
					EnableWindow ((HWND)SendDlgItemMessage (hwnd, ctrl2, UDM_GETBUDDY, 0, NULL), is_enabled);

					break;
				}

				case IDC_HOTKEY_CLEAN_CHK:
				{
					BOOL is_enabled = IsWindowEnabled (GetDlgItem (hwnd, IDC_HOTKEY_CLEAN_CHK)) && (IsDlgButtonChecked (hwnd, IDC_HOTKEY_CLEAN_CHK) == BST_CHECKED);

					EnableWindow (GetDlgItem (hwnd, IDC_HOTKEY_CLEAN), is_enabled);

					break;
				}

				case IDC_COLOR_TEXT:
				case IDC_COLOR_BACKGROUND:
				case IDC_COLOR_WARNING:
				case IDC_COLOR_DANGER:
				{
					CHOOSECOLOR cc = {0};
					COLORREF cust[16] = {TRAY_COLOR_TEXT, TRAY_COLOR_BG, TRAY_COLOR_WARNING, TRAY_COLOR_DANGER};

					cc.lStructSize = sizeof (cc);
					cc.Flags = CC_RGBINIT | CC_FULLOPEN;
					cc.hwndOwner = hwnd;
					cc.lpCustColors = cust;
					cc.rgbResult = (COLORREF)GetProp (GetDlgItem (hwnd, LOWORD (wparam)), L"color");

					if (ChooseColor (&cc))
					{
						SetProp (GetDlgItem (hwnd, LOWORD (wparam)), L"color", (HANDLE)cc.rgbResult);
					}

					break;
				}

				case IDC_FONT:
				{
					CHOOSEFONT cf = {0};

					cf.lStructSize = sizeof (cf);
					cf.hwndOwner = hwnd;
					cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;

					data.lf.lfHeight /= data.scale; // font size fix
					cf.lpLogFont = &data.lf;

					if (ChooseFont (&cf))
					{
						app.ConfigSet (L"TrayFontName", data.lf.lfFaceName);
						app.ConfigSet (L"TrayFontSize", MulDiv (-data.lf.lfHeight, 72, GetDeviceCaps (data.cdc1, LOGPIXELSY)));
						app.ConfigSet (L"TrayFontWeight", data.lf.lfWeight);

						SetDlgItemText (hwnd, IDC_FONT, _r_fmt (L"%s, %dpx", app.ConfigGet (L"TrayFontName", FONT_NAME), app.ConfigGet (L"TrayFontSize", FONT_SIZE)));

						_Application_Initialize (app.GetHWND (), FALSE);
					}

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

BOOL SettingsSaveCallback (HWND hwnd, DWORD page_id)
{
	CString buffer;
	BOOL result = FALSE;

	switch (page_id)
	{
		case 0:
		{
			app.ConfigSet (L"AlwaysOnTop", INT ((IsDlgButtonChecked (hwnd, IDC_ALWAYSONTOP_CHK) == BST_CHECKED) ? TRUE : FALSE));
			app.AutorunCreate (IsDlgButtonChecked (hwnd, IDC_LOADONSTARTUP_CHK) == BST_UNCHECKED);
			app.ConfigSet (L"StartMinimized", INT ((IsDlgButtonChecked (hwnd, IDC_STARTMINIMIZED_CHK) == BST_CHECKED) ? TRUE : FALSE));

			if (!_r_system_uacstate ())
			{
				app.SkipUacCreate (IsDlgButtonChecked (hwnd, IDC_SKIPUACWARNING_CHK) == BST_UNCHECKED);
			}

			app.ConfigSet (L"CheckUpdates", INT ((IsDlgButtonChecked (hwnd, IDC_CHECKUPDATES_CHK) == BST_CHECKED) ? TRUE : FALSE));

			// set language
			if (SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, NULL) >= 1)
			{
				GetDlgItemText (hwnd, IDC_LANGUAGE, buffer.GetBuffer (MAX_PATH), MAX_PATH);
				buffer.ReleaseBuffer ();
			}

			app.LocaleSet (buffer);

			if (((INT)GetProp (app.GetHWND (), L"language") != (INT)SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, NULL)))
			{
				result = TRUE;
			}

			break;
		}

		case 1:
		{
			DWORD mask = 0;

			if (IsDlgButtonChecked (hwnd, IDC_WORKINGSET_CHK) == BST_CHECKED)
			{
				mask |= REDUCT_WORKING_SET;
			}

			if (IsDlgButtonChecked (hwnd, IDC_SYSTEMWORKINGSET_CHK) == BST_CHECKED)
			{
				mask |= REDUCT_SYSTEM_WORKING_SET;
			}

			if (IsDlgButtonChecked (hwnd, IDC_STANDBYLISTPRIORITY0_CHK) == BST_CHECKED)
			{
				mask |= REDUCT_STANDBY_PRIORITY0_LIST;
			}

			if (IsDlgButtonChecked (hwnd, IDC_STANDBYLIST_CHK) == BST_CHECKED)
			{
				mask |= REDUCT_STANDBY_LIST;
			}

			if (IsDlgButtonChecked (hwnd, IDC_MODIFIEDLIST_CHK) == BST_CHECKED)
			{
				mask |= REDUCT_MODIFIED_LIST;
			}

			app.ConfigSet (L"ReductMask", mask);

			app.ConfigSet (L"AutoreductEnable", (IsDlgButtonChecked (hwnd, IDC_AUTOREDUCTENABLE_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"AutoreductValue", (DWORD)SendDlgItemMessage (hwnd, IDC_AUTOREDUCTVALUE, UDM_GETPOS32, 0, NULL));

			app.ConfigSet (L"AutoreductIntervalEnable", (IsDlgButtonChecked (hwnd, IDC_AUTOREDUCTINTERVALENABLE_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"AutoreductIntervalValue", (DWORD)SendDlgItemMessage (hwnd, IDC_AUTOREDUCTINTERVALVALUE, UDM_GETPOS32, 0, NULL));

			app.ConfigSet (L"HotkeyCleanEnable", (IsDlgButtonChecked (hwnd, IDC_HOTKEY_CLEAN_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"HotkeyClean", (DWORD)SendDlgItemMessage (hwnd, IDC_HOTKEY_CLEAN, HKM_GETHOTKEY, 0, NULL));

			break;
		}

		case 2:
		{
			app.ConfigSet (L"TrayUseTransparency", (IsDlgButtonChecked (hwnd, IDC_TRAYUSETRANSPARENCY_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"TrayShowBorder", (IsDlgButtonChecked (hwnd, IDC_TRAYSHOWBORDER_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"TrayRoundCorners", (IsDlgButtonChecked (hwnd, IDC_TRAYROUNDCORNERS_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"TrayChangeBg", (IsDlgButtonChecked (hwnd, IDC_TRAYCHANGEBG_CHK) == BST_CHECKED) ? TRUE : FALSE);
			app.ConfigSet (L"TrayUseAntialiasing", (IsDlgButtonChecked (hwnd, IDC_TRAYUSEANTIALIASING_CHK) == BST_CHECKED) ? TRUE : FALSE);

			app.ConfigSet (L"TrayColorText", (DWORD)GetProp (GetDlgItem (hwnd, IDC_COLOR_TEXT), L"color"));
			app.ConfigSet (L"TrayColorBg", (DWORD)GetProp (GetDlgItem (hwnd, IDC_COLOR_BACKGROUND), L"color"));
			app.ConfigSet (L"TrayColorWarning", (DWORD)GetProp (GetDlgItem (hwnd, IDC_COLOR_WARNING), L"color"));
			app.ConfigSet (L"TrayColorDanger", (DWORD)GetProp (GetDlgItem (hwnd, IDC_COLOR_DANGER), L"color"));

			break;
		}

		case 3:
		{
			app.ConfigSet (L"TrayActionDc", (INT)SendDlgItemMessage (hwnd, IDC_TRAYACTIONDC, CB_GETCURSEL, 0, NULL));
			app.ConfigSet (L"TrayActionMc", (INT)SendDlgItemMessage (hwnd, IDC_TRAYACTIONMC, CB_GETCURSEL, 0, NULL));

			app.ConfigSet (L"TrayLevelWarning", (DWORD)SendDlgItemMessage (hwnd, IDC_TRAYLEVELWARNING, UDM_GETPOS32, 0, NULL));
			app.ConfigSet (L"TrayLevelDanger", (DWORD)SendDlgItemMessage (hwnd, IDC_TRAYLEVELDANGER, UDM_GETPOS32, 0, NULL));

			break;
		}

		case (DWORD)-1:
		{
			_Application_Initialize (app.GetHWND (), FALSE);

			if (!hwnd)
			{
				_r_windowtotop (app.GetHWND (), app.ConfigGet (L"AlwaysOnTop", 0));
			}

			break;
		}
	}

	return result;
}

LRESULT CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_TASKBARCREATED)
	{
		_Application_Initialize (hwnd, TRUE);
		return FALSE;
	}

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			// static initializer
			data.is_admin = _r_system_adminstate ();
			data.is_supported_os = _r_system_validversion (6, 0);

			// set privileges
			if (data.is_admin)
			{
				_r_system_setprivilege (SE_INCREASE_QUOTA_NAME, TRUE);
				_r_system_setprivilege (SE_PROF_SINGLE_PROCESS_NAME, TRUE);
			}

			// set priority
			SetPriorityClass (GetCurrentProcess (), HIGH_PRIORITY_CLASS);

			// dynamic initializer
			_Application_Initialize (hwnd, TRUE);

			// enable taskbarcreated message bypass uipi
			_r_window_changemessagefilter (hwnd, WM_TASKBARCREATED, MSGFLT_ALLOW);

			// uac indicator (windows vista and above)
			if (_r_system_uacstate ())
			{
				RECT rc = {0};

				SendDlgItemMessage (hwnd, IDC_CLEAN, BCM_SETSHIELD, 0, TRUE);

				SendDlgItemMessage (hwnd, IDC_CLEAN, BCM_GETTEXTMARGIN, 0, (LPARAM)&rc);
				rc.left += GetSystemMetrics (SM_CXSMICON) / 2;
				SendDlgItemMessage (hwnd, IDC_CLEAN, BCM_SETTEXTMARGIN, 0, (LPARAM)&rc);
			}

			// configure listview
			_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, nullptr, 50, 1, LVCFMT_RIGHT);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, nullptr, 50, 2, LVCFMT_LEFT);

			for (INT i = 0; i < 3; i++)
			{
				_r_listview_addgroup (hwnd, IDC_LISTVIEW, i, I18N (&app, IDS_GROUP_1 + i, _r_fmt (L"IDS_GROUP_%d", i + 1)));

				for (INT j = 0; j < 3; j++)
				{
					_r_listview_additem (hwnd, IDC_LISTVIEW, I18N (&app, IDS_ITEM_1 + j, _r_fmt (L"IDS_ITEM_%d", j + 1)), -1, 0, -1, i);
				}
			}

			if (!wcsstr (GetCommandLine (), L"/minimized") && !app.ConfigGet (L"StartMinimized", 1))
			{
				_r_windowtoggle (hwnd, TRUE);
			}

			// localize
			HMENU menu = GetMenu (hwnd);

			app.LocaleMenu (menu, I18N (&app, IDS_FILE, 0), 0, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_SETTINGS, 0), IDM_SETTINGS, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_EXIT, 0), IDM_EXIT, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_HELP, 0), 1, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_WEBSITE, 0), IDM_WEBSITE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_CHECKUPDATES, 0), IDM_CHECKUPDATES, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ABOUT, 0), IDM_ABOUT, FALSE);

			DrawMenuBar (hwnd); // redraw menu

			SetDlgItemText (hwnd, IDC_CLEAN, I18N (&app, IDS_CLEAN, 0));

			break;
		}

		case WM_DESTROY:
		{
			_Application_Unitialize (hwnd, TRUE);

			PostQuitMessage (0);

			break;
		}

		case WM_QUERYENDSESSION:
		{
			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			HDC dc = BeginPaint (hwnd, &ps);

			RECT rc = {0};
			GetClientRect (hwnd, &rc);

			static INT height = MulDiv (35, GetDeviceCaps (data.cdc1, LOGPIXELSY), 72);

			rc.top = rc.bottom - height;
			rc.bottom = rc.top + height;

			COLORREF clr_prev = SetBkColor (dc, GetSysColor (COLOR_BTNFACE));
			ExtTextOut (dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
			SetBkColor (dc, clr_prev);

			for (INT i = 0; i < rc.right; i++)
			{
				SetPixel (dc, i, rc.top, RGB (223, 223, 223));
			}

			EndPaint (hwnd, &ps);

			break;
		}

		case WM_HOTKEY:
		{
			if (wparam == UID)
			{
				_Memory_Clean (nullptr, data.reduct_mask);
			}

			break;
		}

		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORDLG:
		{
			return (INT_PTR)GetSysColorBrush (COLOR_WINDOW);
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			if (nmlp->idFrom == IDC_LISTVIEW)
			{
				switch (nmlp->code)
				{
					case NM_CUSTOMDRAW:
					{
						LONG result = CDRF_DODEFAULT;
						LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;

						switch (lpnmlv->nmcd.dwDrawStage)
						{
							case CDDS_PREPAINT:
							{
								result = (CDRF_NOTIFYPOSTPAINT | CDRF_NOTIFYITEMDRAW);
								break;
							}

							case CDDS_ITEMPREPAINT:
							{
								if ((UINT)lpnmlv->nmcd.lItemlParam >= app.ConfigGet (L"TrayLevelDanger", 90))
								{
									lpnmlv->clrText = app.ConfigGet (L"TrayColorDanger", TRAY_COLOR_DANGER);
									result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
								}
								else if ((UINT)lpnmlv->nmcd.lItemlParam >= app.ConfigGet (L"TrayLevelWarning", 60))
								{
									lpnmlv->clrText = app.ConfigGet (L"TrayColorWarning", TRAY_COLOR_WARNING);
									result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
								}

								break;
							}
						}

						SetWindowLongPtr (hwnd, DWLP_MSGRESULT, result);
						return TRUE;
					}
				}
			}

			break;
		}

		case WM_SIZE:
		{
			if (wparam == SIZE_MINIMIZED)
			{
				_r_windowtoggle (hwnd, FALSE);
			}

			break;
		}

		case WM_SYSCOMMAND:
		{
			if (wparam == SC_CLOSE)
			{
				_r_windowtoggle (hwnd, FALSE);
				return TRUE;
			}

			break;
		}

		case WM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case WM_LBUTTONUP:
				{
					SetForegroundWindow (hwnd);
					break;
				}

				case WM_LBUTTONDBLCLK:
				case WM_MBUTTONDOWN:
				{
					DWORD action = LOWORD (lparam) == WM_LBUTTONDBLCLK ? app.ConfigGet (L"TrayActionDc", 0) : app.ConfigGet (L"TrayActionMc", 1);

					switch (action)
					{
						case 1:
						{
							_Memory_Clean (nullptr, data.reduct_mask);
							break;
						}

						case 2:
						{
							_r_run (L"taskmgr.exe");
							break;
						}

						default:
						{
							_r_windowtoggle (hwnd, FALSE);
							break;
						}
					}

					break;
				}

				case WM_RBUTTONUP:
				{
					SetForegroundWindow (hwnd); // don't touch

#define SUBMENU1 3
#define SUBMENU2 4

					HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_TRAY)), submenu = GetSubMenu (menu, 0);

					HMENU submenu1 = GetSubMenu (submenu, SUBMENU1);
					HMENU submenu2 = GetSubMenu (submenu, SUBMENU2);

					// localize
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_SHOW, 0), IDM_TRAY_SHOW, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_CLEAN, 0), IDM_TRAY_CLEAN, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_POPUP_1, 0), SUBMENU1, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_POPUP_2, 0), SUBMENU2, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_SETTINGS, 0), IDM_TRAY_SETTINGS, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_WEBSITE, 0), IDM_TRAY_WEBSITE, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_ABOUT, 0), IDM_TRAY_ABOUT, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_EXIT, 0), IDM_TRAY_EXIT, FALSE);

					app.LocaleMenu (submenu1, I18N (&app, IDS_TRAY_DISABLE, 0), IDM_TRAY_DISABLE_1, FALSE);
					app.LocaleMenu (submenu2, I18N (&app, IDS_TRAY_DISABLE, 0), IDM_TRAY_DISABLE_2, FALSE);

					// generate submenu #1
					limit_vec = generate_menu_array (app.ConfigGet (L"AutoreductValue", 90), 10);

					for (size_t i = 0; i < limit_vec.size (); i++)
					{
						AppendMenu (submenu1, MF_STRING, IDM_TRAY_POPUP_1 + i, _r_fmt (L"%d%%", limit_vec.at (i)));

						if (app.ConfigGet (L"AutoreductValue", 90) == limit_vec.at (i))
						{
							CheckMenuRadioItem (submenu1, 0, (UINT)limit_vec.size (), i + 2, MF_BYPOSITION);
						}
					}

					// generate submenu #2
					interval_vec = generate_menu_array (app.ConfigGet (L"AutoreductIntervalValue", 30), 10);

					for (size_t i = 0; i < interval_vec.size (); i++)
					{
						AppendMenu (submenu2, MF_STRING, IDM_TRAY_POPUP_2 + i, _r_fmt (L"%d min.", interval_vec.at (i)));

						if (app.ConfigGet (L"AutoreductIntervalValue", 30) == interval_vec.at (i))
						{
							CheckMenuRadioItem (submenu2, 0, (UINT)interval_vec.size (), i + 2, MF_BYPOSITION);
						}
					}

					// configure menu-items
					if (!app.ConfigGet (L"AutoreductEnable", 1))
					{
						CheckMenuItem (submenu1, IDM_TRAY_DISABLE_1, MF_BYCOMMAND | MF_CHECKED);
					}

					if (!app.ConfigGet (L"AutoreductIntervalEnable", 0))
					{
						CheckMenuItem (submenu2, IDM_TRAY_DISABLE_2, MF_BYCOMMAND | MF_CHECKED);
					}

					if (GetProp (hwnd, L"is_reduct_opened"))
					{
						EnableMenuItem (submenu, IDM_TRAY_CLEAN, MF_BYCOMMAND | MF_DISABLED);
					}

					if (GetProp (hwnd, L"is_settings_opened"))
					{
						EnableMenuItem (submenu, IDM_TRAY_SETTINGS, MF_BYCOMMAND | MF_DISABLED);
					}

					if (GetProp (hwnd, L"is_about_opened"))
					{
						EnableMenuItem (submenu, IDM_TRAY_ABOUT, MF_BYCOMMAND | MF_DISABLED);
					}

					POINT pt = {0};
					GetCursorPos (&pt);

					TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

					DestroyMenu (submenu1);
					DestroyMenu (submenu2);

					DestroyMenu (menu);
					DestroyMenu (submenu);

					break;
				}
			}

			break;
		}
		case WM_COMMAND:
		{
			if ((LOWORD (wparam) >= IDM_TRAY_POPUP_1 && LOWORD (wparam) <= IDM_TRAY_POPUP_1 + 50))
			{
				app.ConfigSet (L"AutoreductValue", limit_vec.at (LOWORD (wparam) - IDM_TRAY_POPUP_1));
				break;
			}
			else if ((LOWORD (wparam) >= IDM_TRAY_POPUP_2 && LOWORD (wparam) <= IDM_TRAY_POPUP_2 + 50))
			{
				app.ConfigSet (L"AutoreductIntervalValue", interval_vec.at (LOWORD (wparam) - IDM_TRAY_POPUP_2));
				break;
			}

			switch (LOWORD (wparam))
			{
				case IDM_TRAY_DISABLE_1:
				{
					app.ConfigSet (L"AutoreductEnable", !app.ConfigGet (L"AutoreductEnable", 1));
					break;
				}

				case IDM_TRAY_DISABLE_2:
				{
					app.ConfigSet (L"AutoreductIntervalEnable", !app.ConfigGet (L"AutoreductIntervalEnable", 0));
					break;
				}

				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				{
					if (!GetProp (hwnd, L"is_settings_opened"))
					{
						SetProp (hwnd, L"is_settings_opened", (HANDLE)TRUE);

						app.CreateSettingsWindow (APP_SETTINGS_COUNT, PagesDlgProc, SettingsSaveCallback);

						SetProp (hwnd, L"is_settings_opened", FALSE);
					}

					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_windowtoggle (hwnd, FALSE);
					break;
				}

				case IDOK: // process Enter key
				case IDC_CLEAN:
				case IDM_TRAY_CLEAN:
				{
					SetProp (hwnd, L"is_reduct_opened", (HANDLE)TRUE);

					if (!data.is_admin)
					{
						if (app.SkipUacRun ())
						{
							DestroyWindow (hwnd);
						}
						else
						{
							_r_tray_balloontip (&nid, NIIF_ERROR, APP_NAME, I18N (&app, IDS_STATUS_NOPRIVILEGES, 0));
						}
					}
					else
					{
						_Memory_Clean (hwnd, data.reduct_mask);
					}

					SetProp (hwnd, L"is_reduct_opened", FALSE);

					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, APP_WEBSITE, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_CHECKUPDATES:
				{
					app.CheckForUpdates (FALSE);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					if (!GetProp (hwnd, L"is_about_opened"))
					{
						SetProp (hwnd, L"is_about_opened", (HANDLE)TRUE);

						app.CreateAboutWindow ();

						SetProp (hwnd, L"is_about_opened", FALSE);
					}

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	app.SetCopyright (APP_COPYRIGHT);
	app.SetLinks (APP_WEBSITE, APP_GITHUB);

#ifndef _WIN64
	if (_r_system_iswow64 ())
	{
		_r_msg (nullptr, MB_OK | MB_ICONEXCLAMATION, APP_NAME, L"WARNING! 32-bit executable may incompatible with 64-bit operating system version!");
	}
#endif // _WIN64

	if (app.CreateMainWindow ((DLGPROC)DlgProc))
	{
		MSG msg = {0};

		while (GetMessage (&msg, nullptr, 0, 0))
		{
			if (!IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
}

	_Application_Unitialize (app.GetHWND (), TRUE);

	return ERROR_SUCCESS;
}
