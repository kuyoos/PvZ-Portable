// SPDX-License-Identifier: LGPL-3.0-or-later
// Chinese UI font for the multiplayer screens.
//
// The game's pak ships Latin-only bitmap fonts, so the Chinese card names,
// room list and lobby text need a generated atlas (see tools/font/gen_cjk_font.py).
// The atlas is loaded lazily and every caller falls back to the built-in Latin
// font when the two files are missing, so a build without the assets still runs.
#pragma once

namespace Sexy
{
class _Font;
class SexyAppBase;
}

namespace PvzpFont
{
// Called once from LawnApp::Init so the lazy loader knows where to ask for files.
void SetApp(Sexy::SexyAppBase* theApp);
// Never returns nullptr.
Sexy::_Font* GetChineseFont();
bool IsChineseFontLoaded();
}
