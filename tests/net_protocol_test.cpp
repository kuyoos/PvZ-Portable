// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Headless protocol tests for the lobby client codec. They only touch pure
// serialisation code, so they run without SDL, a window or a server.
#include <cstdio>
#include <string>
#include <vector>

#include "PvzpLib/net/NetFrame.h"
#include "PvzpLib/net/NetJson.h"
#include "PvzpLib/net/NetMsg.h"

using namespace PvzpNet;

namespace
{
int gFailures = 0;
int gChecks = 0;

void Check(bool theCondition, const char* theWhat, int theLine)
{
	gChecks++;
	if (!theCondition)
	{
		gFailures++;
		std::printf("FAIL line %d: %s\n", theLine, theWhat);
	}
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

void TestFrames()
{
	std::vector<uint8_t> aBuffer;
	CHECK(EncodeFrame(aBuffer, FRAME_JSON, "hello", 5));
	CHECK(aBuffer.size() == FRAME_HEADER_SIZE + 5);
	CHECK(aBuffer[0] == 6 && aBuffer[4] == FRAME_JSON);

	uint8_t aType = 0;
	std::vector<uint8_t> aPayload;
	size_t aConsumed = 0;
	FrameStatus aStatus = DecodeFrame(aBuffer.data(), aBuffer.size(), aType, aPayload, aConsumed);
	CHECK(aStatus == FrameStatus::Ok);
	CHECK(aType == FRAME_JSON);
	CHECK(aConsumed == aBuffer.size());
	CHECK(std::string(aPayload.begin(), aPayload.end()) == "hello");

	// Split delivery must not be treated as an error.
	aStatus = DecodeFrame(aBuffer.data(), 3, aType, aPayload, aConsumed);
	CHECK(aStatus == FrameStatus::NeedMore);
	aStatus = DecodeFrame(aBuffer.data(), aBuffer.size() - 1, aType, aPayload, aConsumed);
	CHECK(aStatus == FrameStatus::NeedMore);

	// A zero length frame is malformed.
	const uint8_t aBad[] = {0, 0, 0, 0, FRAME_JSON};
	aStatus = DecodeFrame(aBad, sizeof(aBad), aType, aPayload, aConsumed);
	CHECK(aStatus == FrameStatus::Error);

	// Oversized frames must be refused before allocating.
	std::vector<uint8_t> aBig(MAX_FRAME_SIZE, 0);
	CHECK(!EncodeFrame(aBuffer, FRAME_JSON, aBig.data(), aBig.size()));

	// Two frames back to back: the first one is consumed exactly.
	std::vector<uint8_t> aTwo;
	EncodeFrame(aTwo, FRAME_PING, "abcd", 4);
	EncodeFrame(aTwo, FRAME_HASH, "xy", 2);
	aStatus = DecodeFrame(aTwo.data(), aTwo.size(), aType, aPayload, aConsumed);
	CHECK(aStatus == FrameStatus::Ok && aType == FRAME_PING && aConsumed == 9);
	aStatus = DecodeFrame(aTwo.data() + aConsumed, aTwo.size() - aConsumed, aType, aPayload, aConsumed);
	CHECK(aStatus == FrameStatus::Ok && aType == FRAME_HASH);
}

void TestJson()
{
	Json aRoot;
	std::string aError;
	CHECK(Json::Parse(R"({"a":1,"b":"x\n\"y\"","c":[true,false,null],"d":-2.5})", aRoot, &aError));
	CHECK(aRoot.GetType() == Json::Type::Object);
	CHECK(aRoot.FindInt("a") == 1);
	CHECK(aRoot.FindString("b") == "x\n\"y\"");
	CHECK(aRoot.Find("c")->GetSize() == 3);
	CHECK(aRoot.Find("c")->At(2).IsNull());
	CHECK(aRoot.Find("d")->AsInt() == -2);

	// Escaped non-ASCII from Go's encoder.
	Json aUnicode;
	CHECK(Json::Parse(R"({"n":"\u73a9\u5bb6A"})", aUnicode, &aError));
	CHECK(aUnicode.FindString("n") == "玩家A");

	// Writer output has to parse back identically.
	Json aBuilt = Json::MakeObject();
	aBuilt.Set("t", Json::MakeString("room"));
	aBuilt.Set("name", Json::MakeString("老王的房间"));
	Json aList = Json::MakeArray();
	aList.Push(Json::MakeInt(2));
	aList.Push(Json::MakeInt(3));
	aBuilt.Set("list", std::move(aList));

	Json aReparsed;
	CHECK(Json::Parse(aBuilt.Dump(), aReparsed, &aError));
	CHECK(aReparsed.FindString("name") == "老王的房间");
	CHECK(aReparsed.Find("list")->GetSize() == 2);

	CHECK(!Json::Parse("{", aReparsed, &aError));
	CHECK(!Json::Parse("{} junk", aReparsed, &aError));
}

void TestMessages()
{
	const std::string aWelcome =
		R"({"t":"welcome","ver":"0.2.0","proto":1,"res":"1073-cn","pid":1,"serverTime":1793356400000})";
	NetMsg aMsg;
	std::string aError;
	CHECK(NetMsg::FromJson(aWelcome, aMsg, &aError));
	CHECK(aMsg.mType == MsgType::WELCOME);
	CHECK(aMsg.mVersion == "0.2.0");
	CHECK(aMsg.mProto == 1);
	CHECK(aMsg.mRes == "1073-cn");
	CHECK(aMsg.mPid == 1);
	CHECK(aMsg.mServerTime == 1793356400000LL);

	const std::string aRoom =
		R"({"t":"room","code":"GJDKMG","name":"老王的房间","host":2,"maxp":2,"state":"picking",)"
		R"("res":"1073-cn","seed":3938308940,"members":[{"pid":2,"nick":"PlayerA","ready":true,"host":true,"ping":7}]})";
	CHECK(NetMsg::FromJson(aRoom, aMsg, &aError));
	CHECK(aMsg.mCode == "GJDKMG");
	CHECK(aMsg.mName == "老王的房间");
	CHECK(aMsg.mState == RoomState::PICKING);
	CHECK(aMsg.mSeed == 3938308940u);
	CHECK(aMsg.mMembers.size() == 1);
	CHECK(aMsg.mMembers[0].mNick == "PlayerA");
	CHECK(aMsg.mMembers[0].mReady && aMsg.mMembers[0].mHost);
	CHECK(aMsg.mMembers[0].mPing == 7);

	const std::string aPickStart =
		R"({"t":"pick_start","code":"GJDKMG","role":"zombie",)"
		R"("pool":{"plant":["SEED_SUNFLOWER","SEED_PEASHOOTER"],"zombie":["SEED_ZOMBIE_NORMAL"]},)"
		R"("slots":6,"deadline":1793356480399})";
	CHECK(NetMsg::FromJson(aPickStart, aMsg, &aError));
	CHECK(aMsg.mRole == Role::ZOMBIE);
	CHECK(aMsg.mHasPool);
	CHECK(aMsg.mPool.mPlant.size() == 2);
	CHECK(aMsg.mPool.mZombie[0] == "SEED_ZOMBIE_NORMAL");
	CHECK(aMsg.mSlots == 6);
	CHECK(aMsg.mDeadline == 1793356480399LL);

	const std::string aStart =
		R"({"t":"start","code":"GJDKMG","seed":3938308940,"startAt":1793356424904,"res":"1073-cn",)"
		R"("picks":{"plant":["SEED_REPEATER"],"zombie":["SEED_ZOMBIE_LADDER"]}})";
	CHECK(NetMsg::FromJson(aStart, aMsg, &aError));
	CHECK(aMsg.mHasPicks);
	CHECK(aMsg.mPicks.mPlant.size() == 1 && aMsg.mPicks.mPlant[0] == "SEED_REPEATER");
	CHECK(aMsg.mPicks.mZombie[0] == "SEED_ZOMBIE_LADDER");

	const std::string aPickState = R"({"t":"pick_state","code":"GJDKMG","done":[2,3]})";
	CHECK(NetMsg::FromJson(aPickState, aMsg, &aError));
	CHECK(aMsg.mDone.size() == 2 && aMsg.mDone[1] == 3);

	// Round trip of an outgoing pick message.
	const NetMsg aPick = MakePick(Role::PLANT,
		{"SEED_SUNFLOWER", "SEED_PEASHOOTER", "SEED_WALLNUT", "SEED_SNOWPEA", "SEED_CHERRYBOMB", "SEED_POTATOMINE"});
	const std::string aPickText = aPick.ToJson();
	CHECK(aPickText.find("\"t\":\"pick\"") != std::string::npos);
	CHECK(NetMsg::FromJson(aPickText, aMsg, &aError));
	CHECK(aMsg.mRole == Role::PLANT);
	CHECK(aMsg.mCards.size() == 6);
	CHECK(aMsg.mCards[5] == "SEED_POTATOMINE");

	// Hello carries the version triple the server validates.
	const NetMsg aHello = MakeHello("0.2.0", 1, "1073-cn", "玩家A");
	const std::string aHelloText = aHello.ToJson();
	CHECK(aHelloText.find("\"proto\":1") != std::string::npos);
	CHECK(NetMsg::FromJson(aHelloText, aMsg, &aError));
	CHECK(aMsg.mNick == "玩家A");

	// The error frame carries the numeric code used by the UI.
	CHECK(NetMsg::FromJson(R"({"t":"err","err":1012,"text":"卡组数量必须是 6 张"})", aMsg, &aError));
	CHECK(aMsg.mError == NetError::Pick);
	CHECK(aMsg.mText == "卡组数量必须是 6 张");

	CHECK(!NetMsg::FromJson("not json", aMsg, &aError));
}
}

int main()
{
	TestFrames();
	TestJson();
	TestMessages();

	if (gFailures == 0)
	{
		std::printf("net protocol tests passed (%d checks)\n", gChecks);
		return 0;
	}

	std::printf("net protocol tests failed: %d/%d\n", gFailures, gChecks);
	return 1;
}
