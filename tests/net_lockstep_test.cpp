/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * Headless lockstep test: two peers exchange INPUT payloads over an in-memory
 * link and must execute exactly the same action list on every tick.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "../src/PvzpLib/net/Lockstep.h"

using namespace PvzpNet;

namespace
{

int gFailures = 0;

void Check(bool theCondition, const char* theWhat)
{
	if (!theCondition)
	{
		std::printf("FAIL: %s\n", theWhat);
		gFailures++;
	}
}

uint32_t Mix(uint32_t h, uint32_t v)
{
	h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

// Both peers see the same actions in a different order, so the digest sorts.
uint32_t Digest(std::vector<Action>& theActions)
{
	std::sort(theActions.begin(), theActions.end(), [](const Action& a, const Action& b) {
		if (a.mKind != b.mKind) return a.mKind < b.mKind;
		if (a.mX != b.mX) return a.mX < b.mX;
		if (a.mY != b.mY) return a.mY < b.mY;
		return a.mSeed < b.mSeed;
	});

	uint32_t h = 2166136261u;
	for (const Action& a : theActions)
	{
		h = Mix(h, (static_cast<uint32_t>(a.mKind) << 24) |
			(static_cast<uint32_t>(static_cast<uint16_t>(a.mX)) << 8) |
			static_cast<uint32_t>(static_cast<uint16_t>(a.mY)));
		h = Mix(h, a.mSeed);
	}
	return h;
}

void TestCodec()
{
	std::vector<Frame> aFrames;
	Frame a;
	a.mTick = 42;
	a.mActions.push_back({ACTION_PLANT, 3, 4, 17});
	a.mActions.push_back({ACTION_SHOVEL, -1, 5, 0});
	aFrames.push_back(a);
	Frame b;
	b.mTick = 43;
	aFrames.push_back(b);

	const std::vector<uint8_t> aPayload = Lockstep::EncodeFrames(aFrames);
	std::vector<Frame> aDecoded;
	Check(Lockstep::DecodeFrames(aPayload, aDecoded), "encode/decode round trip");
	Check(aDecoded.size() == 2, "frame count survives the round trip");
	if (aDecoded.size() == 2)
	{
		Check(aDecoded[0].mTick == 42 && aDecoded[1].mTick == 43, "tick numbers survive");
		Check(aDecoded[0].mActions.size() == 2, "action count survives");
		Check(aDecoded[0].mActions[0] == Action({ACTION_PLANT, 3, 4, 17}), "plant action survives");
		Check(aDecoded[0].mActions[1] == Action({ACTION_SHOVEL, -1, 5, 0}), "negative x survives");
		Check(aDecoded[1].mActions.empty(), "empty frame survives");
	}

	std::vector<uint8_t> aTruncated = aPayload;
	aTruncated.resize(aPayload.size() - 3);
	std::vector<Frame> aIgnored;
	Check(!Lockstep::DecodeFrames(aTruncated, aIgnored), "truncated payload is rejected");

	Check(!Lockstep::DecodeFrames(std::vector<uint8_t>{5, 1}, aIgnored), "short frame header is rejected");
}

void TestGating()
{
	Lockstep a;
	a.PushLocal({ACTION_PLANT, 1, 2, 3});
	// Nothing from the peer yet: the simulation may not run.
	Check(!a.CanAdvance(), "a lone peer cannot advance");

	a.OnRemoteInput(Lockstep::EncodeFrames({}));
	Check(!a.CanAdvance(), "an empty payload carries no frames");

	std::vector<Frame> aFrames;
	for (uint32_t t = 1; t <= Lockstep::DELAY + 1; t++)
	{
		Frame f;
		f.mTick = t;
		aFrames.push_back(f);
	}
	a.OnRemoteInput(Lockstep::EncodeFrames(aFrames));

	int aTicks = 0;
	std::vector<Action> aAll;
	while (a.CanAdvance() && aTicks < 10)
	{
		std::vector<Action> aReady = a.Advance();
		aAll.insert(aAll.end(), aReady.begin(), aReady.end());
		aTicks++;
	}
	Check(aTicks == static_cast<int>(Lockstep::DELAY + 1), "advances only as far as the peer's frames");
	Check(aAll.size() == 1, "the local action is executed on the delayed tick");
	if (!aAll.empty())
	{
		Check(aAll[0] == Action({ACTION_PLANT, 1, 2, 3}), "the local action keeps its payload");
	}
}

void TestTwoPeers()
{
	Lockstep a;
	Lockstep b;
	std::mt19937 rng(20261030u);

	std::vector<uint32_t> aDigests;
	std::vector<uint32_t> bDigests;

	for (int i = 0; i < 2000; i++)
	{
		if (rng() % 5 == 0)
		{
			a.PushLocal({ACTION_PLANT, static_cast<int16_t>(rng() % 9), static_cast<int16_t>(rng() % 6),
				static_cast<uint8_t>(rng() % 49)});
		}
		if (rng() % 4 == 0)
		{
			b.PushLocal({ACTION_ZOMBIE, static_cast<int16_t>(rng() % 9), static_cast<int16_t>(rng() % 6),
				static_cast<uint8_t>(60 + rng() % 15)});
		}

		std::vector<Frame> aOut = a.TakeOutbound();
		if (!aOut.empty())
		{
			b.OnRemoteInput(Lockstep::EncodeFrames(aOut));
		}
		std::vector<Frame> bOut = b.TakeOutbound();
		if (!bOut.empty())
		{
			a.OnRemoteInput(Lockstep::EncodeFrames(bOut));
		}

		if (a.CanAdvance() && b.CanAdvance())
		{
			std::vector<Action> aReady = a.Advance();
			std::vector<Action> bReady = b.Advance();
			aDigests.push_back(Digest(aReady));
			bDigests.push_back(Digest(bReady));
		}
	}

	Check(a.Tick() == b.Tick(), "both peers stop on the same tick");
	if (a.Tick() <= 1000)
	{
		std::printf("stalled at tick %u (peer %u, digests %zu/%zu)\n",
			a.Tick(), b.Tick(), aDigests.size(), bDigests.size());
	}
	Check(a.Tick() > 1000, "the pipeline keeps running without stalling");
	Check(aDigests.size() == bDigests.size(), "both peers executed the same number of ticks");
	Check(aDigests == bDigests, "both peers executed identical action streams");
	Check(!a.Desynced() && !b.Desynced(), "no desync was latched");
}

void TestHashMismatch()
{
	Lockstep a;
	a.OnLocalHash(100, 0x1234);
	a.OnRemoteHash(100, 0x1234);
	Check(!a.Desynced(), "equal hashes do not latch a desync");

	Lockstep b;
	b.OnRemoteHash(100, 0x1234);
	b.OnLocalHash(100, 0x5678);
	Check(b.Desynced(), "differing hashes latch a desync");
}

}

int main()
{
	TestCodec();
	TestGating();
	TestTwoPeers();
	TestHashMismatch();

	if (gFailures != 0)
	{
		std::printf("%d lockstep check(s) failed\n", gFailures);
		return 1;
	}

	std::printf("lockstep: all checks passed\n");
	return 0;
}
