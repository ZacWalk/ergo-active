// ui.h - GDI drawing helpers, RAII wrappers, and Win32 window utilities.
// Provides scoped GDI object management, buffered painting, DPI-aware
// text/shape drawing, color blending, window centering, font creation,
// and the usage graph renderer.

#pragma once

#include "win.h"


inline constexpr UINT DefaultDpi = USER_DEFAULT_SCREEN_DPI;

inline std::wstring load_string(const HINSTANCE hInstance, const UINT resourceId)
{
	wchar_t buffer[256] = {0};
	const int count = LoadStringW(hInstance, resourceId, buffer, ARRAYSIZE(buffer));
	return std::wstring(buffer, count > 0 ? static_cast<size_t>(count) : 0);
}

inline UINT get_system_dpi()
{
	return GetDpiForSystem();
}

inline UINT get_window_dpi(const HWND hWnd)
{
	return IsWindow(hWnd) ? GetDpiForWindow(hWnd) : get_system_dpi();
}

inline int scale(const int value, const UINT dpi)
{
	return MulDiv(value, static_cast<int>(dpi), DefaultDpi);
}

inline float scale(const float value, const UINT dpi)
{
	return value * (static_cast<float>(dpi) / static_cast<float>(DefaultDpi));
}

inline UINT extract_dpi_from_wparam(const WPARAM wParam)
{
	return HIWORD(wParam);
}

inline void apply_suggested_window_rect(const HWND hWnd, const RECT& rect)
{
	SetWindowPos(hWnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
	             SWP_NOACTIVATE | SWP_NOZORDER);
}

inline void enable_non_client_dpi_scaling(const HWND hWnd)
{
	EnableNonClientDpiScaling(hWnd);
}

inline HFONT create_message_font(UINT dpi)
{
	NONCLIENTMETRICSW metrics = {0};
	metrics.cbSize = sizeof(metrics);

	if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
	{
		metrics.lfMessageFont = {0};
		metrics.lfMessageFont.lfHeight = -MulDiv(10, static_cast<int>(dpi), 72);
		metrics.lfMessageFont.lfWeight = FW_NORMAL;
		StringCchCopyW(metrics.lfMessageFont.lfFaceName, ARRAYSIZE(metrics.lfMessageFont.lfFaceName), L"Tahoma");
	}

	metrics.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;

	return CreateFontIndirectW(&metrics.lfMessageFont);
}

inline RECT get_placement_reference_rect(const HWND hParent)
{
	RECT referenceRect = {0};
	if (hParent != nullptr)
		GetWindowRect(hParent, &referenceRect);
	else
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &referenceRect, 0);

	return referenceRect;
}

inline void center_window(const HWND hWnd, const HWND hParent)
{
	RECT windowRect = {0};
	GetWindowRect(hWnd, &windowRect);

	const RECT referenceRect = get_placement_reference_rect(hParent);
	const int width = windowRect.right - windowRect.left;
	const int height = windowRect.bottom - windowRect.top;
	const int x = referenceRect.left + ((referenceRect.right - referenceRect.left) - width) / 2;
	const int y = referenceRect.top + ((referenceRect.bottom - referenceRect.top) - height) / 2;

	SetWindowPos(hWnd, nullptr, x, y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
}

inline void restore_and_show(const HWND hWnd, const bool activate = true)
{
	if (!IsWindow(hWnd))
		return;

	ShowWindow(hWnd, SW_SHOW);
	ShowWindow(hWnd, SW_RESTORE);
	if (activate)
		SetForegroundWindow(hWnd);
}

inline COLORREF blend_color(const COLORREF base, const COLORREF tint, const float amount)
{
	const float clampedAmount = std::clamp(amount, 0.0f, 1.0f);
	const auto blendChannel = [clampedAmount](const BYTE a, const BYTE b) -> BYTE
	{
		return static_cast<BYTE>(std::lround(
			(static_cast<float>(a) * (1.0f - clampedAmount)) + (static_cast<float>(b) * clampedAmount)));
	};

	return RGB(
		blendChannel(GetRValue(base), GetRValue(tint)),
		blendChannel(GetGValue(base), GetGValue(tint)),
		blendChannel(GetBValue(base), GetBValue(tint)));
}

class scoped_delete_object
{
public:
	explicit scoped_delete_object(const HGDIOBJ handle = nullptr) : _handle(handle)
	{
	}

	~scoped_delete_object()
	{
		reset();
	}

	scoped_delete_object(const scoped_delete_object&) = delete;
	scoped_delete_object& operator=(const scoped_delete_object&) = delete;
	scoped_delete_object(scoped_delete_object&&) = delete;
	scoped_delete_object& operator=(scoped_delete_object&&) = delete;

	HGDIOBJ get() const
	{
		return _handle;
	}

	void reset(const HGDIOBJ handle = nullptr)
	{
		if (_handle != nullptr)
			DeleteObject(_handle);

		_handle = handle;
	}

private:
	HGDIOBJ _handle;
};

class scoped_select_object
{
public:
	scoped_select_object(const HDC hdc, const HGDIOBJ object) : _hdc(hdc), _previous_object(nullptr)
	{
		if (_hdc != nullptr && object != nullptr)
			_previous_object = SelectObject(_hdc, object);
	}

	~scoped_select_object()
	{
		if (_hdc != nullptr && _previous_object != nullptr)
			SelectObject(_hdc, _previous_object);
	}

	scoped_select_object(const scoped_select_object&) = delete;
	scoped_select_object& operator=(const scoped_select_object&) = delete;
	scoped_select_object(scoped_select_object&&) = delete;
	scoped_select_object& operator=(scoped_select_object&&) = delete;

private:
	HDC _hdc;
	HGDIOBJ _previous_object;
};

class buffered_paint_surface
{
public:
	buffered_paint_surface(const HDC target, const int width, const int height) :
		_target(target),
		_buffer(CreateCompatibleDC(target)),
		_bitmap(CreateCompatibleBitmap(target, width, height)),
		_previous_bitmap(nullptr),
		_width(width),
		_height(height)
	{
		if (_buffer != nullptr && _bitmap != nullptr)
			_previous_bitmap = SelectObject(_buffer, _bitmap);
	}

	~buffered_paint_surface()
	{
		if (_buffer != nullptr && _previous_bitmap != nullptr)
			SelectObject(_buffer, _previous_bitmap);
		if (_bitmap != nullptr)
			DeleteObject(_bitmap);
		if (_buffer != nullptr)
			DeleteDC(_buffer);
	}

	buffered_paint_surface(const buffered_paint_surface&) = delete;
	buffered_paint_surface& operator=(const buffered_paint_surface&) = delete;
	buffered_paint_surface(buffered_paint_surface&&) = delete;
	buffered_paint_surface& operator=(buffered_paint_surface&&) = delete;

	bool is_valid() const
	{
		return _target != nullptr && _buffer != nullptr && _bitmap != nullptr;
	}

	HDC get_dc() const
	{
		return _buffer;
	}

	void present(const int x = 0, const int y = 0) const
	{
		if (is_valid())
			BitBlt(_target, x, y, _width, _height, _buffer, 0, 0, SRCCOPY);
	}

private:
	HDC _target;
	HDC _buffer;
	HBITMAP _bitmap;
	HGDIOBJ _previous_bitmap;
	int _width;
	int _height;
};

inline int get_dpi_y(const HDC hdc)
{
	return hdc != nullptr ? GetDeviceCaps(hdc, LOGPIXELSY) : static_cast<int>(DefaultDpi);
}

struct rect_f
{
	float X = 0.0f;
	float Y = 0.0f;
	float Width = 0.0f;
	float Height = 0.0f;

	rect_f() = default;

	rect_f(const float x, const float y, const float w, const float h) : X(x), Y(y), Width(w), Height(h)
	{
	}

	void inflate(const float dx, const float dy)
	{
		X -= dx;
		Y -= dy;
		Width += (dx * 2.0f);
		Height += (dy * 2.0f);
	}

	RECT to_rect() const
	{
		const RECT rc = {
			static_cast<LONG>(std::floor(X)),
			static_cast<LONG>(std::floor(Y)),
			static_cast<LONG>(std::ceil(X + Width)),
			static_cast<LONG>(std::ceil(Y + Height))
		};
		return rc;
	}
};

struct font_spec
{
	static constexpr int TitleSize = 28;
	static constexpr int LabelSize = 16;
	static constexpr int BodySize = 16;
	static constexpr int FooterSize = 9;

	static constexpr auto FaceName = L"Segoe UI";

	int size = BodySize;
	bool bold = false;
};

inline HFONT create_font_handle(const font_spec& font, const int dpiY)
{
	return CreateFontW(
		-MulDiv(font.size, dpiY, 72),
		0,
		0,
		0,
		font.bold ? FW_BOLD : FW_NORMAL,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		VARIABLE_PITCH | FF_SWISS,
		font_spec::FaceName);
}

enum text_align : unsigned
{
	align_left = 0x00,
	align_top = 0x00,
	align_hcenter = 0x01,
	align_right = 0x02,
	align_vcenter = 0x10,
	align_bottom = 0x20,
};

inline int measure_text_height(const HDC hdc, const LPCWSTR text, const font_spec& font, const UINT dpi = 0)
{
	if (hdc == nullptr || text == nullptr || *text == L'\0')
		return 0;

	const int dpiY = (dpi != 0) ? static_cast<int>(dpi) : get_dpi_y(hdc);
	const scoped_delete_object fontHandle(create_font_handle(font, dpiY));
	scoped_select_object selectFont(hdc, fontHandle.get());

	SIZE sz = {};
	GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz);
	return sz.cy;
}

inline void draw_text(const HDC hdc, const LPCWSTR text, const font_spec& font, const rect_f& rect,
                      const COLORREF color, const unsigned align = align_left, const UINT dpi = 0)
{
	if (hdc == nullptr || text == nullptr || *text == L'\0')
		return;

	const int dpiY = (dpi != 0) ? static_cast<int>(dpi) : get_dpi_y(hdc);
	const scoped_delete_object fontHandle(create_font_handle(font, dpiY));
	scoped_select_object selectFont(hdc, fontHandle.get());
	const int oldBkMode = SetBkMode(hdc, TRANSPARENT);
	const COLORREF oldColor = SetTextColor(hdc, color);

	const int len = static_cast<int>(wcslen(text));
	SIZE sz = {};
	GetTextExtentPoint32W(hdc, text, len, &sz);

	const RECT rc = rect.to_rect();
	int tx = rc.left;
	int ty = rc.top;

	if (align & align_hcenter)
		tx = rc.left + ((rc.right - rc.left) - sz.cx) / 2;
	else if (align & align_right)
		tx = rc.right - sz.cx;

	if (align & align_vcenter)
		ty = rc.top + ((rc.bottom - rc.top) - sz.cy) / 2;
	else if (align & align_bottom)
		ty = rc.bottom - sz.cy;

	ExtTextOutW(hdc, tx, ty, ETO_CLIPPED, &rc, text, static_cast<UINT>(len), nullptr);

	SetTextColor(hdc, oldColor);
	SetBkMode(hdc, oldBkMode);
}

inline void fill_rect(const HDC hdc, const rect_f& rect, const COLORREF color)
{
	const RECT rc = rect.to_rect();
	const scoped_delete_object brushHandle(CreateSolidBrush(color));
	FillRect(hdc, &rc, static_cast<HBRUSH>(brushHandle.get()));
}

inline void draw_rect(const HDC hdc, const rect_f& rect, const COLORREF color)
{
	const scoped_delete_object penHandle(CreatePen(PS_SOLID, 1, color));
	scoped_select_object selectPen(hdc, penHandle.get());
	scoped_select_object selectBrush(hdc, GetStockObject(NULL_BRUSH));

	const RECT rc = rect.to_rect();
	Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
}

class draw_context
{
public:
	draw_context(const HDC hdc, const UINT dpi) : _hdc(hdc), _dpi(dpi),
		_default_font(static_cast<HFONT>(GetCurrentObject(hdc, OBJ_FONT))),
		_default_pen(static_cast<HPEN>(GetCurrentObject(hdc, OBJ_PEN))),
		_default_brush(static_cast<HBRUSH>(GetCurrentObject(hdc, OBJ_BRUSH)))
	{
	}

	~draw_context()
	{
		SelectObject(_hdc, _default_font);
		SelectObject(_hdc, _default_pen);
		SelectObject(_hdc, _default_brush);

		for (auto& e : _font_cache) DeleteObject(e.handle);
		for (auto& e : _brush_cache) DeleteObject(e.handle);
		for (auto& e : _pen_cache) DeleteObject(e.handle);
	}

	draw_context(const draw_context&) = delete;
	draw_context& operator=(const draw_context&) = delete;
	draw_context(draw_context&&) = delete;
	draw_context& operator=(draw_context&&) = delete;

	HDC hdc() const { return _hdc; }
	UINT dpi() const { return _dpi; }

	int scale(const int value) const
	{
		return ::scale(value, _dpi);
	}

	float scale(const float value) const
	{
		return ::scale(value, _dpi);
	}

	void draw_text(const LPCWSTR text, const font_spec& font, const rect_f& rect,
	               const COLORREF color, const unsigned align = align_left)
	{
		if (_hdc == nullptr || text == nullptr || *text == L'\0')
			return;

		SelectObject(_hdc, get_or_create_font(font));
		const int oldBkMode = SetBkMode(_hdc, TRANSPARENT);
		const COLORREF oldColor = SetTextColor(_hdc, color);

		const int len = static_cast<int>(wcslen(text));
		SIZE sz = {};
		GetTextExtentPoint32W(_hdc, text, len, &sz);

		const RECT rc = rect.to_rect();
		int tx = rc.left;
		int ty = rc.top;

		if (align & align_hcenter)
			tx = rc.left + ((rc.right - rc.left) - sz.cx) / 2;
		else if (align & align_right)
			tx = rc.right - sz.cx;

		if (align & align_vcenter)
			ty = rc.top + ((rc.bottom - rc.top) - sz.cy) / 2;
		else if (align & align_bottom)
			ty = rc.bottom - sz.cy;

		ExtTextOutW(_hdc, tx, ty, ETO_CLIPPED, &rc, text, static_cast<UINT>(len), nullptr);

		SetTextColor(_hdc, oldColor);
		SetBkMode(_hdc, oldBkMode);
	}

	int measure_text_height(const LPCWSTR text, const font_spec& font)
	{
		if (_hdc == nullptr || text == nullptr || *text == L'\0')
			return 0;

		SelectObject(_hdc, get_or_create_font(font));

		SIZE sz = {};
		GetTextExtentPoint32W(_hdc, text, static_cast<int>(wcslen(text)), &sz);
		return sz.cy;
	}

	void fill_rect(const rect_f& rect, const COLORREF color)
	{
		const RECT rc = rect.to_rect();
		FillRect(_hdc, &rc, get_or_create_brush(color));
	}

	void draw_rect(const rect_f& rect, const COLORREF color)
	{
		SelectObject(_hdc, get_or_create_pen(PS_SOLID, 1, color));
		SelectObject(_hdc, GetStockObject(NULL_BRUSH));

		const RECT rc = rect.to_rect();
		Rectangle(_hdc, rc.left, rc.top, rc.right, rc.bottom);
	}

	void draw_rounded_panel(const rect_f& rect, const COLORREF color)
	{
		const int r = scale(6);
		const RECT rc = rect.to_rect();
		SelectObject(_hdc, get_or_create_brush(color));
		SelectObject(_hdc, get_or_create_pen(PS_SOLID, 1, blend_color(color, RGB(255, 255, 255), 0.06f)));
		RoundRect(_hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
	}

	void draw_usage_graph(const rect_f& rect,
	                      const COLORREF mouseClr, const COLORREF kbClr, const COLORREF breakClr,
	                      const COLORREF bgClr, const COLORREF textClr,
	                      const struct usage_tick* uses, const uint8_t* break_markers,
	                      const int maxUses, const int timerGap);

	void draw_pie_chart(const rect_f& rect,
	                    const float* segments, const COLORREF* segColors,
	                    const int numSegments,
	                    const COLORREF centerColor, const COLORREF bgColor);

private:
	struct font_cache_entry
	{
		int size;
		bool bold;
		HFONT handle;
	};

	struct brush_cache_entry
	{
		COLORREF color;
		HBRUSH handle;
	};

	struct pen_cache_entry
	{
		int style;
		int width;
		COLORREF color;
		HPEN handle;
	};

	HFONT get_or_create_font(const font_spec& font)
	{
		for (const auto& entry : _font_cache)
		{
			if (entry.size == font.size && entry.bold == font.bold)
				return entry.handle;
		}

		HFONT handle = create_font_handle(font, static_cast<int>(_dpi));
		if (handle == nullptr)
			handle = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
		_font_cache.push_back({font.size, font.bold, handle});
		return handle;
	}

	HBRUSH get_or_create_brush(const COLORREF color)
	{
		for (const auto& entry : _brush_cache)
		{
			if (entry.color == color)
				return entry.handle;
		}

		HBRUSH handle = CreateSolidBrush(color);
		if (handle == nullptr)
			return static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
		_brush_cache.push_back({color, handle});
		return handle;
	}

	HPEN get_or_create_pen(const int style, const int width, const COLORREF color)
	{
		for (const auto& entry : _pen_cache)
		{
			if (entry.style == style && entry.width == width && entry.color == color)
				return entry.handle;
		}

		HPEN handle = CreatePen(style, width, color);
		if (handle == nullptr)
			return static_cast<HPEN>(GetStockObject(BLACK_PEN));
		_pen_cache.push_back({style, width, color, handle});
		return handle;
	}

	HDC _hdc;
	UINT _dpi;
	HFONT _default_font;
	HPEN _default_pen;
	HBRUSH _default_brush;
	std::vector<font_cache_entry> _font_cache;
	std::vector<brush_cache_entry> _brush_cache;
	std::vector<pen_cache_entry> _pen_cache;
};

