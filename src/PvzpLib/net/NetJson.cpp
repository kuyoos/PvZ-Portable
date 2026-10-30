// SPDX-License-Identifier: LGPL-3.0-or-later
#include "NetJson.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace PvzpNet
{
namespace
{
constexpr int MAX_DEPTH = 32;

void AppendUtf8(std::string& theOut, unsigned int theCode)
{
	if (theCode < 0x80)
	{
		theOut += static_cast<char>(theCode);
	}
	else if (theCode < 0x800)
	{
		theOut += static_cast<char>(0xC0 | (theCode >> 6));
		theOut += static_cast<char>(0x80 | (theCode & 0x3F));
	}
	else if (theCode < 0x10000)
	{
		theOut += static_cast<char>(0xE0 | (theCode >> 12));
		theOut += static_cast<char>(0x80 | ((theCode >> 6) & 0x3F));
		theOut += static_cast<char>(0x80 | (theCode & 0x3F));
	}
	else
	{
		theOut += static_cast<char>(0xF0 | (theCode >> 18));
		theOut += static_cast<char>(0x80 | ((theCode >> 12) & 0x3F));
		theOut += static_cast<char>(0x80 | ((theCode >> 6) & 0x3F));
		theOut += static_cast<char>(0x80 | (theCode & 0x3F));
	}
}

bool ReadHex4(const char* theText, unsigned int& theValue)
{
	theValue = 0;
	for (int i = 0; i < 4; i++)
	{
		const char aChar = theText[i];
		unsigned int aDigit;
		if (aChar >= '0' && aChar <= '9')
		{
			aDigit = static_cast<unsigned int>(aChar - '0');
		}
		else if (aChar >= 'a' && aChar <= 'f')
		{
			aDigit = static_cast<unsigned int>(aChar - 'a' + 10);
		}
		else if (aChar >= 'A' && aChar <= 'F')
		{
			aDigit = static_cast<unsigned int>(aChar - 'A' + 10);
		}
		else
		{
			return false;
		}
		theValue = (theValue << 4) | aDigit;
	}
	return true;
}

struct Parser
{
	const char* p;
	const char* end;
	int depth = 0;
	std::string error;

	bool Fail(const char* theMessage)
	{
		if (error.empty())
		{
			error = theMessage;
		}
		return false;
	}

	void SkipSpace()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		{
			p++;
		}
	}

	bool ParseValue(Json& theOut);

	bool ParseString(std::string& theOut)
	{
		if (p >= end || *p != '"')
		{
			return Fail("expected string");
		}
		p++;

		while (p < end)
		{
			const char aChar = *p++;
			if (aChar == '"')
			{
				return true;
			}
			if (aChar != '\\')
			{
				theOut += aChar;
				continue;
			}
			if (p >= end)
			{
				break;
			}
			const char anEscape = *p++;
			switch (anEscape)
			{
			case '"': theOut += '"'; break;
			case '\\': theOut += '\\'; break;
			case '/': theOut += '/'; break;
			case 'b': theOut += '\b'; break;
			case 'f': theOut += '\f'; break;
			case 'n': theOut += '\n'; break;
			case 'r': theOut += '\r'; break;
			case 't': theOut += '\t'; break;
			case 'u':
			{
				if (end - p < 4)
				{
					return Fail("bad \\u escape");
				}
				unsigned int aCode = 0;
				if (!ReadHex4(p, aCode))
				{
					return Fail("bad \\u escape");
				}
				p += 4;

				// Surrogate pair: the server may escape characters above the
				// BMP, so merge them back into one code point.
				if (aCode >= 0xD800 && aCode <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
				{
					unsigned int aLow = 0;
					if (ReadHex4(p + 2, aLow) && aLow >= 0xDC00 && aLow <= 0xDFFF)
					{
						p += 6;
						aCode = 0x10000 + ((aCode - 0xD800) << 10) + (aLow - 0xDC00);
					}
				}
				AppendUtf8(theOut, aCode);
				break;
			}
			default:
				return Fail("bad escape");
			}
		}
		return Fail("unterminated string");
	}

	bool ParseNumber(Json& theOut)
	{
		const char* aStart = p;
		if (p < end && *p == '-')
		{
			p++;
		}
		while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
		{
			p++;
		}
		if (aStart == p)
		{
			return Fail("bad number");
		}

		double aValue = 0;
		const auto aResult = std::from_chars(aStart, p, aValue);
		if (aResult.ec != std::errc())
		{
			return Fail("bad number");
		}
		theOut = Json::MakeNumber(aValue);
		return true;
	}

	bool ParseArray(Json& theOut)
	{
		theOut = Json::MakeArray();
		p++; // [
		SkipSpace();
		if (p < end && *p == ']')
		{
			p++;
			return true;
		}
		while (true)
		{
			Json aValue;
			if (!ParseValue(aValue))
			{
				return false;
			}
			theOut.Push(std::move(aValue));
			SkipSpace();
			if (p < end && *p == ',')
			{
				p++;
				SkipSpace();
				continue;
			}
			if (p < end && *p == ']')
			{
				p++;
				return true;
			}
			return Fail("expected , or ] in array");
		}
	}

	bool ParseObject(Json& theOut)
	{
		theOut = Json::MakeObject();
		p++; // {
		SkipSpace();
		if (p < end && *p == '}')
		{
			p++;
			return true;
		}
		while (true)
		{
			std::string aKey;
			if (!ParseString(aKey))
			{
				return false;
			}
			SkipSpace();
			if (p >= end || *p != ':')
			{
				return Fail("expected :");
			}
			p++;
			SkipSpace();
			Json aValue;
			if (!ParseValue(aValue))
			{
				return false;
			}
			theOut.Set(aKey, std::move(aValue));
			SkipSpace();
			if (p < end && *p == ',')
			{
				p++;
				SkipSpace();
				continue;
			}
			if (p < end && *p == '}')
			{
				p++;
				return true;
			}
			return Fail("expected , or } in object");
		}
	}
};

bool Parser::ParseValue(Json& theOut)
{
	if (depth >= MAX_DEPTH)
	{
		return Fail("too deep");
	}

	SkipSpace();
	if (p >= end)
	{
		return Fail("unexpected end");
	}

	const char aChar = *p;
	if (aChar == '{')
	{
		depth++;
		const bool aOk = ParseObject(theOut);
		depth--;
		return aOk;
	}
	if (aChar == '[')
	{
		depth++;
		const bool aOk = ParseArray(theOut);
		depth--;
		return aOk;
	}
	if (aChar == '"')
	{
		std::string aText;
		if (!ParseString(aText))
		{
			return false;
		}
		theOut = Json::MakeString(std::move(aText));
		return true;
	}
	if (aChar == 't' && end - p >= 4 && std::memcmp(p, "true", 4) == 0)
	{
		p += 4;
		theOut = Json::MakeBool(true);
		return true;
	}
	if (aChar == 'f' && end - p >= 5 && std::memcmp(p, "false", 5) == 0)
	{
		p += 5;
		theOut = Json::MakeBool(false);
		return true;
	}
	if (aChar == 'n' && end - p >= 4 && std::memcmp(p, "null", 4) == 0)
	{
		p += 4;
		theOut = Json();
		return true;
	}
	if (aChar == '-' || (aChar >= '0' && aChar <= '9'))
	{
		return ParseNumber(theOut);
	}
	return Fail("unexpected character");
}

void DumpString(std::string& theOut, const std::string& theText)
{
	theOut += '"';
	for (const char aChar : theText)
	{
		switch (aChar)
		{
		case '"': theOut += "\\\""; break;
		case '\\': theOut += "\\\\"; break;
		case '\n': theOut += "\\n"; break;
		case '\r': theOut += "\\r"; break;
		case '\t': theOut += "\\t"; break;
		case '\b': theOut += "\\b"; break;
		case '\f': theOut += "\\f"; break;
		default:
			if (static_cast<unsigned char>(aChar) < 0x20)
			{
				char aBuf[8];
				std::snprintf(aBuf, sizeof(aBuf), "\\u%04x", static_cast<unsigned char>(aChar));
				theOut += aBuf;
			}
			else
			{
				theOut += aChar;
			}
		}
	}
	theOut += '"';
}
}

Json::Json() = default;

Json Json::MakeBool(bool theValue)
{
	Json aJson;
	aJson.mType = Type::Bool;
	aJson.mBool = theValue;
	return aJson;
}

Json Json::MakeInt(long long theValue)
{
	Json aJson;
	aJson.mType = Type::Number;
	aJson.mNumber = static_cast<double>(theValue);
	return aJson;
}

Json Json::MakeNumber(double theValue)
{
	Json aJson;
	aJson.mType = Type::Number;
	aJson.mNumber = theValue;
	return aJson;
}

Json Json::MakeString(std::string theValue)
{
	Json aJson;
	aJson.mType = Type::String;
	aJson.mString = std::move(theValue);
	return aJson;
}

Json Json::MakeArray()
{
	Json aJson;
	aJson.mType = Type::Array;
	return aJson;
}

Json Json::MakeObject()
{
	Json aJson;
	aJson.mType = Type::Object;
	return aJson;
}

bool Json::AsBool(bool theDefault) const
{
	return mType == Type::Bool ? mBool : theDefault;
}

long long Json::AsInt(long long theDefault) const
{
	if (mType != Type::Number)
	{
		return theDefault;
	}
	return static_cast<long long>(mNumber);
}

const std::string& Json::AsString() const
{
	static const std::string anEmpty;
	return mType == Type::String ? mString : anEmpty;
}

size_t Json::GetSize() const
{
	if (mType == Type::Array)
	{
		return mArray.size();
	}
	if (mType == Type::Object)
	{
		return mObject.size();
	}
	return 0;
}

const Json& Json::At(size_t theIndex) const
{
	static const Json aNull;
	if (mType != Type::Array || theIndex >= mArray.size())
	{
		return aNull;
	}
	return mArray[theIndex];
}

const Json* Json::Find(const char* theKey) const
{
	if (mType != Type::Object)
	{
		return nullptr;
	}
	for (const auto& aPair : mObject)
	{
		if (aPair.first == theKey)
		{
			return &aPair.second;
		}
	}
	return nullptr;
}

long long Json::FindInt(const char* theKey, long long theDefault) const
{
	const Json* aValue = Find(theKey);
	return aValue ? aValue->AsInt(theDefault) : theDefault;
}

bool Json::FindBool(const char* theKey, bool theDefault) const
{
	const Json* aValue = Find(theKey);
	return aValue ? aValue->AsBool(theDefault) : theDefault;
}

std::string Json::FindString(const char* theKey) const
{
	const Json* aValue = Find(theKey);
	return aValue ? aValue->AsString() : std::string();
}

void Json::Set(const std::string& theKey, Json theValue)
{
	if (mType != Type::Object)
	{
		*this = MakeObject();
	}
	for (auto& aPair : mObject)
	{
		if (aPair.first == theKey)
		{
			aPair.second = std::move(theValue);
			return;
		}
	}
	mObject.emplace_back(theKey, std::move(theValue));
}

void Json::Push(Json theValue)
{
	if (mType != Type::Array)
	{
		*this = MakeArray();
	}
	mArray.push_back(std::move(theValue));
}

std::string Json::Dump() const
{
	std::string aText;
	DumpTo(aText);
	return aText;
}

void Json::DumpTo(std::string& theOut) const
{
	switch (mType)
	{
	case Type::Null:
		theOut += "null";
		break;
	case Type::Bool:
		theOut += mBool ? "true" : "false";
		break;
	case Type::Number:
	{
		char aBuf[32];
		if (mNumber == std::trunc(mNumber) && std::fabs(mNumber) < 9.0e15)
		{
			const auto aResult = std::to_chars(aBuf, aBuf + sizeof(aBuf), static_cast<long long>(mNumber));
			theOut.append(aBuf, static_cast<size_t>(aResult.ptr - aBuf));
		}
		else
		{
			const auto aResult = std::to_chars(aBuf, aBuf + sizeof(aBuf), mNumber);
			theOut.append(aBuf, static_cast<size_t>(aResult.ptr - aBuf));
		}
		break;
	}
	case Type::String:
		DumpString(theOut, mString);
		break;
	case Type::Array:
	{
		theOut += '[';
		bool aFirst = true;
		for (const auto& aValue : mArray)
		{
			if (!aFirst)
			{
				theOut += ',';
			}
			aFirst = false;
			aValue.DumpTo(theOut);
		}
		theOut += ']';
		break;
	}
	case Type::Object:
	{
		theOut += '{';
		bool aFirst = true;
		for (const auto& aPair : mObject)
		{
			if (!aFirst)
			{
				theOut += ',';
			}
			aFirst = false;
			DumpString(theOut, aPair.first);
			theOut += ':';
			aPair.second.DumpTo(theOut);
		}
		theOut += '}';
		break;
	}
	}
}

bool Json::Parse(const std::string& theText, Json& theOut, std::string* theError)
{
	Parser aParser;
	aParser.p = theText.data();
	aParser.end = theText.data() + theText.size();

	if (!aParser.ParseValue(theOut))
	{
		if (theError)
		{
			*theError = aParser.error;
		}
		return false;
	}

	aParser.SkipSpace();
	if (aParser.p != aParser.end)
	{
		if (theError)
		{
			*theError = "trailing characters";
		}
		return false;
	}
	return true;
}
}
