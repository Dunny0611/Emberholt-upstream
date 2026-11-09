/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "BotLogin.h"
#include "LifecyclePolicy.h"
#include "Session/BotSession.h"

#include "AccountMgr.h"
#include "DB2Structure.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "RealmList.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

bool OwnedOnline(ObjectGuid character)
{
    return std::any_of(g_logins.begin(), g_logins.end(), [character](LiveLogin const& live)
    {
        Player* player = live.session->GetPlayer();
        return player && player->GetGUID() == character;
    });
}

void AcceptPendingInvites()
{
    for (LiveLogin const& live : g_logins)
    {
        Player* bot = live.session->GetPlayer();
        if (!bot || !bot->IsInWorld())
            continue;
        Group* group = bot->GetGroupInvite();
        if (!group)
            continue;

        Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        bool const leaderIsBot = leader && leader->GetSession() && leader->GetSession()->IsBot();
        BotFaction const leaderFaction = leader ? FactionForStarterRace(leader->GetRace()) : BotFaction::Unknown;
        if (!AcceptGroupInvite(true, leaderIsBot, live.faction, leaderFaction, false))
        {
            bot->UninviteFromGroup();
            continue;
        }

        group->RemoveInvite(bot);
        if (group->GetLeaderGUID() == bot->GetGUID() || group->IsFull())
            continue;
        if (!group->IsCreated())
        {
            if (!leader)
            {
                group->RemoveAllInvites();
                continue;
            }
            group->RemoveInvite(leader);
            group->Create(leader);
            sGroupMgr->AddGroup(group);
        }
        if (group->AddMember(bot))
            group->BroadcastGroupUpdate();
    }
}

void StepOneOwnedBot()
{
    if (g_logins.empty())
        return;
    static std::size_t cursor = 0;
    auto const started = std::chrono::steady_clock::now();
    uint32_t steps = 0;
    for (std::size_t n = 0; n < g_logins.size() && steps < AiStepsPerTick; ++n)
    {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() >= AiBudgetMs)
            break;
        LiveLogin const& live = g_logins[cursor % g_logins.size()];
        cursor = (cursor + 1) % g_logins.size();
        Player* bot = live.session->GetPlayer();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            continue;

        Unit* attacker = nullptr;
        for (Unit* unit : bot->getAttackers())
        {
            if (unit && bot->GetDistance(unit) <= 5.0f)
            {
                attacker = unit;
                break;
            }
        }

        bool pathUsable = false;
        PathGenerator path(bot);
        if (!attacker && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == IDLE_MOTION_TYPE)
        {
            float const destX = bot->GetPositionX() + 8.0f * std::cos(bot->GetOrientation());
            float const destY = bot->GetPositionY() + 8.0f * std::sin(bot->GetOrientation());
            bool const calculated = path.CalculatePath(destX, destY, bot->GetPositionZ());
            pathUsable = CorePathIsUsable(calculated, uint32_t(path.GetPathType())) && path.GetPath().size() >= 2;
            if (pathUsable)
            {
                G3D::Vector3 const& point = path.GetPath()[1];
                pathUsable = LocalStepPoint(point.x - bot->GetPositionX(), point.y - bot->GetPositionY(), point.z - bot->GetPositionZ());
            }
        }

        switch (ChooseStep({ true, false, attacker != nullptr, pathUsable, steps }))
        {
            case StepKind::Melee:
                if (attacker && bot->GetVictim() != attacker)
                    bot->Attack(attacker, true);
                ++steps;
                break;
            case StepKind::Path:
            {
                // MovePoint walks a spline to the next mmap point. It is not a teleport.
                G3D::Vector3 const& point = path.GetPath()[1];
                bot->GetMotionMaster()->MovePoint(1, point.x, point.y, point.z);
                ++steps;
                break;
            }
            case StepKind::Hold:
            case StepKind::None:
                break;
        }
    }
}

void TickSessions(uint32_t diff, bool gateClosed)
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

    if (gateClosed)
        for (LiveLogin const& live : g_logins)
            live.session->KickPlayer("population gate closed");

    for (auto it = g_logins.begin(); it != g_logins.end();)
    {
        PacketFilter filter(it->session.get());
        bool const keep = it->session->Update(diff, filter);
        if (!keep || (!it->session->PlayerLoading() && !it->session->GetPlayer()))
            it = g_logins.erase(it);
        else
            ++it;
    }

    if (!gateClosed)
    {
        AcceptPendingInvites();
        StepOneOwnedBot();
    }
}

bool LogoutOwned(ObjectGuid character, std::string& detail)
{
    for (LiveLogin const& live : g_logins)
    {
        Player* player = live.session->GetPlayer();
        if (!player || player->GetGUID() != character)
            continue;
        live.session->KickPlayer("owned bot logout");
        detail = "owned bot session will log out";
        return true;
    }
    detail = "that character is not an owned bot session";
    return false;
}

bool TryScheduleLogin(PopulationConfig const& config, BotFaction faction, uint32_t alreadyOnline, uint32_t accountId, ObjectGuid character, uint8_t level, std::string& detail)
{
    if (!PopulationAllowsNewBot(config, faction, alreadyOnline))
    {
        detail = "population gate is closed";
        return false;
    }
    if (level != NewBotLevel)
    {
        detail = "only a level-1 character is logged in";
        return false;
    }
    if (accountId == 0 || character.IsEmpty())
    {
        detail = "account or character guid is empty";
        return false;
    }
    if (OwnedOnline(character))
    {
        detail = "that owned bot is already logging in or online";
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
    if (!PoolSecurityAllowed(sAccountMgr->GetSecurity(accountId, int32_t(sRealmList->GetCurrentRealmId().Realm))))
    {
        detail = "pool account is GM or admin; login refused";
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
