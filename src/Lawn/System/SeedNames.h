// SPDX-License-Identifier: LGPL-3.0-or-later
//
// SeedType <-> protocol name mapping. The lobby transfers cards by name so the
// Go server never has to duplicate the enum numbering; this table is generated
// from src/ConstEnums.h by tools/gen_seed_names.py.
#pragma once

#include <string>

#include "../ConstEnums.h"

namespace PvzpSeed
{
// Returns nullptr for unknown values.
const char* SeedTypeToName(SeedType theType);
// Accepts exactly the names produced by SeedTypeToName.
bool SeedNameToType(const std::string& theName, SeedType& theType);
}
