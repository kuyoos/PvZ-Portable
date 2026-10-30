// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Framing shared with server/internal/proto/frame.go:
//
//	[len u32 little endian][type u8][payload ...]
//
// len counts the type byte plus the payload.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace PvzpNet
{
constexpr uint32_t MAX_FRAME_SIZE = 64 * 1024;
constexpr size_t FRAME_HEADER_SIZE = 5;

enum FrameType : uint8_t
{
	FRAME_JSON = 1,
	FRAME_INPUT = 2,
	FRAME_PING = 3,
	FRAME_PONG = 4,
	FRAME_STATE = 5,
	FRAME_HASH = 6
};

enum class FrameStatus
{
	Ok,
	NeedMore,
	Error
};

// Appends one frame to theOut.
bool EncodeFrame(std::vector<uint8_t>& theOut, uint8_t theType, const void* theData, size_t theSize);

// Decodes one frame from theData. theConsumed reports how many bytes belong to
// the frame so the caller can erase them from its buffer.
FrameStatus DecodeFrame(const uint8_t* theData, size_t theSize, uint8_t& theType,
	std::vector<uint8_t>& thePayload, size_t& theConsumed);
}
