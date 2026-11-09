/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "BotLogin.h"
#include "CharacterFactory.h"
#include "LifecyclePolicy.h"
#include "QuestGuide.h"
#include "Config.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ObjectMgr.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace Trinity::ChatCommands;

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

std::vector<uint32_t> PoolAccounts(Playerbot::V2::BotFaction faction)
{
    using Playerbot::V2::AppendAccountIds;
    std::vector<uint32_t> ids;
    if (faction == Playerbot::V2::BotFaction::Alliance)
    {
        AppendAccountIds(std::to_string(std::max(0, sConfigMgr->GetIntDefault("Playerbot.Account.Alliance"sv, 0))), ids);
        AppendAccountIds(sConfigMgr->GetStringDefault("Playerbot.Account.AllianceIds"sv, ""sv), ids);
    }
    else if (faction == Playerbot::V2::BotFaction::Horde)
    {
        AppendAccountIds(std::to_string(std::max(0, sConfigMgr->GetIntDefault("Playerbot.Account.Horde"sv, 0))), ids);
        AppendAccountIds(sConfigMgr->GetStringDefault("Playerbot.Account.HordeIds"sv, ""sv), ids);
    }
    return ids;
}

bool HandleCreate(ChatHandler* handler, std::string const& name, uint8 race, uint8 classId, uint8 sex)
{
    using namespace Playerbot::V2;
    if (race == 0)
    {
        static uint32_t slot = 0;
        race = StarterRaceForSlot(uint8_t(slot % 2), slot / 2);
        ++slot;
    }
    PopulationConfig const config = ReadPopulation();
    BotFaction const faction = FactionForStarterRace(race);
    uint32_t const owner = handler->GetSession() ? handler->GetSession()->GetAccountId() : 0;
    PersistRequest request;
    request.config = config;
    request.alreadyOnline = OnlineCount(faction);
    request.maxOnAccount = uint32_t(std::max(0, sConfigMgr->GetIntDefault("Playerbot.Account.MaxCharacters"sv, 10)));
    std::vector<uint32_t> const mine = PoolAccounts(faction);
    std::vector<uint32_t> const other = PoolAccounts(faction == BotFaction::Alliance ? BotFaction::Horde : BotFaction::Alliance);
    request.poolFaction = faction;
    request.otherFactionPoolAccount = 0;
    request.character.ownerAccountId = owner;
    request.character.race = race;
    request.character.classId = classId;
    request.character.sex = sex;
    request.character.level = NewBotLevel;
    request.name = name;

    PersistResult result { CreateRefusal::MissingPoolAccount, ObjectGuid::Empty, "no pool account is configured for that faction" };
    for (uint32_t accountId : mine)
    {
        if (accountId == 0 || accountId == owner)
            continue;
        if (std::find(other.begin(), other.end(), accountId) != other.end())
            continue;
        request.character.poolAccountId = accountId;
        result = PersistLevelOneCharacter(request);
        if (result.refusal != CreateRefusal::AccountFull && result.refusal != CreateRefusal::PrivilegedAccount && result.refusal != CreateRefusal::UnknownAccount)
            break;
    }
    if (result.refusal != CreateRefusal::None)
    {
        handler->PSendSysMessage("Playerbot create refused (%u): %s", uint32_t(result.refusal), result.detail.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    handler->PSendSysMessage("Playerbot create queued %s %s (%s). Apply sql/playerbot_v2_character.sql first or the commit fails. .playerbot login uses that row after the commit. Walking is not started.",
        request.name.c_str(), request.surname.c_str(), result.guid.ToString().c_str());
    return true;
}

bool HandleLogin(ChatHandler* handler, std::string const& name)
{
    using namespace Playerbot::V2;
    std::string normalized = name;
    if (!normalizePlayerName(normalized))
    {
        handler->PSendSysMessage("Playerbot login refused: name failed normalization");
        handler->SetSentErrorMessage(true);
        return false;
    }

    CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByName(normalized);
    if (!entry || entry->IsDeleted)
    {
        handler->PSendSysMessage("Playerbot login refused: %s is not in the character cache", normalized.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }
    BotFaction const faction = FactionForStarterRace(entry->Race);
    PopulationConfig const config = ReadPopulation();
    std::vector<uint32_t> const accounts = PoolAccounts(faction);
    if (std::find(accounts.begin(), accounts.end(), entry->AccountId) == accounts.end())
    {
        handler->PSendSysMessage("Playerbot login refused: %s is not on the configured pool account", normalized.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    std::string detail;
    uint32_t const configuredMax = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint8_t const maxLevel = uint8_t(std::min<uint32_t>(255u, std::max<uint32_t>(1u, configuredMax)));
    if (!TryScheduleLogin(config, faction, OnlineCount(faction), entry->AccountId, entry->Guid, entry->Level, maxLevel, detail))
    {
        handler->PSendSysMessage("Playerbot login refused: %s", detail.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    handler->PSendSysMessage("Playerbot login submitted for %s. %s", normalized.c_str(), detail.c_str());
    return true;
}

bool HandleLogout(ChatHandler* handler, std::string const& name)
{
    using namespace Playerbot::V2;
    std::string normalized = name;
    if (!normalizePlayerName(normalized))
    {
        handler->PSendSysMessage("Playerbot logout refused: name failed normalization");
        handler->SetSentErrorMessage(true);
        return false;
    }
    CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByName(normalized);
    if (!entry)
    {
        handler->PSendSysMessage("Playerbot logout refused: %s is not in the character cache", normalized.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }
    std::string detail;
    if (!LogoutOwned(entry->Guid, detail))
    {
        handler->PSendSysMessage("Playerbot logout refused: %s", detail.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }
    handler->PSendSysMessage("Playerbot logout: %s. %s", normalized.c_str(), detail.c_str());
    return true;
}

class playerbot_v2_commands : public CommandScript
{
public:
    playerbot_v2_commands() : CommandScript("playerbot_v2_commands") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable playerbotTable =
        {
            { "create", HandleCreate, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "login", HandleLogin, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "logout", HandleLogout, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
        };
        static ChatCommandTable table =
        {
            { "playerbot", playerbotTable },
        };
        return table;
    }
};
}

void AddSC_playerbot_v2_commands()
{
    new playerbot_v2_commands();
}
