/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 */

#ifndef __PVZP_NET_LOCKSTEP_H__
#define __PVZP_NET_LOCKSTEP_H__

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace PvzpNet
{

// Semantic input: everything the simulation needs, never a raw mouse event.
enum ActionKind : uint8_t
{
	ACTION_PLANT = 1,      // seed placed on the lawn
	ACTION_ZOMBIE = 2,     // zombie card dropped on the lawn
	ACTION_SHOVEL = 3,     // shovel used on a grid cell
	ACTION_MOWER = 4,      // lawn mower triggered
	NUM_ACTION_KINDS
};

struct Action
{
	uint8_t mKind = 0;
	int16_t mX = 0;
	int16_t mY = 0;
	uint8_t mSeed = 0;

	bool operator==(const Action& theOther) const
	{
		return mKind == theOther.mKind && mX == theOther.mX && mY == theOther.mY && mSeed == theOther.mSeed;
	}
};

struct Frame
{
	uint32_t mTick = 0;
	std::vector<Action> mActions;
};

// Deterministic lockstep scheduler for one local peer.
//
// The simulation only advances when every peer's frame for the next tick is
// already in hand; local actions are scheduled DELAY ticks ahead so they have
// the same travel budget in both directions.
class Lockstep
{
public:
	static constexpr uint32_t DELAY = 6;
	static constexpr uint32_t BATCH = 5;
	static constexpr uint32_t HASH_INTERVAL = 100;

	Lockstep() { Reset(); }

	void Reset();

	// Queue a locally produced action; it executes at tick() + DELAY.
	void PushLocal(const Action& theAction);

	// True when every remote frame for tick() + 1 has arrived.
	bool CanAdvance() const;

	// Consume one tick. The returned actions are the ones to execute now.
	std::vector<Action> Advance();

	// Feed a decoded INPUT payload (frames for one or more ticks).
	void OnRemoteInput(const std::vector<uint8_t>& thePayload);

	// Take the pending outbound frames; empty when nothing is due yet.
	std::vector<Frame> TakeOutbound();

	uint32_t Tick() const { return mTick; }
	bool Desynced() const { return mDesynced; }
	void MarkDesynced() { mDesynced = true; }

	// Hash bookkeeping: local and peer hashes for a tick are compared as soon
	// as both are known; a mismatch latches Desynced().
	void OnLocalHash(uint32_t theTick, uint32_t theHash);
	void OnRemoteHash(uint32_t theTick, uint32_t theHash);

	static std::vector<uint8_t> EncodeFrames(const std::vector<Frame>& theFrames);
	static bool DecodeFrames(const std::vector<uint8_t>& thePayload, std::vector<Frame>& theFrames);

private:
	// Frames for ticks that can no longer receive local input have been queued.
	void FlushClosed();

	uint32_t mTick = 0;
	uint32_t mFlushed = 0;
	std::map<uint32_t, std::vector<Action>> mLocal;
	std::map<uint32_t, std::vector<Action>> mRemote;
	std::vector<Frame> mOutbound;
	std::map<uint32_t, uint32_t> mLocalHashes;
	std::map<uint32_t, uint32_t> mRemoteHashes;
	bool mDesynced = false;
};

}

#endif
