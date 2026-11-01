// win-services.h - The explicitly app-native Windows monitoring seam.
//
// GetLastInputInfo and the session state are domain inputs that do not belong
// in libdesktop's generic GUI or persistence services.  Tray, settings and
// files are deliberately absent: desktop-services.h routes those through
// libdesktop.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT_WIN10
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#endif

#include "app-services.h"

#include <windows.h>

class win_input_monitor final : public input_monitor
{
public:
	win_input_monitor()
	{
		seed();
	}

	input_sample sample() override
	{
		LASTINPUTINFO last_input{sizeof(LASTINPUTINFO)};
		if (!GetLastInputInfo(&last_input) || last_input.dwTime == _last_input_tick)
			return {};

		// Keep the old sampling contract: a changed last-input tick is counted
		// once, and a failed cursor query is not misclassified as keyboard input.
		_last_input_tick = last_input.dwTime;

		POINT cursor = {};
		if (!GetCursorPos(&cursor))
			return {};

		if (cursor.x != _last_mouse.x || cursor.y != _last_mouse.y)
		{
			_last_mouse = cursor;
			return {.mouse = 1};
		}

		return {.keyboard = 1};
	}

private:
	void seed()
	{
		LASTINPUTINFO last_input{sizeof(LASTINPUTINFO)};
		if (GetLastInputInfo(&last_input))
			_last_input_tick = last_input.dwTime;
		GetCursorPos(&_last_mouse);
	}

	DWORD _last_input_tick = 0;
	POINT _last_mouse = {};
};

class win_session_state final : public session_monitor
{
public:
	void poll() override
	{
		const HDESK desktop = OpenInputDesktop(0, FALSE, GENERIC_READ);
		if (desktop == nullptr)
			return;

		wchar_t name[64] = {};
		DWORD bytes = 0;
		if (GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes))
			_locked = wcscmp(name, L"Winlogon") == 0;
		CloseDesktop(desktop);
	}

	bool is_locked() const override { return _locked; }
	void set_locked(const bool locked) override { _locked = locked; }

private:
	bool _locked = false;
};
