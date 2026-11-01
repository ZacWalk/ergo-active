// dashboard-layout.h - Headless geometry for the dashboard controls.

#pragma once

#include <algorithm>
#include <cmath>

struct dashboard_rect
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	[[nodiscard]] int width() const { return right - left; }
	[[nodiscard]] int height() const { return bottom - top; }
	[[nodiscard]] bool contains(const int x, const int y) const
	{
		return x >= left && x <= right && y >= top && y <= bottom;
	}
};

struct dashboard_layout
{
	float margin = 0.0f;
	float content_width = 0.0f;
	float bar_y = 0.0f;
	float bar_height = 0.0f;
	float controls_y = 0.0f;
	float label_width = 0.0f;
	dashboard_rect slider;
	dashboard_rect edit;

	static int scale(const double dpi, const double value)
	{
		return static_cast<int>(std::lround(value * dpi));
	}

	static dashboard_layout measure(const int width, const int height, const double dpi)
	{
		dashboard_layout result;
		result.margin = static_cast<float>(scale(dpi, 12.0));
		result.content_width = static_cast<float>(width) - result.margin * 2.0f;
		result.bar_height = static_cast<float>(scale(dpi, 32.0));
		result.bar_y = static_cast<float>(height) - result.bar_height - scale(dpi, 8.0);
		result.label_width = static_cast<float>(scale(dpi, 130.0));

		const int slider_width = scale(dpi, 180.0);
		const int slider_height = scale(dpi, 28.0);
		const int edit_height = scale(dpi, 24.0);
		result.controls_y = result.bar_y + (result.bar_height - slider_height) / 2.0f;

		const int slider_x = static_cast<int>(result.margin + result.label_width);
		const int slider_y = static_cast<int>(result.controls_y);
		result.slider = {slider_x, slider_y, slider_x + slider_width, slider_y + slider_height};

		const int edit_x = slider_x + slider_width + scale(dpi, 4.0);
		const int edit_y = slider_y + (slider_height - edit_height) / 2;
		result.edit = {edit_x, edit_y, edit_x + scale(dpi, 48.0), edit_y + edit_height};
		return result;
	}
};
