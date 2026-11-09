/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "BotLogin.h"
#include "CreationPolicy.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"
#include <algorithm>
#include <string_view>

void AddSC_playerbot_v2_commands();

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
        TC_LOG_INFO("server.loading",
            "Playerbot V2 experimental port: enable={} killSwitch={} allianceCap={} hordeCap={}. "
            "Startup does not create or log in a character. .playerbot create, login, and logout stay refused while the gate is closed. "
            "A logged-in owned bot may path, cast a known harmful spell, or attack a legal creature. Quest accept and turn-in are not linked.",
            config.enable, config.killSwitch, config.allianceCap, config.hordeCap);
    }

    void OnUpdate(uint32 diff) override
    {
        Playerbot::V2::PopulationConfig const config = ReadPopulation();
        Playerbot::V2::TickSessions(diff, !config.enable || config.killSwitch);
    }
};
}

void AddSC_playerbot_v2()
{
    new playerbot_v2_world();
    AddSC_playerbot_v2_commands();
}
