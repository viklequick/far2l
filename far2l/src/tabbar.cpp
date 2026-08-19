/*
tabbar.cpp
*/
/*
Copyright (c) 1996 Eugene Roshal
Copyright (c) 2000 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "headers.hpp"

#include "tabbar.hpp"
#include "lang.hpp"
#include "colors.hpp"
#include "interf.hpp"
#include "farcolors.hpp"
#include "ctrlobj.hpp"
#include "filepanels.hpp"

void TabBar::DisplayObject()
{
	//FARString strMsg = L"Tab bar is here" + text;
	GotoXY(X1, Y1);
	SetFarColor(COL_HMENUTEXT);

	joinLeafsWithOffsets(tabs, X2 - X1 - 4, activeTab);
}

std::wstring widestCommonPrefix(const std::vector<std::wstring>& paths) {
    if (paths.empty())
        return L"";

    // Start with the first path as the candidate prefix
    std::wstring prefix = paths[0];

    for (size_t i = 1; i < paths.size(); ++i) {
        const std::wstring& p = paths[i];
        size_t j = 0;

        // Walk until mismatch or end of one of the strings
        while (j < prefix.size() && j < p.size() && prefix[j] == p[j])
            ++j;
        prefix.resize(j); // shrink to the matched part
        if (prefix.empty())
            break;
    }
    return prefix;
}

std::wstring widestCommonDirectory(const std::vector<std::wstring>& paths) {
    std::wstring prefix = widestCommonPrefix(paths);
    if (prefix.empty())
        return L"";

    // Find last slash or backslash
    size_t pos = prefix.find_last_of(L"/");
    if (pos == std::wstring::npos)
        return L"";

    return prefix.substr(0, pos + 1); // include the slash
}

std::wstring makeLeaf(const std::wstring& fullPath, const std::wstring& prefix) {
    if (fullPath.size() <= prefix.size())
        return fullPath;

    std::wstring leaf = fullPath.substr(prefix.size());

    // Remove leading slash/backslash if present
    if (!leaf.empty() && (leaf[0] == L'/' || leaf[0] == L'\\'))
        leaf.erase(0, 1);

    return leaf;
}

std::vector<std::wstring> compactifyFileNamesUpTo(std::vector<std::wstring> v) {
	if (v.size() < 2) return v;

	std::vector<std::wstring> r;
	r.reserve(v.size());

	std::wstring commonPrefix = widestCommonDirectory(v);
	for(size_t i = 0; i < v.size(); ++i) {
    	r.push_back(makeLeaf(v[i], commonPrefix));
	}

	return r;
}

std::wstring shortenLeaf(const std::wstring& leaf, size_t maxLeafWidth) {
    const wchar_t ell = L'…'; // L'\u2026' -- Unicode ellipsis

    if (leaf.size() <= maxLeafWidth)
        return leaf;

    if (maxLeafWidth <= 1)
        return std::wstring(1, ell);

    // Split prefix/suffix
    size_t prefixLen = maxLeafWidth / 4;
    if (prefixLen < 1) prefixLen = 1;
    size_t suffixLen = maxLeafWidth - prefixLen - 1; // -1 for ellipsis

    std::wstring prefix = leaf.substr(0, prefixLen);
    std::wstring suffix = leaf.substr(leaf.size() - suffixLen);

    return prefix + ell + suffix;
}

int TabBar::joinLeafsWithOffsets(const std::vector<std::wstring>& v, size_t maxWidth, int activeIndex) 
{
    if (v.empty()) return 0;

	std::vector<std::wstring> leafs = v; // compactifyFileNamesUpTo(v);
	FARString strLocalTitle = v[activeIndex];

	int x1 = WhereX();

    tabPos.clear();

    --maxWidth; // first file has icon too
    std::wstring separator = L"║📜";
    size_t N = leafs.size();
    size_t sepWidth = separator.size();

    // Total width consumed by separators
    size_t totalSepWidth = sepWidth * (N - 1);

    if (maxWidth <= totalSepWidth) {
        // Not enough space even for separators → return empty
        return 0;
    }

    // Minimum width per leaf
    size_t minLeafWidth = (maxWidth - totalSepWidth) / N;

    // First pass: compute actual widths for each leaf
    std::vector<size_t> leafWidths(N);

    for (size_t i = 0; i < N; ++i) {
        size_t full = leafs[i].size();
        leafWidths[i] = std::max(minLeafWidth, std::min(full, minLeafWidth));
    }

    // Second pass: distribute remaining space
    size_t usedWidth = totalSepWidth;
    for (size_t w : leafWidths)
        usedWidth += w;

    size_t remaining = (maxWidth > usedWidth ? maxWidth - usedWidth : 0);

    // Distribute remaining space greedily
    for (size_t i = 0; i < N && remaining > 0; ++i) {
        size_t full = leafs[i].size();
        size_t current = leafWidths[i];

        if (current < full) {
            size_t add = std::min(full - current, remaining);
            leafWidths[i] += add;
            remaining -= add;
        }
    }

    // Third pass: render
    for (size_t i = 0; i < N; ++i) {

		bool active = i == (size_t)activeIndex;
		bool hover  = i == (size_t)hoveredTab;

		// fprintf(stderr, "... [%d]: %ls active=%c hover=%c\n", (int)i, leafs[i].c_str(), active ? 'Y' : 'n', hover ? 'Y' : 'n');


        std::wstring leaf = shortenLeaf(leafs[i], leafWidths[i]);

		FARString strTab = leafs[i];
		tabPos.push_back({ strTab, WhereX() - X1, (int)strTab.CellsCount() });

		uint64_t color2 = SoftenItemColor(FarColorToReal(COL_HMENUTEXT), 0, 0, 0, 0);
		SetColor(color2);
		if(i > 0) FS << L"║";

		if (v.size() > 1){
			color2 = SoftenItemColor(FarColorToReal(COL_HMENUTEXT), 0, leftPinHovered == (int)i ? 1 : 0, 0, 0);
			SetColor(color2);
			FS << L"🧷";
			tabPos[i].leftPinX = WhereX() - X1 - 1;
			tabPos[i].x = WhereX() - X1;
		} 

		color2 = SoftenItemColor(FarColorToReal(COL_HMENUTEXT /* COL_EDITORSTATUS */), 
			active ? 1 : 0, 
			hover ? 1 : 0, 0, 0);
		SetColor(color2);
		FS << (active ? L"📲" /* L"📜" */ : L"📱" /* L"📝" */);
		FS << strTab;
		if (v.size() > 1){ 
			color2 = SoftenItemColor(FarColorToReal(COL_HMENUTEXT), 0, rightPinHovered == (int)i ? 1 : 0, 0, 0);
			SetColor(color2);
			FS << L"📎";
			tabPos[i].rightPinX = WhereX() - X1 - 1;

			color2 = SoftenItemColor(FarColorToReal(COL_HMENUTEXT), 0, delHovered == (int)i ? 1 : 0, 0, 0);
			SetColor(color2);

			FS << L" ✘";
			tabPos[i].delX = WhereX() - X1 - 2;
		}
    }

	uint64_t color3 = SoftenItemColor(FarColorToReal(COL_HMENUTEXT), 0, plusHovered ? 1 : 0, 0, 0);
	SetColor(color3);

    plusX = WhereX() + 1;
    FS << L"║ 🞧 ";

	SetColor(FarColorToReal(COL_HMENUTEXT));

    if (WhereX() < X2) 
    	FS << fmt::LeftAlign() << fmt::Cells() << fmt::Expand(X2 - WhereX()) << L" ";

    return WhereX() - x1;
}

void TabBar::SetTexts(const std::vector<std::wstring>& v, int _activeTab) {
	tabPos.clear();
	tabs = v;
	activeTab = _activeTab;
	hoveredTab = -1;
}

void TabBar::setHoverMask(int tabNo, bool plus, bool del, bool leftPin, bool rightPin) 
{
	plusHovered = plus;
	delHovered = del ? tabNo : -1;
	leftPinHovered = leftPin ? tabNo : -1;
	rightPinHovered = rightPin ? tabNo : -1;

	SetHovered(plus || del || leftPin || rightPin ? -1 : tabNo);
	CtrlObject->Cp()->SwitchHoveredTabTo(hoveredTab);
	Redraw();
}

int TabBar::ProcessMouse(MOUSE_EVENT_RECORD *MouseEvent) 
{
	int MsX = MouseEvent->dwMousePosition.X;
	int MsY = MouseEvent->dwMousePosition.Y;

	if (MsX < X1 || MsX > X2 || MsY != Y1)
		return FALSE;

	int pos = MsX - X1;
	plusHovered = false;
	delHovered = leftPinHovered = rightPinHovered = -1;

	for(size_t i = 0; i < tabPos.size(); ++i) {
		if(tabPos[i].x < 0 || tabPos[i].w <= 0)
			continue;
		if (pos >= tabPos[i].x && pos < tabPos[i].x + tabPos[i].w) { // tab
			if ((MouseEvent->dwEventFlags & MOUSE_MOVED)) {
				if((int)i != hoveredTab){ 
					setHoverMask(i, false, false, false, false);
				}
				return TRUE;
			}
			else if ((MouseEvent->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
				SetActive(i);
				CtrlObject->Cp()->SwitchActiveTabTo(i);
				Redraw();
				return TRUE;
			}

			break;
		}
		else if (tabPos[i].leftPinX >= 0 && pos >= tabPos[i].leftPinX && pos <= tabPos[i].leftPinX + 1) { // left pin
			if ((MouseEvent->dwEventFlags & MOUSE_MOVED)) {
				setHoverMask(i, false, false, true, false);
				return TRUE;
			}
			else if ((MouseEvent->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
				// vk: todo: choose Panel from other tab and swap it with left one
				return TRUE;
			}
		}
		else if (tabPos[i].rightPinX > 0 && pos >= tabPos[i].rightPinX && pos <= tabPos[i].rightPinX + 1) { // left pin
			if ((MouseEvent->dwEventFlags & MOUSE_MOVED)) {
				setHoverMask(i, false, false, false, true);
				return TRUE;
			}
			else if ((MouseEvent->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
				// vk: todo: choose Panel from other tab and swap it with right one
				return TRUE;
			}
		}
		else if (tabPos[i].delX > 0 && pos >= tabPos[i].delX && pos <= tabPos[i].delX + 3) { // delete sign
			if ((MouseEvent->dwEventFlags & MOUSE_MOVED)) {
				setHoverMask(i, false, true, false, false);
				return TRUE;
			}
			else if ((MouseEvent->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
				CtrlObject->Cp()->DeleteTab(i);
				return TRUE;
			}
		}
	}

	if (MsX >= plusX && MsX < plusX + 4) { // plus sign
		if ((MouseEvent->dwEventFlags & MOUSE_MOVED)) {
			setHoverMask(-1, true, false, false, false);
			return TRUE;
		}
		else if ((MouseEvent->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
			CtrlObject->Cp()->AppendNewTab();
			return TRUE;
		}
	}

	return FALSE;
}
