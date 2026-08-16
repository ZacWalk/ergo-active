// ui-frame.h - Main application window (main_frame class). Owns the dark-themed
// dashboard UI with activity graph, stats panel, delay slider, system tray icon
// with color-coded urgency, break/eye-strain reminders, posture tips, daily
// history persistence, and micro-break tracking.

#pragma once

#include "win.h"

class main_frame
{
public:
	static constexpr int MinDelay = 20;
	static constexpr int MaxDelay = 120;
	static constexpr LPCWSTR AppVersionText = L"ergo-active 1.1";

	static LPCWSTR window_class_name()
	{
		return L"ergo-active-main-window";
	}

	// Broadcast by a second instance to surface the window that is already running.
	static UINT show_window_message()
	{
		static const UINT message = RegisterWindowMessageW(L"ergo-active-show-window");
		return message;
	}

	main_frame()
	{
		load_delay_setting();
	}

	// The window stores `this`, so the frame must stay put.
	main_frame(const main_frame&) = delete;
	main_frame& operator=(const main_frame&) = delete;
	main_frame(main_frame&&) = delete;
	main_frame& operator=(main_frame&&) = delete;

	~main_frame()
	{
		save_delay_setting();
		if (_control_font != nullptr)
			DeleteObject(_control_font);
		if (_brush_background != nullptr)
			DeleteObject(_brush_background);
		if (_brush_edit != nullptr)
			DeleteObject(_brush_edit);
	}

	HWND create(const HINSTANCE hInstance)
	{
		_instance = hInstance;
		register_window_class();

		constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
			WS_THICKFRAME | WS_CLIPCHILDREN;
		_dpi = get_system_dpi();
		RECT rc = {
			0, 0, scale(880),
			scale(460)
		};
		AdjustWindowRectExForDpi(&rc, style, FALSE, 0, _dpi);

		return CreateWindowExW(0, window_class_name(), L"ergo-active", style,
		                       CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr,
		                       hInstance, this);
	}

	void show(const int nCmdShow) const
	{
		ShowWindow(_hwnd, nCmdShow == SW_SHOWDEFAULT ? SW_SHOW : nCmdShow);
		UpdateWindow(_hwnd);
	}

	bool pre_translate_message(const MSG& msg)
	{
		if (msg.message != WM_KEYDOWN)
			return false;

		if (msg.hwnd != _hwnd && msg.hwnd != _slider)
			return false;

		switch (static_cast<int>(msg.wParam))
		{
		case VK_LEFT:
			set_delay(_delay - 1);
			return true;

		case VK_RIGHT:
			set_delay(_delay + 1);
			return true;
		}

		return false;
	}

private:
	usage_data _usage;
	daily_stats _daily;
	task_bar_icon _ti;
	HINSTANCE _instance = nullptr;
	HWND _hwnd = nullptr;
	HWND _edit = nullptr;
	HWND _slider = nullptr;
	HBRUSH _brush_background = nullptr;
	HBRUSH _brush_edit = nullptr;
	HFONT _control_font = nullptr;
	bool _setting = false;
	int _delay = 50;
	int _last_balloon = 0;
	UINT _dpi = DefaultDpi;
	DWORD _lastInputTick = 0;
	int64_t _keyboard_activity = 0;
	int64_t _mouse_activity = 0;
	POINT _lastMousePos = {};
	bool _locked = false;
	int _posture_tip_index = 0;
	int _ticks_since_flush = 0;

	static constexpr int FlushIntervalTicks = 5 * usage_data::TimerGap;

	static constexpr int IDC_DELAY_SLIDER = 0x1001;
	static constexpr int IDC_DELAY = 0x1002;
	static constexpr wchar_t RegKeyPath[] = L"Software\\ergo-active";

	static constexpr COLORREF BackgroundColor = RGB(0x1A, 0x1A, 0x2E);
	static constexpr COLORREF SurfaceColor = RGB(0x24, 0x24, 0x3A);
	static constexpr COLORREF AccentColor = RGB(0x3A, 0x9B, 0xDC);
	static constexpr COLORREF TextColor = RGB(0xE0, 0xE0, 0xE0);
	static constexpr COLORREF DimTextColor = RGB(0x80, 0x80, 0x98);
	static constexpr COLORREF GraphBorderColor = RGB(0x3A, 0x3A, 0x4A);
	static constexpr COLORREF GreenColor = RGB(0x4E, 0xC9, 0x6F);
	static constexpr COLORREF YellowColor = RGB(0xE0, 0xA0, 0x20);
	static constexpr COLORREF RedColor = RGB(0xE0, 0x40, 0x40);

	static constexpr int BaseEditWidth = 48;
	static constexpr int BaseEditHeight = 24;
	static constexpr int BaseSliderWidth = 180;
	static constexpr int BaseSliderHeight = 28;

	static constexpr LPCWSTR PostureTips[] = {
		L"\x2728 Roll your shoulders back and down",
		L"\x2728 Stretch your wrists \u2014 extend and flex",
		L"\x2728 Adjust your chair so feet are flat on the floor",
		L"\x2728 Blink deliberately \u2014 your eyes need moisture",
		L"\x2728 Stand up and stretch your legs",
		L"\x2728 Look away from the screen at something distant",
		L"\x2728 Relax your jaw and unclench your teeth",
		L"\x2728 Check your posture \u2014 sit up straight",
	};

	int scale(const int value) const
	{
		return ::scale(value, _dpi);
	}

	float scale(const float value) const
	{
		return ::scale(value, _dpi);
	}

	void update_control_font()
	{
		if (_control_font != nullptr)
			DeleteObject(_control_font);

		_control_font = create_message_font(_dpi);
		if (_edit != nullptr && _control_font != nullptr)
			SendMessageW(_edit, WM_SETFONT, reinterpret_cast<WPARAM>(_control_font), TRUE);
	}

	// Geometry shared by the layout pass and the painter, so the two cannot drift.
	struct layout_metrics
	{
		float margin = 0.0f;
		float content_width = 0.0f;
		float bar_y = 0.0f;
		float bar_h = 0.0f;
		float controls_y = 0.0f;
		float label_w = 0.0f;
		RECT slider = {};
		RECT edit = {};
	};

	layout_metrics measure(const RECT& client) const
	{
		layout_metrics m;
		m.margin = scale(12.0f);
		m.content_width = static_cast<float>(client.right - client.left) - m.margin * 2.0f;
		m.bar_h = scale(32.0f);
		m.bar_y = static_cast<float>(client.bottom) - m.bar_h - scale(8.0f);
		m.label_w = scale(130.0f);

		const int sliderW = scale(BaseSliderWidth);
		const int sliderH = scale(BaseSliderHeight);
		const int editH = scale(BaseEditHeight);

		m.controls_y = m.bar_y + (m.bar_h - static_cast<float>(sliderH)) / 2.0f;

		const int sliderX = static_cast<int>(static_cast<float>(client.left) + m.margin + m.label_w);
		const int sliderY = static_cast<int>(m.controls_y);
		m.slider = {sliderX, sliderY, sliderX + sliderW, sliderY + sliderH};

		const int editX = sliderX + sliderW + scale(4);
		const int editY = sliderY + (sliderH - editH) / 2;
		m.edit = {editX, editY, editX + scale(BaseEditWidth), editY + editH};
		return m;
	}

	void layout_controls() const
	{
		if (_slider == nullptr || _edit == nullptr)
			return;

		RECT client = {0};
		GetClientRect(_hwnd, &client);
		const layout_metrics m = measure(client);

		SetWindowPos(_slider, nullptr, m.slider.left, m.slider.top,
		             m.slider.right - m.slider.left, m.slider.bottom - m.slider.top,
		             SWP_NOACTIVATE | SWP_NOZORDER);
		SetWindowPos(_edit, nullptr, m.edit.left, m.edit.top,
		             m.edit.right - m.edit.left, m.edit.bottom - m.edit.top,
		             SWP_NOACTIVATE | SWP_NOZORDER);
	}

	void update_dpi(UINT dpi, const RECT* suggestedRect = nullptr)
	{
		if (dpi == 0)
			dpi = get_window_dpi(_hwnd);

		_dpi = dpi;
		if (suggestedRect != nullptr)
			apply_suggested_window_rect(_hwnd, *suggestedRect);

		update_control_font();
		layout_controls();
		update_background_brush();
		invalidate_window();
	}

	void register_window_class() const
	{
		WNDCLASSEXW wc = {0};
		if (GetClassInfoExW(_instance, window_class_name(), &wc))
			return;

		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = wnd_proc;
		wc.hInstance = _instance;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hIcon = static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDR_MAINFRAME), IMAGE_ICON, 32, 32,
		                                         LR_DEFAULTCOLOR));
		wc.hIconSm = static_cast<HICON>(LoadImageW(_instance, MAKEINTRESOURCEW(IDR_MAINFRAME), IMAGE_ICON,
		                                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
		                                           LR_DEFAULTCOLOR));
		wc.hbrBackground = nullptr;
		wc.lpszClassName = window_class_name();
		if (RegisterClassExW(&wc) == 0)
			return;
	}

	void load_delay_setting()
	{
		DWORD value = 0;
		DWORD size = sizeof(value);
		if (RegGetValueW(HKEY_CURRENT_USER, RegKeyPath, L"Delay", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
			ERROR_SUCCESS)
			_delay = std::clamp(static_cast<int>(value), MinDelay, MaxDelay);
	}

	void save_delay_setting() const
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, RegKeyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) ==
			ERROR_SUCCESS)
		{
			const DWORD value = static_cast<DWORD>(_delay);
			RegSetValueExW(hKey, L"Delay", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
			RegCloseKey(hKey);
		}
	}

	void invalidate_window() const
	{
		InvalidateRect(_hwnd, nullptr, FALSE);
	}

	int get_slider_pos() const
	{
		return static_cast<int>(SendMessageW(_slider, TBM_GETPOS, 0, 0));
	}

	void set_slider_pos(const int pos)
	{
		SendMessageW(_slider, TBM_SETPOS, TRUE, pos);
	}

	void update_background_brush()
	{
		if (_brush_background != nullptr)
			DeleteObject(_brush_background);

		_brush_background = CreateSolidBrush(BackgroundColor);

		if (_brush_edit != nullptr)
		{
			DeleteObject(_brush_edit);
			_brush_edit = nullptr;
		}
	}

	std::wstring format_break_message(const int lastBreakMinutes) const
	{
		std::wstring format = load_string(_instance, IDS_BREAK);
		if (format.empty())
			format = L"You have not had a 3 minute break for %d or more minutes!";

		wchar_t buffer[256] = {0};
		swprintf_s(buffer, format.c_str(), lastBreakMinutes);
		return buffer;
	}

	void set_text()
	{
		_setting = true;

		const int position = get_slider_pos();
		SetWindowTextW(_edit, std::to_wstring(position).c_str());
		_delay = position;

		_setting = false;
	}

	void set_delay(const int minutes)
	{
		set_slider_pos(std::clamp(minutes, MinDelay, MaxDelay));
		set_text();
		invalidate_window();
	}

	void poll_input()
	{
		LASTINPUTINFO lii{sizeof(LASTINPUTINFO)};
		if (GetLastInputInfo(&lii) && lii.dwTime != _lastInputTick)
		{
			_lastInputTick = lii.dwTime;

			POINT pt = {};
			if (GetCursorPos(&pt))
			{
				if (pt.x != _lastMousePos.x || pt.y != _lastMousePos.y)
				{
					_mouse_activity += 1;
					_lastMousePos = pt;
				}
				else
				{
					_keyboard_activity += 1;
				}
			}
			// GetCursorPos failed — skip this tick rather than misclassify
		}
	}

	void step()
	{
		// A locked session means the user is away: count it as time off the keyboard
		// so it can accrue towards a break instead of blocking one.
		const bool keyboard = !_locked && _keyboard_activity > 0;
		const bool mouse = !_locked && _mouse_activity > 0;

		const usage_data::step_events events = _usage.step(mouse ? _mouse_activity : 0,
		                                                   keyboard ? _keyboard_activity : 0);
		_last_balloon += 1;

		_daily.record_tick({
			.keyboard = keyboard,
			.mouse = mouse,
			.locked = _locked,
			.stretch_minutes = _usage.get_last_break_minutes(),
			.break_interval = _delay
		});

		if (events.break_completed)
			_daily.record_break();
		if (events.micro_pause)
			_daily.record_micro_pause();

		_keyboard_activity = 0;
		_mouse_activity = 0;
	}

	int get_last_break_mins() const
	{
		return _usage.get_last_break_minutes();
	}

	bool can_show_balloon() const
	{
		const int timeFromLastBalloon = _last_balloon / usage_data::TimerGap;
		return (timeFromLastBalloon > 3) && _usage.is_active();
	}

	LPCWSTR get_posture_tip()
	{
		constexpr int tipCount = sizeof(PostureTips) / sizeof(PostureTips[0]);
		const LPCWSTR tip = PostureTips[_posture_tip_index % tipCount];
		_posture_tip_index++;
		return tip;
	}

	void show_break_balloon(const int lastBreakMinutes)
	{
		const std::wstring message = format_break_message(lastBreakMinutes);
		const std::wstring tip = std::format(L"{}\n{}", message, get_posture_tip());
		_ti.show_balloon(L"ergo-active \u2014 Take a Break!", tip, message);
		_last_balloon = 0;
	}

	void show_eye_reminder()
	{
		_ti.show_balloon(L"ergo-active \u2014 20-20-20 Rule",
		                 L"Look at something 20 feet away for 20 seconds.\nYour eyes will thank you!",
		                 L"Eye break reminder");
		_last_balloon = 0;
	}

	void draw_stat_card(draw_context& ctx, const rect_f& rect, const LPCWSTR label, const LPCWSTR value,
	                    const COLORREF valueColor) const
	{
		ctx.draw_rounded_panel(rect, SurfaceColor);

		constexpr font_spec labelFont{font_spec::FooterSize};
		constexpr font_spec valueFont{font_spec::LabelSize, true};

		const float pad = scale(12.0f);
		const int labelH = ctx.measure_text_height(label, labelFont);
		const int valueH = ctx.measure_text_height(value, valueFont);

		const float gap = scale(2.0f);
		const float totalH = static_cast<float>(labelH) + gap + static_cast<float>(valueH);
		const float startY = rect.Y + (rect.Height - totalH) / 2.0f;

		const rect_f labelRect(rect.X + pad, startY, rect.Width - pad * 2, static_cast<float>(labelH));
		ctx.draw_text(label, labelFont, labelRect, DimTextColor, align_hcenter);

		const rect_f valueRect(rect.X + pad, startY + static_cast<float>(labelH) + gap,
		                       rect.Width - pad * 2, static_cast<float>(valueH));
		ctx.draw_text(value, valueFont, valueRect, valueColor, align_hcenter);
	}

	void paint(draw_context& ctx, const RECT& rect)
	{
		const layout_metrics lm = measure(rect);
		const auto& today = _daily.today();

		const float x = static_cast<float>(rect.left);
		const float y = static_cast<float>(rect.top);
		const float width = static_cast<float>(rect.right - rect.left);
		const float height = static_cast<float>(rect.bottom - rect.top);
		const float margin = lm.margin;
		const float contentWidth = lm.content_width;

		ctx.fill_rect(rect_f(x, y, width, height), BackgroundColor);

		// Consistent chart/pie colors
		constexpr COLORREF chartKbColor = RGB(0x3A, 0x9B, 0xDC); // blue
		constexpr COLORREF chartMouseColor = RGB(0xE8, 0x8D, 0x2A); // orange
		constexpr COLORREF chartBreakColor = RGB(0x4E, 0xC9, 0x6F); // green (= Machine On)

		// --- Bottom bar: slider label (left), micro-pauses, footer (right) ---
		constexpr font_spec footerFont(font_spec::FooterSize);
		const float bottomBarY = lm.bar_y;
		const float controlsH = static_cast<float>(lm.slider.bottom - lm.slider.top);

		rect_f sliderLabel(x + margin, lm.controls_y, lm.label_w, controlsH);
		ctx.draw_text(L"Break interval (min):", footerFont, sliderLabel,
		              DimTextColor, align_left | align_vcenter);

		wchar_t microStr[64] = {};
		swprintf_s(microStr, L"\u23F8 %d micro-pauses", today.micro_pauses);
		const float microX = static_cast<float>(lm.edit.right + scale(12));
		const float microW = x + margin + contentWidth * 0.55f - microX;
		if (microW > 40.0f)
		{
			rect_f microRect(microX, lm.controls_y, microW, controlsH);
			ctx.draw_text(microStr, footerFont, microRect,
			              DimTextColor, align_left | align_vcenter);
		}

		// Footer text (bottom-right)
		wchar_t footerBuf[128] = {};
		swprintf_s(footerBuf, L"%s \u2014 built %S", AppVersionText, __DATE__);
		rect_f footerRect(x + margin, bottomBarY, contentWidth, lm.bar_h);
		ctx.draw_text(footerBuf, footerFont, footerRect,
		              blend_color(DimTextColor, BackgroundColor, 0.3f), align_right | align_vcenter);

		// --- Title bar area (vertically centered) ---
		constexpr font_spec titleFont(font_spec::TitleSize, true);
		const float titleAreaH = scale(48.0f);
		const float titleY = y;
		const float titleBottom = y + margin + titleAreaH;

		rect_f titleRect(x + margin, titleY, contentWidth, titleBottom - titleY);
		ctx.draw_text(L"ergo-active", titleFont, titleRect, AccentColor,
		              align_left | align_vcenter);

		// Score badge (vertically centered in title area)
		COLORREF scoreColor = GreenColor;
		if (today.score < 50) scoreColor = RedColor;
		else if (today.score < 75) scoreColor = YellowColor;

		wchar_t scoreStr[32] = {};
		swprintf_s(scoreStr, L"Score: %d", today.score);
		constexpr font_spec scoreFont{font_spec::LabelSize, true};
		const float scoreW = static_cast<float>(ctx.measure_text_width(scoreStr, scoreFont)) + scale(2.0f);
		rect_f scoreRect(x + margin + contentWidth - scoreW, titleY, scoreW, titleBottom - titleY);
		ctx.draw_text(scoreStr, scoreFont, scoreRect, scoreColor,
		              align_right | align_vcenter);

		// --- Stats cards row ---
		float cardsY = titleBottom + scale(6.0f);
		const float cardHeight = scale(56.0f);
		const float cardGap = scale(6.0f);
		constexpr int numCards = 4;
		const float cardWidth = (contentWidth - cardGap * (numCards - 1)) / numCards;

		wchar_t activeStr[32] = {};
		swprintf_s(activeStr, L"%dh %dm", today.active_minutes / 60, today.active_minutes % 60);
		draw_stat_card(ctx, rect_f(x + margin, cardsY, cardWidth, cardHeight),
		               L"ACTIVE TODAY", activeStr, TextColor);

		const int minsUntil = _usage.get_minutes_until_warning(_delay);
		wchar_t countdownStr[16] = {};
		swprintf_s(countdownStr, L"%dm", minsUntil);
		const COLORREF countdownColor = minsUntil <= 5 ? RedColor : (minsUntil <= 10 ? YellowColor : GreenColor);
		draw_stat_card(ctx, rect_f(x + margin + (cardWidth + cardGap), cardsY, cardWidth, cardHeight),
		               L"NEXT BREAK", countdownStr, countdownColor);

		wchar_t breaksStr[16] = {};
		swprintf_s(breaksStr, L"%d", today.break_count);
		draw_stat_card(ctx, rect_f(x + margin + (cardWidth + cardGap) * 2, cardsY, cardWidth, cardHeight),
		               L"BREAKS", breaksStr, GreenColor);

		wchar_t stretchStr[16] = {};
		swprintf_s(stretchStr, L"%dm", today.longest_stretch);
		const COLORREF stretchColor = today.longest_stretch > _delay
			                              ? RedColor
			                              : (today.longest_stretch > _delay * 3 / 4 ? YellowColor : TextColor);
		draw_stat_card(ctx, rect_f(x + margin + (cardWidth + cardGap) * 3, cardsY, cardWidth, cardHeight),
		               L"MAX STRETCH", stretchStr, stretchColor);

		// --- Legend row (right-aligned, vertically centered labels with dots) ---
		const float graphAreaY = cardsY + cardHeight + scale(8.0f);
		constexpr font_spec labelFont(font_spec::FooterSize);
		const float legendH = scale(14.0f);
		const float dotSize = scale(6.0f);
		const float legendY = graphAreaY;

		const float legendRightX = x + margin + contentWidth;
		const float dotYCenter = legendY + (legendH - dotSize) / 2.0f;

		// Laid out right-to-left from measured widths so no label can clip.
		constexpr LPCWSTR legendLabels[] = {L"Mouse", L"Keyboard", L"Break"};
		constexpr COLORREF legendColors[] = {chartMouseColor, chartKbColor, chartBreakColor};
		const float dotGap = scale(3.0f);
		const float legendItemGap = scale(14.0f);
		float legendX = legendRightX;

		for (int i = 0; i < 3; i++)
		{
			const float textW = static_cast<float>(ctx.measure_text_width(legendLabels[i], labelFont));
			legendX -= textW;
			ctx.draw_text(legendLabels[i], labelFont, rect_f(legendX, legendY, textW, legendH),
			              DimTextColor, align_left | align_vcenter);

			legendX -= dotGap + dotSize;
			ctx.fill_rect(rect_f(legendX, dotYCenter, dotSize, dotSize), legendColors[i]);
			legendX -= legendItemGap;
		}

		// --- Content area: pie chart (25% width) + graph ---
		const float contentAreaY = legendY + legendH + scale(4.0f);
		const float contentAreaH = bottomBarY - contentAreaY - scale(4.0f);
		if (contentAreaH <= 0.0f) return;

		const float pieChartWidth = contentWidth * 0.25f;
		const float graphPieGap = scale(8.0f);
		const float graphWidth = contentWidth - pieChartWidth - graphPieGap;

		// Pie chart panel (left, 25% of client width)
		if (contentAreaH > 0.0f && pieChartWidth > 0.0f)
		{
			const float pieX = x + margin;
			rect_f pieOuter(pieX, contentAreaY, pieChartWidth, contentAreaH);
			ctx.draw_rounded_panel(pieOuter, SurfaceColor);

			const auto& rec = today;
			const float kb = static_cast<float>(rec.keyboard_ticks);
			const float ms = static_cast<float>(rec.mouse_ticks);
			const float idle = static_cast<float>(rec.idle_ticks);
			const float locked = static_cast<float>(rec.locked_ticks);

			const time_t now = time(nullptr);
			tm lt = {};
			localtime_s(&lt, &now);
			const float elapsed_ticks = static_cast<float>(lt.tm_hour * 60 + lt.tm_min) * usage_data::TimerGap;
			const float tracked = kb + ms + idle + locked;
			const float off = std::max(0.0f, elapsed_ticks - tracked);

			constexpr COLORREF PieKbColor = chartKbColor;
			constexpr COLORREF PieMouseColor = chartMouseColor;
			constexpr COLORREF PieIdleColor = chartBreakColor; // green (= Break)
			constexpr COLORREF PieLockedColor = RGB(0xE0, 0xA0, 0x20);
			constexpr COLORREF PieOffColor = RGB(0x30, 0x30, 0x42);

			const float segments[] = {kb, ms, idle, locked, off};
			constexpr COLORREF colors[] = {PieKbColor, PieMouseColor, PieIdleColor, PieLockedColor, PieOffColor};

			const float piePad = scale(8.0f);
			const float pieLegendH = scale(72.0f);
			const float pieAreaH = contentAreaH - piePad * 2 - pieLegendH;
			const float pieSize = std::min(pieChartWidth - piePad * 2, pieAreaH);

			if (pieSize > 20.0f)
			{
				const float pieCX = pieX + (pieChartWidth - pieSize) / 2.0f;
				const float pieCY = contentAreaY + piePad;
				ctx.draw_pie_chart(rect_f(pieCX, pieCY, pieSize, pieSize),
				                   segments, colors, 5, SurfaceColor, SurfaceColor);

				// Pie legend (centered horizontally below chart)
				constexpr font_spec pieLabelFont{font_spec::FooterSize};
				const float legDotSize = scale(6.0f);
				const float legRowH = scale(12.0f);
				float legY = pieCY + pieSize + scale(6.0f);

				constexpr LPCWSTR labels[] = {L"Keyboard", L"Mouse", L"Machine On", L"Locked", L"Off"};
				const float total_segs = kb + ms + idle + locked + off;

				// Format up front so the block can be sized to its widest row.
				wchar_t rows[5][64] = {};
				float widestRow = 0.0f;
				for (int i = 0; i < 5; i++)
				{
					const int pct = (total_segs > 0)
						                ? static_cast<int>(segments[i] / total_segs * 100.0f + 0.5f)
						                : 0;
					swprintf_s(rows[i], L"%s %d%%", labels[i], pct);
					widestRow = std::max(widestRow, static_cast<float>(ctx.measure_text_width(rows[i], pieLabelFont)));
				}

				const float legTextX = legDotSize + scale(3.0f);
				const float legItemW = std::min(legTextX + widestRow, pieChartWidth - scale(4.0f));
				const float legStartX = pieX + (pieChartWidth - legItemW) / 2.0f;

				for (int i = 0; i < 5; i++)
				{
					const float dotYC = legY + (legRowH - legDotSize) / 2.0f;
					ctx.fill_rect(rect_f(legStartX, dotYC, legDotSize, legDotSize), colors[i]);

					rect_f lblRect(legStartX + legTextX, legY, legItemW - legTextX, legRowH);
					ctx.draw_text(rows[i], pieLabelFont, lblRect, DimTextColor,
					              align_left | align_vcenter);

					legY += legRowH + scale(1.0f);
				}
			}
		}

		// Graph panel (right)
		if (contentAreaH > 0.0f && graphWidth > 0.0f)
		{
			const float graphX = x + margin + pieChartWidth + graphPieGap;
			rect_f graphOuter(graphX, contentAreaY, graphWidth, contentAreaH);
			ctx.draw_rounded_panel(graphOuter, SurfaceColor);

			rect_f graphInner(graphX + scale(4.0f), contentAreaY + scale(4.0f),
			                  graphWidth - scale(8.0f), contentAreaH - scale(8.0f));
			ctx.draw_usage_graph(graphInner, chartMouseColor, chartKbColor, chartBreakColor,
			                     BackgroundColor, TextColor,
			                     _usage.get_uses(), _usage.get_break_markers(),
			                     usage_data::MaxUses, usage_data::TimerGap);
		}
	}

	LRESULT handle_create()
	{
		_dpi = get_window_dpi(_hwnd);
		center_window(_hwnd, nullptr);
		apply_dark_titlebar(_hwnd);

		_ti.install(_instance, _hwnd, 1, IDR_MAINFRAME);

		_edit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_NUMBER | WS_TABSTOP, 0, 0,
		                        scale(BaseEditWidth), scale(BaseEditHeight), _hwnd,
		                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DELAY)), _instance, nullptr);

		_slider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
		                          scale(BaseSliderWidth), scale(BaseSliderHeight), _hwnd,
		                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_DELAY_SLIDER)), _instance, nullptr);

		if (_edit == nullptr || _slider == nullptr)
			return -1;

		SendMessageW(_edit, EM_SETLIMITTEXT, 3, 0);
		update_control_font();
		layout_controls();
		SendMessageW(_slider, TBM_SETRANGE, TRUE, MAKELPARAM(MinDelay, MaxDelay));
		set_slider_pos(_delay);
		set_text();

		// Seed the input baseline so the first tick is not counted as a keystroke.
		LASTINPUTINFO lii{sizeof(LASTINPUTINFO)};
		if (GetLastInputInfo(&lii))
			_lastInputTick = lii.dwTime;
		GetCursorPos(&_lastMousePos);

		if (SetTimer(_hwnd, 1, (1000 * 60) / usage_data::TimerGap, nullptr) == 0)
			return -1;
		update_background_brush();
		WTSRegisterSessionNotification(_hwnd, NOTIFY_FOR_THIS_SESSION);
		return 0;
	}

	LRESULT handle_paint()
	{
		PAINTSTRUCT ps = {nullptr};
		const HDC hdc = BeginPaint(_hwnd, &ps);
		if (hdc == nullptr)
			return 0;

		RECT rect = {0};
		GetClientRect(_hwnd, &rect);
		const int width = rect.right - rect.left;
		const int height = rect.bottom - rect.top;
		if (width > 0 && height > 0)
		{
			const buffered_paint_surface buffer(hdc, width, height);
			if (buffer.is_valid())
			{
				draw_context ctx(buffer.get_dc(), _dpi);
				paint(ctx, rect);
				buffer.present();
			}
		}

		EndPaint(_hwnd, &ps);
		return 0;
	}

	LRESULT handle_control_color(const HDC hdc)
	{
		SetBkMode(hdc, TRANSPARENT);
		return reinterpret_cast<LRESULT>(_brush_background);
	}

	LRESULT handle_control_color_edit(const HDC hdc)
	{
		SetTextColor(hdc, TextColor);
		SetBkColor(hdc, SurfaceColor);
		if (_brush_edit == nullptr)
			_brush_edit = CreateSolidBrush(SurfaceColor);
		return reinterpret_cast<LRESULT>(_brush_edit);
	}

	LRESULT handle_command_message(const WORD commandId, const WORD notifyCode)
	{
		switch (commandId)
		{
		case ID_APP_EXIT:
			PostMessageW(_hwnd, WM_CLOSE, 0, 0);
			return 0;

		case IDC_ST_RESTORE:
			restore_and_show(_hwnd, false);
			return 0;

		case IDC_DELAY:
			if (_setting)
				return 0;

			if (notifyCode == EN_CHANGE)
			{
				wchar_t text[32] = {0};
				GetWindowTextW(_edit, text, ARRAYSIZE(text));
				_delay = std::clamp(_wtoi(text), MinDelay, MaxDelay);
				set_slider_pos(_delay);
				invalidate_window();
			}
			else if (notifyCode == EN_KILLFOCUS)
			{
				// Show the value that was actually accepted, not what was typed.
				set_text();
			}
			return 0;
		}

		return 0;
	}

	LRESULT handle_close();

	LRESULT handle_timer()
	{
		poll_input();
		step();

		if (can_show_balloon())
		{
			// The break warning outranks the eye reminder; never fire both at once.
			const int lastBreakMinutes = get_last_break_mins();
			if (lastBreakMinutes > _delay)
				show_break_balloon(lastBreakMinutes);
			else if (_usage.should_show_eye_reminder())
				show_eye_reminder();
		}

		// Update tray icon color based on urgency
		_ti.update_urgency(_usage.get_urgency_level(_delay));

		const int overdue = get_last_break_mins() - _delay;
		wchar_t tip[128] = {};
		if (overdue > 0)
			swprintf_s(tip, L"ergo-active \u2014 Break overdue by %dm | Score: %d", overdue, _daily.today().score);
		else
			swprintf_s(tip, L"ergo-active \u2014 Next break in %dm | Score: %d", -overdue, _daily.today().score);
		_ti.update_tooltip(tip);

		// Persist periodically so an unclean shutdown loses minutes, not a day.
		if (++_ticks_since_flush >= FlushIntervalTicks)
		{
			_ticks_since_flush = 0;
			_usage.flush();
			_daily.flush();
		}

		if (IsWindowVisible(_hwnd))
			invalidate_window();
		return 0;
	}

	LRESULT handle_hscroll(const HWND hScroll)
	{
		if (hScroll == _slider)
		{
			set_text();
			invalidate_window();
		}
		return 0;
	}

	LRESULT handle_notify(const WPARAM wParam, const LPARAM lParam)
	{
		const auto* nmhdr = reinterpret_cast<const NMHDR*>(lParam);
		if (nmhdr->hwndFrom == _slider && nmhdr->code == NM_CUSTOMDRAW)
		{
			const auto* nmcd = reinterpret_cast<const NMCUSTOMDRAW*>(lParam);
			switch (nmcd->dwDrawStage)
			{
			case CDDS_PREPAINT:
				return CDRF_NOTIFYITEMDRAW;
			case CDDS_ITEMPREPAINT:
				switch (nmcd->dwItemSpec)
				{
				case TBCD_CHANNEL:
					{
						const RECT rc = nmcd->rc;
						const scoped_delete_object bgBrush(CreateSolidBrush(RGB(0x30, 0x30, 0x42)));
						FillRect(nmcd->hdc, &rc, static_cast<HBRUSH>(bgBrush.get()));
						RECT thumbRc = {};
						SendMessageW(_slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumbRc));
						RECT fillRc = rc;
						fillRc.right = (thumbRc.left + thumbRc.right) / 2;
						if (fillRc.right > fillRc.left)
						{
							const scoped_delete_object fillBrush(CreateSolidBrush(AccentColor));
							FillRect(nmcd->hdc, &fillRc, static_cast<HBRUSH>(fillBrush.get()));
						}
						return CDRF_SKIPDEFAULT;
					}
				case TBCD_THUMB:
					{
						const scoped_delete_object brush(CreateSolidBrush(AccentColor));
						const scoped_delete_object pen(CreatePen(PS_SOLID, 1,
						                                         blend_color(AccentColor, RGB(255, 255, 255), 0.2f)));
						scoped_select_object selBrush(nmcd->hdc, brush.get());
						scoped_select_object selPen(nmcd->hdc, pen.get());
						RoundRect(nmcd->hdc, nmcd->rc.left, nmcd->rc.top,
						          nmcd->rc.right, nmcd->rc.bottom, 4, 4);
						return CDRF_SKIPDEFAULT;
					}
				case TBCD_TICS:
					return CDRF_SKIPDEFAULT;
				}
				break;
			}
		}
		return DefWindowProcW(_hwnd, WM_NOTIFY, wParam, lParam);
	}

	LRESULT handle_destroy()
	{
		WTSUnRegisterSessionNotification(_hwnd);
		KillTimer(_hwnd, 1);
		_ti.uninstall();
		PostQuitMessage(0);
		return 0;
	}

	LRESULT handle_dpi_changed(const WPARAM wParam, const LPARAM lParam)
	{
		update_dpi(extract_dpi_from_wparam(wParam), reinterpret_cast<const RECT*>(lParam));
		return 0;
	}

	static LRESULT CALLBACK wnd_proc(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		main_frame* self = nullptr;
		if (uMsg == WM_NCCREATE)
		{
			const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
			self = static_cast<main_frame*>(createStruct->lpCreateParams);
			self->_hwnd = hWnd;
			enable_non_client_dpi_scaling(hWnd);
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		else
		{
			self = reinterpret_cast<main_frame*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		}

		if (self != nullptr)
			return self->handle_message(uMsg, wParam, lParam);

		return DefWindowProcW(hWnd, uMsg, wParam, lParam);
	}

	LRESULT handle_message(const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		LRESULT trayResult = 0;
		if (_ti.handle_message(uMsg, lParam, trayResult))
			return trayResult;

		if (uMsg != 0 && uMsg == show_window_message())
		{
			restore_and_show(_hwnd);
			return 0;
		}

		switch (uMsg)
		{
		case WM_CREATE:
			return handle_create();

		case WM_CLOSE:
			return handle_close();

		case WM_DESTROY:
			return handle_destroy();

		case WM_NCDESTROY:
			SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
			_hwnd = nullptr;
			break;

		case WM_PAINT:
			return handle_paint();

		case WM_ERASEBKGND:
			return 1;

		case WM_SIZE:
			if (wParam == SIZE_MINIMIZED)
			{
				ShowWindow(_hwnd, SW_HIDE);
				return 0;
			}
			layout_controls();
			invalidate_window();
			return 0;

		case WM_GETMINMAXINFO:
			{
				auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
				mmi->ptMinTrackSize.x = scale(700);
				mmi->ptMinTrackSize.y = scale(420);
				return 0;
			}

		case WM_DPICHANGED:
			return handle_dpi_changed(wParam, lParam);

		case WM_TIMER:
			return handle_timer();

		case WM_HSCROLL:
			return handle_hscroll(reinterpret_cast<HWND>(lParam));

		case WM_CTLCOLORSTATIC:
			return handle_control_color(reinterpret_cast<HDC>(wParam));

		case WM_CTLCOLOREDIT:
			return handle_control_color_edit(reinterpret_cast<HDC>(wParam));

		case WM_NOTIFY:
			return handle_notify(wParam, lParam);

		case WM_COMMAND:
			return handle_command_message(LOWORD(wParam), HIWORD(wParam));

		case WM_ENDSESSION:
			if (wParam)
			{
				_usage.flush();
				_daily.flush();
				save_delay_setting();
			}
			return 0;

		case WM_WTSSESSION_CHANGE:
			if (wParam == WTS_SESSION_LOCK)
				_locked = true;
			else if (wParam == WTS_SESSION_UNLOCK)
				_locked = false;
			return 0;

		default:
			break;
		}

		return DefWindowProcW(_hwnd, uMsg, wParam, lParam);
	}
};
