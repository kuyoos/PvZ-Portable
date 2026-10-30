// SPDX-License-Identifier: LGPL-3.0-or-later
#include "NetMsg.h"

namespace PvzpNet
{
namespace
{
Json MakeStringArray(const std::vector<std::string>& theValues)
{
	Json anArray = Json::MakeArray();
	for (const auto& aValue : theValues)
	{
		anArray.Push(Json::MakeString(aValue));
	}
	return anArray;
}

std::vector<std::string> ReadStringArray(const Json* theValue)
{
	std::vector<std::string> aResult;
	if (theValue == nullptr)
	{
		return aResult;
	}
	for (size_t i = 0; i < theValue->GetSize(); i++)
	{
		aResult.push_back(theValue->At(i).AsString());
	}
	return aResult;
}
}

std::string NetMsg::ToJson() const
{
	Json aRoot = Json::MakeObject();
	aRoot.Set("t", Json::MakeString(mType));

	if (!mVersion.empty())
	{
		aRoot.Set("ver", Json::MakeString(mVersion));
	}
	if (mProto != 0)
	{
		aRoot.Set("proto", Json::MakeInt(mProto));
	}
	if (!mRes.empty())
	{
		aRoot.Set("res", Json::MakeString(mRes));
	}
	if (!mNick.empty())
	{
		aRoot.Set("nick", Json::MakeString(mNick));
	}
	if (mPid != 0)
	{
		aRoot.Set("pid", Json::MakeInt(mPid));
	}
	if (mServerTime != 0)
	{
		aRoot.Set("serverTime", Json::MakeInt(mServerTime));
	}

	if (!mCode.empty())
	{
		aRoot.Set("code", Json::MakeString(mCode));
	}
	if (!mName.empty())
	{
		aRoot.Set("name", Json::MakeString(mName));
	}
	if (!mMode.empty())
	{
		aRoot.Set("mode", Json::MakeString(mMode));
	}
	if (mMaxPlayers != 0)
	{
		aRoot.Set("maxp", Json::MakeInt(mMaxPlayers));
	}
	if (mHasReady)
	{
		aRoot.Set("ready", Json::MakeBool(mReady));
	}
	if (!mState.empty())
	{
		aRoot.Set("state", Json::MakeString(mState));
	}
	if (mHostPid != 0)
	{
		aRoot.Set("host", Json::MakeInt(mHostPid));
	}
	if (mSeed != 0)
	{
		aRoot.Set("seed", Json::MakeInt(mSeed));
	}
	if (mStartAt != 0)
	{
		aRoot.Set("startAt", Json::MakeInt(mStartAt));
	}

	if (!mReason.empty())
	{
		aRoot.Set("reason", Json::MakeString(mReason));
	}
	if (!mText.empty())
	{
		aRoot.Set("text", Json::MakeString(mText));
	}

	if (!mRole.empty())
	{
		aRoot.Set("role", Json::MakeString(mRole));
	}
	if (mHasPool)
	{
		Json aPool = Json::MakeObject();
		aPool.Set("plant", MakeStringArray(mPool.mPlant));
		aPool.Set("zombie", MakeStringArray(mPool.mZombie));
		aRoot.Set("pool", std::move(aPool));
	}
	if (mSlots != 0)
	{
		aRoot.Set("slots", Json::MakeInt(mSlots));
	}
	if (mDeadline != 0)
	{
		aRoot.Set("deadline", Json::MakeInt(mDeadline));
	}
	if (!mCards.empty())
	{
		aRoot.Set("cards", MakeStringArray(mCards));
	}
	if (mHasPicks)
	{
		Json aPicks = Json::MakeObject();
		aPicks.Set("plant", MakeStringArray(mPicks.mPlant));
		aPicks.Set("zombie", MakeStringArray(mPicks.mZombie));
		aRoot.Set("picks", std::move(aPicks));
	}
	if (!mDone.empty())
	{
		Json aDone = Json::MakeArray();
		for (const int aPid : mDone)
		{
			aDone.Push(Json::MakeInt(aPid));
		}
		aRoot.Set("done", std::move(aDone));
	}

	if (mPing != 0)
	{
		aRoot.Set("ping", Json::MakeInt(mPing));
	}

	return aRoot.Dump();
}

bool NetMsg::FromJson(const std::string& theText, NetMsg& theOut, std::string* theError)
{
	Json aRoot;
	if (!Json::Parse(theText, aRoot, theError))
	{
		return false;
	}
	if (aRoot.GetType() != Json::Type::Object)
	{
		if (theError)
		{
			*theError = "message is not an object";
		}
		return false;
	}

	NetMsg aMsg;
	aMsg.mType = aRoot.FindString("t");
	aMsg.mVersion = aRoot.FindString("ver");
	aMsg.mProto = static_cast<int>(aRoot.FindInt("proto"));
	aMsg.mRes = aRoot.FindString("res");
	aMsg.mNick = aRoot.FindString("nick");
	aMsg.mPid = static_cast<int>(aRoot.FindInt("pid"));
	aMsg.mServerTime = aRoot.FindInt("serverTime");

	aMsg.mCode = aRoot.FindString("code");
	aMsg.mName = aRoot.FindString("name");
	aMsg.mMode = aRoot.FindString("mode");
	aMsg.mMaxPlayers = static_cast<int>(aRoot.FindInt("maxp"));
	if (const Json* aReady = aRoot.Find("ready"))
	{
		aMsg.mHasReady = true;
		aMsg.mReady = aReady->AsBool();
	}
	aMsg.mState = aRoot.FindString("state");
	aMsg.mHostPid = static_cast<int>(aRoot.FindInt("host"));
	aMsg.mSeed = static_cast<uint32_t>(aRoot.FindInt("seed"));
	aMsg.mStartAt = aRoot.FindInt("startAt");

	aMsg.mError = static_cast<NetError>(aRoot.FindInt("err"));
	aMsg.mReason = aRoot.FindString("reason");
	aMsg.mText = aRoot.FindString("text");

	aMsg.mRole = aRoot.FindString("role");
	if (const Json* aPool = aRoot.Find("pool"))
	{
		aMsg.mHasPool = true;
		aMsg.mPool.mPlant = ReadStringArray(aPool->Find("plant"));
		aMsg.mPool.mZombie = ReadStringArray(aPool->Find("zombie"));
	}
	aMsg.mSlots = static_cast<int>(aRoot.FindInt("slots"));
	aMsg.mDeadline = aRoot.FindInt("deadline");
	aMsg.mCards = ReadStringArray(aRoot.Find("cards"));
	if (const Json* aPicks = aRoot.Find("picks"))
	{
		aMsg.mHasPicks = true;
		aMsg.mPicks.mPlant = ReadStringArray(aPicks->Find("plant"));
		aMsg.mPicks.mZombie = ReadStringArray(aPicks->Find("zombie"));
	}
	if (const Json* aDone = aRoot.Find("done"))
	{
		for (size_t i = 0; i < aDone->GetSize(); i++)
		{
			aMsg.mDone.push_back(static_cast<int>(aDone->At(i).AsInt()));
		}
	}

	aMsg.mPing = static_cast<int>(aRoot.FindInt("ping"));

	if (const Json* aList = aRoot.Find("list"))
	{
		for (size_t i = 0; i < aList->GetSize(); i++)
		{
			const Json& anItem = aList->At(i);
			RoomInfo aRoom;
			aRoom.mCode = anItem.FindString("code");
			aRoom.mName = anItem.FindString("name");
			aRoom.mHost = anItem.FindString("host");
			aRoom.mMode = anItem.FindString("mode");
			aRoom.mCount = static_cast<int>(anItem.FindInt("n"));
			aRoom.mMaxPlayers = static_cast<int>(anItem.FindInt("maxp"));
			aRoom.mState = anItem.FindString("state");
			aRoom.mRes = anItem.FindString("res");
			aMsg.mRooms.push_back(aRoom);
		}
	}

	if (const Json* aMembers = aRoot.Find("members"))
	{
		for (size_t i = 0; i < aMembers->GetSize(); i++)
		{
			const Json& anItem = aMembers->At(i);
			MemberInfo aMember;
			aMember.mPid = static_cast<int>(anItem.FindInt("pid"));
			aMember.mNick = anItem.FindString("nick");
			aMember.mReady = anItem.FindBool("ready");
			aMember.mHost = anItem.FindBool("host");
			aMember.mPing = static_cast<int>(anItem.FindInt("ping"));
			aMember.mRes = anItem.FindString("res");
			aMsg.mMembers.push_back(aMember);
		}
	}

	theOut = std::move(aMsg);
	return true;
}

NetMsg MakeHello(const std::string& theVersion, int theProto, const std::string& theRes, const std::string& theNick)
{
	NetMsg aMsg;
	aMsg.mType = MsgType::HELLO;
	aMsg.mVersion = theVersion;
	aMsg.mProto = theProto;
	aMsg.mRes = theRes;
	aMsg.mNick = theNick;
	return aMsg;
}

NetMsg MakeCreate(const std::string& theName, const std::string& theMode, int theMaxPlayers,
	const std::string& theHostRole)
{
	NetMsg aMsg;
	aMsg.mType = MsgType::CREATE;
	aMsg.mName = theName;
	aMsg.mMode = theMode;
	aMsg.mMaxPlayers = theMaxPlayers;
	aMsg.mRole = theHostRole;
	return aMsg;
}

NetMsg MakePick(const std::string& theRole, const std::vector<std::string>& theCards)
{
	NetMsg aMsg;
	aMsg.mType = MsgType::PICK;
	aMsg.mRole = theRole;
	aMsg.mCards = theCards;
	return aMsg;
}
}
