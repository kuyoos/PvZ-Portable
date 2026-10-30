// SPDX-License-Identifier: LGPL-3.0-or-later
//
// C++ mirror of server/internal/proto/message.go. One struct covers both
// directions; empty optional fields are simply omitted when serialising, which
// matches the server's `omitempty` tags.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "NetJson.h"

namespace PvzpNet
{
// Message types (Msg.T).
namespace MsgType
{
inline constexpr const char* HELLO = "hello";
inline constexpr const char* WELCOME = "welcome";
inline constexpr const char* ROOMS = "rooms";
inline constexpr const char* CREATE = "create";
inline constexpr const char* JOIN = "join";
inline constexpr const char* LEAVE = "leave";
inline constexpr const char* KICK = "kick";
inline constexpr const char* READY = "ready";
inline constexpr const char* START = "start";
inline constexpr const char* ROOM = "room";
inline constexpr const char* LEFT = "left";
inline constexpr const char* ERR = "err";
inline constexpr const char* CHAT = "chat";
inline constexpr const char* PRTT = "prtt";
inline constexpr const char* PICK = "pick";
inline constexpr const char* PICK_START = "pick_start";
inline constexpr const char* PICK_STATE = "pick_state";
}

namespace RoomState
{
inline constexpr const char* IDLE = "idle";
inline constexpr const char* READY = "ready";
inline constexpr const char* PICKING = "picking";
inline constexpr const char* PLAYING = "playing";
inline constexpr const char* CLOSED = "closed";
}

namespace Role
{
inline constexpr const char* PLANT = "plant";
inline constexpr const char* ZOMBIE = "zombie";
inline constexpr const char* HOST_PLANT = "host_plant";
inline constexpr const char* HOST_ZOMBIE = "host_zombie";
inline constexpr const char* HOST_RANDOM = "random";
}

enum class NetError : int
{
	None = 0,
	Version = 1001,
	Resource = 1002,
	NoRoom = 1003,
	RoomFull = 1004,
	InGame = 1005,
	NotHost = 1006,
	BadRequest = 1007,
	Timeout = 1008,
	NotReady = 1009,
	TooFew = 1010,
	RateLimited = 1011,
	Pick = 1012
};

struct RoomInfo
{
	std::string mCode;
	std::string mName;
	std::string mHost;
	std::string mMode;
	std::string mState;
	std::string mRes;
	int mCount = 0;
	int mMaxPlayers = 0;
};

struct MemberInfo
{
	int mPid = 0;
	std::string mNick;
	std::string mRes;
	bool mReady = false;
	bool mHost = false;
	int mPing = 0;
};

struct CardPools
{
	std::vector<std::string> mPlant;
	std::vector<std::string> mZombie;
};

struct MatchPicks
{
	std::vector<std::string> mPlant;
	std::vector<std::string> mZombie;
};

struct NetMsg
{
	std::string mType;

	// handshake
	std::string mVersion;
	int mProto = 0;
	std::string mRes;
	std::string mNick;
	int mPid = 0;
	int64_t mServerTime = 0;

	// room
	std::string mCode;
	std::string mName;
	std::string mMode;
	int mMaxPlayers = 0;
	bool mReady = false;
	bool mHasReady = false;
	std::string mState;
	int mHostPid = 0;
	uint32_t mSeed = 0;
	int64_t mStartAt = 0;
	std::vector<RoomInfo> mRooms;
	std::vector<MemberInfo> mMembers;

	// notices
	NetError mError = NetError::None;
	std::string mReason;
	std::string mText;

	// card picking
	std::string mRole;
	CardPools mPool;
	bool mHasPool = false;
	int mSlots = 0;
	int64_t mDeadline = 0;
	std::vector<std::string> mCards;
	bool mHasPicks = false;
	MatchPicks mPicks;
	std::vector<int> mDone;

	// latency
	int mPing = 0;

	std::string ToJson() const;
	static bool FromJson(const std::string& theText, NetMsg& theOut, std::string* theError = nullptr);
};

// Builders for the messages the client sends.
NetMsg MakeHello(const std::string& theVersion, int theProto, const std::string& theRes, const std::string& theNick);
NetMsg MakeCreate(const std::string& theName, const std::string& theMode, int theMaxPlayers,
	const std::string& theHostRole);
NetMsg MakePick(const std::string& theRole, const std::vector<std::string>& theCards);
}
