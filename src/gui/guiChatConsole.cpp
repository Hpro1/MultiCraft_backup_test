/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "guiChatConsole.h"
#include "chat.h"
#include "client/client.h"
#include "debug.h"
#include "gettime.h"
#include "client/keycode.h"
#include "settings.h"
#include "porting.h"
#include "client/tile.h"
#include "client/fontengine.h"
#include "log.h"
#include "gettext.h"
#include <string>
#include "touchscreengui.h"

#if USE_FREETYPE
	#include "irrlicht_changes/CGUITTFont.h"
#endif

inline u32 clamp_u8(s32 value)
{
	return (u32) MYMIN(MYMAX(value, 0), 255);
}


GUIChatConsole::GUIChatConsole(
		gui::IGUIEnvironment* env,
		gui::IGUIElement* parent,
		s32 id,
		ChatBackend* backend,
		Client* client,
		IMenuManager* menumgr
):
	IGUIElement(gui::EGUIET_ELEMENT, env, parent, id,
			core::rect<s32>(0,0,100,100)),
	m_chat_backend(backend),
	m_client(client),
	m_menumgr(menumgr),
	m_animate_time_old(porting::getTimeMs())
{
	// load background settings
	s32 console_alpha = g_settings->getS32("console_alpha");
	m_background_color.setAlpha(clamp_u8(console_alpha));

	// load the background texture depending on settings
	ITextureSource *tsrc = client->getTextureSource();
	if (tsrc->isKnownSourceImage("background_chat.jpg")) {
		m_background = tsrc->getTexture("background_chat.jpg");
		m_background_color.setRed(255);
		m_background_color.setGreen(255);
		m_background_color.setBlue(255);
	} else {
		v3f console_color = g_settings->getV3F("console_color");
		m_background_color.setRed(clamp_u8(myround(console_color.X)));
		m_background_color.setGreen(clamp_u8(myround(console_color.Y)));
		m_background_color.setBlue(clamp_u8(myround(console_color.Z)));
	}

	u16 chat_font_size = g_settings->getU16("chat_font_size");
	m_font = g_fontengine->getFont(chat_font_size != 0 ?
		chat_font_size : FONT_SIZE_UNSPECIFIED, FM_Mono);

	if (!m_font) {
		errorstream << "GUIChatConsole: Unable to load mono font" << std::endl;
	} else {
		core::dimension2d<u32> dim = m_font->getDimension(L"M");
		m_fontsize = v2u32(dim.Width, dim.Height);
		m_font->grab();
	}
	m_fontsize.X = MYMAX(m_fontsize.X, 1);
	m_fontsize.Y = MYMAX(m_fontsize.Y, 1);

	// set default cursor options
	setCursor(true, true, 2.0, 0.1);
}

GUIChatConsole::~GUIChatConsole()
{
	if (m_font)
		m_font->drop();
}

void GUIChatConsole::openConsole(f32 scale)
{
	assert(scale > 0.0f && scale <= 1.0f);

	m_open = true;
	m_desired_height_fraction = scale;
	m_desired_height = scale * m_screensize.Y;
	reformatConsole();
	m_animate_time_old = porting::getTimeMs();
	IGUIElement::setVisible(true);
	Environment->setFocus(this);
	m_menumgr->createdMenu(this);
}

bool GUIChatConsole::isOpen() const
{
	return m_open;
}

bool GUIChatConsole::isOpenInhibited() const
{
	return m_open_inhibited > 0;
}

void GUIChatConsole::closeConsole()
{
	m_open = false;
	Environment->removeFocus(this);
	m_menumgr->deletingMenu(this);
}

void GUIChatConsole::closeConsoleAtOnce()
{
	closeConsole();
	m_height = 0;
	recalculateConsolePosition();
#ifdef HAVE_TOUCHSCREENGUI
	if (g_touchscreengui)
		g_touchscreengui->show();
#endif
}

void GUIChatConsole::replaceAndAddToHistory(const std::wstring &line)
{
	ChatPrompt& prompt = m_chat_backend->getPrompt();
	prompt.addToHistory(prompt.getLine());
	prompt.replace(line);
}


void GUIChatConsole::setCursor(
	bool visible, bool blinking, f32 blink_speed, f32 relative_height)
{
	if (visible)
	{
		if (blinking)
		{
			// leave m_cursor_blink unchanged
			m_cursor_blink_speed = blink_speed;
		}
		else
		{
			m_cursor_blink = 0x8000;  // on
			m_cursor_blink_speed = 0.0;
		}
	}
	else
	{
		m_cursor_blink = 0;  // off
		m_cursor_blink_speed = 0.0;
	}
	m_cursor_height = relative_height;
}

void GUIChatConsole::draw()
{
	if(!IsVisible)
		return;

	video::IVideoDriver* driver = Environment->getVideoDriver();

	// Check screen size
	v2u32 screensize = driver->getScreenSize();
	if (screensize != m_screensize)
	{
		// screen size has changed
		// scale current console height to new window size
		if (m_screensize.Y != 0)
			m_height = m_height * screensize.Y / m_screensize.Y;
		m_screensize = screensize;
		m_desired_height = m_desired_height_fraction * m_screensize.Y;
		reformatConsole();
	}

	// Animation
	u64 now = porting::getTimeMs();
	animate(now - m_animate_time_old);
	m_animate_time_old = now;

	// Draw console elements if visible
	if (m_height > 0)
	{
		drawBackground();
		drawText();
		drawPrompt();
	}

	gui::IGUIElement::draw();
}

void GUIChatConsole::reformatConsole()
{
	s32 cols = m_screensize.X / m_fontsize.X - 2; // make room for a margin (looks better)
	s32 rows = m_desired_height / m_fontsize.Y - 1; // make room for the input prompt
	if (cols <= 0 || rows <= 0)
		cols = rows = 0;
	recalculateConsolePosition();
	m_chat_backend->reformat(cols, rows);
}

void GUIChatConsole::recalculateConsolePosition()
{
	core::rect<s32> rect(0, 0, m_screensize.X, m_height);
	DesiredRect = rect;
	recalculateAbsolutePosition(false);
}

void GUIChatConsole::animate(u32 msec)
{
	// animate the console height
	s32 goal = m_open ? m_desired_height : 0;

	// Set invisible if close animation finished (reset by openConsole)
	// This function (animate()) is never called once its visibility becomes false so do not
	//		actually set visible to false before the inhibited period is over
	if (!m_open && m_height == 0 && m_open_inhibited == 0)
		IGUIElement::setVisible(false);

	if (m_height != goal)
	{
		s32 max_change = msec * m_screensize.Y * (m_height_speed / 1000.0);
		if (max_change == 0)
			max_change = 1;

		if (m_height < goal)
		{
			// increase height
			if (m_height + max_change < goal)
				m_height += max_change;
			else
				m_height = goal;
		}
		else
		{
			// decrease height
			if (m_height > goal + max_change)
				m_height -= max_change;
			else
				m_height = goal;
		}

		recalculateConsolePosition();
	}

	// blink the cursor
	if (m_cursor_blink_speed != 0.0)
	{
		u32 blink_increase = 0x10000 * msec * (m_cursor_blink_speed / 1000.0);
		if (blink_increase == 0)
			blink_increase = 1;
		m_cursor_blink = ((m_cursor_blink + blink_increase) & 0xffff);
	}

	// decrease open inhibit counter
	if (m_open_inhibited > msec)
		m_open_inhibited -= msec;
	else
		m_open_inhibited = 0;
}

void GUIChatConsole::drawBackground()
{
	video::IVideoDriver* driver = Environment->getVideoDriver();
	if (m_background != NULL)
	{
		core::rect<s32> sourcerect(0, -m_height, m_screensize.X, 0);
		driver->draw2DImage(
			m_background,
			v2s32(0, 0),
			sourcerect,
			&AbsoluteClippingRect,
			m_background_color,
			false);
	}
	else
	{
		driver->draw2DRectangle(
			m_background_color,
			core::rect<s32>(0, 0, m_screensize.X, m_height),
			&AbsoluteClippingRect);
	}
}

void GUIChatConsole::drawText()
{
	if (m_font == NULL)
		return;

	ChatBuffer& buf = m_chat_backend->getConsoleBuffer();
	for (u32 row = 0; row < buf.getRows(); ++row)
	{
		const ChatFormattedLine& line = buf.getFormattedLine(row);
		if (line.fragments.empty())
			continue;

		s32 line_height = m_fontsize.Y;
		s32 y = row * line_height + m_height - m_desired_height;
		if (y + line_height < 0)
			continue;

		for (const ChatFormattedFragment &fragment : line.fragments) {
			s32 x = (fragment.column + 1) * m_fontsize.X;
			core::rect<s32> destrect(
				x, y, x + m_fontsize.X * fragment.text.size(), y + m_fontsize.Y);

#if USE_FREETYPE
			if (m_font->getType() == irr::gui::EGFT_CUSTOM) {
				// Draw colored text if FreeType is enabled
				irr::gui::CGUITTFont *tmp = dynamic_cast<irr::gui::CGUITTFont *>(m_font);
				tmp->draw(
					fragment.text,
					destrect,
					false,
					false,
					&AbsoluteClippingRect);
			} else
#endif
			{
				// Otherwise use standard text
				m_font->draw(
					fragment.text.c_str(),
					destrect,
					video::SColor(255, 255, 255, 255),
					false,
					false,
					&AbsoluteClippingRect);
			}
		}
	}
}

void GUIChatConsole::drawPrompt()
{
	if (!m_font)
		return;

	u32 row = m_chat_backend->getConsoleBuffer().getRows();
	s32 line_height = m_fontsize.Y;
	s32 y = row * line_height + m_height - m_desired_height;

	ChatPrompt& prompt = m_chat_backend->getPrompt();
	std::wstring prompt_text = prompt.getVisiblePortion();

	// FIXME Draw string at once, not character by character
	// That will only work with the cursor once we have a monospace font
	for (u32 i = 0; i < prompt_text.size(); ++i)
	{
		wchar_t ws[2] = {prompt_text[i], 0};
		s32 x = (1 + i) * m_fontsize.X;
		core::rect<s32> destrect(
			x, y, x + m_fontsize.X, y + m_fontsize.Y);
		m_font->draw(
			ws,
			destrect,
			video::SColor(255, 255, 255, 255),
			false,
			false,
			&AbsoluteClippingRect);
	}

	// Draw the cursor during on periods
	if ((m_cursor_blink & 0x8000) != 0)
	{
		s32 cursor_pos = prompt.getVisibleCursorPosition();
		if (cursor_pos >= 0)
		{
			s32 cursor_len = prompt.getCursorLength();
			video::IVideoDriver* driver = Environment->getVideoDriver();
			s32 x = (1 + cursor_pos) * m_fontsize.X;
			core::rect<s32> destrect(
				x,
				y + m_fontsize.Y * (1.0 - m_cursor_height),
				x + m_fontsize.X * MYMAX(cursor_len, 1),
				y + m_fontsize.Y * (cursor_len ? m_cursor_height+1 : 1)
			);
			video::SColor cursor_color(255,255,255,255);
			driver->draw2DRectangle(
				cursor_color,
				destrect,
				&AbsoluteClippingRect);
		}
	}

}

bool GUIChatConsole::OnEvent(const SEvent& event)
{

	ChatPrompt &prompt = m_chat_backend->getPrompt();

	if(event.EventType == EET_KEY_INPUT_EVENT && event.KeyInput.PressedDown)
	{
		// Key input
		if (KeyPress(event.KeyInput) == getKeySetting("keymap_console")) {
			closeConsole();

			// inhibit open so the_game doesn't reopen immediately
			m_open_inhibited = 50;
			m_close_on_enter = false;
			return true;
		}

		if (event.KeyInput.Key == KEY_ESCAPE || event.KeyInput.Key == KEY_CANCEL) {
			closeConsoleAtOnce();
			m_close_on_enter = false;
			// inhibit open so the_game doesn't reopen immediately
			m_open_inhibited = 1; // so the ESCAPE button doesn't open the "pause menu"
			return true;
		}
		else if(event.KeyInput.Key == KEY_PRIOR)
		{
			m_chat_backend->scrollPageUp();
			return true;
		}
		else if(event.KeyInput.Key == KEY_NEXT)
		{
			m_chat_backend->scrollPageDown();
			return true;
		}
		else if(event.KeyInput.Key == KEY_RETURN)
		{
			prompt.addToHistory(prompt.getLine());
			std::wstring text = prompt.replace(L"");
			m_client->typeChatMessage(text);
			if (m_close_on_enter) {
				closeConsoleAtOnce();
				m_close_on_enter = false;
			}
			return true;
		}
		else if(event.KeyInput.Key == KEY_UP)
		{
			// Up pressed
			// Move back in history
			prompt.historyPrev();
			return true;
		}
		else if(event.KeyInput.Key == KEY_DOWN)
		{
			// Down pressed
			// Move forward in history
			prompt.historyNext();
			return true;
		}
		else if(event.KeyInput.Key == KEY_LEFT || event.KeyInput.Key == KEY_RIGHT)
		{
			// Left/right pressed
			// Move/select character/word to the left depending on control and shift keys
			ChatPrompt::CursorOp op = event.KeyInput.Shift ?
				ChatPrompt::CURSOROP_SELECT :
				ChatPrompt::CURSOROP_MOVE;
			ChatPrompt::CursorOpDir dir = event.KeyInput.Key == KEY_LEFT ?
				ChatPrompt::CURSOROP_DIR_LEFT :
				ChatPrompt::CURSOROP_DIR_RIGHT;
			ChatPrompt::CursorOpScope scope = event.KeyInput.Control ?
				ChatPrompt::CURSOROP_SCOPE_WORD :
				ChatPrompt::CURSOROP_SCOPE_CHARACTER;
			prompt.cursorOperation(op, dir, scope);
			return true;
		}
		else if(event.KeyInput.Key == KEY_HOME)
		{
			// Home pressed
			// move to beginning of line
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_MOVE,
				ChatPrompt::CURSOROP_DIR_LEFT,
				ChatPrompt::CURSOROP_SCOPE_LINE);
			return true;
		}
		else if(event.KeyInput.Key == KEY_END)
		{
			// End pressed
			// move to end of line
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_MOVE,
				ChatPrompt::CURSOROP_DIR_RIGHT,
				ChatPrompt::CURSOROP_SCOPE_LINE);
			return true;
		}
		else if(event.KeyInput.Key == KEY_BACK)
		{
			// Backspace or Ctrl-Backspace pressed
			// delete character / word to the left
			ChatPrompt::CursorOpScope scope =
				event.KeyInput.Control ?
				ChatPrompt::CURSOROP_SCOPE_WORD :
				ChatPrompt::CURSOROP_SCOPE_CHARACTER;
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_DELETE,
				ChatPrompt::CURSOROP_DIR_LEFT,
				scope);
			return true;
		}
		else if(event.KeyInput.Key == KEY_DELETE)
		{
			// Delete or Ctrl-Delete pressed
			// delete character / word to the right
			ChatPrompt::CursorOpScope scope =
				event.KeyInput.Control ?
				ChatPrompt::CURSOROP_SCOPE_WORD :
				ChatPrompt::CURSOROP_SCOPE_CHARACTER;
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_DELETE,
				ChatPrompt::CURSOROP_DIR_RIGHT,
				scope);
			return true;
		}
		else if(event.KeyInput.Key == KEY_KEY_A && event.KeyInput.Control)
		{
			// Ctrl-A pressed
			// Select all text
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_SELECT,
				ChatPrompt::CURSOROP_DIR_LEFT, // Ignored
				ChatPrompt::CURSOROP_SCOPE_LINE);
			return true;
		}
		else if(event.KeyInput.Key == KEY_KEY_C && event.KeyInput.Control)
		{
			// Ctrl-C pressed
			// Copy text to clipboard
			if (prompt.getCursorLength() <= 0)
				return true;
			std::wstring wselected = prompt.getSelection();
			std::string selected = wide_to_utf8(wselected);
			Environment->getOSOperator()->copyToClipboard(selected.c_str());
			return true;
		}
		else if(event.KeyInput.Key == KEY_KEY_V && event.KeyInput.Control)
		{
			// Ctrl-V pressed
			// paste text from clipboard
			if (prompt.getCursorLength() > 0) {
				// Delete selected section of text
				prompt.cursorOperation(
					ChatPrompt::CURSOROP_DELETE,
					ChatPrompt::CURSOROP_DIR_LEFT, // Ignored
					ChatPrompt::CURSOROP_SCOPE_SELECTION);
			}
			IOSOperator *os_operator = Environment->getOSOperator();
			const c8 *text = os_operator->getTextFromClipboard();
			if (!text)
				return true;
			prompt.input(utf8_to_wide(text));
			return true;
		}
		else if(event.KeyInput.Key == KEY_KEY_X && event.KeyInput.Control)
		{
			// Ctrl-X pressed
			// Cut text to clipboard
			if (prompt.getCursorLength() <= 0)
				return true;
			std::wstring wselected = prompt.getSelection();
			std::string selected = wide_to_utf8(wselected);
			Environment->getOSOperator()->copyToClipboard(selected.c_str());
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_DELETE,
				ChatPrompt::CURSOROP_DIR_LEFT, // Ignored
				ChatPrompt::CURSOROP_SCOPE_SELECTION);
			return true;
		}
		else if(event.KeyInput.Key == KEY_KEY_U && event.KeyInput.Control)
		{
			// Ctrl-U pressed
			// kill line to left end
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_DELETE,
				ChatPrompt::CURSOROP_DIR_LEFT,
				ChatPrompt::CURSOROP_SCOPE_LINE);
			return true;
		}
		else if(event.KeyInput.Key == KEY_KEY_K && event.KeyInput.Control)
		{
			// Ctrl-K pressed
			// kill line to right end
			prompt.cursorOperation(
				ChatPrompt::CURSOROP_DELETE,
				ChatPrompt::CURSOROP_DIR_RIGHT,
				ChatPrompt::CURSOROP_SCOPE_LINE);
			return true;
		}
		else if(event.KeyInput.Key == KEY_TAB)
		{
			// Tab or Shift-Tab pressed
			// Nick completion
			std::list<std::string> names = m_client->getConnectedPlayerNames();
			bool backwards = event.KeyInput.Shift;
			prompt.nickCompletion(names, backwards);
			return true;
		} else if (!iswcntrl(event.KeyInput.Char) && !event.KeyInput.Control) {
			#if defined(__linux__) && (IRRLICHT_VERSION_MAJOR == 1 && IRRLICHT_VERSION_MINOR < 9)
				wchar_t wc = L'_';
				mbtowc( &wc, (char *) &event.KeyInput.Char, sizeof(event.KeyInput.Char) );
				prompt.input(wc);
			#else
				prompt.input(event.KeyInput.Char);
			#endif
			return true;
		}
	}
	else if(event.EventType == EET_MOUSE_INPUT_EVENT)
	{
		if(event.MouseInput.Event == EMIE_MOUSE_WHEEL)
		{
			s32 rows = myround(-3.0 * event.MouseInput.Wheel);
			m_chat_backend->scroll(rows);
		}
	}

	return Parent ? Parent->OnEvent(event) : false;
}

void GUIChatConsole::setVisible(bool visible)
{
	m_open = visible;
	IGUIElement::setVisible(visible);
	if (!visible) {
		m_height = 0;
		recalculateConsolePosition();
	}
}

