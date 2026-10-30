// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Background lobby connection. The game thread only calls the non-blocking
// Send*/Poll* methods; a reader thread owns the socket for receiving, a mutex
// serialises writes. No SDL_net dependency: plain BSD sockets, and a stub on
// Emscripten where the browser build has no raw TCP.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "NetFrame.h"
#include "NetMsg.h"

namespace PvzpNet
{
class NetClient
{
public:
	NetClient() = default;
	~NetClient();

	NetClient(const NetClient&) = delete;
	NetClient& operator=(const NetClient&) = delete;

	// Blocking connect with a 3 second timeout.
	bool Connect(const std::string& theHost, int thePort, std::string* theError = nullptr);
	void Disconnect();

	bool IsConnected() const { return mConnected.load(); }
	int GetPingMs() const { return mPingMs.load(); }
	int GetPid() const { return mPid.load(); }
	bool IsWebBuild() const;

	bool SendJson(const NetMsg& theMsg);
	bool SendFrame(uint8_t theType, const void* theData, size_t theSize);

	// Game thread: pops one received control message.
	bool PollMessage(NetMsg& theOut);
	// Game thread: pops one disconnect reason, if the link dropped.
	bool PollDisconnect(std::string& theReason);

	// Call once every couple of seconds; keeps the server's idle timer alive and
	// produces the latency number shown in the room UI.
	void SendPing();

private:
	void ReaderLoop();
	void PushMessage(NetMsg&& theMsg);
	void CloseSocket();

	std::thread mReader;
	std::atomic<bool> mRunning{false};
	std::atomic<bool> mConnected{false};
	std::atomic<int> mPid{0};
	std::atomic<int> mPingMs{0};
	std::atomic<uint32_t> mPingSeq{0};

	intptr_t mSocket = -1;
	std::mutex mWriteMutex;
	std::mutex mQueueMutex;
	std::deque<NetMsg> mMessages;
	std::deque<std::string> mDisconnects;
	std::mutex mPingMutex;
	std::map<uint32_t, int64_t> mPingSent;
};
}
