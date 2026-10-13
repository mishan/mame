// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  webworkermain.cpp - headless OSD for an Emscripten Web Worker
//
//  The emulator runs MAME's normal blocking loop inside a Web
//  Worker.  The page supplies input and receives frames through
//  JavaScript hooks on the Module object:
//
//  Module.osdFrame(seconds)
//      called once per emulated frame with the emulated time;
//      returns true if the host wants the next frame's pixels
//  Module.osdPresent(pixels, width, height)
//      receives a frame as a Uint8Array view of RGBX pixels
//      (alpha is undefined) that is only valid during the call
//  Module.osdPollInput(events)
//      fills an Int32Array with (type, a, b) triples and returns
//      the number of values written:
//        1: key, a = index into Module.osdKeys, b = pressed
//        2: mouse motion, a = dx, b = dy
//        3: mouse button, a = DOM button number, b = pressed
//        4: mouse wheel, a = steps (positive is down)
//  Module.osdKeys
//      set by the OSD to the KeyboardEvent.code names it knows
//
//============================================================

#include "emu.h"
#include "emuopts.h"
#include "input.h"
#include "main.h"
#include "render.h"
#include "rendersw.hxx"

#include "interface/inputdev.h"
#include "modules/diagnostics/diagnostics_module.h"
#include "modules/lib/osdobj_common.h"
#include "modules/osdmodule.h"
#include "modules/render/render_module.h"

#include <emscripten.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <vector>


namespace {

EM_JS(bool, js_frame, (double seconds), {
	return Module.osdFrame ? !!Module.osdFrame(seconds) : false;
});

EM_JS(void, js_present, (void const *pixels, int width, int height), {
	if (Module.osdPresent)
		Module.osdPresent(HEAPU8.subarray(pixels, pixels + width * height * 4), width, height);
});

EM_JS(int, js_poll_input, (std::int32_t *events, int count), {
	return Module.osdPollInput ? Module.osdPollInput(HEAP32.subarray(events >> 2, (events >> 2) + count)) : 0;
});

EM_JS(void, js_add_key, (int index, char const *code, int length), {
	(Module.osdKeys ||= [])[index] = new TextDecoder().decode(HEAPU8.slice(code, code + length));
});


constexpr int MOUSE_BUTTONS = 5;

enum
{
	EVENT_KEY = 1,
	EVENT_MOUSE_MOTION,
	EVENT_MOUSE_BUTTON,
	EVENT_MOUSE_WHEEL
};


struct key_info
{
	char const *code; // KeyboardEvent.code
	input_item_id item;
	char const *name;
};

const key_info f_keys[] =
{
	{ "KeyA", ITEM_ID_A, "A" }, { "KeyB", ITEM_ID_B, "B" }, { "KeyC", ITEM_ID_C, "C" }, { "KeyD", ITEM_ID_D, "D" },
	{ "KeyE", ITEM_ID_E, "E" }, { "KeyF", ITEM_ID_F, "F" }, { "KeyG", ITEM_ID_G, "G" }, { "KeyH", ITEM_ID_H, "H" },
	{ "KeyI", ITEM_ID_I, "I" }, { "KeyJ", ITEM_ID_J, "J" }, { "KeyK", ITEM_ID_K, "K" }, { "KeyL", ITEM_ID_L, "L" },
	{ "KeyM", ITEM_ID_M, "M" }, { "KeyN", ITEM_ID_N, "N" }, { "KeyO", ITEM_ID_O, "O" }, { "KeyP", ITEM_ID_P, "P" },
	{ "KeyQ", ITEM_ID_Q, "Q" }, { "KeyR", ITEM_ID_R, "R" }, { "KeyS", ITEM_ID_S, "S" }, { "KeyT", ITEM_ID_T, "T" },
	{ "KeyU", ITEM_ID_U, "U" }, { "KeyV", ITEM_ID_V, "V" }, { "KeyW", ITEM_ID_W, "W" }, { "KeyX", ITEM_ID_X, "X" },
	{ "KeyY", ITEM_ID_Y, "Y" }, { "KeyZ", ITEM_ID_Z, "Z" },
	{ "Digit0", ITEM_ID_0, "0" }, { "Digit1", ITEM_ID_1, "1" }, { "Digit2", ITEM_ID_2, "2" }, { "Digit3", ITEM_ID_3, "3" },
	{ "Digit4", ITEM_ID_4, "4" }, { "Digit5", ITEM_ID_5, "5" }, { "Digit6", ITEM_ID_6, "6" }, { "Digit7", ITEM_ID_7, "7" },
	{ "Digit8", ITEM_ID_8, "8" }, { "Digit9", ITEM_ID_9, "9" },
	{ "F1", ITEM_ID_F1, "F1" }, { "F2", ITEM_ID_F2, "F2" }, { "F3", ITEM_ID_F3, "F3" }, { "F4", ITEM_ID_F4, "F4" },
	{ "F5", ITEM_ID_F5, "F5" }, { "F6", ITEM_ID_F6, "F6" }, { "F7", ITEM_ID_F7, "F7" }, { "F8", ITEM_ID_F8, "F8" },
	{ "F9", ITEM_ID_F9, "F9" }, { "F10", ITEM_ID_F10, "F10" }, { "F11", ITEM_ID_F11, "F11" }, { "F12", ITEM_ID_F12, "F12" },
	{ "Escape", ITEM_ID_ESC, "Esc" },
	{ "Backquote", ITEM_ID_TILDE, "`" },
	{ "Minus", ITEM_ID_MINUS, "-" },
	{ "Equal", ITEM_ID_EQUALS, "=" },
	{ "Backspace", ITEM_ID_BACKSPACE, "Backspace" },
	{ "Tab", ITEM_ID_TAB, "Tab" },
	{ "BracketLeft", ITEM_ID_OPENBRACE, "[" },
	{ "BracketRight", ITEM_ID_CLOSEBRACE, "]" },
	{ "Enter", ITEM_ID_ENTER, "Enter" },
	{ "Semicolon", ITEM_ID_COLON, ";" },
	{ "Quote", ITEM_ID_QUOTE, "'" },
	{ "Backslash", ITEM_ID_BACKSLASH, "\\" },
	{ "IntlBackslash", ITEM_ID_BACKSLASH2, "<" },
	{ "Comma", ITEM_ID_COMMA, "," },
	{ "Period", ITEM_ID_STOP, "." },
	{ "Slash", ITEM_ID_SLASH, "/" },
	{ "Space", ITEM_ID_SPACE, "Space" },
	{ "Insert", ITEM_ID_INSERT, "Insert" },
	{ "Delete", ITEM_ID_DEL, "Delete" },
	{ "Home", ITEM_ID_HOME, "Home" },
	{ "End", ITEM_ID_END, "End" },
	{ "PageUp", ITEM_ID_PGUP, "Page Up" },
	{ "PageDown", ITEM_ID_PGDN, "Page Down" },
	{ "ArrowLeft", ITEM_ID_LEFT, "Left" },
	{ "ArrowRight", ITEM_ID_RIGHT, "Right" },
	{ "ArrowUp", ITEM_ID_UP, "Up" },
	{ "ArrowDown", ITEM_ID_DOWN, "Down" },
	{ "Numpad0", ITEM_ID_0_PAD, "0 PAD" }, { "Numpad1", ITEM_ID_1_PAD, "1 PAD" }, { "Numpad2", ITEM_ID_2_PAD, "2 PAD" },
	{ "Numpad3", ITEM_ID_3_PAD, "3 PAD" }, { "Numpad4", ITEM_ID_4_PAD, "4 PAD" }, { "Numpad5", ITEM_ID_5_PAD, "5 PAD" },
	{ "Numpad6", ITEM_ID_6_PAD, "6 PAD" }, { "Numpad7", ITEM_ID_7_PAD, "7 PAD" }, { "Numpad8", ITEM_ID_8_PAD, "8 PAD" },
	{ "Numpad9", ITEM_ID_9_PAD, "9 PAD" },
	{ "NumpadDivide", ITEM_ID_SLASH_PAD, "/ PAD" },
	{ "NumpadMultiply", ITEM_ID_ASTERISK, "* PAD" },
	{ "NumpadSubtract", ITEM_ID_MINUS_PAD, "- PAD" },
	{ "NumpadAdd", ITEM_ID_PLUS_PAD, "+ PAD" },
	{ "NumpadDecimal", ITEM_ID_DEL_PAD, ". PAD" },
	{ "NumpadEnter", ITEM_ID_ENTER_PAD, "Enter PAD" },
	{ "NumpadEqual", ITEM_ID_EQUALS_PAD, "= PAD" },
	{ "PrintScreen", ITEM_ID_PRTSCR, "Print Screen" },
	{ "Pause", ITEM_ID_PAUSE, "Pause" },
	{ "ShiftLeft", ITEM_ID_LSHIFT, "Left Shift" },
	{ "ShiftRight", ITEM_ID_RSHIFT, "Right Shift" },
	{ "ControlLeft", ITEM_ID_LCONTROL, "Left Ctrl" },
	{ "ControlRight", ITEM_ID_RCONTROL, "Right Ctrl" },
	{ "AltLeft", ITEM_ID_LALT, "Left Alt" },
	{ "AltRight", ITEM_ID_RALT, "Right Alt" },
	{ "ScrollLock", ITEM_ID_SCRLOCK, "Scroll Lock" },
	{ "NumLock", ITEM_ID_NUMLOCK, "Num Lock" },
	{ "CapsLock", ITEM_ID_CAPSLOCK, "Caps Lock" },
	{ "MetaLeft", ITEM_ID_LWIN, "Left Meta" },
	{ "MetaRight", ITEM_ID_RWIN, "Right Meta" },
	{ "ContextMenu", ITEM_ID_MENU, "Menu" },
};


template <typename T>
s32 get_state(void *device_internal, void *item_internal)
{
	return *reinterpret_cast<T const *>(item_internal);
}


class webworker_osd_interface : public osd_common_t
{
public:
	webworker_osd_interface(osd_options &options) : osd_common_t(options) { }

	virtual void init(running_machine &machine) override
	{
		osd_common_t::init(machine);
		osd_common_t::init_subsystems();
	}

	virtual void update(bool skip_redraw) override
	{
		osd_common_t::update(skip_redraw);

		if (!js_frame(machine().time().as_double()) || skip_redraw)
			return;

		s32 width, height;
		m_target->compute_minimum_size(width, height);
		m_target->set_bounds(width, height);
		m_pixels.resize(width * height);

		render_primitive_list &primitives = m_target->get_primitives();
		primitives.acquire_lock();
		software_renderer<u32, 0,0,0, 0,8,16>::draw_primitives(primitives, &m_pixels[0], width, height, width);
		primitives.release_lock();

		js_present(&m_pixels[0], width, height);
	}

	virtual void input_update(bool relative_reset) override
	{
		// MAME samples inputs once per frame, so a switch may only change once per frame
		// or a quick press and release would never be seen; later events wait in the queue
		if (relative_reset)
			std::fill(std::begin(m_switch_changed), std::end(m_switch_changed), false);

		std::int32_t events[3 * 64];
		for (int count = js_poll_input(events, std::size(events)); count >= 3; count = js_poll_input(events, std::size(events)))
		{
			for (int i = 0; (i + 3) <= count; i += 3)
				m_events.push_back({ events[i], events[i + 1], events[i + 2] });
		}
		while (!m_events.empty() && handle_event(m_events.front()[0], m_events.front()[1], m_events.front()[2]))
			m_events.pop_front();

		if (relative_reset)
		{
			m_mouse_axes[0] = m_mouse_delta[0] * osd::input_device::RELATIVE_PER_PIXEL;
			m_mouse_axes[1] = m_mouse_delta[1] * osd::input_device::RELATIVE_PER_PIXEL;
			m_mouse_axes[2] = m_mouse_delta[2] * osd::input_device::RELATIVE_PER_PIXEL;
			m_mouse_delta[0] = m_mouse_delta[1] = m_mouse_delta[2] = 0;
		}

		poll_input_modules(relative_reset);
	}

	virtual void check_osd_inputs() override { }
	virtual void process_events() override { }
	virtual bool has_focus() const override { return true; }

	virtual bool video_init() override
	{
		m_target = machine().render().target_alloc();
		return true;
	}

	virtual void video_exit() override
	{
		if (m_target)
			machine().render().target_free(m_target);
		m_target = nullptr;
	}

protected:
	virtual bool input_init() override
	{
		osd_common_t::input_init();

		osd::input_device &keyboard = machine().input().add_device(DEVICE_CLASS_KEYBOARD, "Browser keyboard", "keyboard0", nullptr);
		for (int i = 0; i < std::size(f_keys); i++)
		{
			keyboard.add_item(f_keys[i].name, f_keys[i].code, f_keys[i].item, &get_state<s32>, &m_keys[i]);
			js_add_key(i, f_keys[i].code, std::strlen(f_keys[i].code));
		}

		osd::input_device &mouse = machine().input().add_device(DEVICE_CLASS_MOUSE, "Browser mouse", "mouse0", nullptr);
		mouse.add_item("X", "", ITEM_ID_XAXIS, &get_state<s32>, &m_mouse_axes[0]);
		mouse.add_item("Y", "", ITEM_ID_YAXIS, &get_state<s32>, &m_mouse_axes[1]);
		mouse.add_item("Scroll V", "", ITEM_ID_ZAXIS, &get_state<s32>, &m_mouse_axes[2]);
		for (int i = 0; i < std::size(m_mouse_buttons); i++)
			mouse.add_item(util::string_format("Button %d", i + 1), "", input_item_id(ITEM_ID_BUTTON1 + i), &get_state<s32>, &m_mouse_buttons[i]);

		return true;
	}

private:
	bool set_switch(s32 &state, bool &changed, int value)
	{
		if (state == value)
			return true;
		if (changed)
			return false;
		state = value;
		changed = true;
		return true;
	}

	// returns false if the event has to wait for the next frame
	bool handle_event(int type, int a, int b)
	{
		switch (type)
		{
		case EVENT_KEY:
			if ((a >= 0) && (a < std::size(m_keys)))
				return set_switch(m_keys[a], m_switch_changed[a], b ? 1 : 0);
			break;

		case EVENT_MOUSE_MOTION:
			m_mouse_delta[0] += a;
			m_mouse_delta[1] += b;
			break;

		case EVENT_MOUSE_BUTTON:
			{
				// DOM numbers the middle button 1 and the right button 2
				static const int buttons[MOUSE_BUTTONS] = { 0, 2, 1, 3, 4 };
				if ((a >= 0) && (a < std::size(buttons)))
					return set_switch(m_mouse_buttons[buttons[a]], m_switch_changed[std::size(m_keys) + buttons[a]], b ? 1 : 0);
			}
			break;

		case EVENT_MOUSE_WHEEL:
			m_mouse_delta[2] += a;
			break;
		}
		return true;
	}

	render_target *m_target = nullptr;
	std::vector<u32> m_pixels;
	s32 m_keys[std::size(f_keys)] = { };
	s32 m_mouse_axes[3] = { };
	s32 m_mouse_delta[3] = { };
	s32 m_mouse_buttons[MOUSE_BUTTONS] = { };
	bool m_switch_changed[std::size(f_keys) + MOUSE_BUTTONS] = { };
	std::deque<std::array<s32, 3> > m_events;
};

} // anonymous namespace


namespace osd { namespace { MODULE_NOT_SUPPORTED(video_bgfx, OSD_RENDERER_PROVIDER, "bgfx") } }

MODULE_DEFINITION(RENDERER_BGFX, osd::video_bgfx)


void osd_setup_osd_specific_emu_options(emu_options &opts)
{
	opts.add_entries(osd_options::s_option_entries);
}


void osd_set_aggressive_input_focus(bool aggressive_focus)
{
	// the page owns focus
}


int main(int argc, char *argv[])
{
	std::vector<std::string> args = osd_get_command_line(argc, argv);

	// disable I/O buffering
	setvbuf(stdout, (char *) nullptr, _IONBF, 0);
	setvbuf(stderr, (char *) nullptr, _IONBF, 0);

	diagnostics_module::get_instance()->init_crash_diagnostics();

	osd_options options;
	webworker_osd_interface osd(options);
	osd.register_options();
	return emulator_info::start_frontend(options, osd, args);
}
