// ui-task-bar-icon.h - System tray (notification area) icon management.
// Encapsulates Shell_NotifyIcon operations: install, uninstall, context
// menu handling, balloon tip display, and TaskbarCreated restart recovery.

#pragma once


class task_bar_icon
{
public:
	task_bar_icon()
	{
		_nid.cbSize = sizeof(_nid);
		_nid.uCallbackMessage = RegisterWindowMessageW(L"TaskbarNotifyMsg");
	}

	~task_bar_icon()
	{
		uninstall();
	}

	BOOL install(const HINSTANCE hInstance, const HWND hWnd, const UINT iID, const UINT nRes)
	{
		assert(::IsWindow(hWnd));
		assert(_menu == nullptr);
		assert(_nid.hIcon == nullptr);

		_nid.hWnd = hWnd;
		_nid.uID = iID;
		_hInstance = hInstance;
		_iconRes = nRes;
		const UINT dpi = GetDpiForSystem();
		_nid.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(nRes), IMAGE_ICON,
		                                           GetSystemMetricsForDpi(SM_CXSMICON, dpi),
		                                           GetSystemMetricsForDpi(SM_CYSMICON, dpi),
		                                           LR_DEFAULTCOLOR));
		if (_nid.hIcon == nullptr)
			return FALSE;

		_nid.szTip[0] = L'\0';
		const std::wstring tooltip = load_string(hInstance, nRes);
		StringCchCopyW(_nid.szTip, ARRAYSIZE(_nid.szTip), tooltip.c_str());
		_menu = LoadMenuW(hInstance, MAKEINTRESOURCEW(nRes));
		return add_task_bar_icon();
	}

	BOOL uninstall()
	{
		BOOL result = TRUE;
		if (_nid.hWnd != nullptr)
			result = delete_task_bar_icon();

		if (result)
			_nid.hWnd = nullptr;
		if (_nid.hIcon != nullptr)
			DestroyIcon(_nid.hIcon);
		_nid.hIcon = nullptr;
		if (_menu != nullptr)
			DestroyMenu(_menu);
		_menu = nullptr;
		return result;
	}

	BOOL is_installed() const
	{
		return _nid.hWnd != nullptr;
	}

	BOOL handle_message(const UINT uMsg, const WPARAM wParam, const LPARAM lParam, LRESULT& lResult)
	{
		if (uMsg == _taskbar_restart_msg)
		{
			lResult = add_task_bar_icon();
			return TRUE;
		}

		if (uMsg != _nid.uCallbackMessage || HIWORD(lParam) != _nid.uID)
			return FALSE;

		switch (LOWORD(lParam))
		{
		case NIN_SELECT:
			lResult = on_taskbar_dbl_click();
			return TRUE;

		case WM_CONTEXTMENU:
			lResult = on_taskbar_context_menu();
			return TRUE;
		}

		return FALSE;
	}

	BOOL show_balloon(const std::wstring& infoTitle, const std::wstring& info, const std::wstring& tip,
	                  const int timeoutSeconds)
	{
		_nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_INFO;
		_nid.dwInfoFlags = NIIF_INFO;
		_nid.uTimeout = timeoutSeconds * 1000;

		StringCchCopyW(_nid.szInfoTitle, ARRAYSIZE(_nid.szInfoTitle), infoTitle.c_str());
		StringCchCopyW(_nid.szInfo, ARRAYSIZE(_nid.szInfo), info.c_str());
		StringCchCopyW(_nid.szTip, ARRAYSIZE(_nid.szTip), tip.c_str());

		return Shell_NotifyIconW(NIM_MODIFY, &_nid);
	}

	// Update tray icon color: 0=green, 1=yellow, 2=red
	void update_urgency(const int level)
	{
		if (level == _current_urgency)
			return;

		const HICON hNew = create_colored_icon(level);
		if (hNew == nullptr)
		{
			_current_urgency = -1; // Force retry on next call
			return;
		}

		_current_urgency = level;

		if (_nid.hIcon != nullptr)
			DestroyIcon(_nid.hIcon);

		_nid.hIcon = hNew;
		_nid.uFlags = NIF_ICON;
		Shell_NotifyIconW(NIM_MODIFY, &_nid);
	}

	void update_tooltip(const std::wstring& tip)
	{
		StringCchCopyW(_nid.szTip, ARRAYSIZE(_nid.szTip), tip.c_str());
		_nid.uFlags = NIF_TIP;
		Shell_NotifyIconW(NIM_MODIFY, &_nid);
	}

private:
	NOTIFYICONDATAW _nid{};
	HMENU _menu = nullptr;
	HINSTANCE _hInstance = nullptr;
	UINT _iconRes = 0;
	UINT _taskbar_restart_msg = RegisterWindowMessageW(L"TaskbarCreated");
	int _current_urgency = -1;

	HICON create_colored_icon(const int level) const
	{
		const UINT dpi = GetDpiForSystem();
		const int cx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
		const int cy = GetSystemMetricsForDpi(SM_CYSMICON, dpi);

		const HDC hdcScreen = GetDC(nullptr);
		if (hdcScreen == nullptr) return nullptr;

		// Choose color based on urgency
		COLORREF bgColor;
		switch (level)
		{
		case 2: bgColor = RGB(0xE0, 0x40, 0x40); break;
		case 1: bgColor = RGB(0xE0, 0xA0, 0x20); break;
		default: bgColor = RGB(0x4E, 0xC9, 0x6F); break;
		}

		// Raw-pixel 32-bit ARGB DIB
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx;
		bmi.bmiHeader.biHeight = -cy; // top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void* pixelData = nullptr;
		const HBITMAP hbmColor = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pixelData, nullptr, 0);

		if (!hbmColor || !pixelData)
		{
			ReleaseDC(nullptr, hdcScreen);
			if (hbmColor) DeleteObject(hbmColor);
			return nullptr;
		}

		auto* pixels = static_cast<DWORD*>(pixelData);

		const int bgR = GetRValue(bgColor), bgG = GetGValue(bgColor), bgB = GetBValue(bgColor);
		const float halfX = cx * 0.5f;
		const float halfY = cy * 0.5f;
		const float radius = std::min(cx, cy) * 0.5f - 0.5f;

		// Draw the colored circle background
		for (int iy = 0; iy < cy; iy++)
		{
			DWORD* line = pixels + iy * cx;
			for (int ix = 0; ix < cx; ix++)
			{
				const float dx = ix + 0.5f - halfX;
				const float dy = iy + 0.5f - halfY;
				const float dist = std::sqrt(dx * dx + dy * dy);

				if (dist > radius + 0.7f)
				{
					line[ix] = 0;
					continue;
				}

				const float circleAlpha = std::clamp(radius + 0.5f - dist, 0.0f, 1.0f);
				const int a = static_cast<int>(circleAlpha * 255.0f);
				line[ix] = (static_cast<DWORD>(a) << 24) |
					(static_cast<DWORD>(bgR * a / 255) << 16) |
					(static_cast<DWORD>(bgG * a / 255) << 8) |
					static_cast<DWORD>(bgB * a / 255);
			}
		}

		// Load the app icon and composite it over the circle
		const HICON hAppIcon = static_cast<HICON>(LoadImageW(
			_hInstance, MAKEINTRESOURCEW(_iconRes), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));

		if (hAppIcon)
		{
			// Render the app icon into a temporary DIB to read its pixels
			void* iconPixelData = nullptr;
			const HBITMAP hbmIcon = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &iconPixelData, nullptr, 0);

			if (hbmIcon && iconPixelData)
			{
				const HDC hdcMem = CreateCompatibleDC(hdcScreen);
				const HGDIOBJ hOld = SelectObject(hdcMem, hbmIcon);

				// Clear to transparent
				memset(iconPixelData, 0, static_cast<size_t>(cx) * cy * 4);
				DrawIconEx(hdcMem, 0, 0, hAppIcon, cx, cy, 0, nullptr, DI_NORMAL);

				SelectObject(hdcMem, hOld);
				DeleteDC(hdcMem);

				// Composite icon pixels over the circle using source-over
				const auto* iconPx = static_cast<const DWORD*>(iconPixelData);
				for (int i = 0; i < cx * cy; i++)
				{
					const DWORD src = iconPx[i];
					const int sa = static_cast<int>((src >> 24) & 0xFF);
					if (sa == 0) continue;

					const DWORD dst = pixels[i];
					if (sa == 255)
					{
						pixels[i] = src;
						continue;
					}

					// Source-over blend (both sides premultiplied alpha)
					const DWORD inv = 255 - static_cast<DWORD>(sa);
					const DWORD da = std::min<DWORD>(255, ((dst >> 24) & 0xFF) * inv / 255 + static_cast<DWORD>(sa));
					const DWORD dr = std::min<DWORD>(255, (((dst >> 16) & 0xFF) * inv / 255) + ((src >> 16) & 0xFF));
					const DWORD dg = std::min<DWORD>(255, (((dst >> 8) & 0xFF) * inv / 255) + ((src >> 8) & 0xFF));
					const DWORD db = std::min<DWORD>(255, ((dst & 0xFF) * inv / 255) + (src & 0xFF));
					pixels[i] = (da << 24) | (dr << 16) | (dg << 8) | db;
				}
			}

			if (hbmIcon) DeleteObject(hbmIcon);
			DestroyIcon(hAppIcon);
		}

		ReleaseDC(nullptr, hdcScreen);

		// All-zero monochrome mask (alpha in color DIB controls transparency)
		const size_t maskBytes = static_cast<size_t>((cx + 7) / 8) * cy;
		std::vector<uint8_t> maskBits(maskBytes, 0);
		const HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, maskBits.data());

		ICONINFO ii = {};
		ii.fIcon = TRUE;
		ii.hbmMask = hbmMask;
		ii.hbmColor = hbmColor;
		const HICON hIcon = CreateIconIndirect(&ii);

		DeleteObject(hbmColor);
		DeleteObject(hbmMask);

		return hIcon;
	}

	BOOL add_task_bar_icon()
	{
		assert(::IsWindow(_nid.hWnd));
		_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
		const BOOL result = Shell_NotifyIconW(NIM_ADD, &_nid);
		if (result)
		{
			_nid.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW(NIM_SETVERSION, &_nid);
		}
		return result;
	}

	BOOL delete_task_bar_icon()
	{
		return Shell_NotifyIconW(NIM_DELETE, &_nid);
	}

	LRESULT on_taskbar_dbl_click()
	{
		restore_and_show(_nid.hWnd);
		return 0;
	}

	LRESULT on_taskbar_context_menu()
	{
		if (!IsMenu(_menu))
			return 0;

		const HMENU hSubMenu = GetSubMenu(_menu, 0);
		if (!IsMenu(hSubMenu))
			return 0;

		SetMenuDefaultItem(hSubMenu, 0, TRUE);

		POINT pt = {0, 0};
		GetCursorPos(&pt);
		SetForegroundWindow(_nid.hWnd);
		TrackPopupMenu(hSubMenu, 0, pt.x, pt.y, 0, _nid.hWnd, nullptr);
		PostMessageW(_nid.hWnd, WM_NULL, 0, 0);
		return 0;
	}
};
