// main.cpp - Application entry point. Creates the main frame window
// and runs the Win32 message loop.

#include "win.h"

#include "resource.h"

#include "data.h"
#include "ui.h"
#include "ui-task-bar-icon.h"
#include "ui-frame.h"

void draw_context::draw_usage_graph(const rect_f& rect,
	const COLORREF mouseClr, const COLORREF kbClr, const COLORREF breakClr,
	const COLORREF bgClr, const COLORREF textClr,
	const usage_tick* uses, const uint8_t* break_markers,
	const int maxUses, const int timerGap)
{
	const int w = static_cast<int>(rect.Width);
	const int h = static_cast<int>(rect.Height);
	if (w <= 0 || h <= 0) return;

	const int nWidth = std::min(w, maxUses);
	if (nWidth <= 0) return;

	// Smooth data with double box blur (approximates Gaussian)
	const int smoothR = std::clamp(nWidth / 80, 3, 12);
	std::vector<float> sMouse(nWidth), sKb(nWidth), sBreak(nWidth);

	// First pass
	std::vector<float> tMouse(nWidth), tKb(nWidth);
	for (int i = 0; i < nWidth; i++)
	{
		float sumM = 0, sumK = 0;
		int n = 0;
		for (int j = std::max(0, i - smoothR); j <= std::min(nWidth - 1, i + smoothR); j++)
		{
			sumM += static_cast<float>(uses[j].mouse);
			sumK += static_cast<float>(uses[j].keyboard);
			n++;
		}
		tMouse[i] = sumM / static_cast<float>(n);
		tKb[i] = sumK / static_cast<float>(n);
	}

	// Second pass
	for (int i = 0; i < nWidth; i++)
	{
		float sumM = 0, sumK = 0, sumB = 0;
		int n = 0;
		for (int j = std::max(0, i - smoothR); j <= std::min(nWidth - 1, i + smoothR); j++)
		{
			sumM += tMouse[j];
			sumK += tKb[j];
			sumB += static_cast<float>(break_markers[j]);
			n++;
		}
		sMouse[i] = sumM / static_cast<float>(n);
		sKb[i] = sumK / static_cast<float>(n);
		sBreak[i] = sumB / static_cast<float>(n);
	}

	// Find max smoothed value
	float maxVal = 0.001f;
	for (int i = 0; i < nWidth; i++)
	{
		const float total = sMouse[i] + sKb[i];
		if (total > maxVal) maxVal = total;
	}

	// Bitmap buffer
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	std::vector<DWORD> buffer(w * h);
	auto* pixels = buffer.data();

	auto to_px = [](const COLORREF c) -> DWORD
		{
			return (static_cast<DWORD>(GetRValue(c)) << 16) |
				(static_cast<DWORD>(GetGValue(c)) << 8) |
				static_cast<DWORD>(GetBValue(c));
		};

	auto px_blend = [](const DWORD base, const DWORD tint, const float t) -> DWORD
		{
			const float inv = 1.0f - t;
			const DWORD r = static_cast<DWORD>(static_cast<float>((base >> 16) & 0xFF) * inv + static_cast<float>((tint >> 16) & 0xFF) * t);
			const DWORD g = static_cast<DWORD>(static_cast<float>((base >> 8) & 0xFF) * inv + static_cast<float>((tint >> 8) & 0xFF) * t);
			const DWORD b = static_cast<DWORD>(static_cast<float>(base & 0xFF) * inv + static_cast<float>(tint & 0xFF) * t);
			return (r << 16) | (g << 8) | b;
		};

	const DWORD bgPx = to_px(bgClr);
	const DWORD mousePx = to_px(mouseClr);
	const DWORD kbPx = to_px(kbClr);
	const DWORD breakPx = to_px(breakClr);
	const DWORD gridPx = to_px(blend_color(bgClr, textClr, 0.08f));
	const DWORD outlinePx = to_px(blend_color(kbClr, RGB(255, 255, 255), 0.3f));
	const DWORD axisPx = to_px(blend_color(textClr, bgClr, 0.5f));

	// Fill background
	for (int i = 0; i < w * h; i++)
		pixels[i] = bgPx;

	// Grid lines (horizontal at 25%, 50%, 75%, 100%)
	for (int g = 1; g <= 4; g++)
	{
		const int gy = h - static_cast<int>(static_cast<float>(h) * g / 4.0f);
		if (gy >= 0 && gy < h)
		{
			DWORD* line = pixels + gy * w;
			for (int x = 0; x < w; x++)
				line[x] = gridPx;
		}
	}

	// Break bands + stacked columns
	for (int i = 0; i < nWidth; i++)
	{
		const int px = (w - 1) - i;
		if (px < 0 || px >= w) continue;

		// Break band (full height tinted column)
		if (sBreak[i] > 0.01f)
		{
			const DWORD bandPx = px_blend(bgPx, breakPx, sBreak[i] * 0.25f);
			for (int y = 0; y < h; y++)
				pixels[y * w + px] = bandPx;
		}

		// Stacked columns
		const float mv = sMouse[i], kv = sKb[i];
		const float total = mv + kv;
		if (total <= 0.001f) continue;

		const int mouseH = static_cast<int>(mv * static_cast<float>(h) / maxVal);
		const int totalH = static_cast<int>(total * static_cast<float>(h) / maxVal);

		// Mouse (bottom portion)
		if (mouseH > 0)
		{
			const DWORD mPx = px_blend(bgPx, mousePx, 0.35f + (mv / maxVal) * 0.45f);
			for (int dy = 0; dy < mouseH && dy < h; dy++)
				pixels[(h - 1 - dy) * w + px] = mPx;
		}

		// Keyboard (stacked on top of mouse)
		if (totalH > mouseH)
		{
			const DWORD kPx = px_blend(bgPx, kbPx, 0.35f + (kv / maxVal) * 0.45f);
			for (int dy = mouseH; dy < totalH && dy < h; dy++)
				pixels[(h - 1 - dy) * w + px] = kPx;
		}
	}

	// Top-edge outline (2 pixels thick)
	for (int i = 0; i < nWidth; i++)
	{
		const int px = (w - 1) - i;
		if (px < 0 || px >= w) continue;

		const float total = sMouse[i] + sKb[i];
		if (total <= 0.001f) continue;

		const int topY = h - static_cast<int>(total * static_cast<float>(h) / maxVal);
		for (int dy = 0; dy < 2; dy++)
		{
			const int py = topY + dy;
			if (py >= 0 && py < h)
				pixels[py * w + px] = outlinePx;
		}
	}

	// Tick marks at time intervals
	constexpr int intervals[] = { 30, 60, 120, 180 };
	for (const int mins : intervals)
	{
		const int tickPos = mins * timerGap;
		if (tickPos >= nWidth) continue;

		const int lx = w - tickPos;
		if (lx >= 0 && lx < w)
		{
			for (int dy = 0; dy < 4; dy++)
			{
				const int py = h - 1 - dy;
				if (py >= 0)
					pixels[py * w + lx] = axisPx;
			}
		}
	}

	// Blit buffer to target DC
	SetDIBitsToDevice(_hdc, static_cast<int>(rect.X), static_cast<int>(rect.Y),
		w, h, 0, 0, 0, h, pixels, &bmi, DIB_RGB_COLORS);

	// Time axis labels (GDI text on top of bitmap)
	const float gR = rect.X + rect.Width;
	const float gB = rect.Y + rect.Height;
	const COLORREF axisColor = blend_color(textClr, bgClr, 0.5f);
	constexpr font_spec axisFont{ font_spec::FooterSize };
	for (const int mins : intervals)
	{
		const int tickPos = mins * timerGap;
		if (tickPos >= nWidth) continue;

		const int xPos = static_cast<int>(gR) - tickPos;
		auto label = std::format(L"{}m", mins);
		if (mins >= 60 && (mins % 60) == 0)
			label = std::format(L"{}h", mins / 60);

		rect_f labelRect(static_cast<float>(xPos - 14), gB - 22.0f, 28.0f, 14.0f);
		draw_text(label.c_str(), axisFont, labelRect, axisColor,
			align_hcenter);
	}
}

void draw_context::draw_pie_chart(const rect_f& rect,
	const float* segments, const COLORREF* segColors,
	const int numSegments,
	const COLORREF centerColor, const COLORREF bgColor)
{
	const int w = static_cast<int>(rect.Width);
	const int h = static_cast<int>(rect.Height);
	if (w <= 0 || h <= 0) return;

	const int chartDim = std::min(w, h);
	const int radius = chartDim / 2 - 2;
	if (radius < 8) return;

	const int cx = w / 2;
	const int cy = h / 2;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	std::vector<DWORD> buffer(w * h);
	auto* pixels = buffer.data();

	auto to_px = [](const COLORREF c) -> DWORD
		{
			return (static_cast<DWORD>(GetRValue(c)) << 16) |
				(static_cast<DWORD>(GetGValue(c)) << 8) |
				static_cast<DWORD>(GetBValue(c));
		};

	auto px_lerp = [](const DWORD a, const DWORD b, const int t256) -> DWORD
		{
			const int inv = 256 - t256;
			const DWORD r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * t256) >> 8;
			const DWORD g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * t256) >> 8;
			const DWORD bl = ((a & 0xFF) * inv + (b & 0xFF) * t256) >> 8;
			return (r << 16) | (g << 8) | bl;
		};

	auto px_lighten = [](const DWORD c, const float f) -> DWORD
		{
			const unsigned int ff = static_cast<unsigned int>(f * 40.0f);
			const unsigned int r = std::min(255u, ((static_cast<unsigned int>(c) >> 16) & 0xFFu) + ff);
			const unsigned int g = std::min(255u, ((static_cast<unsigned int>(c) >> 8) & 0xFFu) + ff);
			const unsigned int b = std::min(255u, (static_cast<unsigned int>(c) & 0xFFu) + ff);
			return (static_cast<DWORD>(r) << 16) | (static_cast<DWORD>(g) << 8) | static_cast<DWORD>(b);
		};

	// Build 64-slot color ring from segment proportions
	DWORD ring[64] = {};
	{
		float total = 0.0f;
		for (int i = 0; i < numSegments; i++) total += segments[i];
		if (total <= 0.0f) total = 1.0f;

		int slot = 0;
		for (int i = 0; i < numSegments && slot < 64; i++)
		{
			int count = static_cast<int>(std::round(segments[i] / total * 64.0));
			if (i == numSegments - 1) count = 64 - slot;
			const DWORD px = to_px(segColors[i]);
			for (int j = 0; j < count && slot < 64; j++)
				ring[slot++] = px;
		}
		if (slot > 0)
			while (slot < 64)
			{
				ring[slot] = ring[slot - 1];
				slot++;
			}
	}

	const DWORD bgPx = to_px(bgColor);
	const DWORD centerPx = to_px(centerColor);

	const int outerR1 = (radius - 1) * (radius - 1);
	const int outerR2 = radius * radius;
	const int outerDiff = outerR2 - outerR1;

	const int innerR = radius / 2;
	const int innerR1 = (innerR - 1) * (innerR - 1);
	const int innerR2 = innerR * innerR;
	const int innerDiff = innerR2 - innerR1;

	constexpr float dither[4][4] = {
		{1.0f / 64, 9.0f / 64, 3.0f / 64, 11.0f / 64},
		{13.0f / 64, 5.0f / 64, 15.0f / 64, 7.0f / 64},
		{4.0f / 64, 12.0f / 64, 2.0f / 64, 10.0f / 64},
		{16.0f / 64, 8.0f / 64, 14.0f / 64, 6.0f / 64}
	};

	constexpr double PI = std::numbers::pi;

	for (int y = 0; y < h; y++)
	{
		DWORD* line = pixels + y * w;
		const int pdy = y - cy;

		for (int x = 0; x < w; x++)
		{
			const int pdx = x - cx;
			const int r = pdx * pdx + pdy * pdy;

			DWORD c;

			if (r < innerR1)
			{
				c = centerPx;
				const float ff = (static_cast<float>(r) / static_cast<float>(outerR2) + dither[x % 4][y % 4]) * 0.25f;
				c = px_lighten(c, ff);
			}
			else if (r < outerR2)
			{
				const double i1 = (PI + atan2(static_cast<double>(pdy), static_cast<double>(pdx))) / PI * 32.0;
				const double i2 = (PI + atan2(static_cast<double>(pdy + 1), static_cast<double>(pdx + 1))) / PI * 32.0;

				const DWORD c1 = ring[static_cast<int>(i1) % 64];
				const DWORD c2 = ring[static_cast<int>(i2) % 64];

				c = (c1 == c2)
					? c1
					: px_lerp(c1, c2, static_cast<int>(std::clamp(i2 > i1 ? i2 - i1 : i1 - i2, 0.0, 1.0) * 255));

				if (r > outerR1)
					c = px_lerp(c, bgPx, MulDiv(r - outerR1, 255, outerDiff));
				else if (r < innerR2)
					c = px_lerp(centerPx, c, MulDiv(r - innerR1, 255, innerDiff));

				const float ff = (static_cast<float>(r) / static_cast<float>(outerR2) + dither[x % 4][y % 4]) * 0.25f;
				c = px_lighten(c, ff);
			}
			else
			{
				c = bgPx;
			}

			line[x] = c;
		}
	}

	SetDIBitsToDevice(_hdc, static_cast<int>(rect.X), static_cast<int>(rect.Y),
		w, h, 0, 0, 0, h, pixels, &bmi, DIB_RGB_COLORS);
}




inline INT_PTR CALLBACK dialog_proc(const HWND hDlg, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);

	switch (uMsg)
	{
	case WM_INITDIALOG:
		center_window(hDlg, GetParent(hDlg));
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
		case IDCANCEL:
		case ID_EXIT:
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}

	return FALSE;
}

inline int exit_dialog_show(const HINSTANCE hInstance, const HWND hParent)
{
	return static_cast<int>(DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_EXIT), hParent, dialog_proc, 0));
}

LRESULT main_frame::handle_close()
{
	switch (exit_dialog_show(_instance, _hwnd))
	{
	case IDOK:
		ShowWindow(_hwnd, SW_HIDE);
		break;

	case ID_EXIT:
		DestroyWindow(_hwnd);
		break;
	}

	return 0;
}

int WINAPI wWinMain(const HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*lpCmdLine*/, const int nCmdShow)
{
	const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hr))
		return 1;

	main_frame wndMain;
	if (wndMain.create(hInstance) == nullptr)
	{
		OutputDebugStringW(L"Main window creation failed.\n");
		CoUninitialize();
		return 1;
	}

	wndMain.show(nCmdShow);

	// Register for automatic restart after Windows update/reboot
	RegisterApplicationRestart(nullptr, RESTART_NO_PATCH);

	MSG msg = {};
	BOOL bRet;
	while ((bRet = GetMessageW(&msg, nullptr, 0, 0)) != 0)
	{
		if (bRet == -1)
		{
			OutputDebugStringW(L"GetMessageW returned an error.\n");
			break;
		}

		if (wndMain.pre_translate_message(msg))
			continue;

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	CoUninitialize();
	return static_cast<int>(msg.wParam);
}
