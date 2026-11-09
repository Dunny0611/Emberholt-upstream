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
#include "BotLogin.h"
#include "CharacterFactory.h"
#include "CreationPolicy.h"
#include "LifecyclePolicy.h"

using Playerbot::V2::AcceptGroupInvite;
using Playerbot::V2::AdmitPersistence;
using Playerbot::V2::AiStepsPerTick;
using Playerbot::V2::BotFaction;
using Playerbot::V2::ChooseStep;
using Playerbot::V2::ClassifyName;
using Playerbot::V2::CorePathIsUsable;
using Playerbot::V2::DropOwnedSession;
using Playerbot::V2::LocalStepPoint;
using Playerbot::V2::NameUse;
using Playerbot::V2::PoolScreen;
using Playerbot::V2::PoolSecurityAllowed;
using Playerbot::V2::ScreenPoolIds;
using Playerbot::V2::StepInput;
using Playerbot::V2::StepKind;
using Playerbot::V2::CreateRefusal;
using Playerbot::V2::DecideNewCharacter;
using Playerbot::V2::NewCharacterDecision;
using Playerbot::V2::NewCharacterRequest;
using Playerbot::V2::PersistLevelOneCharacter;
using Playerbot::V2::PersistRequest;
using Playerbot::V2::PersistResult;
using Playerbot::V2::TryScheduleLogin;
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

#ifdef TRINITY_PLAYERBOT_V2
TEST_CASE("A closed gate returns before a character insert or a login session", "[PlayerbotCreation]")
{
    PopulationConfig const defaults;
    PersistRequest request;
    request.config = defaults;
    request.character.poolAccountId = 50;
    request.character.ownerAccountId = 9;
    request.character.race = 1;
    request.character.classId = 1;
    request.character.sex = 0;
    request.character.level = 1;
    request.name = "Notcreated";

    PersistResult const created = PersistLevelOneCharacter(request);
    CHECK(created.refusal == CreateRefusal::GateClosed);
    CHECK(created.guid.IsEmpty());

    std::string detail;
    CHECK_FALSE(TryScheduleLogin(defaults, BotFaction::Alliance, 0, 50, ObjectGuid::Empty, 1, detail));
    CHECK(detail == "population gate is closed");
}

TEST_CASE("Persistence admission does not require the navigation flag", "[PlayerbotCreation]")
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
    NewCharacterDecision const admitted = AdmitPersistence(armed, 0, request);
    CHECK(admitted.refusal == CreateRefusal::None);
    CHECK(admitted.level == 1);
    CHECK_FALSE(MayEnterWorld(armed, BotFaction::Alliance, 0, false));
}
#endif

TEST_CASE("Pool accounts stay dedicated, unprivileged, and faction-matched", "[PlayerbotCreation]")
{
    CHECK(ScreenPoolIds(0, 0, BotFaction::Alliance, BotFaction::Alliance) == PoolScreen::Missing);
    CHECK(ScreenPoolIds(50, 50, BotFaction::Alliance, BotFaction::Alliance) == PoolScreen::Shared);
    CHECK(ScreenPoolIds(50, 60, BotFaction::Horde, BotFaction::Alliance) == PoolScreen::WrongFaction);
    CHECK(ScreenPoolIds(50, 60, BotFaction::Alliance, BotFaction::Alliance) == PoolScreen::Ok);
    CHECK(PoolSecurityAllowed(0));
    CHECK_FALSE(PoolSecurityAllowed(2));
}

TEST_CASE("A taken name is reused only for an owned level-1 marker row", "[PlayerbotCreation]")
{
    CHECK(ClassifyName(false, false, 1, 0, 50) == NameUse::Free);
    CHECK(ClassifyName(true, true, 1, 50, 50) == NameUse::Reuse);
    CHECK(ClassifyName(true, true, 60, 50, 50) == NameUse::Reject);
    CHECK(ClassifyName(true, false, 1, 50, 50) == NameUse::Reject);
    CHECK(ClassifyName(true, true, 1, 9, 50) == NameUse::Reject);
}

TEST_CASE("The kill switch and logout drop owned sessions only", "[PlayerbotCreation]")
{
    CHECK(DropOwnedSession(true, true));
    CHECK_FALSE(DropOwnedSession(false, true));
    CHECK_FALSE(DropOwnedSession(true, false));
}

TEST_CASE("A same-faction player invite is accepted and a bot or cross-faction invite is not", "[PlayerbotCreation]")
{
    CHECK(AcceptGroupInvite(true, false, BotFaction::Alliance, BotFaction::Alliance, false));
    CHECK_FALSE(AcceptGroupInvite(true, true, BotFaction::Alliance, BotFaction::Alliance, false));
    CHECK_FALSE(AcceptGroupInvite(true, false, BotFaction::Alliance, BotFaction::Horde, false));
    CHECK_FALSE(AcceptGroupInvite(false, false, BotFaction::Alliance, BotFaction::Alliance, false));
    CHECK_FALSE(AcceptGroupInvite(true, false, BotFaction::Horde, BotFaction::Horde, true));
}

TEST_CASE("Movement follows a core mmap path or a melee attacker and does not invent a shortcut", "[PlayerbotCreation]")
{
    CHECK(CorePathIsUsable(true, 0x01));
    CHECK(CorePathIsUsable(true, 0x04));
    CHECK_FALSE(CorePathIsUsable(true, 0x10));
    CHECK_FALSE(CorePathIsUsable(true, 0x08));
    CHECK_FALSE(CorePathIsUsable(true, 0x02));
    CHECK_FALSE(CorePathIsUsable(true, 0x40));
    CHECK_FALSE(CorePathIsUsable(true, 0x80));
    CHECK_FALSE(CorePathIsUsable(false, 0x01));
    CHECK(LocalStepPoint(3.0f, 4.0f, 1.0f));
    CHECK_FALSE(LocalStepPoint(20.0f, 0.0f, 0.0f));
    CHECK_FALSE(LocalStepPoint(0.0f, 0.0f, 9.0f));

    StepInput input;
    input.owned = true;
    CHECK(ChooseStep(input) == StepKind::Hold);
    input.attackerInMelee = true;
    CHECK(ChooseStep(input) == StepKind::Melee);
    input.attackerInMelee = false;
    input.corePathUsable = true;
    CHECK(ChooseStep(input) == StepKind::Path);
    input.stepsAlreadyThisTick = AiStepsPerTick;
    CHECK(ChooseStep(input) == StepKind::Hold);
    input.stepsAlreadyThisTick = 0;
    input.gateClosed = true;
    CHECK(ChooseStep(input) == StepKind::None);
    input.gateClosed = false;
    input.owned = false;
    CHECK(ChooseStep(input) == StepKind::None);
}
