// SPDX-License-Identifier: LGPL-3.0-or-later
// Lobby and room screen for the 1v1 multiplayer mode.
//
// Everything is drawn with plain rects and the generated Chinese font so the
// screen needs no art assets and stays visually close to the game (dark green
// panels, gold highlights).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "widget/Widget.h"

#include "../../ConstEnums.h"

#include "../../PvzpLib/net/NetClient.h"

class LawnApp;

class MultiplayerScreen : public Sexy::Widget
{
public:
	explicit MultiplayerScreen(LawnApp* theApp);
	~MultiplayerScreen() override;

	void Update() override;
	void Draw(Sexy::Graphics* g) override;
	void MouseDown(int x, int y, int theClickCount) override;
	void Resize(int theX, int theY, int theWidth, int theHeight) override;

private:
	enum class Phase
	{
		Offline,
		Connecting,
		Lobby,
		Room,
		Failed
	};

	enum ButtonId
	{
		B_NONE = 0,
		B_CREATE,
		B_JOIN,
		B_REFRESH,
		B_BACK,
		B_READY,
		B_START,
		B_KICK,
		B_LEAVE,
		B_ROLE,
		B_RETRY
	};

	struct Hit
	{
		Sexy::Rect mRect;
		int mId = B_NONE;
		std::string mLabel;
		bool mEnabled = true;
	};

	void Connect();
	void SubmitPick(const std::vector<SeedType>& theCards);
	void HandleMsg(const PvzpNet::NetMsg& theMsg);
	void Send(const PvzpNet::NetMsg& theMsg);
	void SendRooms();

	void Layout();
	void AddButton(int theId, int theX, int theY, int theWidth, const std::string& theLabel,
		bool theEnabled = true);
	int HitTest(int x, int y) const;

	void DrawPanel(Sexy::Graphics* g, const Sexy::Rect& theRect) const;
	void DrawText(Sexy::Graphics* g, int theX, int theY, const std::string& theText,
		const Sexy::Color& theColor) const;
	void DrawButton(Sexy::Graphics* g, const Hit& theHit, bool theHovered) const;

	const PvzpNet::MemberInfo* FindMember(int thePid) const;
	bool AmHost() const;
	std::string StateText(const std::string& theState) const;
	std::string RoleText() const;

	LawnApp* mApp;
	PvzpNet::NetClient mClient;

	Phase mPhase = Phase::Offline;
	bool mConnectTried = false;
	std::string mStatus;
	std::string mServer;

	std::vector<PvzpNet::RoomInfo> mRooms;
	int mSelectedRoom = -1;

	std::string mRoomCode;
	std::string mRoomName;
	std::string mRoomState;
	int mHostPid = 0;
	std::vector<PvzpNet::MemberInfo> mMembers;
	int mSelectedMember = 0;
	std::string mMyRole;
	uint32_t mMatchSeed = 0;
	std::string mHostRole;

	int mPid = 0;
	int mPingTimer = 0;
	double mDeadlineLeft = 0;

	std::vector<Hit> mHits;
	int mHovered = B_NONE;
};
