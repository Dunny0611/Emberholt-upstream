/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tc_catch2.h"
#include "Classic/ClassicUpstreamPolicy.h"

TEST_CASE("Classic upstream item, enchant, map, SmartAI and transmog policies", "[ClassicUpstreamPolicy]")
{
    std::vector<std::string> const failures = ClassicUpstream::RunSelfTest();
    for (std::string const& failure : failures)
        FAIL(failure);
    CHECK(failures.empty());
}
