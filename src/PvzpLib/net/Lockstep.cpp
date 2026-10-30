/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 */

#include "Lockstep.h"

namespace PvzpNet
{

void Lockstep::Reset()
{
	mTick = 0;
	mFlushed = 0;
	mLocal.clear();
	mRemote.clear();
	mOutbound.clear();
	mLocalHashes.clear();
	mRemoteHashes.clear();
	mDesynced = false;

	// Prime the pipe: the first DELAY-1 frames cannot contain local input, and
	// the peer needs them before it is allowed to run its first tick.
	FlushClosed();
}

void Lockstep::PushLocal(const Action& theAction)
{
	mLocal[mTick + DELAY].push_back(theAction);
}

bool Lockstep::CanAdvance() const
{
	return mRemote.find(mTick + 1) != mRemote.end();
}

std::vector<Action> Lockstep::Advance()
{
	std::vector<Action> aReady;

	auto aLocalIt = mLocal.find(mTick + 1);
	if (aLocalIt != mLocal.end())
	{
		aReady.insert(aReady.end(), aLocalIt->second.begin(), aLocalIt->second.end());
		mLocal.erase(aLocalIt);
	}

	auto aRemoteIt = mRemote.find(mTick + 1);
	if (aRemoteIt != mRemote.end())
	{
		aReady.insert(aReady.end(), aRemoteIt->second.begin(), aRemoteIt->second.end());
		mRemote.erase(aRemoteIt);
	}

	mTick++;
	FlushClosed();

	// Drop anything that can no longer be executed.
	mRemote.erase(mRemote.begin(), mRemote.upper_bound(mTick));
	mLocal.erase(mLocal.begin(), mLocal.upper_bound(mTick));

	return aReady;
}

void Lockstep::FlushClosed()
{
	// A local action targets mTick + DELAY, so every tick below that is final.
	const uint32_t aClosed = mTick + DELAY - 1;
	while (mFlushed < aClosed)
	{
		mFlushed++;

		Frame aFrame;
		aFrame.mTick = mFlushed;
		// Copy, do not consume: the same actions still have to run locally when
		// the simulation reaches this tick.
		auto aIt = mLocal.find(mFlushed);
		if (aIt != mLocal.end())
		{
			aFrame.mActions = aIt->second;
		}
		mOutbound.push_back(aFrame);
	}
}

std::vector<Frame> Lockstep::TakeOutbound()
{
	if (mOutbound.size() < BATCH)
	{
		return {};
	}
	std::vector<Frame> aFrames;
	aFrames.swap(mOutbound);
	return aFrames;
}

void Lockstep::OnRemoteInput(const std::vector<uint8_t>& thePayload)
{
	std::vector<Frame> aFrames;
	if (!DecodeFrames(thePayload, aFrames))
	{
		return;
	}
	for (const Frame& aFrame : aFrames)
	{
		if (aFrame.mTick <= mTick)
		{
			continue;  // already executed, cannot rewind
		}
		mRemote[aFrame.mTick] = aFrame.mActions;
	}
}

void Lockstep::OnLocalHash(uint32_t theTick, uint32_t theHash)
{
	mLocalHashes[theTick] = theHash;
	auto aIt = mRemoteHashes.find(theTick);
	if (aIt != mRemoteHashes.end())
	{
		if (aIt->second != theHash)
		{
			mDesynced = true;
		}
		mRemoteHashes.erase(aIt);
	}
}

void Lockstep::OnRemoteHash(uint32_t theTick, uint32_t theHash)
{
	mRemoteHashes[theTick] = theHash;
	auto aIt = mLocalHashes.find(theTick);
	if (aIt != mLocalHashes.end())
	{
		if (aIt->second != theHash)
		{
			mDesynced = true;
		}
		mLocalHashes.erase(aIt);
	}
}

std::vector<uint8_t> Lockstep::EncodeFrames(const std::vector<Frame>& theFrames)
{
	std::vector<uint8_t> aOut;
	aOut.push_back(static_cast<uint8_t>(theFrames.size()));
	for (const Frame& aFrame : theFrames)
	{
		aOut.push_back(static_cast<uint8_t>(aFrame.mTick & 0xFF));
		aOut.push_back(static_cast<uint8_t>((aFrame.mTick >> 8) & 0xFF));
		aOut.push_back(static_cast<uint8_t>((aFrame.mTick >> 16) & 0xFF));
		aOut.push_back(static_cast<uint8_t>((aFrame.mTick >> 24) & 0xFF));

		aOut.push_back(static_cast<uint8_t>(aFrame.mActions.size() & 0xFF));
		aOut.push_back(static_cast<uint8_t>((aFrame.mActions.size() >> 8) & 0xFF));
		for (const Action& aAction : aFrame.mActions)
		{
			aOut.push_back(aAction.mKind);
			aOut.push_back(static_cast<uint8_t>(aAction.mX & 0xFF));
			aOut.push_back(static_cast<uint8_t>((aAction.mX >> 8) & 0xFF));
			aOut.push_back(static_cast<uint8_t>(aAction.mY & 0xFF));
			aOut.push_back(static_cast<uint8_t>((aAction.mY >> 8) & 0xFF));
			aOut.push_back(aAction.mSeed);
		}
	}
	return aOut;
}

bool Lockstep::DecodeFrames(const std::vector<uint8_t>& thePayload, std::vector<Frame>& theFrames)
{
	theFrames.clear();
	size_t aPos = 0;
	if (thePayload.size() < 1)
	{
		return false;
	}

	const size_t aFrameCount = thePayload[aPos++];
	for (size_t i = 0; i < aFrameCount; i++)
	{
		if (aPos + 6 > thePayload.size())
		{
			return false;
		}

		Frame aFrame;
		aFrame.mTick = static_cast<uint32_t>(thePayload[aPos]) |
			(static_cast<uint32_t>(thePayload[aPos + 1]) << 8) |
			(static_cast<uint32_t>(thePayload[aPos + 2]) << 16) |
			(static_cast<uint32_t>(thePayload[aPos + 3]) << 24);
		const size_t aActionCount = static_cast<size_t>(thePayload[aPos + 4]) |
			(static_cast<size_t>(thePayload[aPos + 5]) << 8);
		aPos += 6;

		if (aPos + aActionCount * 6 > thePayload.size())
		{
			return false;
		}
		for (size_t j = 0; j < aActionCount; j++)
		{
			Action aAction;
			aAction.mKind = thePayload[aPos];
			aAction.mX = static_cast<int16_t>(static_cast<uint16_t>(thePayload[aPos + 1]) |
				(static_cast<uint16_t>(thePayload[aPos + 2]) << 8));
			aAction.mY = static_cast<int16_t>(static_cast<uint16_t>(thePayload[aPos + 3]) |
				(static_cast<uint16_t>(thePayload[aPos + 4]) << 8));
			aAction.mSeed = thePayload[aPos + 5];
			aPos += 6;
			aFrame.mActions.push_back(aAction);
		}
		theFrames.push_back(aFrame);
	}

	return true;
}

}
