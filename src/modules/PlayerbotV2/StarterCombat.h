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

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Playerbot::V2
{
inline constexpr float MeleeReachYards = 5.0f;
inline constexpr float AcquireYards = 30.0f;
inline constexpr float FollowMinYards = 8.0f;
inline constexpr float FollowMaxYards = 40.0f;
// Starter beasts are often a few levels above a new character. Elites far above this stay refused.
inline constexpr uint8_t LevelHeadroom = 4;

// Faction-template fields copied from the public FactionTemplate row. Reaction and
// reputation at-war are not part of this record: a clientless bot often has no
// AtWar bit, and classic starter beasts are neutral on these masks.
struct FactionTemplateFacts
{
    uint32_t id = 0;
    uint16_t faction = 0;
    uint32_t flags = 0;
    uint8_t factionGroup = 0;
    uint8_t friendGroup = 0;
    uint8_t enemyGroup = 0;
    std::array<uint16_t, 8> enemies{};
    std::array<uint16_t, 8> friends{};
};

inline constexpr uint32_t FactionTemplateHostileByDefault = 0x00002000u;
inline constexpr uint32_t NpcFlagQuestGiver = 0x00000002u;
// Gossip alone is not enough. Quest givers, vendors, trainers, and other service
// NPCs are the civilians that stay non-combat.
inline constexpr uint32_t NpcFlagNonCombatService =
    NpcFlagQuestGiver | 0x00000010u | 0x00000020u | 0x00000040u | 0x00000080u | 0x00000100u | 0x00000200u
    | 0x00000400u | 0x00000800u | 0x00001000u | 0x00002000u | 0x00004000u | 0x00008000u | 0x00010000u
    | 0x00020000u | 0x00040000u | 0x00080000u | 0x00200000u | 0x00400000u;
inline constexpr uint32_t UnitFlagNotAttackable = 0x00000002u | 0x00000080u | 0x00000100u | 0x00010000u | 0x02000000u | 0x80000000u;

inline bool TemplateIsFriendly(FactionTemplateFacts const& self, FactionTemplateFacts const& other)
{
    if (self.id != 0 && self.id == other.id)
        return true;
    if (other.faction)
    {
        for (uint16_t enemy : self.enemies)
            if (enemy != 0 && enemy == other.faction)
                return false;
        for (uint16_t friendly : self.friends)
            if (friendly != 0 && friendly == other.faction)
                return true;
    }
    return (self.friendGroup & other.factionGroup) != 0 || (self.factionGroup & other.friendGroup) != 0;
}

inline bool TemplateIsHostile(FactionTemplateFacts const& self, FactionTemplateFacts const& other)
{
    if (self.id != 0 && self.id == other.id)
        return false;
    if (other.faction)
    {
        for (uint16_t enemy : self.enemies)
            if (enemy != 0 && enemy == other.faction)
                return true;
        for (uint16_t friendly : self.friends)
            if (friendly != 0 && friendly == other.faction)
                return false;
    }
    return (self.enemyGroup & other.factionGroup) != 0;
}

struct HostileFacts
{
    bool player = false;
    bool gameMaster = false;
    bool creature = false;
    bool alive = false;
    bool validAttack = false;
    bool factionHostile = false;
    bool templateHostile = false;
    bool templateFriendly = false;
    bool neutralPrey = false;
    bool civilian = false;
    bool nonCombatService = false;
    bool immune = false;
    bool critter = false;
    bool pet = false;
    bool totem = false;
    bool sameFaction = false;
    uint8_t level = 0;
    uint8_t botLevel = 1;
    float distance = 100.0f;
    bool pvpEnabled = false;
    uint32_t entry = 0;
    uint32_t factionTemplate = 0;
    uint32_t unitFlags = 0;
    uint32_t npcFlags = 0;
    int32_t reaction = 0;
    bool hostileTo = false;
    bool friendlyTo = false;
    bool hostileFrom = false;
    bool friendlyFrom = false;
};

// Hostile by template, or neutral toward this player. Friendly templates are not prey.
// A zero id means the template was missing, which is not treated as neutral prey.
inline void ApplyFactionRelation(HostileFacts& facts, FactionTemplateFacts const& creature, FactionTemplateFacts const& player)
{
    bool const hostile = TemplateIsHostile(creature, player);
    bool const friendly = TemplateIsFriendly(creature, player) || TemplateIsFriendly(player, creature);
    facts.templateHostile = hostile || (!friendly && (creature.flags & FactionTemplateHostileByDefault) != 0);
    facts.templateFriendly = !facts.templateHostile && friendly;
    facts.neutralPrey = creature.id != 0 && player.id != 0 && !facts.templateHostile && !facts.templateFriendly;
    facts.factionTemplate = creature.id;
}

enum class HostileUse : uint8_t
{
    Refuse = 0,
    Melee,
    Approach
};

inline HostileUse JudgeHostile(HostileFacts const& facts)
{
    // CIVILIAN (template extra flag 0x2) is not a combat ban. Rockjaw troggs carry it and are prey.
    // Classic starter beasts are neutral on the faction template (no enemy mask), and a clientless
    // bot's reputation reaction is often friendly or neutral, so template neutrality is prey.
    // IsValidAttackTarget is not required: it re-checks phase and the at-war reputation bit.
    // A creature with unit level 0 still uses band level 1.
    (void)facts.civilian;
    if (!facts.alive || facts.gameMaster || facts.critter || facts.totem || facts.pet)
        return HostileUse::Refuse;
    if (facts.player)
    {
        if (!facts.pvpEnabled || facts.sameFaction || !facts.validAttack)
            return HostileUse::Refuse;
    }
    else if (!facts.creature)
        return HostileUse::Refuse;
    else if (facts.immune)
        return HostileUse::Refuse;
    else if (facts.nonCombatService && !facts.templateHostile && !facts.factionHostile)
        return HostileUse::Refuse;
    else if (!facts.templateHostile && !facts.factionHostile && !facts.validAttack && !facts.neutralPrey)
        return HostileUse::Refuse;
    uint8_t const bandLevel = facts.level == 0 && facts.creature ? uint8_t(1) : facts.level;
    if (bandLevel == 0 || uint32_t(bandLevel) > uint32_t(facts.botLevel) + LevelHeadroom)
        return HostileUse::Refuse;
    if (facts.distance <= MeleeReachYards)
        return HostileUse::Melee;
    if (facts.distance <= AcquireYards)
        return HostileUse::Approach;
    return HostileUse::Refuse;
}

inline char const* HostileRefuseReason(HostileFacts const& facts)
{
    if (JudgeHostile(facts) != HostileUse::Refuse)
        return "ok";
    if (!facts.alive)
        return "dead";
    if (facts.gameMaster)
        return "gm";
    if (facts.critter)
        return "critter";
    if (facts.totem)
        return "totem";
    if (facts.pet)
        return "pet";
    if (facts.player && !facts.pvpEnabled)
        return "pvp-off";
    if (facts.player && facts.sameFaction)
        return "same-faction";
    if (facts.player && !facts.validAttack)
        return "player-not-attackable";
    if (!facts.creature)
        return "not-creature";
    if (facts.immune)
        return "not-attackable";
    if (facts.nonCombatService && !facts.templateHostile && !facts.factionHostile)
        return "civilian";
    if (!facts.templateHostile && !facts.factionHostile && !facts.validAttack && !facts.neutralPrey)
        return "not-hostile";
    return "level-or-range";
}

struct RefuseHistogram
{
    uint32_t dead = 0;
    uint32_t gm = 0;
    uint32_t critter = 0;
    uint32_t totem = 0;
    uint32_t pet = 0;
    uint32_t civilian = 0;
    uint32_t notAttackable = 0;
    uint32_t notHostile = 0;
    uint32_t levelOrRange = 0;
    uint32_t other = 0;
};

inline void AccumulateRefuse(RefuseHistogram& histogram, char const* reason)
{
    std::string_view const code = reason ? reason : "";
    if (code == "dead")
        ++histogram.dead;
    else if (code == "gm")
        ++histogram.gm;
    else if (code == "critter")
        ++histogram.critter;
    else if (code == "totem")
        ++histogram.totem;
    else if (code == "pet")
        ++histogram.pet;
    else if (code == "civilian")
        ++histogram.civilian;
    else if (code == "not-attackable")
        ++histogram.notAttackable;
    else if (code == "not-hostile")
        ++histogram.notHostile;
    else if (code == "level-or-range")
        ++histogram.levelOrRange;
    else
        ++histogram.other;
}

inline std::string FormatRefuseHistogram(RefuseHistogram const& histogram)
{
    return "dead=" + std::to_string(histogram.dead)
        + " gm=" + std::to_string(histogram.gm)
        + " critter=" + std::to_string(histogram.critter)
        + " totem=" + std::to_string(histogram.totem)
        + " pet=" + std::to_string(histogram.pet)
        + " civilian=" + std::to_string(histogram.civilian)
        + " not-attackable=" + std::to_string(histogram.notAttackable)
        + " not-hostile=" + std::to_string(histogram.notHostile)
        + " level-or-range=" + std::to_string(histogram.levelOrRange)
        + " other=" + std::to_string(histogram.other);
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
