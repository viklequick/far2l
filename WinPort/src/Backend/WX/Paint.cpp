#include "Backend.h"
#include "wxWinTranslations.h"
#include <wx/fontdlg.h>
#include <wx/fontenum.h>
#include <wx/textfile.h>
#include <wx/graphics.h>
#include "Paint.h"
#include "PathHelpers.h"
#include "WinPort.h"
#include <utils.h>

#define COLOR_ATTRIBUTES ( FOREGROUND_INTENSITY | BACKGROUND_INTENSITY | \
					FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | \
					BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE )

#define DYNAMIC_FONTS

#ifdef __APPLE__
# define DEFAULT_FONT_SIZE	20
#else
# define DEFAULT_FONT_SIZE	16
#endif

/////////////////////////////////////////////////////////////////////////////////
static const char *g_known_good_fonts[] = { "Ubuntu", "Terminus", "DejaVu",
											"Liberation", "Droid", "Monospace", "PT Mono", "Menlo",
											nullptr};


class FixedFontLookup : wxFontEnumerator
{
	wxString _any, _known_good;
	virtual bool OnFacename(const wxString &face_name)
	{
		_any = face_name;
		for (const char **p = g_known_good_fonts; *p; ++p) {
			if (face_name.find(*p)!=wxString::npos) {
				_known_good = face_name;
			}
		}

		/* unfortunately following code gives nothing interesting
		wxFont f(wxFontInfo(DEFAULT_FONT_SIZE).Underlined().FaceName(face_name));
		if (f.IsOk()) {
			fprintf(stderr, "FONT family %u encoding %u face_name='%ls' \n",
				(unsigned int)f.GetFamily(), (unsigned int)f.GetEncoding(), static_cast<const wchar_t*>(face_name.wc_str()));
		} else {
			fprintf(stderr, "BAD FONT: face_name='%ls'\n", static_cast<const wchar_t*>(face_name.wc_str()));
		} */
		return true;
	}
public:

	wxString Query()
	{
		_any.Empty();
		_known_good.Empty();
		EnumerateFacenames(wxFONTENCODING_SYSTEM, true);
		fprintf(stderr, "FixedFontLookup: _any='%ls' _known_good='%ls'\n",
			static_cast<const wchar_t*>(_any.wc_str()),
			static_cast<const wchar_t*>(_known_good.wc_str()));
		return _known_good.IsEmpty() ? _any : _known_good;
	}
};

static bool LoadFontFromSettings(wxFont& font)
{
	const std::string &path = InMyConfig("font");
	wxTextFile file(path);
	if (file.Exists() && file.Open()) {
		for (wxString str = file.GetFirstLine(); !file.Eof(); str = file.GetNextLine()) {
			font.SetNativeFontInfo(str);
			if (font.IsOk()) {
				printf("LoadFontFromSettings: used %ls\n",
					static_cast<const wchar_t*>(str.wc_str()));
				return true;
			}
		}
	}

	return false;
}

static bool ChooseFontAndSaveToSettings(wxWindow *parent, wxFont& font)
{
	font = wxGetFontFromUser(parent, font);
	if (font.IsOk()) {
		const std::string &path = InMyConfig("font");
		unlink(path.c_str());
		wxTextFile file;
		file.Create(path);

		file.InsertLine(font.GetNativeFontInfoDesc(), 0);
		file.Write();
		return true;
	}

	return false;
}

static void InitializeFont(wxWindow *parent, wxFont& font)
{
	if (LoadFontFromSettings(font))
		return;


	for (;;) {
		FixedFontLookup ffl;
		wxString fixed_font = ffl.Query();
		if (!fixed_font.empty()) {
			font = wxFont(DEFAULT_FONT_SIZE, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fixed_font);
		}
		if (fixed_font.empty() || !font.IsOk())
			font = wxFont(wxSystemSettings::GetFont(wxSYS_ANSI_FIXED_FONT));
#if defined(__WXOSX__) && !wxCHECK_VERSION(3, 1, 0)
		return;//older (not sure what exactly version) wxwidgets crashes in wxGetFontFromUser under OSX, this allows at least to start
#else
		if (ChooseFontAndSaveToSettings(parent, font))
			return;
#endif
	}
}

ConsolePaintContext::ConsolePaintContext(wxWindow *window) :
	_window(window), _font_width(12), _font_height(16), _font_descent(0), _font_thickness(2),
	_buffered_paint(false), _sharp(false), _stage(STG_NOT_REFRESHED)
{
	_char_fit_cache.checked.resize(0xffff);
	_char_fit_cache.result.resize(0xffff);
	_fonts.reserve(32);

	_window->SetBackgroundColour(*wxBLACK);
	wxFont font;
	InitializeFont(_window, font);
	SetFont(font);
}



class FontSizeInspector
{
	wxBitmap _bitmap;
	wxMemoryDC _dc;

	int _max_width, _prev_width;
	int _max_height, _prev_height;
	int _max_descent;
	bool _unstable_size, _fractional_size;

	void InspectChar(const wchar_t c)
	{
		wchar_t wz[2] = { c, 0};
		wxCoord width = 0, height = 0, descent = 0;
		_dc.GetTextExtent(wz, &width, &height, &descent);

		if (_max_width < width) _max_width = width;
		if (_max_height < height) _max_height = height;
		if (_max_descent < descent) _max_descent = descent;

		if ( _prev_width != width ) {
			if (_prev_width!=-1)
				_unstable_size = true;
			_prev_width = width;
		}
		if ( _prev_height != height ) {
			if (_prev_height!=-1) _unstable_size = true;
			_prev_height = height;
		}
	}

	void DetectFractionalSize(const wchar_t *chars)
	{
		// If font is non-monospaced there is no sense to detect if widths are fractional
		if (_unstable_size) return;
		_fractional_size = _dc.GetTextExtent(chars).GetWidth() != (int)(_max_width * wcslen(chars));
	}

	public:
	FontSizeInspector(wxFont& font)
		: _bitmap(48, 48, wxBITMAP_SCREEN_DEPTH),
		_max_width(4), _prev_width(-1),
		_max_height(6), _prev_height(-1),
		_max_descent(0),
		_unstable_size(false), _fractional_size(false)
	{
		_dc.SelectObject(_bitmap);
		_dc.SetFont(font);
	}

	void InspectChars(const wchar_t *chars)
	{
		for(const wchar_t *s = chars; *s; ++s)
			InspectChar(*s);
#if defined(__WXOSX__)
		// There are font rendering artifacts on MacOS if buffering is enabled and font size differs from 10, 15, 20;
		// E.g. if font size = 13, one char in a string has width 9px (GetTextExtent returns 9), but total string width
		// is less than N*9px, because internally one char could have fractional width.
		// We need to disable buffering for certain font sizes as done for non-monospaced ("unstable size") fonts.
		DetectFractionalSize(chars);
#endif
	}

	bool IsUnstableSize() const { return _unstable_size; }
	bool IsFractionalSize() const { return _fractional_size; }
	int GetMaxWidth() const { return _max_width; }
	int GetMaxHeight() const { return _max_height; }
	int GetMaxDescent() const { return _max_descent; }
};



void ConsolePaintContext::SetFont(wxFont font)
{
	FontSizeInspector fsi(font);
	fsi.InspectChars(L" 1234567890-=!@#$%^&*()_+qwertyuiop[]asdfghjkl;'zxcvbnm,./QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?");
	//fsi.InspectChars(L"QWERTYUIOPASDFGHJKL");

	bool is_unstable = fsi.IsUnstableSize();
	bool is_fractional = fsi.IsFractionalSize();
	_font_width = fsi.GetMaxWidth();
	_font_height = fsi.GetMaxHeight();
	_font_descent = fsi.GetMaxDescent();
	//font_height+= _font_height/4;

	_font_thickness = (_font_width > 8) ? _font_width / 8 : 1;
	switch (font.GetWeight()) {
		case wxFONTWEIGHT_LIGHT:
			if (_font_thickness > 1) {
				--_font_thickness;
			}
			break;

		case wxFONTWEIGHT_BOLD:
			++_font_thickness;
			break;

		case wxFONTWEIGHT_NORMAL:
		default:
			;
	}

	fprintf(stderr, "Font %u x %u . %u: '%ls' - %s\n", _font_width, _font_height, _font_thickness,
		static_cast<const wchar_t*>(font.GetFaceName().wc_str()),
		font.IsFixedWidth() ?
			(is_unstable ?
				"monospaced unstable" :
				(is_fractional ?
					"monospaced stable (fractional)" :
					"monospaced stable (integer)")) :
			"not monospaced");

	struct stat s{};

	_custom_draw_enabled = stat(InMyConfig("nocustomdraw").c_str(), &s) != 0;
	_buffered_paint = false;

	if (font.IsFixedWidth() && !is_unstable && !is_fractional) {
		if (stat(InMyConfig("nobuffering").c_str(), &s) != 0)
			_buffered_paint = true;
	}

	_fonts.clear();
	_fonts.push_back(font);
}

void ConsolePaintContext::ShowFontDialog()
{
	wxFont font;
	if (!_fonts.empty()) {
		font = _fonts.front();
		if (!ChooseFontAndSaveToSettings(_window, font))
			return;

	} else
		InitializeFont(_window, font);

	SetFont(font);
}

uint8_t ConsolePaintContext::CharFitTest(wxPaintDC &dc, wchar_t wc, unsigned int nx)
{
#ifdef DYNAMIC_FONTS
	const bool cacheable = (size_t((uint32_t)wc) - 1 < _char_fit_cache.checked.size()); // && wcz[1] == 0
	if (cacheable && _char_fit_cache.checked[ size_t((uint32_t)wc) - 1 ]) {
		return _char_fit_cache.result[ size_t((uint32_t)wc) - 1 ];
	}

	uint8_t font_index = 0;
	_cft_tmp = wc;
	for (font_index = 0; font_index != 0xff; ++font_index) {
		if (font_index >= _fonts.size()) {
			const auto &prev = _fonts.back();
			auto pt_size = prev.GetPointSize();
			if (pt_size <= 4)
				break;

			_fonts.emplace_back(prev);
			_fonts.back().SetPointSize(pt_size - 1);
		}
		assert(font_index < _fonts.size());

		wxCoord w = _font_width, h = _font_height, d = _font_descent;
		dc.GetTextExtent(_cft_tmp, &w, &h, &d, NULL, &_fonts[font_index]);
		const unsigned limh = _font_height + std::max(0, int(d) - int(_font_descent));
		const unsigned limw = _font_width * nx;
		if (unsigned(h) <= limh && unsigned(w) <= limw) {
			break;
		}
	}
//	if (font_index > 0) {
//		fprintf(stderr, "CharFitTest('%lc') -> %u\n", wc, font_index);
//	}

	if (cacheable) {
		_char_fit_cache.result[ size_t((uint32_t)wc) - 1 ] = font_index;
		_char_fit_cache.checked[ size_t((uint32_t)wc) - 1 ] = true;
	}

	return font_index;

#else
	return 0;

#endif
}

void ConsolePaintContext::ApplyFont(wxPaintDC &dc, uint8_t index)
{
	if (index < _fonts.size())
		dc.SetFont(_fonts[index]);
}

void ConsolePaintContext::OnPaint(wxPaintDC &dc, SMALL_RECT *qedit)
{
	if (UNLIKELY(_stage == STG_NOT_REFRESHED)) {
		// not refreshed yet - so early start so nothing to paint yet
		// so simple fill with background color for the sake of faster start
		dc.SetBackground(GetBrush(g_wx_palette.background[0]));
		dc.Clear();
		return;
	}

#if wxUSE_GRAPHICS_CONTEXT
	wxGraphicsContext* gctx = dc.GetGraphicsContext();
	if (gctx) {
		if (_sharp) {
			gctx->SetInterpolationQuality(wxINTERPOLATION_FAST);
			gctx->SetAntialiasMode(wxANTIALIAS_NONE);
		} else {
			gctx->SetInterpolationQuality(wxINTERPOLATION_DEFAULT);
			gctx->SetAntialiasMode(wxANTIALIAS_DEFAULT);
		}
	}
#endif
	unsigned int cw, ch; g_winport_con_out->GetSize(cw, ch);
	if (UNLIKELY(cw > MAXSHORT)) cw = MAXSHORT;
	if (UNLIKELY(ch > MAXSHORT)) ch = MAXSHORT;

	wxRegion rgn = _window->GetUpdateRegion();
	wxRect box = rgn.GetBox();
	SMALL_RECT area = {SHORT(box.GetLeft() / _font_width), SHORT(box.GetTop() / _font_height),
		SHORT(box.GetRight() / _font_width), SHORT(box.GetBottom() / _font_height)};

	if (UNLIKELY(area.Left < 0)) {
		area.Left = 0;
	}
	if (UNLIKELY(area.Top < 0)) {
		area.Top = 0;
	}
	if (UNLIKELY((unsigned)area.Right >= cw)) {
		area.Right = cw - 1;
	}
	if (UNLIKELY((unsigned)area.Bottom >= ch)) {
		area.Bottom = ch - 1;
	}
	if (UNLIKELY(area.Right < area.Left) || UNLIKELY(area.Bottom < area.Top)) {
		return;
	}

	_line.resize(cw);
	ApplyFont(dc);

	_cursor_props.Update();

	ConsolePainter painter(this, dc, _buffer, _cursor_props);
	for (unsigned int cy = (unsigned)area.Top; cy <= (unsigned)area.Bottom; ++cy) {
		wxRegionContain lc = rgn.Contains(0, cy * _font_height, cw * _font_width, _font_height);

		if (lc == wxOutRegion) {
			continue;
		}

		const CHAR_INFO *line;
		{
			// dont keep console output locked for a long time to avoid output slowdown
			IConsoleOutput::DirectLineAccess dla(g_winport_con_out, cy);
			line = dla.Line();
			unsigned int cur_cw = line ? dla.Width() : 0;
			if (cur_cw < cw) {
				memcpy(&_line[0], line, cur_cw * sizeof(*line));
				memset(&_line[cur_cw], 0, (cw - cur_cw) * sizeof(*line));
			} else {
				memcpy(&_line[0], line, cw * sizeof(*line));
			}
			line = &_line[0];
		}

		painter.LineBegin(cy);
		wchar_t tmp_wcz[2] = {0, 0};
		DWORD64 attributes = line->Attributes;
		const unsigned int cx_begin = (area.Left > 0 && !line[area.Left].Char.UnicodeChar) ? area.Left - 1 : area.Left;
		const unsigned int cx_end = std::min(cw, (unsigned)area.Right + 1);
		for (unsigned int cx = cx_begin; cx < cx_end; ++cx) {
			if (!line[cx].Char.UnicodeChar) {
				painter.LineFlush(cx + 1);
				continue;
			}
			const wchar_t *pwcz;
			if (UNLIKELY(CI_USING_COMPOSITE_CHAR(line[cx]))) {
				pwcz = WINPORT(CompositeCharLookup)(line[cx].Char.UnicodeChar);
			} else {
				tmp_wcz[0] = line[cx].Char.UnicodeChar ? wchar_t(line[cx].Char.UnicodeChar) : L' ';
				pwcz = tmp_wcz;
			}

			attributes = line[cx].Attributes;
			if (qedit && cx >= (unsigned)qedit->Left && cx <= (unsigned)qedit->Right
				&& cy >= (unsigned)qedit->Top && cy <= (unsigned)qedit->Bottom) {
				attributes^= COLOR_ATTRIBUTES;
				if (attributes & FOREGROUND_TRUECOLOR) {
					attributes^= 0x000000ffffff0000;
				}
				if (attributes & BACKGROUND_TRUECOLOR) {
					attributes^= 0xffffff0000000000;
				}
			}
			const int nx = (cx + 1 < cw && !line[cx + 1].Char.UnicodeChar) ? 2 : 1;
			painter.NextChar(cx, attributes, pwcz, nx);
		}
		painter.LineFlush(area.Right + 1);
	}

	// check if there is unused space in right and bottom and fill it with black color
	const int right_edge = (area.Right + 1) * _font_width;
	const int bottom_edge = (area.Bottom + 1) * _font_height;
	if (right_edge <= box.GetRight()) {
		painter.SetFillColor(g_wx_palette.background[0]);
		dc.DrawRectangle((area.Right + 1) * _font_width, box.GetTop(),
			box.GetRight() + 1 - right_edge, box.GetHeight());
	}
	if (bottom_edge <= box.GetBottom()) {
		painter.SetFillColor(g_wx_palette.background[0]);
		dc.DrawRectangle(box.GetLeft(), bottom_edge,
			box.GetWidth(), box.GetBottom() + 1 - bottom_edge);
	}


	if (UNLIKELY(_stage == STG_REFRESHED)) {
		_stage = STG_PAINTED;
		fprintf(stderr, "FIRST_PAINT: %lu msec\n", (unsigned long)GetProcessUptimeMSec());
	}
}


void ConsolePaintContext::RefreshArea( const SMALL_RECT &area )
{
	if (UNLIKELY(_stage == STG_NOT_REFRESHED)) {
		_stage = STG_REFRESHED;
	}

	wxRect rc;
	rc.SetLeft(((int)area.Left) * _font_width);
	rc.SetRight(((int)area.Right) * _font_width + _font_width - 1);
	rc.SetTop(((int)area.Top) * _font_height);
	rc.SetBottom(((int)area.Bottom) * _font_height + _font_height - 1);
	_window->Refresh(false, &rc);
}


void ConsolePaintContext::BlinkCursor()
{
	if (_cursor_props.Blink()) {
		SMALL_RECT area = {
			_cursor_props.pos.X, _cursor_props.pos.Y,
			_cursor_props.pos.X, _cursor_props.pos.Y
		};
		CHAR_INFO ci{};
		if (g_winport_con_out->Read(ci, _cursor_props.pos)) {
			if (!ci.Char.UnicodeChar && area.Left > 0) {
				--area.Left;
			} else if (CI_FULL_WIDTH_CHAR(ci)) {
				++area.Right;
			}
		}
		RefreshArea(area);
	}
}

void ConsolePaintContext::SetSharp(bool sharp)
{
	if (_sharp != sharp) {
		_sharp = sharp;
		_window->Refresh();
	}
}

bool ConsolePaintContext::IsSharpSupported()
{
#if wxUSE_GRAPHICS_CONTEXT
	return true;
#else
	return false;
#endif
}

wxBrush &ConsolePaintContext::GetBrush(const WinPortRGB &clr)
{
	auto it = _color2brush.find(clr);
	if (it != _color2brush.end()) {
		return it->second;
	}

	return _color2brush.emplace(clr, wxColour(clr.r, clr.g, clr.b)).first->second;
}

/////////////////////////////

bool CursorProps::Blink()
{
	bool prev_blink_state = blink_state;
	blink_state = !blink_state;
	Update();
	return (blink_state != prev_blink_state);
}

void CursorProps::Update()
{
	pos = g_winport_con_out->GetCursor(height, visible);
	if (prev_pos.X != pos.X || prev_pos.Y != pos.Y) {
		prev_pos = pos;
		blink_state = true;
	}
}

//////////////////////

ConsolePainter::ConsolePainter(ConsolePaintContext *context, wxPaintDC &dc, wxString &buffer, CursorProps &cursor_props) :
	_context(context), _dc(dc), _buffer(buffer), _cursor_props(cursor_props),
	_start_cx((unsigned int)-1), _start_back_cx((unsigned int)-1), _prev_fit_font_index(0),
	_prev_underlined(false), _prev_strikeout(false)
{
	_dc.SetPen(context->GetTransparentPen());
	_dc.SetBackgroundMode(wxPENSTYLE_TRANSPARENT);
	_buffer.Empty();
}


void ConsolePainter::SetFillColor(const WinPortRGB &clr)
{
	if (_brush_clr.Change(clr)) {
		wxBrush &brush = _context->GetBrush(clr);
		_dc.SetBrush(brush);
		_dc.SetBackground(brush);
	}
}

void ConsolePainter::PrepareBackground(unsigned int cx, const WinPortRGB &clr, unsigned int nx)
{
	const bool cursor_here = (_cursor_props.visible && _cursor_props.blink_state
		&& cx == (unsigned int)_cursor_props.pos.X
		&& _start_cy == (unsigned int)_cursor_props.pos.Y);

	if (!cursor_here && _start_back_cx != (unsigned int)-1 && _clr_back == clr)
		return;

	FlushBackground(cx + nx - 1);

	if (!cursor_here) {
		_clr_back = clr;
		_start_back_cx = cx;
		return;
	}

	_start_back_cx = (unsigned int)-1;

	const unsigned int x = cx * _context->FontWidth();
	unsigned int h = (_context->FontHeight() * _cursor_props.height) / 100;
	if (h==0) h = 1;
	unsigned int fill_height = _context->FontHeight() - h;
	if (fill_height > _context->FontHeight()) fill_height = _context->FontHeight();
	WinPortRGB clr_xored(clr.r ^ 0xff, clr.g ^ 0xff, clr.b ^ 0xff);
	SetFillColor(clr_xored);
	_dc.DrawRectangle(x, _start_y + fill_height, _context->FontWidth() * nx, h);

	if (fill_height) {
		SetFillColor(clr);
		_dc.DrawRectangle(x, _start_y, _context->FontWidth() * nx, fill_height);
	}
}


void ConsolePainter::FlushBackground(unsigned int cx_end)
{
	if (_start_back_cx != ((unsigned int)-1)) {
		SetFillColor(_clr_back);
		_dc.DrawRectangle(_start_back_cx * _context->FontWidth(), _start_y,
			(cx_end - _start_back_cx) * _context->FontWidth(), _context->FontHeight());
		_start_back_cx = ((unsigned int)-1);
	}
}

void ConsolePainter::FlushText(unsigned int cx_end)
{
	if (!_buffer.empty()) {
		_dc.SetTextForeground(wxColour(_clr_text.r, _clr_text.g, _clr_text.b));
		_dc.DrawText(_buffer, _start_cx * _context->FontWidth(), _start_y);
		_buffer.Empty();
	}
	FlushDecorations(cx_end);
	_start_cx = (unsigned int)-1;
	_prev_fit_font_index = 0;
}

void ConsolePainter::FlushDecorations(unsigned int cx_end)
{
	if (!_prev_underlined && !_prev_strikeout) {
		return;
	}
	_dc.SetPen(wxColour(_clr_text.r, _clr_text.g, _clr_text.b));

	if (_prev_underlined) {
		_dc.DrawLine(_start_cx * _context->FontWidth(), _start_y + _context->FontHeight() - 1,
			cx_end * _context->FontWidth(), _start_y + _context->FontHeight() - 1);
		_prev_underlined = false;
	}

	if (_prev_strikeout) {
		_dc.DrawLine(_start_cx * _context->FontWidth(), _start_y + (_context->FontHeight() / 2),
			cx_end * _context->FontWidth(), _start_y + (_context->FontHeight() / 2));
		_prev_strikeout = false;
	}

	_dc.SetPen(_context->GetTransparentPen());
}

static inline unsigned char CalcFadeColor(unsigned char bg, unsigned char fg)
{
	unsigned out = fg;
	out*= 2;
	out+= bg;
	out/= 3;
	return (unsigned char)std::min(out, (unsigned)0xff);
}

static inline unsigned char CalcExtraFadeColor(unsigned char bg, unsigned char fg)
{
	unsigned out = bg;
	out+= fg;
	out/= 2;
	return (unsigned char)std::min(out, (unsigned)0xff);
}

// #define DEBUG_FADED_EDGES

WinPortRGB ComputeEmbossColor_HSL(const WinPortRGB& xbg, const WinPortRGB& xline);
WinPortRGB ComputeEmbossColor_LAB(const WinPortRGB& xbg, const WinPortRGB& xline);

struct WXCustomDrawCharPainter : WXCustomDrawChar::Painter
{
	ConsolePainter &_painter;
	const WinPortRGB &_clr_text;
	const WinPortRGB &_clr_back;

	inline WXCustomDrawCharPainter(ConsolePainter &painter, const WinPortRGB &clr_text, const WinPortRGB &clr_back)
		: _painter(painter), _clr_text(clr_text), _clr_back(clr_back)
	{
		fw = (wxCoord)_painter._context->FontWidth();
		fh = (wxCoord)_painter._context->FontHeight(),
		thickness = (wxCoord)_painter._context->FontThickness();
		_painter.SetFillColor(clr_text);
	}

	inline bool MayDrawFadedEdgesImpl()
	{
		return (fw > 7 && fh > 7 && !_painter._context->IsSharp());
	}

	inline void SetColorFadedImpl()
	{
#ifndef DEBUG_FADED_EDGES
		WinPortRGB clr_fade(CalcFadeColor(_clr_back.r, _clr_text.r),
			CalcFadeColor(_clr_back.g, _clr_text.g), CalcFadeColor(_clr_back.b, _clr_text.b));
#else
		WinPortRGB clr_fade(0xff, 0, 0);
#endif
		_painter.SetFillColor(clr_fade);
	}

	inline void SetColorExtraFadedImpl()
	{
#ifndef DEBUG_FADED_EDGES
		WinPortRGB clr_fade(CalcExtraFadeColor(_clr_back.r, _clr_text.r),
			CalcExtraFadeColor(_clr_back.g, _clr_text.g), CalcExtraFadeColor(_clr_back.b, _clr_text.b));
#else
		WinPortRGB clr_fade(0, 0xff, 0);
#endif
		_painter.SetFillColor(clr_fade);
	}

	inline void SetColorEmbossImpl()
	{
		WinPortRGB clr_fade;
		/* near to black / near to white means LAB */
		int blackb = _clr_back.r + _clr_back.g + _clr_back.b;
		int blackf = _clr_text.r + _clr_text.g + _clr_text.b;
		if (blackb < 0x5f || blackf < 0x5f || blackb > 700 || blackf > 700) 
			clr_fade = ComputeEmbossColor_LAB(_clr_back, _clr_text);
		else
			clr_fade = ComputeEmbossColor_HSL(_clr_back, _clr_text);
		_painter.SetFillColor(clr_fade);
	}

	inline void SetColorRedImpl()
	{
		WinPortRGB clr_fade(0xff, 0, 0);
		_painter.SetFillColor(clr_fade);
	}

	inline int GetFontAscentImpl()
	{
		int w, h, descent, externalLeading; 
		_painter._dc.GetTextExtent("Ag", &w, &h, &descent, &externalLeading);
		return _painter._dc.GetCharHeight() - descent;
	}

	inline void FillRectangleImpl(wxCoord left, wxCoord top, wxCoord right, wxCoord bottom)
	{
		_painter._dc.DrawRectangle(left, top, right + 1 - left , bottom + 1 - top);
	}

	inline void DrawEllipticArcImpl(wxCoord left, wxCoord top, wxCoord width, wxCoord height, double start, double end, wxCoord thickness) {
		wxBrush oldBrush = _painter._dc.GetBrush(); 
		wxColour brushColor = oldBrush.GetColour();
		wxPen oldPen = _painter._dc.GetPen(); 
		_painter._dc.SetPen(wxPen(brushColor, thickness < 1 ? 1 : thickness));

		_painter._dc.SetBrush(*wxTRANSPARENT_BRUSH);
		_painter._dc.DrawEllipticArc(left, top, width, height, start, end);
		_painter._dc.SetBrush(oldBrush);
		_painter._dc.SetPen(oldPen);
	}

	inline void FillEllipticPieImpl(wxCoord left, wxCoord top, wxCoord width, wxCoord height, double start, double end) {
		_painter._dc.DrawEllipticArc(left, top, width, height, start, end);
	}

	inline void DrawLineImpl(wxCoord X1, wxCoord Y1, wxCoord X2, wxCoord Y2, wxCoord thickness) {
		wxBrush oldBrush = _painter._dc.GetBrush(); 
		wxColour brushColor = oldBrush.GetColour();
		wxPen oldPen = _painter._dc.GetPen(); 
		_painter._dc.SetPen(wxPen(brushColor, thickness < 1 ? 1 : thickness));

		_painter._dc.SetBrush(*wxTRANSPARENT_BRUSH);
		_painter._dc.DrawLine(X1, Y1, X2, Y2);
		_painter._dc.SetBrush(oldBrush);
		_painter._dc.SetPen(oldPen);
	}

	wxBrush savedBrush;

	inline void SaveBrushImpl() {
		savedBrush = _painter._dc.GetBrush();
	}

	inline void RestoreBrushImpl() {
		_painter._dc.SetBrush(savedBrush);
	}
};

// this code little bit wacky just to avoid virtual methods overhead
bool WXCustomDrawChar::Painter::MayDrawFadedEdges()
{
	return ((WXCustomDrawCharPainter *)this)->MayDrawFadedEdgesImpl();
}

void WXCustomDrawChar::Painter::SetColorFaded()
{
	((WXCustomDrawCharPainter *)this)->SetColorFadedImpl();
}

void WXCustomDrawChar::Painter::SetColorExtraFaded()
{
	((WXCustomDrawCharPainter *)this)->SetColorExtraFadedImpl();
}

void WXCustomDrawChar::Painter::SetColorEmboss()
{
	((WXCustomDrawCharPainter *)this)->SetColorEmbossImpl();
}

void WXCustomDrawChar::Painter::SetColorRed()
{
	((WXCustomDrawCharPainter *)this)->SetColorRedImpl();
}

int WXCustomDrawChar::Painter::GetFontAscent()
{
	return ((WXCustomDrawCharPainter *)this)->GetFontAscentImpl();
}

void WXCustomDrawChar::Painter::FillRectangle(wxCoord left, wxCoord top, wxCoord right, wxCoord bottom)
{
	((WXCustomDrawCharPainter *)this)->FillRectangleImpl(left, top, right, bottom);
}

void WXCustomDrawChar::Painter::DrawEllipticArc(wxCoord left, wxCoord top, wxCoord width, wxCoord height, double start, double end, wxCoord thickness)
{
	((WXCustomDrawCharPainter *)this)->DrawEllipticArcImpl(left, top, width, height, start, end, thickness);
}

void WXCustomDrawChar::Painter::FillPixel(wxCoord left, wxCoord top)
{
	((WXCustomDrawCharPainter *)this)->FillRectangleImpl(left, top, left, top);
}

void WXCustomDrawChar::Painter::FillEllipticPie(wxCoord left, wxCoord top, wxCoord width, wxCoord height, double start, double end)
{
	((WXCustomDrawCharPainter *)this)->FillEllipticPieImpl(left, top, width, height, start, end);
}

void WXCustomDrawChar::Painter::DrawLine(wxCoord X1, wxCoord Y1, wxCoord X2, wxCoord Y2, wxCoord thickness) 
{
	((WXCustomDrawCharPainter *)this)->DrawLineImpl(X1, Y1, X2, Y2, thickness);
}

void WXCustomDrawChar::Painter::SaveBrush() {
	((WXCustomDrawCharPainter *)this)->SaveBrushImpl();
}

void WXCustomDrawChar::Painter::RestoreBrush() {
	((WXCustomDrawCharPainter *)this)->RestoreBrushImpl();
}

void ConsolePainter::NextChar(unsigned int cx, DWORD64 attributes, const wchar_t *wcz, unsigned int nx)
{
	if (!wcz[0] || !WCHAR_IS_VALID(wcz[0])) {
		wcz = L" ";
	}

	WXCustomDrawChar::DrawT custom_draw = nullptr;

	if ((!wcz[1] && (wcz[0] == L' ' || (_context->IsCustomDrawEnabled()
	 && (custom_draw = WXCustomDrawChar::Get(wcz[0])) != nullptr)))) {
		if (!_buffer.empty())
			FlushBackground(cx + nx - 1);
		FlushText(cx + nx - 1);
	}

	const WinPortRGB &clr_back = WxConsoleBackground2RGB(attributes);
	PrepareBackground(cx, clr_back, nx);

	const bool underlined = (attributes & COMMON_LVB_UNDERSCORE) != 0;
	const bool strikeout = (attributes & COMMON_LVB_STRIKEOUT) != 0;

	if (!strikeout && !underlined && wcz[0] == L' ' && !wcz[1]) {
		return;
	}

	const WinPortRGB &clr_text = WxConsoleForeground2RGB(attributes);

	if (custom_draw) {
		FlushBackground(cx + nx);
		WXCustomDrawCharPainter cdp(*this, clr_text, clr_back);
		cdp.wc = wcz[0];
		custom_draw(cdp, _start_y, cx);
		if (underlined || strikeout) {
			_start_cx = cx;
			_prev_underlined = underlined;
			_prev_strikeout = strikeout;
			_clr_text = clr_text;
			FlushDecorations(cx + nx);
		}
		_start_cx = (unsigned int)-1;
		_prev_fit_font_index = 0;
		return;
	}

	uint8_t fit_font_index = _context->CharFitTest(_dc, *wcz, nx);

	if (fit_font_index == _prev_fit_font_index && _prev_underlined == underlined && _prev_strikeout == strikeout
		&& _start_cx != (unsigned int)-1 && _clr_text == clr_text && _context->IsPaintBuffered())
	{
		_buffer+= wcz;
		return;
	}

	FlushBackground(cx + nx);
	FlushText(cx);

	_prev_fit_font_index = fit_font_index;
	_prev_underlined = underlined;
	_prev_strikeout = strikeout;

	_start_cx = cx;
	_buffer = wcz;
	_clr_text = clr_text;

	if (fit_font_index != 0 && fit_font_index != 0xff) {
		_context->ApplyFont(_dc, fit_font_index);
		FlushText(cx + nx);
		_context->ApplyFont(_dc);
	}
}

#include <algorithm>
#include <cmath>

struct RGB { double r, g, b; }; // 0..1
struct LAB { double L, a, b; };

static WinPortRGB RGBtoFar(const RGB& rgb) 
{
	return WinPortRGB((int)(rgb.r*255), (int)(rgb.g*255), (int)(rgb.b*255));
}

static RGB FarToRGB(const WinPortRGB& c) 
{
	RGB rgb{c.r/255.0, c.g/255.0, c.b/255.0};
	return rgb;
}

// Convert RGB → HSL
static void RGBtoHSL(const RGB& c, double& h, double& s, double& l)
{
    double maxv = std::max({c.r, c.g, c.b});
    double minv = std::min({c.r, c.g, c.b});
    l = (maxv + minv) * 0.5;

    if (maxv == minv) {
        h = s = 0.0;
        return;
    }

    double d = maxv - minv;
    s = (l > 0.5) ? d / (2.0 - maxv - minv) : d / (maxv + minv);

    if (maxv == c.r)
        h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    else if (maxv == c.g)
        h = (c.b - c.r) / d + 2.0;
    else
        h = (c.r - c.g) / d + 4.0;

    h /= 6.0;
}

// Helper for HSL → RGB
static double HueToRGB(double p, double q, double t)
{
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

// Convert HSL → RGB
static RGB HSLtoRGB(double h, double s, double l)
{
    RGB out;
    if (s == 0.0) {
        out.r = out.g = out.b = l;
        return out;
    }

    double q = (l < 0.5) ? l * (1.0 + s) : (l + s - l * s);
    double p = 2.0 * l - q;

    out.r = HueToRGB(p, q, h + 1.0/3.0);
    out.g = HueToRGB(p, q, h);
    out.b = HueToRGB(p, q, h - 1.0/3.0);

    return out;
}

// Compute raise color using HSL
static RGB ComputeRaiseColor_HSL(const RGB& bg, const RGB& line)
{
    double hb, sb, lb;
    double hl, sl, ll;

    RGBtoHSL(bg, hb, sb, lb);
    RGBtoHSL(line, hl, sl, ll);

    // Opposite lightness shift
    double shift = 0.25; // 20% is a good UI default

    double newL = (ll < lb) ? ll + shift : ll - shift;
    newL = std::clamp(newL, 0.0, 1.0);

    return HSLtoRGB(hl, sl, newL);
}

// Gamma correction
static double InvGamma(double x)
{
    return (x <= 0.04045) ? (x / 12.92) : std::pow((x + 0.055) / 1.055, 2.4);
}

static double Gamma(double x)
{
    return (x <= 0.0031308) ? (12.92 * x) : (1.055 * std::pow(x, 1.0/2.4) - 0.055);
}

// RGB → LAB
static LAB RGBtoLAB(const RGB& c)
{
    double r = InvGamma(c.r);
    double g = InvGamma(c.g);
    double b = InvGamma(c.b);

    // RGB → XYZ (D65)
    double X = r*0.4124 + g*0.3576 + b*0.1805;
    double Y = r*0.2126 + g*0.7152 + b*0.0722;
    double Z = r*0.0193 + g*0.1192 + b*0.9505;

    // Normalize by D65 white point
    X /= 0.95047;
    Y /= 1.00000;
    Z /= 1.08883;

    auto f = [](double t) {
        return (t > 0.008856) ? std::cbrt(t) : (7.787 * t + 16.0/116.0);
    };

    LAB out;
    out.L = 116.0 * f(Y) - 16.0;
    out.a = 500.0 * (f(X) - f(Y));
    out.b = 200.0 * (f(Y) - f(Z));
    return out;
}

// LAB → RGB
static RGB LABtoRGB(const LAB& lab)
{
    double fy = (lab.L + 16.0) / 116.0;
    double fx = fy + lab.a / 500.0;
    double fz = fy - lab.b / 200.0;

    auto invf = [](double t) {
        double t3 = t*t*t;
        return (t3 > 0.008856) ? t3 : ((t - 16.0/116.0) / 7.787);
    };

    double X = invf(fx) * 0.95047;
    double Y = invf(fy);
    double Z = invf(fz) * 1.08883;

    // XYZ → RGB
    double r =  3.2406*X - 1.5372*Y - 0.4986*Z;
    double g = -0.9689*X + 1.8758*Y + 0.0415*Z;
    double b =  0.0557*X - 0.2040*Y + 1.0570*Z;

    // Gamma correction
    r = Gamma(r);
    g = Gamma(g);
    b = Gamma(b);

    return { std::clamp(r,0.0,1.0), std::clamp(g,0.0,1.0), std::clamp(b,0.0,1.0) };
}

// Compute raise color using LAB
static RGB ComputeRaiseColor_LAB(const RGB& bg, const RGB& line)
{
    LAB Lbg = RGBtoLAB(bg);
    LAB Lln = RGBtoLAB(line);

    LAB out = Lln;

    // Opposite lightness shift
    double shift = 25.0; // LAB L is 0..100, so 12 is ~12%
    out.L = (Lln.L < Lbg.L) ? (Lln.L + shift) : (Lln.L - shift);
    out.L = std::clamp(out.L, 0.0, 100.0);

    return LABtoRGB(out);
}

struct ColorsCache {
	WinPortRGB bg;
	WinPortRGB fg;
	WinPortRGB rg;
	int raised;
};

static ColorsCache _last_colors;

static bool ColorEq(const WinPortRGB& c1, const WinPortRGB& c2)
{
	return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b;
}

WinPortRGB ComputeEmbossColor_HSL(const WinPortRGB& xbg, const WinPortRGB& xline) 
{
	if (_last_colors.raised && ColorEq(_last_colors.bg, xbg) && ColorEq(_last_colors.fg, xline)) {
		return _last_colors.rg;
	}

	RGB bg = FarToRGB(xbg);
	RGB fg = FarToRGB(xline);
	RGB r = ComputeRaiseColor_HSL(bg, fg);

	_last_colors.bg = xbg;
	_last_colors.fg = xline;
	_last_colors.rg = RGBtoFar(r);
	_last_colors.raised = 1;

	return _last_colors.rg;
}

WinPortRGB ComputeEmbossColor_LAB(const WinPortRGB& xbg, const WinPortRGB& xline) 
{
	if (_last_colors.raised && ColorEq(_last_colors.bg, xbg) && ColorEq(_last_colors.fg, xline)) {
		return _last_colors.rg;
	}

	RGB bg = FarToRGB(xbg);
	RGB fg = FarToRGB(xline);
	RGB r = ComputeRaiseColor_LAB(bg, fg);

	_last_colors.bg = xbg;
	_last_colors.fg = xline;
	_last_colors.rg = RGBtoFar(r);
	_last_colors.raised = 1;

	return _last_colors.rg;
}

