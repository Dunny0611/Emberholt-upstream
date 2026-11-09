/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Decisions for pool accounts, invites, and logout. SEC_PLAYER is 0. A greater
 * security value is a GM, admin, or console account and cannot own bots.
 */

#ifndef TRINITY_PLAYERBOT_V2_LIFECYCLE_POLICY_H
#define TRINITY_PLAYERBOT_V2_LIFECYCLE_POLICY_H

#include "CreationPolicy.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace Playerbot::V2
{
inline constexpr uint32_t AiStepsPerTick = 1;
inline constexpr uint32_t AiBudgetMs = 5;
inline constexpr uint32_t DecisionIntervalMs = 400;

enum class PoolScreen : uint8_t
{
    Ok = 0,
    Missing,
    Shared,
    WrongFaction
};

inline PoolScreen ScreenPoolIds(uint32_t poolId, uint32_t otherFactionPoolId, BotFaction raceFaction, BotFaction accountFaction)
{
    if (poolId == 0)
        return PoolScreen::Missing;
    if (otherFactionPoolId != 0 && otherFactionPoolId == poolId)
        return PoolScreen::Shared;
    if (raceFaction == BotFaction::Unknown || accountFaction == BotFaction::Unknown || raceFaction != accountFaction)
        return PoolScreen::WrongFaction;
    return PoolScreen::Ok;
}

// security is AccountTypes. Only SEC_PLAYER (0) may be a pool account.
inline bool PoolSecurityAllowed(uint32_t security)
{
    return security == 0;
}

enum class NameUse : uint8_t
{
    Free = 0,
    Reuse,
    Reject
};

// Reuse only the same guid this factory already marked on this pool account.
// Any other character with that name is a collision, whatever its level.
inline NameUse ClassifyName(bool taken, bool sameCharacterOwned, uint32_t characterAccount, uint32_t poolId)
{
    if (!taken)
        return NameUse::Free;
    if (sameCharacterOwned && characterAccount == poolId && poolId != 0)
        return NameUse::Reuse;
    return NameUse::Reject;
}

inline constexpr uint32_t FactoryVersion = 1;

struct MarkerRow
{
    bool present = false;
    uint32_t account = 0;
    uint8_t faction = 255;
    uint32_t factoryVersion = 0;
    uint8_t startLevel = 0;
};

// The marker records factory origin. Current level is not part of the bind.
inline bool MarkerBindsOwner(MarkerRow const& row, uint32_t characterAccount, BotFaction raceFaction)
{
    return row.present
        && row.account != 0
        && row.account == characterAccount
        && BotFaction(row.faction) == raceFaction
        && raceFaction != BotFaction::Unknown
        && row.factoryVersion == FactoryVersion
        && row.startLevel == NewBotLevel;
}

// A new character starts at level 1. A later login may be any level the core allows.
inline bool LoginLevelAllowed(uint8_t level, uint8_t maxLevel)
{
    return maxLevel >= NewBotLevel && level >= NewBotLevel && level <= maxLevel;
}

// MaxCharacters counts persisted rows, not the online cap. Either the marker
// count or the character-table count fills the account.
// Comma or whitespace separated account ids. Zero is dropped. Duplicates are dropped.
inline void AppendAccountIds(std::string_view text, std::vector<uint32_t>& ids)
{
    uint32_t value = 0;
    bool inNumber = false;
    auto push = [&]()
    {
        if (inNumber && value != 0)
        {
            bool seen = false;
            for (uint32_t existing : ids)
                if (existing == value)
                    seen = true;
            if (!seen)
                ids.push_back(value);
        }
        value = 0;
        inNumber = false;
    };
    for (char character : text)
    {
        if (character >= '0' && character <= '9')
        {
            inNumber = true;
            if (value <= 100000000u)
                value = value * 10u + uint32_t(character - '0');
        }
        else
            push();
    }
    push();
}

inline bool PersistedCapReached(uint32_t markerRows, uint32_t characterRows, uint32_t maxOnAccount)
{
    return maxOnAccount == 0 || markerRows >= maxOnAccount || characterRows >= maxOnAccount;
}

inline bool DropOwnedSession(bool sessionIsOwned, bool gateClosed)
{
    return sessionIsOwned && gateClosed;
}

// A normal player of the same faction can invite an owned bot. A bot does not invite itself in.
inline bool AcceptGroupInvite(bool ownedBot, bool leaderIsBot, BotFaction botFaction, BotFaction leaderFaction, bool gateClosed)
{
    return ownedBot && !leaderIsBot && !gateClosed
        && botFaction != BotFaction::Unknown
        && botFaction == leaderFaction;
}

enum class StepKind : uint8_t
{
    None = 0,
    Hold,   // owned, but no mmap path and no melee target; do not slide through geometry
    Melee,
    Path
};

struct StepInput
{
    bool owned = false;
    bool gateClosed = false;
    bool attackerInMelee = false;
    bool corePathUsable = false;
    uint32_t stepsAlreadyThisTick = 0;
};

inline StepKind ChooseStep(StepInput const& input)
{
    if (!input.owned || input.gateClosed)
        return StepKind::None;
    if (input.stepsAlreadyThisTick >= AiStepsPerTick)
        return StepKind::Hold;
    if (input.attackerInMelee)
        return StepKind::Melee;
    if (input.corePathUsable)
        return StepKind::Path;
    return StepKind::Hold;
}

// Follow only a normal or partial core path. Missing tiles, shortcuts, and
// points far from the navmesh are refused. Starter areas are not assumed empty.
inline bool CorePathIsUsable(bool calculated, uint32_t pathType)
{
    constexpr uint32_t Normal = 0x01;
    constexpr uint32_t Shortcut = 0x02;
    constexpr uint32_t Incomplete = 0x04;
    constexpr uint32_t NoPath = 0x08;
    constexpr uint32_t NotUsingPath = 0x10;
    constexpr uint32_t FarFromPoly = 0x40 | 0x80;
    if (!calculated)
        return false;
    if (pathType & (NoPath | NotUsingPath | Shortcut | FarFromPoly))
        return false;
    return (pathType & (Normal | Incomplete)) != 0;
}

// The next path point has to be a short step. A long or vertical jump is not followed.
inline bool LocalStepPoint(float dx, float dy, float dz)
{
    if (!(dx == dx && dy == dy && dz == dz))
        return false;
    return dx * dx + dy * dy <= 12.0f * 12.0f && (dz < 0.0f ? -dz : dz) <= 4.0f;
}
}
#endif
