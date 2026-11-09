/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Target, spell, follow, and death decisions for one owned bot. These do not
 * grant spells, experience, or quest credit by themselves. A harmful cast uses
 * a spell the character already knows. Quest accept, turn-in, and dungeon
 * scripts are not decided here.
 */

#ifndef TRINITY_PLAYERBOT_V2_STARTER_COMBAT_H
#define TRINITY_PLAYERBOT_V2_STARTER_COMBAT_H

#include <cstdint>

namespace Playerbot::V2
{
inline constexpr float MeleeReachYards = 5.0f;
inline constexpr float AcquireYards = 30.0f;
inline constexpr float FollowMinYards = 8.0f;
inline constexpr float FollowMaxYards = 40.0f;
// Starter beasts are often a few levels above a new character. Elites far above this stay refused.
inline constexpr uint8_t LevelHeadroom = 4;

struct HostileFacts
{
    bool player = false;
    bool gameMaster = false;
    bool creature = false;
    bool alive = false;
    bool validAttack = false;
    bool civilian = false;
    bool critter = false;
    bool pet = false;
    bool sameFaction = false;
    uint8_t level = 0;
    uint8_t botLevel = 1;
    float distance = 100.0f;
    bool pvpEnabled = false;
};

enum class HostileUse : uint8_t
{
    Refuse = 0,
    Melee,
    Approach
};

inline HostileUse JudgeHostile(HostileFacts const& facts)
{
    // CIVILIAN is a template extra flag. Hostile starter beasts can carry it, so it is not a combat ban.
    // IsValidAttackTarget is the combat rule. Critters, pets, and totems stay refused.
    (void)facts.civilian;
    if (!facts.alive || !facts.validAttack || facts.gameMaster || facts.critter || facts.pet)
        return HostileUse::Refuse;
    if (facts.player)
    {
        if (!facts.pvpEnabled || facts.sameFaction)
            return HostileUse::Refuse;
    }
    else if (!facts.creature)
        return HostileUse::Refuse;
    if (facts.level == 0 || uint32_t(facts.level) > uint32_t(facts.botLevel) + LevelHeadroom)
        return HostileUse::Refuse;
    if (facts.distance <= MeleeReachYards)
        return HostileUse::Melee;
    if (facts.distance <= AcquireYards)
        return HostileUse::Approach;
    return HostileUse::Refuse;
}

struct KnownSpellFacts
{
    bool known = false;
    bool active = false;
    bool removed = false;
    bool disabled = false;
    bool passive = false;
    bool positive = false;
    bool cooldown = false;
    float maxRange = 0.0f;
    float distance = 100.0f;
};

// A cast is allowed only for a harmful spell already on the character, in range, and off cooldown.
inline bool HarmfulSpellReady(KnownSpellFacts const& spell)
{
    return spell.known && spell.active && !spell.removed && !spell.disabled && !spell.passive && !spell.positive && !spell.cooldown
        && spell.maxRange >= MeleeReachYards && spell.distance <= spell.maxRange && spell.distance <= AcquireYards;
}

inline bool ShouldFollowLeader(bool hasLeader, bool leaderAlive, bool sameMap, bool botInCombat, float distance)
{
    return hasLeader && leaderAlive && sameMap && !botInCombat && distance > FollowMinYards && distance <= FollowMaxYards;
}

enum class RecoveryKind : uint8_t
{
    None = 0,
    ReleaseSpirit,
    WalkToCorpse,
    Reclaim,
    Hold
};

// Release and reclaim are the core death actions. A ghost without a usable path waits.
inline RecoveryKind ChooseRecovery(bool alive, bool ghost, bool hasCorpse, bool corpseDelayDone, bool corpseInRange, bool pathUsable)
{
    if (alive && !ghost)
        return RecoveryKind::None;
    if (!alive && !ghost)
        return RecoveryKind::ReleaseSpirit;
    if (ghost && hasCorpse && corpseDelayDone && corpseInRange)
        return RecoveryKind::Reclaim;
    if (ghost && hasCorpse && pathUsable)
        return RecoveryKind::WalkToCorpse;
    return RecoveryKind::Hold;
}
}
#endif
