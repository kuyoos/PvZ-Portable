/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef _WIN32

#include "GameDataDownload.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <thread>

namespace
{

constexpr int kIdUrlEdit = 1001;
constexpr int kIdDownload = 1002;
constexpr int kIdClose = 1003;
constexpr UINT kMsgProgress = WM_APP + 1; // wParam: percent (-1 unknown), lParam: bytes received
constexpr UINT kMsgDone = WM_APP + 2;     // wParam: 1 = success, 0 = failure
constexpr uint64_t kMinPakSize = 5ull << 20;

HWND gHwnd = nullptr;
HWND gUrlEdit = nullptr;
HWND gProgress = nullptr;
HWND gStatus = nullptr;
HWND gDownloadButton = nullptr;
HWND gCloseButton = nullptr;

std::wstring gResourceDir;
std::wstring gUrl;
std::wstring gErrorText;
std::thread gWorker;
std::atomic<bool> gCancel{ false };
bool gBusy = false;
bool gSucceeded = false;
bool gCloseRequested = false;

struct WinHttpHandle
{
	HINTERNET Handle = nullptr;

	explicit WinHttpHandle(HINTERNET theHandle = nullptr) : Handle(theHandle) {}
	~WinHttpHandle()
	{
		if (Handle != nullptr)
			WinHttpCloseHandle(Handle);
	}

	WinHttpHandle(const WinHttpHandle&) = delete;
	WinHttpHandle& operator=(const WinHttpHandle&) = delete;

	operator HINTERNET() const { return Handle; }
	bool IsValid() const { return Handle != nullptr; }
};

std::wstring Utf8ToWide(const std::string& theText)
{
	if (theText.empty())
		return std::wstring();

	int aLength = MultiByteToWideChar(CP_UTF8, 0, theText.c_str(), static_cast<int>(theText.size()), nullptr, 0);
	if (aLength <= 0)
		return std::wstring();

	std::wstring aResult(static_cast<size_t>(aLength), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, theText.c_str(), static_cast<int>(theText.size()), aResult.data(), aLength);
	return aResult;
}

std::string WideToUtf8(const std::wstring& theText)
{
	if (theText.empty())
		return std::string();

	int aLength = WideCharToMultiByte(CP_UTF8, 0, theText.c_str(), static_cast<int>(theText.size()), nullptr, 0, nullptr, nullptr);
	if (aLength <= 0)
		return std::string();

	std::string aResult(static_cast<size_t>(aLength), '\0');
	WideCharToMultiByte(CP_UTF8, 0, theText.c_str(), static_cast<int>(theText.size()), aResult.data(), aLength, nullptr, nullptr);
	return aResult;
}

std::wstring TrimUrl(const std::wstring& theText)
{
	size_t aBegin = 0;
	size_t anEnd = theText.size();

	while (aBegin < anEnd && (theText[aBegin] == L' ' || theText[aBegin] == L'\t' ||
		theText[aBegin] == L'\r' || theText[aBegin] == L'\n'))
	{
		aBegin++;
	}
	while (anEnd > aBegin && (theText[anEnd - 1] == L' ' || theText[anEnd - 1] == L'\t' ||
		theText[anEnd - 1] == L'\r' || theText[anEnd - 1] == L'\n'))
	{
		anEnd--;
	}

	return theText.substr(aBegin, anEnd - aBegin);
}

std::filesystem::path GetUrlSeedFile(const std::wstring& theResourceDir)
{
	return std::filesystem::path(theResourceDir) / "main.pak.url.txt";
}

std::wstring LoadSavedUrl(const std::wstring& theResourceDir)
{
	FILE* aFile = _wfopen(GetUrlSeedFile(theResourceDir).c_str(), L"rb");
	if (aFile == nullptr)
		return std::wstring();

	std::string aBytes;
	char aBuffer[1024];
	size_t aRead = 0;
	while ((aRead = fread(aBuffer, 1, sizeof(aBuffer), aFile)) > 0)
		aBytes.append(aBuffer, aRead);
	fclose(aFile);

	// Strip a UTF-8 BOM if the file was written by a text editor
	if (aBytes.size() >= 3 && static_cast<unsigned char>(aBytes[0]) == 0xEF &&
		static_cast<unsigned char>(aBytes[1]) == 0xBB && static_cast<unsigned char>(aBytes[2]) == 0xBF)
	{
		aBytes.erase(0, 3);
	}

	return TrimUrl(Utf8ToWide(aBytes));
}

void SaveUrl(const std::wstring& theResourceDir, const std::wstring& theUrl)
{
	FILE* aFile = _wfopen(GetUrlSeedFile(theResourceDir).c_str(), L"wb");
	if (aFile == nullptr)
		return;

	std::string aBytes = WideToUtf8(theUrl);
	fwrite(aBytes.data(), 1, aBytes.size(), aFile);
	fclose(aFile);
}

bool HasLooseResources(const std::filesystem::path& theResourceDir)
{
	std::error_code aError;
	return std::filesystem::is_regular_file(theResourceDir / "properties" / "resources.xml", aError);
}

bool IsUsablePak(const std::filesystem::path& thePath)
{
	std::error_code aError;
	if (!std::filesystem::is_regular_file(thePath, aError))
		return false;

	const auto aSize = std::filesystem::file_size(thePath, aError);
	if (aError || aSize < kMinPakSize)
		return false;

	FILE* aFile = _wfopen(thePath.c_str(), L"rb");
	if (aFile == nullptr)
		return false;

	unsigned char aHead[4] = { 0, 0, 0, 0 };
	const size_t aRead = fread(aHead, 1, sizeof(aHead), aFile);
	fclose(aFile);

	if (aRead != sizeof(aHead))
		return false;

	// Standard PopCap package header; some distributors XOR the whole file with 0xF7.
	if (aHead[0] == 0xC0 && aHead[1] == 0x4A && aHead[2] == 0xC0 && aHead[3] == 0xBA)
		return true;

	for (unsigned char& aByte : aHead)
		aByte = static_cast<unsigned char>(aByte ^ 0xF7);

	return aHead[0] == 0xC0 && aHead[1] == 0x4A && aHead[2] == 0xC0 && aHead[3] == 0xBA;
}

void SetStatusText(const std::wstring& theText)
{
	if (gStatus != nullptr)
		SetWindowTextW(gStatus, theText.c_str());
}

std::wstring FormatMegabytes(uint64_t theBytes)
{
	const uint64_t aTenths = (theBytes * 10ull + (1ull << 19)) >> 20;
	return std::to_wstring(aTenths / 10ull) + L"." + std::to_wstring(aTenths % 10ull) + L" MB";
}

void DownloadWorker(std::wstring theUrl, std::filesystem::path thePartPath)
{
	gErrorText.clear();

	auto aFail = [](const std::wstring& theMessage)
	{
		gErrorText = theMessage;
		PostMessageW(gHwnd, kMsgDone, 0, 0);
	};

	URL_COMPONENTS aParts{};
	aParts.dwStructSize = sizeof(aParts);
	aParts.dwSchemeLength = static_cast<DWORD>(-1);
	aParts.dwHostNameLength = static_cast<DWORD>(-1);
	aParts.dwUrlPathLength = static_cast<DWORD>(-1);
	aParts.dwExtraInfoLength = static_cast<DWORD>(-1);

	if (!WinHttpCrackUrl(theUrl.c_str(), static_cast<DWORD>(theUrl.size()), 0, &aParts))
	{
		aFail(L"下载地址格式不正确。");
		return;
	}

	const std::wstring aHost(aParts.lpszHostName, aParts.dwHostNameLength);
	std::wstring aPath(aParts.lpszUrlPath, aParts.dwUrlPathLength);
	if (aParts.dwExtraInfoLength > 0 && aParts.lpszExtraInfo != nullptr)
		aPath.append(aParts.lpszExtraInfo, aParts.dwExtraInfoLength);

	const bool aSecure = aParts.nScheme == INTERNET_SCHEME_HTTPS;
	const INTERNET_PORT aPort = aParts.nPort;

	WinHttpHandle aSession(WinHttpOpen(L"PvZ-Portable/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!aSession.IsValid())
	{
		aFail(L"初始化网络失败（WinHTTP）。");
		return;
	}

	WinHttpHandle aConnection(WinHttpConnect(aSession, aHost.c_str(), aPort, 0));
	if (!aConnection.IsValid())
	{
		aFail(L"连接服务器失败。");
		return;
	}

	WinHttpHandle aRequest(WinHttpOpenRequest(aConnection, L"GET", aPath.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, aSecure ? WINHTTP_FLAG_SECURE : 0));
	if (!aRequest.IsValid())
	{
		aFail(L"创建请求失败。");
		return;
	}

	DWORD aRedirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption(aRequest, WINHTTP_OPTION_REDIRECT_POLICY, &aRedirectPolicy, sizeof(aRedirectPolicy));

	if (!WinHttpSendRequest(aRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(aRequest, nullptr))
	{
		aFail(L"请求失败，请检查地址或网络。");
		return;
	}

	DWORD aStatusCode = 0;
	DWORD aStatusSize = sizeof(aStatusCode);
	if (!WinHttpQueryHeaders(aRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &aStatusCode, &aStatusSize, WINHTTP_NO_HEADER_INDEX))
	{
		aFail(L"无法读取服务器响应。");
		return;
	}

	if (aStatusCode != 200)
	{
		aFail(L"服务器返回 HTTP " + std::to_wstring(aStatusCode) + L"，下载中止。");
		return;
	}

	uint64_t aTotalBytes = 0;
	{
		wchar_t aLengthText[64] = { 0 };
		DWORD aLengthSize = sizeof(aLengthText);
		if (WinHttpQueryHeaders(aRequest, WINHTTP_QUERY_CONTENT_LENGTH,
			WINHTTP_HEADER_NAME_BY_INDEX, aLengthText, &aLengthSize, WINHTTP_NO_HEADER_INDEX))
		{
			aTotalBytes = std::wcstoull(aLengthText, nullptr, 10);
		}
	}

	FILE* aFile = _wfopen(thePartPath.c_str(), L"wb");
	if (aFile == nullptr)
	{
		aFail(L"无法写入文件，请检查目录权限。");
		return;
	}

	uint64_t aReceived = 0;
	bool aOk = true;
	wchar_t aBuffer[64 * 1024];
	int aLastPercent = -2;

	for (;;)
	{
		if (gCancel.load())
		{
			aOk = false;
			gErrorText = L"已取消。";
			break;
		}

		DWORD aRead = 0;
		if (!WinHttpReadData(aRequest, aBuffer, static_cast<DWORD>(sizeof(aBuffer)), &aRead))
		{
			aOk = false;
			gErrorText = L"下载中断，请重试。";
			break;
		}

		if (aRead == 0)
			break;

		if (fwrite(aBuffer, 1, aRead, aFile) != aRead)
		{
			aOk = false;
			gErrorText = L"写入文件失败，磁盘可能已满。";
			break;
		}

		aReceived += aRead;

		int aPercent = aTotalBytes > 0
			? static_cast<int>((aReceived * 100ull) / aTotalBytes)
			: -1;
		if (aPercent != aLastPercent)
		{
			aLastPercent = aPercent;
			PostMessageW(gHwnd, kMsgProgress, static_cast<WPARAM>(static_cast<INT_PTR>(aPercent)),
				static_cast<LPARAM>(aReceived));
		}
	}

	fclose(aFile);

	if (!aOk)
	{
		DeleteFileW(thePartPath.c_str());
		PostMessageW(gHwnd, kMsgDone, 0, 0);
		return;
	}

	if (aTotalBytes > 0 && aReceived != aTotalBytes)
	{
		DeleteFileW(thePartPath.c_str());
		gErrorText = L"下载不完整（文件大小不符），请重试。";
		PostMessageW(gHwnd, kMsgDone, 0, 0);
		return;
	}

	// Verify before replacing the real file: a captured HTML page or a truncated
	// download must never end up as main.pak.
	if (!IsUsablePak(thePartPath))
	{
		DeleteFileW(thePartPath.c_str());
		gErrorText = L"下载到的文件不是有效的 main.pak（可能是网页或错误文件）。";
		PostMessageW(gHwnd, kMsgDone, 0, 0);
		return;
	}

	const std::filesystem::path aFinalPath = std::filesystem::path(gResourceDir) / "main.pak";
	if (!MoveFileExW(thePartPath.c_str(), aFinalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(thePartPath.c_str());
		gErrorText = L"无法保存 main.pak，请检查目录权限。";
		PostMessageW(gHwnd, kMsgDone, 0, 0);
		return;
	}

	PostMessageW(gHwnd, kMsgDone, 1, 0);
}

void SetBusy(bool theBusy)
{
	gBusy = theBusy;
	EnableWindow(gUrlEdit, !theBusy);
	EnableWindow(gDownloadButton, !theBusy);
	EnableWindow(gCloseButton, TRUE);
}

void StartDownload()
{
	if (gBusy)
		return;

	wchar_t aUrlText[2048] = { 0 };
	GetWindowTextW(gUrlEdit, aUrlText, static_cast<int>(std::size(aUrlText)));
	std::wstring aUrl = TrimUrl(aUrlText);
	if (aUrl.empty())
	{
		MessageBoxW(gHwnd, L"请先填入 main.pak 的下载地址。", L"下载游戏资源", MB_OK | MB_ICONINFORMATION);
		return;
	}

	if (aUrl.find(L"://") == std::wstring::npos)
	{
		MessageBoxW(gHwnd, L"地址需要以 http:// 或 https:// 开头。", L"下载游戏资源", MB_OK | MB_ICONWARNING);
		return;
	}

	gUrl = aUrl;
	gCancel.store(false);
	gErrorText.clear();
	SetBusy(true);
	SetStatusText(L"正在连接…");
	SendMessageW(gProgress, PBM_SETMARQUEE, TRUE, 30);
	ShowWindow(gProgress, SW_SHOW);

	const std::filesystem::path aPartPath = std::filesystem::path(gResourceDir) / "main.pak.part";
	gWorker = std::thread(DownloadWorker, aUrl, aPartPath);
}

void FinishDownload(bool theSuccess)
{
	if (gWorker.joinable())
		gWorker.join();

	SetBusy(false);
	SendMessageW(gProgress, PBM_SETMARQUEE, FALSE, 0);

	if (theSuccess)
	{
		gSucceeded = true;
		SaveUrl(gResourceDir, gUrl);
		SetStatusText(L"下载完成，正在启动游戏…");
		DestroyWindow(gHwnd);
		return;
	}

	ShowWindow(gProgress, SW_HIDE);
	SetStatusText(gErrorText.empty() ? L"下载失败。" : gErrorText);

	if (gCloseRequested)
		DestroyWindow(gHwnd);
}

LRESULT CALLBACK DownloadWndProc(HWND theHwnd, UINT theMessage, WPARAM theWParam, LPARAM theLParam)
{
	switch (theMessage)
	{
	case WM_CREATE:
	{
		HINSTANCE anInstance = GetModuleHandleW(nullptr);
		const HFONT aFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

		auto aMakeControl = [&](const wchar_t* theClass, const wchar_t* theText, DWORD theStyle,
			int theX, int theY, int theWidth, int theHeight, int theId) -> HWND
		{
			HWND aControl = CreateWindowExW(0, theClass, theText, WS_CHILD | WS_VISIBLE | theStyle,
				theX, theY, theWidth, theHeight, theHwnd,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(theId)), anInstance, nullptr);
			if (aControl != nullptr)
				SendMessageW(aControl, WM_SETFONT, reinterpret_cast<WPARAM>(aFont), TRUE);
			return aControl;
		};

		aMakeControl(L"STATIC", L"未找到游戏资源 main.pak", SS_LEFT, 16, 14, 588, 22, 0);
		aMakeControl(L"STATIC", L"填入下载地址（可直接粘贴），然后点「开始下载」：", SS_LEFT, 16, 40, 588, 20, 0);
		gUrlEdit = aMakeControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 16, 64, 588, 26, kIdUrlEdit);
		gProgress = aMakeControl(PROGRESS_CLASSW, L"", PBS_SMOOTH, 16, 100, 588, 18, 0);
		gStatus = aMakeControl(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 16, 124, 588, 20, 0);
		gDownloadButton = aMakeControl(L"BUTTON", L"开始下载", BS_PUSHBUTTON, 400, 158, 100, 30, kIdDownload);
		gCloseButton = aMakeControl(L"BUTTON", L"退出", BS_PUSHBUTTON, 512, 158, 92, 30, kIdClose);

		if (!gUrl.empty())
			SetWindowTextW(gUrlEdit, gUrl.c_str());

		SetStatusText(L"需要 1.2.0.1073 的 main.pak（约 163 MB）。");
		ShowWindow(gProgress, SW_HIDE);
		return 0;
	}

	case WM_COMMAND:
	{
		const int aControlId = LOWORD(theWParam);
		if (aControlId == kIdDownload)
		{
			StartDownload();
			return 0;
		}
		if (aControlId == kIdClose || aControlId == IDCANCEL)
		{
			if (gBusy)
			{
				gCancel.store(true);
				gCloseRequested = true;
				SetStatusText(L"正在取消…");
			}
			else
			{
				DestroyWindow(theHwnd);
			}
			return 0;
		}
		break;
	}

	case WM_CLOSE:
		if (gBusy)
		{
			gCancel.store(true);
			gCloseRequested = true;
			SetStatusText(L"正在取消…");
			return 0;
		}
		DestroyWindow(theHwnd);
		return 0;

	case kMsgProgress:
	{
		const int aPercent = static_cast<int>(static_cast<INT_PTR>(theWParam));
		const uint64_t aReceived = static_cast<uint64_t>(theLParam);

		std::wstring aText = L"已下载 " + FormatMegabytes(aReceived);
		if (aPercent >= 0)
		{
			SendMessageW(gProgress, PBM_SETMARQUEE, FALSE, 0);
			SendMessageW(gProgress, PBM_SETRANGE32, 0, 100);
			SendMessageW(gProgress, PBM_SETPOS, static_cast<WPARAM>(aPercent), 0);
			aText += L"（" + std::to_wstring(aPercent) + L"%）";
		}
		SetStatusText(aText);
		return 0;
	}

	case kMsgDone:
		FinishDownload(theWParam != 0);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		break;
	}

	return DefWindowProcW(theHwnd, theMessage, theWParam, theLParam);
}

bool RunDownloadWindow(const std::wstring& theResourceDir)
{
	WNDCLASSEXW aClass{};
	aClass.cbSize = sizeof(aClass);
	aClass.lpfnWndProc = DownloadWndProc;
	aClass.hInstance = GetModuleHandleW(nullptr);
	aClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	aClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
	aClass.lpszClassName = L"PvzpGameDataDownload";

	if (RegisterClassExW(&aClass) == 0)
		return false;

	gResourceDir = theResourceDir;
	gUrl = LoadSavedUrl(theResourceDir);

	const int aWidth = 636;
	const int aHeight = 240;
	const int aScreenWidth = GetSystemMetrics(SM_CXSCREEN);
	const int aScreenHeight = GetSystemMetrics(SM_CYSCREEN);

	gHwnd = CreateWindowExW(WS_EX_APPWINDOW, aClass.lpszClassName, L"PvZ Portable - 下载游戏资源",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		(aScreenWidth - aWidth) / 2, (aScreenHeight - aHeight) / 2, aWidth, aHeight,
		nullptr, nullptr, aClass.hInstance, nullptr);

	if (gHwnd == nullptr)
		return false;

	ShowWindow(gHwnd, SW_SHOW);
	UpdateWindow(gHwnd);
	SetForegroundWindow(gHwnd);

	MSG aMessage;
	while (GetMessageW(&aMessage, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&aMessage);
		DispatchMessageW(&aMessage);
	}

	if (gWorker.joinable())
		gWorker.join();

	return gSucceeded;
}

} // namespace

bool PvzpEnsureGameDataWin(const std::string& theResourceDir)
{
	std::wstring aResourceDir = Utf8ToWide(theResourceDir);
	if (aResourceDir.empty())
		return false;

	// Make sure the path ends with a separator so joins behave predictably.
	if (aResourceDir.back() != L'\\' && aResourceDir.back() != L'/')
		aResourceDir.push_back(L'\\');

	// Extracted (loose) resources work just as well as a pak file; do not nag
	// users who already have properties/resources.xml in place.
	if (HasLooseResources(aResourceDir))
		return true;

	if (IsUsablePak(std::filesystem::path(aResourceDir) / "main.pak"))
		return true;

	INITCOMMONCONTROLSEX aControls{};
	aControls.dwSize = sizeof(aControls);
	aControls.dwICC = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&aControls);

	return RunDownloadWindow(aResourceDir);
}

#endif // _WIN32
