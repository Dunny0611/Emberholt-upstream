/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Admission for a new bot character. A decision of None does not call
 * Player::Create, SaveToDB, GiveLevel, or BeginLogin. The module does not
 * create a row.
 */

#ifndef TRINITY_PLAYERBOT_V2_CHARACTER_FACTORY_H
#define TRINITY_PLAYERBOT_V2_CHARACTER_FACTORY_H

#include "CreationPolicy.h"
#include <cstdint>

namespace Playerbot::V2
{
enum class CreateRefusal : uint8_t
{
    None = 0,
    GateClosed,
    UnknownFaction,
    MissingPoolAccount,
    OwnerAccount,
    WrongLevel
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

// poolAccountId 0, or a pool account equal to the owner account, is refused.
// The requested level must already be NewBotLevel. Owner level is not an input.
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
}
#endif
