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

		if (uMsg != _nid.uCallbackMessage || LOWORD(wParam) != _nid.uID)
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
	UINT _taskbar_restart_msg = RegisterWindowMessageW(L"TaskbarCreated");
	int _current_urgency = -1;

	static HICON create_colored_icon(const int level)
	{
		const UINT dpi = GetDpiForSystem();
		const int cx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
		const int cy = GetSystemMetricsForDpi(SM_CYSMICON, dpi);

		const HDC hdcScreen = GetDC(nullptr);
		if (hdcScreen == nullptr) return nullptr;

		// Choose color based on urgency
		COLORREF bgColor, fgColor;
		switch (level)
		{
		case 2: bgColor = RGB(0xE0, 0x40, 0x40);
			fgColor = RGB(0xFF, 0xFF, 0xFF);
			break;
		case 1: bgColor = RGB(0xE0, 0xA0, 0x20);
			fgColor = RGB(0x20, 0x20, 0x20);
			break;
		default: bgColor = RGB(0x4E, 0xC9, 0x6F);
			fgColor = RGB(0xFF, 0xFF, 0xFF);
			break;
		}

		// Raw-pixel 32-bit ARGB DIB (inspired by draw_pie_chart)
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx;
		bmi.bmiHeader.biHeight = -cy; // top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void* pixelData = nullptr;
		const HBITMAP hbmColor = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pixelData, nullptr, 0);
		ReleaseDC(nullptr, hdcScreen);

		if (!hbmColor || !pixelData)
		{
			if (hbmColor) DeleteObject(hbmColor);
			return nullptr;
		}

		auto* pixels = static_cast<DWORD*>(pixelData);

		// Premultiplied-alpha pixel packing
		auto pack_pma = [](const int r, const int g, const int b, const int a) -> DWORD
		{
			return (static_cast<DWORD>(a) << 24) |
				(static_cast<DWORD>(r * a / 255) << 16) |
				(static_cast<DWORD>(g * a / 255) << 8) |
				static_cast<DWORD>(b * a / 255);
		};

		auto px_lerp = [](const DWORD a, const DWORD b, const int t256) -> DWORD
		{
			const int inv = 256 - t256;
			const DWORD r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * t256) >> 8;
			const DWORD g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * t256) >> 8;
			const DWORD bl = ((a & 0xFF) * inv + (b & 0xFF) * t256) >> 8;
			const DWORD al = (((a >> 24) & 0xFF) * inv + ((b >> 24) & 0xFF) * t256) >> 8;
			return (al << 24) | (r << 16) | (g << 8) | bl;
		};

		const int bgR = GetRValue(bgColor), bgG = GetGValue(bgColor), bgB = GetBValue(bgColor);
		const int fgR = GetRValue(fgColor), fgG = GetGValue(fgColor), fgB = GetBValue(fgColor);

		const float halfX = cx * 0.5f;
		const float halfY = cy * 0.5f;
		const float radius = std::min(cx, cy) * 0.5f - 0.5f;

		// "E" glyph geometry (centered in icon)
		const float ew = cx * 0.40f;
		const float eh = cy * 0.50f;
		const float ex0 = (cx - ew) * 0.5f;
		const float ey0 = (cy - eh) * 0.5f;
		const float stemW = std::max(1.5f, ew * 0.30f);
		const float barH = std::max(1.5f, eh * 0.20f);
		const float midBarW = ew * 0.70f;

		auto sample_E = [&](const float px, const float py) -> bool
		{
			const float lx = px - ex0;
			const float ly = py - ey0;
			if (lx < 0.0f || lx > ew || ly < 0.0f || ly > eh)
				return false;
			if (lx < stemW) return true;
			if (ly < barH) return true;
			if (ly > eh - barH) return true;
			const float midY = (eh - barH) * 0.5f;
			return ly >= midY && ly < midY + barH && lx < midBarW;
		};

		// Ordered dither matrix (same style as draw_pie_chart)
		constexpr float dither[4][4] = {
			{1.0f / 64, 9.0f / 64, 3.0f / 64, 11.0f / 64},
			{13.0f / 64, 5.0f / 64, 15.0f / 64, 7.0f / 64},
			{4.0f / 64, 12.0f / 64, 2.0f / 64, 10.0f / 64},
			{16.0f / 64, 8.0f / 64, 14.0f / 64, 6.0f / 64}
		};

		const DWORD bgPx = pack_pma(bgR, bgG, bgB, 255);
		const DWORD fgPx = pack_pma(fgR, fgG, fgB, 255);

		for (int y = 0; y < cy; y++)
		{
			DWORD* line = pixels + y * cx;
			for (int x = 0; x < cx; x++)
			{
				const float px = x + 0.5f;
				const float py = y + 0.5f;
				const float dx = px - halfX;
				const float dy = py - halfY;
				const float dist = std::sqrt(dx * dx + dy * dy);

				if (dist > radius + 0.7f)
				{
					line[x] = 0;
					continue;
				}

				// Circle coverage with 1px anti-alias band
				const float circleAlpha = std::clamp(radius + 0.5f - dist, 0.0f, 1.0f);

				// 2x2 supersampled "E" coverage for anti-aliased glyph edges
				int hits = 0;
				constexpr float offsets[] = {0.25f, 0.75f};
				for (const float oy : offsets)
					for (const float ox : offsets)
						if (sample_E(x + ox, y + oy))
							hits++;
				const float eCoverage = hits * 0.25f;

				// Blend fg/bg based on E coverage, then apply subtle dither
				DWORD c = px_lerp(bgPx, fgPx, static_cast<int>(eCoverage * 255));
				const float ff = dither[x % 4][y % 4] * 0.15f;
				const DWORD cr = std::min(
					255ul, static_cast<unsigned long>((c >> 16) & 0xFFu) + static_cast<unsigned long>(ff * 30));
				const DWORD cg = std::min(
					255ul, static_cast<unsigned long>((c >> 8) & 0xFFu) + static_cast<unsigned long>(ff * 30));
				const DWORD cb = std::min(
					255ul, static_cast<unsigned long>(c & 0xFFu) + static_cast<unsigned long>(ff * 30));
				c = ((c & 0xFF000000u) | (cr << 16) | (cg << 8) | cb);

				// Apply circle edge alpha
				if (circleAlpha < 1.0f)
				{
					const int a = static_cast<int>(circleAlpha * 255.0f);
					c = (static_cast<DWORD>(a) << 24) |
						(((c >> 16) & 0xFF) * a / 255 << 16) |
						(((c >> 8) & 0xFF) * a / 255 << 8) |
						((c & 0xFF) * a / 255);
				}

				line[x] = c;
			}
		}

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
