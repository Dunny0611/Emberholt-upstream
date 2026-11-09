/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "BotLogin.h"
#include "Session/BotSession.h"

#include "AccountMgr.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "WorldSession.h"

#include <algorithm>
#include <vector>

namespace Playerbot::V2
{
namespace
{
struct PendingCreate
{
    std::shared_ptr<BotSession> session;
    std::shared_ptr<bool> finished;
};

struct LiveLogin
{
    std::shared_ptr<BotSession> session;
    BotFaction faction = BotFaction::Unknown;
};

std::vector<PendingCreate> g_creates;
std::vector<LiveLogin> g_logins;
}

void HoldCreateSession(std::shared_ptr<BotSession> session, std::shared_ptr<bool> finished)
{
    g_creates.push_back({ std::move(session), std::move(finished) });
}

uint32_t OnlineCount(BotFaction faction)
{
    return uint32_t(std::count_if(g_logins.begin(), g_logins.end(), [faction](LiveLogin const& live)
    {
        return live.faction == faction;
    }));
}

void TickSessions(uint32_t diff)
{
    for (auto it = g_creates.begin(); it != g_creates.end();)
    {
        PacketFilter filter(it->session.get());
        it->session->Update(diff, filter);
        if (it->finished && *it->finished)
            it = g_creates.erase(it);
        else
            ++it;
    }

    for (auto it = g_logins.begin(); it != g_logins.end();)
    {
        PacketFilter filter(it->session.get());
        bool const keep = it->session->Update(diff, filter);
        if (!keep || (!it->session->PlayerLoading() && !it->session->GetPlayer()))
            it = g_logins.erase(it);
        else
            ++it;
    }
}

bool TryScheduleLogin(PopulationConfig const& config, BotFaction faction, uint32_t alreadyOnline, uint32_t accountId, ObjectGuid character, std::string& detail)
{
    if (!PopulationAllowsNewBot(config, faction, alreadyOnline))
    {
        detail = "population gate is closed";
        return false;
    }
    if (accountId == 0 || character.IsEmpty())
    {
        detail = "account or character guid is empty";
        return false;
    }

    QueryResult marker = CharacterDatabase.PQuery("SELECT account FROM playerbot_v2_character WHERE guid = {}", character.GetCounter());
    if (!marker || marker->Fetch()[0].GetUInt32() != accountId)
    {
        detail = "character is not a factory row; an existing character is not logged in";
        return false;
    }

    std::string accountName;
    if (!sAccountMgr->GetName(accountId, accountName))
    {
        detail = "pool account name was not found";
        return false;
    }

    auto session = std::make_shared<BotSession>(accountId, std::move(accountName));
    if (!session->BeginLogin(character))
    {
        detail = "login holder was not submitted";
        return false;
    }

    g_logins.push_back({ std::move(session), faction });
    TC_LOG_INFO("entities.player.character", "Playerbot V2 login submitted for {} on account {}", character.ToString(), accountId);
    detail = "login submitted; the session is ticked by the module and is not a navigation test";
    return true;
}
}
