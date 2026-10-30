// SPDX-License-Identifier: LGPL-3.0-or-later
#include "NetFrame.h"

#include <cstring>

namespace PvzpNet
{
bool EncodeFrame(std::vector<uint8_t>& theOut, uint8_t theType, const void* theData, size_t theSize)
{
	if (theSize + 1 > MAX_FRAME_SIZE)
	{
		return false;
	}

	const uint32_t aLength = static_cast<uint32_t>(theSize + 1);
	theOut.push_back(static_cast<uint8_t>(aLength & 0xFF));
	theOut.push_back(static_cast<uint8_t>((aLength >> 8) & 0xFF));
	theOut.push_back(static_cast<uint8_t>((aLength >> 16) & 0xFF));
	theOut.push_back(static_cast<uint8_t>((aLength >> 24) & 0xFF));
	theOut.push_back(theType);
	if (theSize > 0 && theData != nullptr)
	{
		const auto* aBytes = static_cast<const uint8_t*>(theData);
		theOut.insert(theOut.end(), aBytes, aBytes + theSize);
	}
	return true;
}

FrameStatus DecodeFrame(const uint8_t* theData, size_t theSize, uint8_t& theType,
	std::vector<uint8_t>& thePayload, size_t& theConsumed)
{
	theConsumed = 0;
	if (theSize < FRAME_HEADER_SIZE)
	{
		return FrameStatus::NeedMore;
	}

	const uint32_t aLength = static_cast<uint32_t>(theData[0]) |
		(static_cast<uint32_t>(theData[1]) << 8) |
		(static_cast<uint32_t>(theData[2]) << 16) |
		(static_cast<uint32_t>(theData[3]) << 24);

	if (aLength < 1 || aLength > MAX_FRAME_SIZE)
	{
		return FrameStatus::Error;
	}
	if (theSize < 4 + aLength)
	{
		return FrameStatus::NeedMore;
	}

	theType = theData[4];
	thePayload.assign(theData + FRAME_HEADER_SIZE, theData + 4 + aLength);
	theConsumed = 4 + aLength;
	return FrameStatus::Ok;
}
}
