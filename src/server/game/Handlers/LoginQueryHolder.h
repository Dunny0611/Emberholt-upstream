/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef TRINITY_LOGINQUERYHOLDER_H
#define TRINITY_LOGINQUERYHOLDER_H

#include "Define.h"
#include "ObjectGuid.h"
#include "QueryHolder.h"

// The same holder WorldSession::HandlePlayerLogin already consumes.
// Initialize() stays in CharacterHandler.cpp. Real logins are unchanged.
class TC_GAME_API LoginQueryHolder : public CharacterDatabaseQueryHolder
{
public:
    LoginQueryHolder(uint32 accountId, ObjectGuid guid)
        : m_accountId(accountId), m_guid(guid) { }

    ObjectGuid GetGuid() const { return m_guid; }
    uint32 GetAccountId() const { return m_accountId; }
    bool Initialize();

private:
    uint32 m_accountId;
    ObjectGuid m_guid;
};

#endif
