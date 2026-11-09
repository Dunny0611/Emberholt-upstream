/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "tc_catch2.h"
#include "BotGroupRules.h"
#include "CharacterFactory.h"
#include "CreationPolicy.h"

using Playerbot::V2::BotFaction;
using Playerbot::V2::CreateRefusal;
using Playerbot::V2::DecideNewCharacter;
using Playerbot::V2::NewCharacterDecision;
using Playerbot::V2::NewCharacterRequest;
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

TEST_CASE("A new character is refused unless the pool account, level, and navigation gate all pass", "[PlayerbotCreation]")
{
    PopulationConfig armed;
    armed.enable = true;
    armed.killSwitch = false;
    armed.allianceCap = 1;
    NewCharacterRequest request;
    request.poolAccountId = 50;
    request.ownerAccountId = 9;
    request.race = 1;
    request.level = 1;

    CHECK(DecideNewCharacter(armed, 0, false, request).refusal == CreateRefusal::GateClosed);

    request.poolAccountId = 0;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::MissingPoolAccount);

    request.poolAccountId = 9;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::OwnerAccount);

    request.poolAccountId = 50;
    request.level = 60;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::WrongLevel);
    CHECK(DecideNewCharacter(armed, 0, true, request).level == 1);

    request.level = 1;
    request.race = 25;
    CHECK(DecideNewCharacter(armed, 0, true, request).refusal == CreateRefusal::UnknownFaction);

    request.race = 1;
    NewCharacterDecision const admitted = DecideNewCharacter(armed, 0, true, request);
    CHECK(admitted.refusal == CreateRefusal::None);
    CHECK(admitted.level == 1);
    CHECK(admitted.faction == BotFaction::Alliance);
}

TEST_CASE("Tap and loot rules match the core when the group has no bot", "[PlayerbotCreation]")
{
    CHECK(BotGroupRules::SharesTap(true, false, false, false));
    CHECK(BotGroupRules::SharesTap(false, true, false, false));
    CHECK_FALSE(BotGroupRules::SharesTap(false, false, false, false));
    CHECK(BotGroupRules::SharesTap(false, false, true, false));
    CHECK_FALSE(BotGroupRules::SharesTap(false, false, true, true));

    std::vector<BotGroupRules::LooterCandidate> candidates(3);
    candidates[0].inRange = false;
    candidates[1].inRange = true;
    candidates[2].inRange = true;
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 0, false) == 1);
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 1, true) == 1);
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 1, false) == 2);

    candidates[1].bot = true;
    CHECK(BotGroupRules::NextRoundRobinLooter(candidates, 0, false) == 2);
}
