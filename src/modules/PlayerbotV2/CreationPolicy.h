/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Level and population decisions for a minimal Playerbot V2 port. The v0.1.33
 * archive leveled a new companion with GiveLevel(owner->GetLevel()) and started
 * world buckets at level 10. This port does not. See PROVENANCE.txt.
 */

#ifndef TRINITY_PLAYERBOT_V2_CREATION_POLICY_H
#define TRINITY_PLAYERBOT_V2_CREATION_POLICY_H

#include <cstdint>

namespace Playerbot::V2
{
inline constexpr uint8_t NewBotLevel = 1;

enum class BotOrigin : uint8_t
{
    NewBot = 0,          // a character this module would create
    OwnerAlt = 1,        // an existing character the player already owns
    RealCharacter = 2    // any other existing player character
};

enum class BotFaction : uint8_t
{
    Alliance = 0,
    Horde = 1,
    Unknown = 2
};

struct PopulationConfig
{
    bool enable = false;       // Playerbot.Enable, default off
    bool killSwitch = true;    // Playerbot.Population.KillSwitch, default engaged
    uint32_t allianceCap = 0;  // 0 creates none
    uint32_t hordeCap = 0;
};

// Classic starter races only. Anything else is Unknown and cannot be created here.
inline BotFaction FactionForStarterRace(uint8_t race)
{
    switch (race)
    {
        case 1: // human
        case 3: // dwarf
        case 4: // night elf
        case 7: // gnome
            return BotFaction::Alliance;
        case 2: // orc
        case 5: // undead
        case 6: // tauren
        case 8: // troll
            return BotFaction::Horde;
        default:
            return BotFaction::Unknown;
    }
}

// New bots are level 1. Owner level is ignored. Existing characters keep their level.
inline uint8_t LevelForCreation(BotOrigin origin, uint8_t existingLevel, uint8_t ownerLevel)
{
    (void)ownerLevel;
    if (origin == BotOrigin::NewBot)
        return NewBotLevel;
    return existingLevel;
}

inline bool PopulationAllowsNewBot(PopulationConfig const& config, BotFaction faction, uint32_t alreadyOnline)
{
    if (!config.enable || config.killSwitch || faction == BotFaction::Unknown)
        return false;
    uint32_t const cap = faction == BotFaction::Alliance ? config.allianceCap : config.hordeCap;
    return cap > 0 && alreadyOnline < cap;
}

// The module does not place a bot on a map. Core mmap pathing is not a starter-zone walk
// until mmaps are regenerated from client 70862, and the handcrafted road graph is not linked.
inline bool MayEnterWorld(PopulationConfig const& config, BotFaction faction, uint32_t alreadyOnline, bool navigationValidated)
{
    return navigationValidated && PopulationAllowsNewBot(config, faction, alreadyOnline);
}
}
#endif
