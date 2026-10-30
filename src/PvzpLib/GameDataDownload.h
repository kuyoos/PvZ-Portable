/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>

#ifdef _WIN32
// Makes sure that a usable main.pak exists in theResourceDir.
//
// When the file is missing (or is not a PopCap package), a small native window
// asks the user for a download URL, fetches the file over WinHTTP and verifies
// it before putting it in place. The URL is remembered in
// "<resource dir>/main.pak.url.txt" so it only has to be entered once.
//
// Returns true when a valid main.pak is present afterwards. Never throws; on
// failure the caller keeps its existing "resources missing" behaviour.
bool PvzpEnsureGameDataWin(const std::string& theResourceDir);
#endif
