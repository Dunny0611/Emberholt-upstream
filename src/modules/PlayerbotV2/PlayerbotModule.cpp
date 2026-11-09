/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "CreationPolicy.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"
#include <algorithm>
#include <string_view>

namespace
{
Playerbot::V2::PopulationConfig ReadPopulation()
{
    Playerbot::V2::PopulationConfig config;
    config.enable = sConfigMgr->GetBoolDefault("Playerbot.Enable"sv, false);
    config.killSwitch = sConfigMgr->GetBoolDefault("Playerbot.Population.KillSwitch"sv, true);
    config.allianceCap = uint32_t(std::max(0, sConfigMgr->GetIntDefault("Playerbot.Population.Alliance"sv, 0)));
    config.hordeCap = uint32_t(std::max(0, sConfigMgr->GetIntDefault("Playerbot.Population.Horde"sv, 0)));
    return config;
}

class playerbot_v2_world : public WorldScript
{
public:
    playerbot_v2_world() : WorldScript("playerbot_v2_world") { }

    void OnWorldInitialized() override
    {
        Playerbot::V2::PopulationConfig const config = ReadPopulation();
        // navigationValidated is false: this build does not include the road graph and has not
        // regenerated mmaps from client 70862. MayEnterWorld therefore stays false.
        bool const wouldPlace = Playerbot::V2::MayEnterWorld(config, Playerbot::V2::BotFaction::Alliance, 0, false)
            || Playerbot::V2::MayEnterWorld(config, Playerbot::V2::BotFaction::Horde, 0, false);
        TC_LOG_INFO("server.loading",
            "Playerbot V2 experimental port: enable={} killSwitch={} allianceCap={} hordeCap={} placeBots={}. "
            "New bots would be level 1. No character is created. Starter-zone walking is not available without regenerated 70862 mmaps.",
            config.enable, config.killSwitch, config.allianceCap, config.hordeCap, wouldPlace);
        (void)wouldPlace;
    }
};
}

void AddSC_playerbot_v2()
{
    new playerbot_v2_world();
}
