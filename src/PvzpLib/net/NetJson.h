// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Minimal JSON reader/writer for the lobby control messages. The wire format is
// owned by us (see server/internal/proto), so a full featured library would be
// overkill: we only need objects, arrays, strings, numbers and booleans.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace PvzpNet
{
class Json
{
public:
	enum class Type
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object
	};

	Json() = default;

	static Json MakeBool(bool theValue);
	static Json MakeInt(long long theValue);
	static Json MakeNumber(double theValue);
	static Json MakeString(std::string theValue);
	static Json MakeArray();
	static Json MakeObject();

	Type GetType() const { return mType; }
	bool IsNull() const { return mType == Type::Null; }

	bool AsBool(bool theDefault = false) const;
	long long AsInt(long long theDefault = 0) const;
	const std::string& AsString() const;

	size_t GetSize() const;
	const Json& At(size_t theIndex) const;
	const Json* Find(const char* theKey) const;
	long long FindInt(const char* theKey, long long theDefault = 0) const;
	bool FindBool(const char* theKey, bool theDefault = false) const;
	std::string FindString(const char* theKey) const;

	void Set(const std::string& theKey, Json theValue);
	void Push(Json theValue);

	std::string Dump() const;

	// Returns false and fills theError when theText is not valid JSON.
	static bool Parse(const std::string& theText, Json& theOut, std::string* theError = nullptr);

private:
	void DumpTo(std::string& theOut) const;

	Type mType = Type::Null;
	bool mBool = false;
	double mNumber = 0;
	std::string mString;
	std::vector<Json> mArray;
	std::vector<std::pair<std::string, Json>> mObject;
};
}
