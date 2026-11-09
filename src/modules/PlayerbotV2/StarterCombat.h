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
// A legal wolf just outside the engage radius is still worth a walk.
inline constexpr float ObjectiveSeekYards = 120.0f;
inline constexpr float PreySeekYards = 80.0f;
inline constexpr float GiverSeekYards = 120.0f;
inline constexpr float WanderLeashYards = 150.0f;
inline constexpr float WanderStepYards = 12.0f;
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
// Client right-click attack cares about these three. The wider mask (full IMMUNE, NOT_ATTACKABLE_1)
// was refusing live starter beasts that a player can still select and swing at.
inline constexpr uint32_t UnitFlagNonAttackable = 0x00000002u;
inline constexpr uint32_t UnitFlagImmuneToPc = 0x00000100u;
inline constexpr uint32_t UnitFlagNotSelectable = 0x02000000u;
inline constexpr uint32_t UnitFlagNotAttackable = UnitFlagNonAttackable | UnitFlagImmuneToPc | UnitFlagNotSelectable;

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
    bool flagNonAttackable = false;
    bool flagImmuneToPc = false;
    bool flagNotSelectable = false;
    bool samePhase = true;
    bool los = true;
    bool pathExists = true;
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
    bool friendlyFrom = false;
    bool hostileFrom = false;
    bool canSee = false;
    bool hasReputation = false;
    bool atWar = false;
    bool peaceful = false;
    bool majorCity = false;
    bool ownSide = false;
    uint32_t reputationFaction = 0;
};

// Public Faction.db2 rows. ReputationIndex < 0 means the faction has no reputation state,
// so IsValidAttackTarget does not look at at-war. These city rows do have reputation.
inline bool IsMajorCityFaction(uint32_t factionId)
{
    switch (factionId)
    {
        case 47:   // Ironforge
        case 54:   // Gnomeregan
        case 68:   // Undercity
        case 69:   // Darnassus
        case 72:   // Stormwind
        case 76:   // Orgrimmar
        case 81:   // Thunder Bluff
        case 530:  // Darkspear Trolls
            return true;
        default:
            return false;
    }
}

// Set at-war only when a real player could right-click attack and the faction has a reputation row.
// No reputation row: the attack check does not consult at-war. Peaceful, city, and own-side factions stay untouched.
inline bool NeedsSetAtWar(HostileFacts const& facts)
{
    return facts.hasReputation && !facts.atWar && !facts.peaceful && !facts.majorCity && !facts.ownSide
        && facts.canSee && !facts.friendlyTo && !facts.friendlyFrom && !facts.templateFriendly;
}

// Client attack rules. IsHostileTo is not required. IsValidAttackTarget is applied later,
// after at-war is set, and only when the faction actually has a reputation row.
inline char const* ClientAttackBlock(HostileFacts const& facts)
{
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
    if (facts.flagNonAttackable)
        return "flag-non-attackable";
    if (facts.flagImmuneToPc)
        return "flag-immune-to-pc";
    if (facts.flagNotSelectable)
        return "flag-not-selectable";
    if (facts.nonCombatService)
        return "civilian";
    if (facts.templateFriendly || facts.ownSide)
        return "friendly-template";
    if (facts.majorCity && facts.templateFriendly)
        return "major-city";
    if (facts.peaceful && facts.hasReputation)
        return "peaceful";
    if (!facts.samePhase)
        return "phase";
    if (facts.distance <= 15.0f && !facts.los)
        return "no-los";
    if (!facts.pathExists)
        return "no-path";
    return nullptr;
}

inline bool ClientCanAttack(HostileFacts const& facts)
{
    return ClientAttackBlock(facts) == nullptr;
}

// Swing gate. Eligibility is ClientCanAttack. Attack itself still waits until
// IsValidAttackTarget is true, or until at-war can be set on a reputation row.
inline bool AttackAllowed(HostileFacts const& facts)
{
    if (!ClientCanAttack(facts))
        return false;
    return facts.validAttack || NeedsSetAtWar(facts);
}

// After NeedsSetAtWar is applied, the at-war gate inside IsValidAttackTarget is open.
inline bool ValidAttackAfterAtWar(HostileFacts facts)
{
    if (NeedsSetAtWar(facts))
        facts.atWar = true;
    if (!facts.canSee || facts.friendlyTo || facts.friendlyFrom || facts.templateFriendly)
        return false;
    if (facts.hasReputation && !facts.atWar)
        return false;
    return true;
}

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

inline HostileUse JudgeHostile(HostileFacts const& facts, float approachYards = AcquireYards)
{
    // CIVILIAN (template extra flag 0x2) is not a combat ban. Rockjaw troggs carry it and are prey.
    // Neutral templates are eligible without IsHostileTo. IsValidAttackTarget runs after at-war.
    // A level of 0 still uses band level 1.
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
    else if (!ClientCanAttack(facts))
        return HostileUse::Refuse;
    uint8_t const bandLevel = facts.level == 0 && facts.creature ? uint8_t(1) : facts.level;
    if (bandLevel == 0 || uint32_t(bandLevel) > uint32_t(facts.botLevel) + LevelHeadroom)
        return HostileUse::Refuse;
    if (facts.distance <= MeleeReachYards)
        return HostileUse::Melee;
    if (facts.distance <= approachYards)
        return HostileUse::Approach;
    return HostileUse::Refuse;
}

enum class SwingAction : uint8_t
{
    Wait = 0,
    Close,
    Face,
    Swing
};

struct SwingFacts
{
    bool inMeleeRange = false;
    bool inArc = false;
    bool attackReady = false;
    uint32_t attackTimer = 0;
};

// Out of range, wrong facing, or a running timer must not call DoMeleeAttackIfReady.
// That function forces the swing timer to 100ms when the check fails.
inline SwingAction ChooseSwing(SwingFacts const& facts)
{
    if (!facts.inMeleeRange)
        return SwingAction::Close;
    if (!facts.inArc)
        return SwingAction::Face;
    if (!facts.attackReady)
        return SwingAction::Wait;
    return SwingAction::Swing;
}

inline bool SwingCallsAttack(SwingAction action)
{
    return action == SwingAction::Swing;
}

// Only a successful swing replaces the timer with the weapon speed. Every other
// action leaves it alone, including the 100ms failure reset inside the core.
inline uint32_t NextSwingTimer(SwingFacts const& facts, uint32_t weaponSpeed)
{
    if (ChooseSwing(facts) == SwingAction::Swing)
        return weaponSpeed;
    return facts.attackTimer;
}

// A target whose health does not fall while the bot is on it is treated as unreachable.
inline constexpr uint32_t UnreachableMeleeMs = 8000;

inline bool MeleeTargetUnreachable(uint32_t elapsedMs, uint32_t healthThen, uint32_t healthNow)
{
    return elapsedMs >= UnreachableMeleeMs && healthThen > 0 && healthNow >= healthThen;
}

// Nearest creature that is still legal prey inside approachYards. Friendly guards and critters are skipped.
inline int32_t ChoosePreyIndex(HostileFacts const* facts, std::size_t count, float approachYards)
{
    int32_t best = -1;
    float bestDistance = approachYards + 1.0f;
    if (!facts)
        return best;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (JudgeHostile(facts[i], approachYards) == HostileUse::Refuse)
            continue;
        if (facts[i].distance < bestDistance)
        {
            best = int32_t(i);
            bestDistance = facts[i].distance;
        }
    }
    return best;
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
    if (char const* const block = ClientAttackBlock(facts))
        return block;
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
    uint32_t flagNonAttackable = 0;
    uint32_t flagImmuneToPc = 0;
    uint32_t flagNotSelectable = 0;
    uint32_t friendlyTemplate = 0;
    uint32_t phase = 0;
    uint32_t noLos = 0;
    uint32_t noPath = 0;
    uint32_t peaceful = 0;
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
    else if (code == "not-attackable" || code == "flag-non-attackable")
    {
        ++histogram.notAttackable;
        if (code == "flag-non-attackable")
            ++histogram.flagNonAttackable;
    }
    else if (code == "flag-immune-to-pc")
        ++histogram.flagImmuneToPc;
    else if (code == "flag-not-selectable")
        ++histogram.flagNotSelectable;
    else if (code == "friendly-template" || code == "major-city" || code == "not-hostile")
        ++histogram.friendlyTemplate;
    else if (code == "phase")
        ++histogram.phase;
    else if (code == "no-los")
        ++histogram.noLos;
    else if (code == "no-path")
        ++histogram.noPath;
    else if (code == "peaceful")
        ++histogram.peaceful;
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
        + " flag-non-attackable=" + std::to_string(histogram.flagNonAttackable)
        + " flag-immune-to-pc=" + std::to_string(histogram.flagImmuneToPc)
        + " flag-not-selectable=" + std::to_string(histogram.flagNotSelectable)
        + " friendly-template=" + std::to_string(histogram.friendlyTemplate)
        + " phase=" + std::to_string(histogram.phase)
        + " no-los=" + std::to_string(histogram.noLos)
        + " no-path=" + std::to_string(histogram.noPath)
        + " peaceful=" + std::to_string(histogram.peaceful)
        + " other=" + std::to_string(histogram.other);
}

inline constexpr uint32_t SpiritHealerTimeoutMs = 20000;

inline bool ShouldFinishSpiritResurrect(bool nearGraveOrHealer, uint32_t elapsedMs)
{
    return nearGraveOrHealer || elapsedMs >= SpiritHealerTimeoutMs;
}

inline bool ResurrectionSicknessApplies(uint8_t level)
{
    return level > 10;
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
    SpiritHealer,
    Hold
};

// Release and reclaim are the core death actions. A ghost with no corpse on this map uses the spirit healer.
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
    if (ghost && !hasCorpse)
        return RecoveryKind::SpiritHealer;
    return RecoveryKind::Hold;
}
}
#endif
