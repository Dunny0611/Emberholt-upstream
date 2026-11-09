/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Derived from the GPL-2.0 BotGroupRules in wow-forever-source-0.1.33.zip
 * (sha256 8ed3810b3f1b04bdc049b705aaa38ace2311e5830c0f68b3ab8c0d493861c6e0).
 * A group with no bots keeps the normal raid and loot results.
 */

#ifndef TRINITY_BOT_GROUP_RULES_H
#define TRINITY_BOT_GROUP_RULES_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace BotGroupRules
{
inline bool QuestRaidRuleApplies(bool raidGroup, std::size_t playerMembers, std::size_t partySize)
{
    return raidGroup && playerMembers > partySize;
}

// No bots: raid map or same subgroup, which is the core tap rule.
// With bots: a real player is also covered when a bot in another subgroup tagged first.
inline bool SharesTap(bool raidMap, bool sameSubGroup, bool groupHasBot, bool memberIsBot)
{
    return raidMap || sameSubGroup || (groupHasBot && !memberIsBot);
}

struct LooterCandidate
{
    bool inRange = false;
    bool bot = false;
};

// No bot in range: the next in-range member, same walk as the core round-robin.
// A player in range: bots do not take the turn.
inline std::size_t NextRoundRobinLooter(std::vector<LooterCandidate> const& candidates, std::size_t current, bool keepCurrent)
{
    std::size_t const count = candidates.size();
    bool const playerInRange = std::ranges::any_of(candidates, [](LooterCandidate const& c) { return c.inRange && !c.bot; });
    auto mayLoot = [&](std::size_t i) { return candidates[i].inRange && (!candidates[i].bot || !playerInRange); };

    if (current < count && keepCurrent && mayLoot(current))
        return current;

    std::size_t const start = current < count ? current + 1 : 0;
    for (std::size_t step = 0; step < count; ++step)
        if (std::size_t const i = (start + step) % count; mayLoot(i))
            return i;

    return count;
}

// order must be a permutation of current. Otherwise the caller changes nothing.
inline bool IsSameMemberOrder(std::vector<uint64_t> const& current, std::vector<uint64_t> const& order)
{
    if (current.size() != order.size())
        return false;
    std::vector<bool> used(current.size(), false);
    for (uint64_t guid : order)
    {
        bool found = false;
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            if (!used[i] && current[i] == guid)
            {
                used[i] = true;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}
}

#endif
