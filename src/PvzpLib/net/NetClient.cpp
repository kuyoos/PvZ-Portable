// SPDX-License-Identifier: LGPL-3.0-or-later
#include "NetClient.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>

#if defined(__EMSCRIPTEN__)
// The browser build has no raw TCP sockets; use the WebSocket gateway once it
// exists. Everything below compiles to a stub there.
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

namespace PvzpNet
{
namespace
{
int64_t NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

#if defined(_WIN32)
bool EnsureWinsock()
{
	static const bool aReady = []() {
		WSADATA aData;
		return WSAStartup(MAKEWORD(2, 2), &aData) == 0;
	}();
	return aReady;
}
#endif

#if !defined(__EMSCRIPTEN__)
bool SetBlocking(intptr_t theSocket, bool theBlocking)
{
#if defined(_WIN32)
	u_long aValue = theBlocking ? 0 : 1;
	return ioctlsocket(static_cast<SOCKET>(theSocket), FIONBIO, &aValue) == 0;
#else
	const int aFlags = fcntl(static_cast<int>(theSocket), F_GETFL, 0);
	if (aFlags < 0)
	{
		return false;
	}
	const int aNewFlags = theBlocking ? (aFlags & ~O_NONBLOCK) : (aFlags | O_NONBLOCK);
	return fcntl(static_cast<int>(theSocket), F_SETFL, aNewFlags) == 0;
#endif
}
#endif
}

NetClient::~NetClient()
{
	Disconnect();
}

bool NetClient::IsWebBuild() const
{
#if defined(__EMSCRIPTEN__)
	return true;
#else
	return false;
#endif
}

bool NetClient::Connect(const std::string& theHost, int thePort, std::string* theError)
{
#if defined(__EMSCRIPTEN__)
	(void)theHost;
	(void)thePort;
	if (theError)
	{
		*theError = "网页版暂不支持联机";
	}
	return false;
#else
	Disconnect();

#if defined(_WIN32)
	if (!EnsureWinsock())
	{
		if (theError)
		{
			*theError = "网络初始化失败";
		}
		return false;
	}
#endif

	addrinfo aHints{};
	aHints.ai_family = AF_UNSPEC;
	aHints.ai_socktype = SOCK_STREAM;

	addrinfo* aAddresses = nullptr;
	const std::string aPort = std::to_string(thePort);
	if (getaddrinfo(theHost.c_str(), aPort.c_str(), &aHints, &aAddresses) != 0)
	{
		if (theError)
		{
			*theError = "无法解析服务器地址";
		}
		return false;
	}

	intptr_t aConnectedSocket = -1;
	for (addrinfo* aIt = aAddresses; aIt != nullptr && aConnectedSocket < 0; aIt = aIt->ai_next)
	{
		intptr_t aCandidate;
#if defined(_WIN32)
		aCandidate = static_cast<intptr_t>(socket(aIt->ai_family, aIt->ai_socktype, aIt->ai_protocol));
		if (aCandidate == static_cast<intptr_t>(INVALID_SOCKET))
		{
			continue;
		}
#else
		aCandidate = static_cast<intptr_t>(socket(aIt->ai_family, aIt->ai_socktype, aIt->ai_protocol));
		if (aCandidate < 0)
		{
			continue;
		}
#endif

		// Non blocking connect so a dead host cannot freeze the game for the
		// whole TCP timeout.
		SetBlocking(aCandidate, false);

#if defined(_WIN32)
		const int aResult = connect(static_cast<SOCKET>(aCandidate), aIt->ai_addr,
			static_cast<int>(aIt->ai_addrlen));
		const int aLastError = aResult == 0 ? 0 : WSAGetLastError();
		const bool aPending = aLastError == WSAEWOULDBLOCK || aLastError == WSAEINPROGRESS;
#else
		const int aResult = connect(static_cast<int>(aCandidate), aIt->ai_addr, aIt->ai_addrlen);
		const bool aPending = aResult != 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK);
#endif

		bool aReady = aResult == 0;
		if (!aReady && aPending)
		{
			fd_set aWriteSet;
			FD_ZERO(&aWriteSet);
			timeval aTimeout{3, 0};
#if defined(_WIN32)
			FD_SET(static_cast<SOCKET>(aCandidate), &aWriteSet);
			const int aSelected = select(0, nullptr, &aWriteSet, nullptr, &aTimeout);
#else
			FD_SET(static_cast<int>(aCandidate), &aWriteSet);
			const int aSelected = select(static_cast<int>(aCandidate) + 1, nullptr, &aWriteSet, nullptr, &aTimeout);
#endif
			if (aSelected > 0)
			{
				int aSocketError = 0;
#if defined(_WIN32)
				int aLength = sizeof(aSocketError);
				getsockopt(static_cast<SOCKET>(aCandidate), SOL_SOCKET, SO_ERROR,
					reinterpret_cast<char*>(&aSocketError), &aLength);
#else
				socklen_t aLength = sizeof(aSocketError);
				getsockopt(static_cast<int>(aCandidate), SOL_SOCKET, SO_ERROR, &aSocketError, &aLength);
#endif
				aReady = aSocketError == 0;
			}
		}

		if (!aReady)
		{
#if defined(_WIN32)
			closesocket(static_cast<SOCKET>(aCandidate));
#else
			close(static_cast<int>(aCandidate));
#endif
			continue;
		}

		// Back to blocking mode: the reader thread wants to sit in recv().
		SetBlocking(aCandidate, true);

		int aNoDelay = 1;
#if defined(_WIN32)
		setsockopt(static_cast<SOCKET>(aCandidate), IPPROTO_TCP, TCP_NODELAY,
			reinterpret_cast<const char*>(&aNoDelay), sizeof(aNoDelay));
#else
		setsockopt(static_cast<int>(aCandidate), IPPROTO_TCP, TCP_NODELAY, &aNoDelay, sizeof(aNoDelay));
#endif
		aConnectedSocket = aCandidate;
	}

	freeaddrinfo(aAddresses);

	if (aConnectedSocket < 0)
	{
		if (theError)
		{
			*theError = "连接服务器失败";
		}
		return false;
	}

	mSocket = aConnectedSocket;
	mRunning.store(true);
	mConnected.store(true);
	mReader = std::thread(&NetClient::ReaderLoop, this);
	return true;
#endif
}

void NetClient::Disconnect()
{
	mRunning.store(false);
	mConnected.store(false);
	CloseSocket();
	if (mReader.joinable())
	{
		mReader.join();
	}
}

void NetClient::CloseSocket()
{
	std::lock_guard<std::mutex> aLock(mWriteMutex);
	const intptr_t aSocket = mSocket;
	mSocket = -1;
	if (aSocket < 0)
	{
		return;
	}
#if defined(_WIN32)
	closesocket(static_cast<SOCKET>(aSocket));
#elif !defined(__EMSCRIPTEN__)
	close(static_cast<int>(aSocket));
#else
	(void)aSocket;
#endif
}

bool NetClient::SendFrame(uint8_t theType, const void* theData, size_t theSize)
{
	if (!mConnected.load())
	{
		return false;
	}

	std::vector<uint8_t> aFrame;
	if (!EncodeFrame(aFrame, theType, theData, theSize))
	{
		return false;
	}

	std::lock_guard<std::mutex> aLock(mWriteMutex);
	if (mSocket < 0)
	{
		return false;
	}

	size_t aSent = 0;
	while (aSent < aFrame.size())
	{
#if defined(_WIN32)
		const int aResult = send(static_cast<SOCKET>(mSocket),
			reinterpret_cast<const char*>(aFrame.data() + aSent), static_cast<int>(aFrame.size() - aSent), 0);
#elif defined(__EMSCRIPTEN__)
		const int aResult = -1;
#else
		const ssize_t aResult = send(static_cast<int>(mSocket), aFrame.data() + aSent,
			aFrame.size() - aSent, MSG_NOSIGNAL);
#endif
		if (aResult <= 0)
		{
			mConnected.store(false);
			return false;
		}
		aSent += static_cast<size_t>(aResult);
	}
	return true;
}

bool NetClient::SendJson(const NetMsg& theMsg)
{
	const std::string aText = theMsg.ToJson();
	return SendFrame(FRAME_JSON, aText.data(), aText.size());
}

void NetClient::ReaderLoop()
{
#if defined(__EMSCRIPTEN__)
	return;
#else
	const intptr_t aSocket = mSocket;
	std::vector<uint8_t> aBuffer;
	char aChunk[8192];

	while (mRunning.load())
	{
#if defined(_WIN32)
		const int aRead = recv(static_cast<SOCKET>(aSocket), aChunk, sizeof(aChunk), 0);
#else
		const ssize_t aRead = recv(static_cast<int>(aSocket), aChunk, sizeof(aChunk), 0);
#endif
		if (aRead <= 0)
		{
			break;
		}

		aBuffer.insert(aBuffer.end(), aChunk, aChunk + aRead);

		bool aBroken = false;
		while (true)
		{
			uint8_t aType = 0;
			std::vector<uint8_t> aPayload;
			size_t aConsumed = 0;
			const FrameStatus aStatus = DecodeFrame(aBuffer.data(), aBuffer.size(), aType, aPayload, aConsumed);
			if (aStatus == FrameStatus::NeedMore)
			{
				break;
			}
			if (aStatus == FrameStatus::Error)
			{
				aBroken = true;
				break;
			}

			aBuffer.erase(aBuffer.begin(), aBuffer.begin() + static_cast<ptrdiff_t>(aConsumed));

			if (aType == FRAME_JSON)
			{
				const std::string aText(aPayload.begin(), aPayload.end());
				NetMsg aMsg;
				if (NetMsg::FromJson(aText, aMsg, nullptr))
				{
					PushMessage(std::move(aMsg));
				}
			}
			else if (aType == FRAME_PONG && aPayload.size() >= 8)
			{
				const uint32_t aSeq = static_cast<uint32_t>(aPayload[0]) |
					(static_cast<uint32_t>(aPayload[1]) << 8) |
					(static_cast<uint32_t>(aPayload[2]) << 16) |
					(static_cast<uint32_t>(aPayload[3]) << 24);

				std::lock_guard<std::mutex> aLock(mPingMutex);
				const auto anIt = mPingSent.find(aSeq);
				if (anIt != mPingSent.end())
				{
					const int64_t aRtt = NowMs() - anIt->second;
					mPingMs.store(static_cast<int>(aRtt < 0 ? 0 : aRtt));
					mPingSent.erase(anIt);
				}
			}
		}

		if (aBroken)
		{
			break;
		}
	}

	const bool aWasConnected = mConnected.exchange(false);
	mRunning.store(false);

	if (aWasConnected)
	{
		std::lock_guard<std::mutex> aLock(mQueueMutex);
		mDisconnects.push_back("与服务器的连接已断开");
	}
#endif
}

void NetClient::PushMessage(NetMsg&& theMsg)
{
	std::lock_guard<std::mutex> aLock(mQueueMutex);
	if (mMessages.size() >= 512)
	{
		mMessages.pop_front();
	}
	mMessages.push_back(std::move(theMsg));
}

bool NetClient::PollMessage(NetMsg& theOut)
{
	std::lock_guard<std::mutex> aLock(mQueueMutex);
	if (mMessages.empty())
	{
		return false;
	}
	theOut = std::move(mMessages.front());
	mMessages.pop_front();
	return true;
}

bool NetClient::PollDisconnect(std::string& theReason)
{
	std::lock_guard<std::mutex> aLock(mQueueMutex);
	if (mDisconnects.empty())
	{
		return false;
	}
	theReason = std::move(mDisconnects.front());
	mDisconnects.pop_front();
	return true;
}

void NetClient::SendPing()
{
	uint8_t aPayload[8];
	const uint32_t aSeq = ++mPingSeq;
	const uint32_t aNow = static_cast<uint32_t>(NowMs() & 0xFFFFFFFF);
	aPayload[0] = static_cast<uint8_t>(aSeq & 0xFF);
	aPayload[1] = static_cast<uint8_t>((aSeq >> 8) & 0xFF);
	aPayload[2] = static_cast<uint8_t>((aSeq >> 16) & 0xFF);
	aPayload[3] = static_cast<uint8_t>((aSeq >> 24) & 0xFF);
	aPayload[4] = static_cast<uint8_t>(aNow & 0xFF);
	aPayload[5] = static_cast<uint8_t>((aNow >> 8) & 0xFF);
	aPayload[6] = static_cast<uint8_t>((aNow >> 16) & 0xFF);
	aPayload[7] = static_cast<uint8_t>((aNow >> 24) & 0xFF);

	{
		std::lock_guard<std::mutex> aLock(mPingMutex);
		mPingSent[aSeq] = NowMs();
		while (mPingSent.size() > 16)
		{
			mPingSent.erase(mPingSent.begin());
		}
	}

	SendFrame(FRAME_PING, aPayload, sizeof(aPayload));
}
}
