/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "CreationPolicy.h"

using Playerbot::V2::BotFaction;
using Playerbot::V2::BotOrigin;
using Playerbot::V2::FactionForStarterRace;
using Playerbot::V2::LevelForCreation;
using Playerbot::V2::MayEnterWorld;
using Playerbot::V2::PopulationAllowsNewBot;
using Playerbot::V2::PopulationConfig;

TEST_CASE("New bots are level 1 and existing characters keep their level", "[PlayerbotCreation]")
{
    CHECK(LevelForCreation(BotOrigin::NewBot, 1, 60) == 1);
    CHECK(LevelForCreation(BotOrigin::NewBot, 20, 60) == 1);
    CHECK(LevelForCreation(BotOrigin::OwnerAlt, 37, 60) == 37);
    CHECK(LevelForCreation(BotOrigin::RealCharacter, 60, 1) == 60);
}

TEST_CASE("Starter races map to a faction and other races do not", "[PlayerbotCreation]")
{
    CHECK(FactionForStarterRace(1) == BotFaction::Alliance);
    CHECK(FactionForStarterRace(7) == BotFaction::Alliance);
    CHECK(FactionForStarterRace(2) == BotFaction::Horde);
    CHECK(FactionForStarterRace(8) == BotFaction::Horde);
    CHECK(FactionForStarterRace(0) == BotFaction::Unknown);
    CHECK(FactionForStarterRace(25) == BotFaction::Unknown);
}

TEST_CASE("Population is off by default and the kill switch stops creation", "[PlayerbotCreation]")
{
    PopulationConfig const defaults;
    CHECK_FALSE(defaults.enable);
    CHECK(defaults.killSwitch);
    CHECK(defaults.allianceCap == 0);
    CHECK(defaults.hordeCap == 0);
    CHECK_FALSE(PopulationAllowsNewBot(defaults, BotFaction::Alliance, 0));
    CHECK_FALSE(PopulationAllowsNewBot(defaults, BotFaction::Horde, 0));

    PopulationConfig armed = defaults;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 2;
    armed.hordeCap = 1;
    CHECK(PopulationAllowsNewBot(armed, BotFaction::Alliance, 0));
    CHECK(PopulationAllowsNewBot(armed, BotFaction::Alliance, 1));
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Alliance, 2));
    CHECK(PopulationAllowsNewBot(armed, BotFaction::Horde, 0));
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Horde, 1));
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Unknown, 0));

    armed.killSwitch = true;
    CHECK_FALSE(PopulationAllowsNewBot(armed, BotFaction::Alliance, 0));
}

TEST_CASE("Bots are not placed in the world until navigation is validated", "[PlayerbotCreation]")
{
    PopulationConfig armed;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 1;
    CHECK_FALSE(MayEnterWorld(armed, BotFaction::Alliance, 0, false));
    CHECK(MayEnterWorld(armed, BotFaction::Alliance, 0, true));
    CHECK_FALSE(MayEnterWorld(armed, BotFaction::Alliance, 1, true));
}
