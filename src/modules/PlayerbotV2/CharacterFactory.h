/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * PersistLevelOneCharacter inserts a new level-1 row on the pool account when
 * the population gate is open. A closed gate returns before any session or
 * database call. It does not log the character in and it does not call GiveLevel.
 */

#ifndef TRINITY_PLAYERBOT_V2_CHARACTER_FACTORY_H
#define TRINITY_PLAYERBOT_V2_CHARACTER_FACTORY_H

#include "CreationPolicy.h"
#include "ObjectGuid.h"
#include <cstdint>
#include <string>

namespace Playerbot::V2
{
enum class CreateRefusal : uint8_t
{
    None = 0,
    GateClosed,
    UnknownFaction,
    MissingPoolAccount,
    OwnerAccount,
    WrongLevel,
    InvalidClass,
    InvalidName,
    UnknownAccount,
    AccountFull,
    CreateFailed,
    PrivilegedAccount,
    SharedPoolAccount,
    WrongPoolFaction
};

struct NewCharacterRequest
{
    uint32_t poolAccountId = 0;
    uint32_t ownerAccountId = 0;
    uint8_t race = 0;
    uint8_t classId = 0;
    uint8_t sex = 0;
    uint8_t level = 0;
};

struct NewCharacterDecision
{
    CreateRefusal refusal = CreateRefusal::GateClosed;
    uint8_t level = NewBotLevel;
    BotFaction faction = BotFaction::Unknown;
};

struct PersistRequest
{
    PopulationConfig config;
    uint32_t alreadyOnline = 0;
    uint32_t maxOnAccount = 10;
    BotFaction poolFaction = BotFaction::Unknown;
    uint32_t otherFactionPoolAccount = 0;
    NewCharacterRequest character;
    std::string name;
};

struct PersistResult
{
    CreateRefusal refusal = CreateRefusal::GateClosed;
    ObjectGuid guid;
    std::string detail;
};

// Entering the world still requires the navigation flag. Persistence does not.
inline NewCharacterDecision DecideNewCharacter(PopulationConfig const& config, uint32_t alreadyOnline, bool navigationValidated, NewCharacterRequest const& request)
{
    NewCharacterDecision decision;
    decision.faction = FactionForStarterRace(request.race);
    decision.level = LevelForCreation(BotOrigin::NewBot, request.level, 0);
    if (decision.faction == BotFaction::Unknown)
    {
        decision.refusal = CreateRefusal::UnknownFaction;
        return decision;
    }
    if (request.poolAccountId == 0)
    {
        decision.refusal = CreateRefusal::MissingPoolAccount;
        return decision;
    }
    if (request.poolAccountId == request.ownerAccountId)
    {
        decision.refusal = CreateRefusal::OwnerAccount;
        return decision;
    }
    if (request.level != decision.level)
    {
        decision.refusal = CreateRefusal::WrongLevel;
        return decision;
    }
    if (!MayEnterWorld(config, decision.faction, alreadyOnline, navigationValidated))
    {
        decision.refusal = CreateRefusal::GateClosed;
        return decision;
    }
    decision.refusal = CreateRefusal::None;
    return decision;
}

inline NewCharacterDecision AdmitPersistence(PopulationConfig const& config, uint32_t alreadyOnline, NewCharacterRequest const& request)
{
    NewCharacterDecision decision;
    decision.faction = FactionForStarterRace(request.race);
    decision.level = LevelForCreation(BotOrigin::NewBot, request.level, 0);
    if (decision.faction == BotFaction::Unknown)
    {
        decision.refusal = CreateRefusal::UnknownFaction;
        return decision;
    }
    if (request.poolAccountId == 0)
    {
        decision.refusal = CreateRefusal::MissingPoolAccount;
        return decision;
    }
    if (request.poolAccountId == request.ownerAccountId)
    {
        decision.refusal = CreateRefusal::OwnerAccount;
        return decision;
    }
    if (request.level != decision.level)
    {
        decision.refusal = CreateRefusal::WrongLevel;
        return decision;
    }
    if (!PopulationAllowsNewBot(config, decision.faction, alreadyOnline))
    {
        decision.refusal = CreateRefusal::GateClosed;
        return decision;
    }
    decision.refusal = CreateRefusal::None;
    return decision;
}

bool IsStarterClass(uint8_t classId);
PersistResult PersistLevelOneCharacter(PersistRequest const& request);
}
#endif
