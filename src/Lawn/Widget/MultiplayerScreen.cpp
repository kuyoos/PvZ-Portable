// SPDX-License-Identifier: LGPL-3.0-or-later
#include "MultiplayerScreen.h"

#include <algorithm>
#include <cstdlib>

#include "graphics/Font.h"
#include "graphics/Graphics.h"

#include "../../LawnApp.h"
#include "../../ConstEnums.h"
#include "../System/SeedNames.h"
#include "../../Resources.h"
#include "../Multiplayer/MultiplayerFont.h"

using namespace Sexy;
using namespace PvzpNet;

namespace
{
const Color COL_BG(24, 38, 20, 245);
const Color COL_PANEL(42, 66, 34);
const Color COL_PANEL_DARK(30, 48, 24);
const Color COL_BORDER(196, 164, 74);
const Color COL_TEXT(240, 236, 214);
const Color COL_MUTED(168, 172, 152);
const Color COL_GOLD(232, 196, 80);
const Color COL_OK(140, 210, 120);
const Color COL_ERR(238, 128, 96);

const int ROW_HEIGHT = 52;
const int MAX_ROWS = 6;

std::string PidText(int thePid)
{
	return "#" + std::to_string(thePid);
}
}

MultiplayerScreen::MultiplayerScreen(LawnApp* theApp)
	: mApp(theApp)
{
	mServer = theApp->GetString("MultiplayerServer", "127.0.0.1:7777");
	mHostRole = Role::HOST_PLANT;
}

MultiplayerScreen::~MultiplayerScreen()
{
	mClient.Disconnect();
}

void MultiplayerScreen::Resize(int theX, int theY, int theWidth, int theHeight)
{
	Widget::Resize(theX, theY, theWidth, theHeight);
}

void MultiplayerScreen::Connect()
{
	std::string aHost = mServer;
	int aPort = 7777;
	const size_t aColon = aHost.rfind(':');
	if (aColon != std::string::npos)
	{
		aPort = std::atoi(aHost.c_str() + aColon + 1);
		aHost = aHost.substr(0, aColon);
	}
	if (aHost.empty())
	{
		aHost = "127.0.0.1";
	}

	mPhase = Phase::Connecting;
	mStatus = "正在连接 " + mServer + " ...";

	std::string aError;
	if (!mClient.Connect(aHost, aPort, &aError))
	{
		mPhase = Phase::Failed;
		mStatus = aError;
		return;
	}

	Send(MakeHello("0.2.0", 1, "1073-cn", "玩家"));
}

void MultiplayerScreen::Send(const NetMsg& theMsg)
{
	if (!mClient.SendJson(theMsg))
	{
		mStatus = "发送失败：连接已断开";
		mPhase = Phase::Failed;
	}
}

void MultiplayerScreen::SendRooms()
{
	NetMsg aMsg;
	aMsg.mType = MsgType::ROOMS;
	Send(aMsg);
}

void MultiplayerScreen::Update()
{
	if (!mConnectTried)
	{
		mConnectTried = true;
		Connect();
	}

	NetMsg aMsg;
	while (mClient.PollMessage(aMsg))
	{
		HandleMsg(aMsg);
	}

	std::string aReason;
	if (mClient.PollDisconnect(aReason))
	{
		mPhase = Phase::Failed;
		mStatus = aReason;
		mRoomCode.clear();
		mMembers.clear();
	}

	if (mClient.IsConnected() && ++mPingTimer >= 200)
	{
		mPingTimer = 0;
		mClient.SendPing();
	}
}

void MultiplayerScreen::HandleMsg(const NetMsg& theMsg)
{
	if (theMsg.mType == MsgType::WELCOME)
	{
		mPid = theMsg.mPid;
		mPhase = Phase::Lobby;
		mStatus.clear();
		SendRooms();
	}
	else if (theMsg.mType == MsgType::ROOMS)
	{
		mRooms = theMsg.mRooms;
		mSelectedRoom = mRooms.empty() ? -1 : std::min(mSelectedRoom < 0 ? 0 : mSelectedRoom,
			static_cast<int>(mRooms.size()) - 1);
		if (mPhase != Phase::Room)
		{
			mPhase = Phase::Lobby;
		}
	}
	else if (theMsg.mType == MsgType::ROOM)
	{
		mRoomCode = theMsg.mCode;
		mRoomName = theMsg.mName;
		mRoomState = theMsg.mState;
		mHostPid = theMsg.mHostPid;
		mMembers = theMsg.mMembers;
		if (!mRoomCode.empty() && mPhase != Phase::Room)
		{
			mPhase = Phase::Room;
			mStatus.clear();
		}
		for (int i = 0; i < static_cast<int>(mMembers.size()); i++)
		{
			if (mMembers[i].mPid == mPid)
			{
				mSelectedMember = i;
			}
		}
	}
	else if (theMsg.mType == MsgType::LEFT)
	{
		mPhase = Phase::Lobby;
		mRoomCode.clear();
		mMembers.clear();
		mStatus = "已离开房间";
		SendRooms();
	}
	else if (theMsg.mType == MsgType::ERR)
	{
		mStatus = theMsg.mText.empty() ? "服务器拒绝了请求（" + std::to_string(static_cast<int>(theMsg.mError)) + "）"
			: theMsg.mText;
	}
	else if (theMsg.mType == MsgType::PICK_START)
	{
		mMyRole = theMsg.mRole;
		mMatchSeed = theMsg.mSeed;
		mDeadlineLeft = static_cast<double>(theMsg.mDeadline);
		mStatus = "进入选卡阶段（选卡界面在下一个版本接入）";
	}
	else if (theMsg.mType == MsgType::START)
	{
		std::vector<SeedType> aPlants;
		std::vector<SeedType> aZombies;
		for (const std::string& aName : theMsg.mPicks.mPlant)
		{
			SeedType aType = SEED_NONE;
			if (PvzpSeed::SeedNameToType(aName, aType)) aPlants.push_back(aType);
		}
		for (const std::string& aName : theMsg.mPicks.mZombie)
		{
			SeedType aType = SEED_NONE;
			if (PvzpSeed::SeedNameToType(aName, aType)) aZombies.push_back(aType);
		}

		mApp->StartVersusGame(theMsg.mSeed, aPlants, aZombies, mMyRole != Role::ZOMBIE);
	}
}

bool MultiplayerScreen::AmHost() const
{
	return mPid != 0 && mPid == mHostPid;
}

const MemberInfo* MultiplayerScreen::FindMember(int thePid) const
{
	for (const MemberInfo& aMember : mMembers)
	{
		if (aMember.mPid == thePid)
		{
			return &aMember;
		}
	}
	return nullptr;
}

std::string MultiplayerScreen::StateText(const std::string& theState) const
{
	if (theState == RoomState::IDLE)
	{
		return "等待准备";
	}
	if (theState == RoomState::READY)
	{
		return "可以开始";
	}
	if (theState == RoomState::PICKING)
	{
		return "选卡中";
	}
	if (theState == RoomState::PLAYING)
	{
		return "对局中";
	}
	return "已关闭";
}

std::string MultiplayerScreen::RoleText() const
{
	if (mHostRole == Role::HOST_ZOMBIE)
	{
		return "房主当僵尸";
	}
	return "房主当植物";
}

void MultiplayerScreen::AddButton(int theId, int theX, int theY, int theWidth, const std::string& theLabel,
	bool theEnabled)
{
	Hit aHit;
	aHit.mRect = Rect(theX, theY, theWidth, 40);
	aHit.mId = theId;
	aHit.mLabel = theLabel;
	aHit.mEnabled = theEnabled;
	mHits.push_back(aHit);
}

void MultiplayerScreen::Layout()
{
	mHits.clear();

	const int aCenterX = mWidth / 2;
	const int aLeft = aCenterX - 360;
	const int aButtonY = 486;

	if (mPhase == Phase::Lobby)
	{
		AddButton(B_CREATE, aLeft, aButtonY, 140, "创建房间");
		AddButton(B_JOIN, aLeft + 150, aButtonY, 120, "加入房间", mSelectedRoom >= 0);
		AddButton(B_REFRESH, aLeft + 280, aButtonY, 110, "刷新列表");
		AddButton(B_ROLE, aLeft + 400, aButtonY, 160, RoleText());
		AddButton(B_BACK, aLeft + 580, aButtonY, 140, "返回");
	}
	else if (mPhase == Phase::Room)
	{
		const MemberInfo* aMe = FindMember(mPid);
		const bool aReady = aMe != nullptr && aMe->mReady;
		const bool aCanStart = AmHost() && mMembers.size() >= 2 && !mRoomState.empty()
			&& mRoomState != RoomState::PLAYING;
		AddButton(B_READY, aLeft, aButtonY, 140, aReady ? "取消准备" : "准备", mRoomState != RoomState::PLAYING);
		AddButton(B_START, aLeft + 150, aButtonY, 120, "开始", aCanStart);
		const bool aCanKick = AmHost() && mSelectedMember >= 0
			&& mSelectedMember < static_cast<int>(mMembers.size())
			&& mMembers[mSelectedMember].mPid != mPid;
		AddButton(B_KICK, aLeft + 280, aButtonY, 110, "踢出", aCanKick);
		AddButton(B_LEAVE, aLeft + 580, aButtonY, 140, "退出房间");
	}
	else if (mPhase == Phase::Failed)
	{
		AddButton(B_RETRY, aCenterX - 150, aButtonY, 140, "重试连接");
		AddButton(B_BACK, aCenterX + 10, aButtonY, 140, "返回");
	}
	else
	{
		AddButton(B_BACK, aCenterX - 70, aButtonY, 140, "取消");
	}
}

int MultiplayerScreen::HitTest(int x, int y) const
{
	for (const Hit& aHit : mHits)
	{
		if (aHit.mEnabled && aHit.mRect.Contains(x, y))
		{
			return aHit.mId;
		}
	}
	return B_NONE;
}

void MultiplayerScreen::DrawText(Graphics* g, int theX, int theY, const std::string& theText,
	const Color& theColor) const
{
	g->SetFont(PvzpFont::GetChineseFont());
	g->SetColor(theColor);
	g->DrawString(theText, theX, theY);
}

void MultiplayerScreen::DrawPanel(Graphics* g, const Rect& theRect) const
{
	g->SetColor(COL_PANEL);
	g->FillRect(theRect);
	g->SetColor(COL_BORDER);
	g->DrawRect(theRect);
}

void MultiplayerScreen::DrawButton(Graphics* g, const Hit& theHit, bool theHovered) const
{
	g->SetColor(theHit.mEnabled ? (theHovered ? COL_GOLD : COL_PANEL) : COL_PANEL_DARK);
	g->FillRect(theHit.mRect);
	g->SetColor(theHit.mEnabled ? COL_BORDER : COL_PANEL_DARK);
	g->DrawRect(theHit.mRect);

	_Font* aFont = PvzpFont::GetChineseFont();
	const int aTextWidth = aFont->StringWidth(theHit.mLabel);
	const int aTextX = theHit.mRect.mX + (theHit.mRect.mWidth - aTextWidth) / 2;
	const int aTextY = theHit.mRect.mY + (theHit.mRect.mHeight - aFont->GetHeight()) / 2;
	DrawText(g, aTextX, aTextY, theHit.mLabel, theHit.mEnabled ? COL_TEXT : COL_MUTED);
}

void MultiplayerScreen::Draw(Graphics* g)
{
	Layout();

	g->SetColor(COL_BG);
	g->FillRect(0, 0, mWidth, mHeight);

	const int aCenterX = mWidth / 2;
	const int aLeft = aCenterX - 360;

	// Header
	g->SetColor(COL_PANEL);
	g->FillRect(0, 0, mWidth, 64);
	g->SetColor(COL_BORDER);
	g->FillRect(0, 62, mWidth, 2);
	DrawText(g, aLeft, 22, "多人游戏", COL_GOLD);

	std::string aPing;
	if (!mClient.IsConnected())
	{
		aPing = "未连接";
	}
	else if (mClient.GetPingMs() > 0)
	{
		aPing = "延迟 " + std::to_string(mClient.GetPingMs()) + " ms";
	}
	else
	{
		aPing = "延迟 测量中";
	}
	_Font* aFont = PvzpFont::GetChineseFont();
	DrawText(g, aLeft + 720 - aFont->StringWidth(aPing), 24, aPing, COL_TEXT);

	if (mPhase == Phase::Connecting)
	{
		DrawText(g, aLeft, 120, "正在连接服务器...", COL_TEXT);
		DrawText(g, aLeft, 152, mServer, COL_MUTED);
	}
	else if (mPhase == Phase::Lobby)
	{
		DrawText(g, aLeft, 84, "房间列表（" + std::to_string(mRooms.size()) + "）", COL_MUTED);
		DrawPanel(g, Rect(aLeft, 110, 720, MAX_ROWS * ROW_HEIGHT + 12));

		if (mRooms.empty())
		{
			DrawText(g, aLeft + 20, 110 + 20, "还没有房间，点“创建房间”开一局", COL_MUTED);
		}

		for (int i = 0; i < static_cast<int>(mRooms.size()) && i < MAX_ROWS; i++)
		{
			const RoomInfo& aRoom = mRooms[i];
			const int aRowY = 116 + i * ROW_HEIGHT;
			if (i == mSelectedRoom)
			{
				g->SetColor(COL_PANEL_DARK);
				g->FillRect(aLeft + 6, aRowY, 708, ROW_HEIGHT - 4);
				g->SetColor(COL_GOLD);
				g->DrawRect(aLeft + 6, aRowY, 708, ROW_HEIGHT - 4);
			}

			DrawText(g, aLeft + 18, aRowY + 6, aRoom.mName.empty() ? "无名房间" : aRoom.mName, COL_TEXT);
			DrawText(g, aLeft + 18, aRowY + 26, aRoom.mCode + " · " + std::to_string(aRoom.mCount) + "/"
				+ std::to_string(aRoom.mMaxPlayers) + " 人", COL_MUTED);
			DrawText(g, aLeft + 560, aRowY + 16, StateText(aRoom.mState), COL_MUTED);
		}
	}
	else if (mPhase == Phase::Room)
	{
		DrawText(g, aLeft, 84, "房间 " + mRoomCode + " · " + mRoomName, COL_TEXT);
		DrawText(g, aLeft + 500, 84, StateText(mRoomState), COL_MUTED);
		DrawPanel(g, Rect(aLeft, 110, 720, 2 * ROW_HEIGHT + 12));

		for (int i = 0; i < static_cast<int>(mMembers.size()) && i < 2; i++)
		{
			const MemberInfo& aMember = mMembers[i];
			const int aRowY = 116 + i * ROW_HEIGHT;
			if (i == mSelectedMember)
			{
				g->SetColor(COL_PANEL_DARK);
				g->FillRect(aLeft + 6, aRowY, 708, ROW_HEIGHT - 4);
				g->SetColor(COL_GOLD);
				g->DrawRect(aLeft + 6, aRowY, 708, ROW_HEIGHT - 4);
			}

			std::string aLine = aMember.mNick.empty() ? PidText(aMember.mPid) : aMember.mNick;
			if (aMember.mHost)
			{
				aLine += "（房主）";
			}
			if (aMember.mPid == mPid)
			{
				aLine += "（我）";
			}
			DrawText(g, aLeft + 18, aRowY + 6, aLine, COL_TEXT);

			const std::string aSub = aMember.mReady ? "已准备" : "未准备";
			DrawText(g, aLeft + 18, aRowY + 26, aSub + " · " + PidText(aMember.mPid), aMember.mReady ? COL_OK : COL_MUTED);
		}

		if (!AmHost())
		{
			DrawText(g, aLeft, 110 + 2 * ROW_HEIGHT + 24, "等待房主开始对局", COL_MUTED);
		}
		else
		{
			DrawText(g, aLeft, 110 + 2 * ROW_HEIGHT + 24,
				mMembers.size() < 2 ? "等对手进来后就能开始" : "点“开始”进入选卡", COL_MUTED);
		}
	}

	if (!mStatus.empty())
	{
		DrawText(g, aLeft, 546, mStatus, mPhase == Phase::Failed ? COL_ERR : COL_MUTED);
	}

	for (const Hit& aHit : mHits)
	{
		DrawButton(g, aHit, false);
	}
}

void MultiplayerScreen::MouseDown(int x, int y, int theClickCount)
{
	(void)theClickCount;

	const int aCenterX = mWidth / 2;
	const int aLeft = aCenterX - 360;

	// Row selection
	if (mPhase == Phase::Lobby)
	{
		const int aRowIndex = (y - 116) / ROW_HEIGHT;
		if (x >= aLeft + 6 && x <= aLeft + 714 && y >= 116 && aRowIndex >= 0
			&& aRowIndex < static_cast<int>(mRooms.size()) && aRowIndex < MAX_ROWS)
		{
			mSelectedRoom = aRowIndex;
			mApp->PlaySample(SOUND_BUTTONCLICK);
			return;
		}
	}
	else if (mPhase == Phase::Room)
	{
		const int aRowIndex = (y - 116) / ROW_HEIGHT;
		if (x >= aLeft + 6 && x <= aLeft + 714 && y >= 116 && aRowIndex >= 0
			&& aRowIndex < static_cast<int>(mMembers.size()) && aRowIndex < 2)
		{
			mSelectedMember = aRowIndex;
			mApp->PlaySample(SOUND_BUTTONCLICK);
			return;
		}
	}

	const int aHit = HitTest(x, y);
	if (aHit == B_NONE)
	{
		return;
	}

	mApp->PlaySample(SOUND_BUTTONCLICK);

	switch (aHit)
	{
	case B_CREATE:
	{
		NetMsg aMsg = MakeCreate("房主的房间", "versus", 2, mHostRole);
		Send(aMsg);
		break;
	}
	case B_JOIN:
		if (mSelectedRoom >= 0 && mSelectedRoom < static_cast<int>(mRooms.size()))
		{
			NetMsg aMsg;
			aMsg.mType = MsgType::JOIN;
			aMsg.mCode = mRooms[mSelectedRoom].mCode;
			Send(aMsg);
		}
		break;
	case B_REFRESH:
		SendRooms();
		break;
	case B_ROLE:
		mHostRole = mHostRole == Role::HOST_ZOMBIE ? Role::HOST_PLANT : Role::HOST_ZOMBIE;
		break;
	case B_READY:
	{
		const MemberInfo* aMe = FindMember(mPid);
		NetMsg aMsg;
		aMsg.mType = MsgType::READY;
		aMsg.mHasReady = true;
		aMsg.mReady = aMe == nullptr || !aMe->mReady;
		Send(aMsg);
		break;
	}
	case B_START:
	{
		NetMsg aMsg;
		aMsg.mType = MsgType::START;
		Send(aMsg);
		break;
	}
	case B_KICK:
		if (mSelectedMember >= 0 && mSelectedMember < static_cast<int>(mMembers.size()))
		{
			NetMsg aMsg;
			aMsg.mType = MsgType::KICK;
			aMsg.mPid = mMembers[mSelectedMember].mPid;
			Send(aMsg);
		}
		break;
	case B_LEAVE:
	{
		NetMsg aMsg;
		aMsg.mType = MsgType::LEAVE;
		Send(aMsg);
		break;
	}
	case B_BACK:
		if (mPhase == Phase::Room)
		{
			NetMsg aMsg;
			aMsg.mType = MsgType::LEAVE;
			Send(aMsg);
		}
		mApp->KillMultiplayerScreen();
		break;
	case B_RETRY:
		mClient.Disconnect();
		mConnectTried = false;
		mPhase = Phase::Offline;
		break;
	default:
		break;
	}
}
