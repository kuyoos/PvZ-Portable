// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MultiplayerFont.h"

#include <memory>

#include "../../Resources.h"
#include "../../SexyAppFramework/SexyAppBase.h"
#include "../../SexyAppFramework/graphics/Font.h"
#include "../../SexyAppFramework/graphics/ImageFont.h"

using namespace Sexy;

namespace
{
const char* const CN_FONT_DESC = "data/pvz_cn.txt";
const char* const CN_FONT_IMAGE = "data/pvz_cn.png";

std::unique_ptr<ImageFont> gChineseFont;
SexyAppBase* gApp = nullptr;
bool gLoadTried = false;

void TryLoad()
{
	if (gLoadTried || gApp == nullptr)
	{
		return;
	}
	gLoadTried = true;

	if (!gApp->FileExists(CN_FONT_DESC) || !gApp->FileExists(CN_FONT_IMAGE))
	{
		return;
	}

	auto aFont = std::make_unique<ImageFont>(gApp, CN_FONT_DESC);
	if (aFont->mFontData == nullptr || !aFont->mFontData->mInitialized)
	{
		return;
	}

	gChineseFont = std::move(aFont);
}
}

void PvzpFont::SetApp(Sexy::SexyAppBase* theApp)
{
	gApp = theApp;
}

Sexy::_Font* PvzpFont::GetChineseFont()
{
	TryLoad();
	return gChineseFont != nullptr ? gChineseFont.get() : FONT_BRIANNETOD16;
}

bool PvzpFont::IsChineseFontLoaded()
{
	TryLoad();
	return gChineseFont != nullptr;
}
