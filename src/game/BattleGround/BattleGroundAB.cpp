/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "Object.h"
#include "Player.h"
#include "BattleGround.h"
#include "BattleGroundAB.h"
#include "GameObject.h"
#include "BattleGroundMgr.h"
#include "Language.h"
#include "WorldPacket.h"
 // TODO REMOVE this when graveyard handling for pvp is updated
#include "DBCStores.h"

/// <summary>
/// Initializes a new instance of the <see cref="BattleGroundAB"/> class.
/// </summary>
BattleGroundAB::BattleGroundAB()
    : m_IsInformedNearVictory(false), // Initialize m_IsInformedNearVictory
    m_honorTicks(0),                // Initialize m_honorTicks
    m_ReputationTics(0)             // Initialize m_ReputationTics
{
    // Initialize start message IDs
    m_StartMessageIds[BG_STARTING_EVENT_FIRST] = 0;
    m_StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_AB_START_ONE_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_THIRD] = LANG_BG_AB_START_HALF_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_AB_HAS_BEGUN;

    // Initialize node-related variables
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        // Initialize m_BannerTimers
        m_BannerTimers[i].timer = 0;
        m_BannerTimers[i].type = 0;
        m_BannerTimers[i].teamIndex = 0;

        // Initialize m_Nodes
        m_Nodes[i] = BG_AB_NODE_TYPE_NEUTRAL;

        // Initialize m_NodeTimers
        m_NodeTimers[i] = 0;

        // Initialize m_prevNodes
        m_prevNodes[i] = 0;
    }

    // Initialize m_ReputationScoreTics and m_honorScoreTicks
    for (uint8 i = 0; i < PVP_TEAM_COUNT; ++i)
    {
        m_ReputationScoreTics[i] = 0;
        m_honorScoreTicks[i] = 0; // Initialize m_honorScoreTicks
        m_lastTick[i] = 0;        // Initialize m_lastTick
    }
}

/// <summary>
/// Finalizes an instance of the <see cref="BattleGroundAB"/> class.
/// </summary>
BattleGroundAB::~BattleGroundAB()
{
}

/// <summary>
/// Updates the specified diff.
/// </summary>
/// <param name="diff">The diff.</param>
void BattleGroundAB::Update(uint32 diff)
{
    // Call parent class update
    BattleGround::Update(diff);

    // Check if the battleground is in progress
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        int team_points[PVP_TEAM_COUNT] = { 0, 0 };

        // Iterate through all nodes
        for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
        {
            // Handle banner timers
            if (m_BannerTimers[node].timer)
            {
                if (m_BannerTimers[node].timer > diff)
                {
                    m_BannerTimers[node].timer -= diff;
                }
                else
                {
                    m_BannerTimers[node].timer = 0;
                    _CreateBanner(node, m_BannerTimers[node].type, m_BannerTimers[node].teamIndex, false);
                }
            }

            // Handle node timers
            if (m_NodeTimers[node])
            {
                if (m_NodeTimers[node] > diff)
                {
                    m_NodeTimers[node] -= diff;
                }
                else
                {
                    m_NodeTimers[node] = 0;
                    // Change from contested to occupied
                    uint8 teamIndex = m_Nodes[node] - 1;
                    m_prevNodes[node] = m_Nodes[node];
                    m_Nodes[node] += 2;
                    // Create new occupied banner
                    _CreateBanner(node, BG_AB_NODE_TYPE_OCCUPIED, teamIndex, true);
                    _SendNodeUpdate(node);
                    _NodeOccupied(node, (teamIndex == 0) ? ALLIANCE : HORDE);
                    // Message to chatlog
                    if (teamIndex == 0)
                    {
                        SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_ALLIANCE, NULL, LANG_BG_ALLY, _GetNodeNameId(node));
                        PlaySoundToAll(BG_AB_SOUND_NODE_CAPTURED_ALLIANCE);
                    }
                    else
                    {
                        SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_HORDE, NULL, LANG_BG_HORDE, _GetNodeNameId(node));
                        PlaySoundToAll(BG_AB_SOUND_NODE_CAPTURED_HORDE);
                    }
                }
            }

            // Accumulate team points
            for (uint8 team = 0; team < PVP_TEAM_COUNT; ++team)
                if (m_Nodes[node] == team + BG_AB_NODE_TYPE_OCCUPIED)
                {
                    ++team_points[team];
                }
        }

        // Accumulate points for each team
        for (uint8 team = 0; team < PVP_TEAM_COUNT; ++team)
        {
            int points = team_points[team];
            if (!points)
            {
                continue;
            }
            m_lastTick[team] += diff;
            if (m_lastTick[team] > BG_AB_TickIntervals[points])
            {
                m_lastTick[team] -= BG_AB_TickIntervals[points];
                m_TeamScores[team] += BG_AB_TickPoints[points];
                m_honorScoreTicks[team] += BG_AB_TickPoints[points];
                m_ReputationScoreTics[team] += BG_AB_TickPoints[points];
                if (m_ReputationScoreTics[team] >= m_ReputationTics)
                {
                    (team == TEAM_INDEX_ALLIANCE) ? RewardReputationToTeam(509, 10, ALLIANCE) : RewardReputationToTeam(510, 10, HORDE);
                    m_ReputationScoreTics[team] -= m_ReputationTics;
                }
                if (m_honorScoreTicks[team] >= m_honorTicks)
                {
                    RewardHonorToTeam(BG_AB_PerTickHonor[GetBracketId()], (team == TEAM_INDEX_ALLIANCE) ? ALLIANCE : HORDE);
                    m_honorScoreTicks[team] -= m_honorTicks;
                }
                if (!m_IsInformedNearVictory && m_TeamScores[team] > BG_AB_WARNING_NEAR_VICTORY_SCORE)
                {
                    if (team == TEAM_INDEX_ALLIANCE)
                    {
                        SendMessageToAll(LANG_BG_AB_A_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                    }
                    else
                    {
                        SendMessageToAll(LANG_BG_AB_H_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                    }
                    PlaySoundToAll(BG_AB_SOUND_NEAR_VICTORY);
                    m_IsInformedNearVictory = true;
                }

                if (m_TeamScores[team] > BG_AB_MAX_TEAM_SCORE)
                {
                    m_TeamScores[team] = BG_AB_MAX_TEAM_SCORE;
                }
                if (team == TEAM_INDEX_ALLIANCE)
                {
                    UpdateWorldState(BG_AB_OP_RESOURCES_ALLY, m_TeamScores[team]);
                }
                if (team == TEAM_INDEX_HORDE)
                {
                    UpdateWorldState(BG_AB_OP_RESOURCES_HORDE, m_TeamScores[team]);
                }
            }
        }

        // Check win condition
        if (m_TeamScores[TEAM_INDEX_ALLIANCE] >= BG_AB_MAX_TEAM_SCORE)
        {
            EndBattleGround(ALLIANCE);
        }
        if (m_TeamScores[TEAM_INDEX_HORDE] >= BG_AB_MAX_TEAM_SCORE)
        {
            EndBattleGround(HORDE);
        }
    }
}

/// <summary>
/// Startings the event open doors.
/// </summary>
void BattleGroundAB::StartingEventOpenDoors()
{
    OpenDoorEvent(BG_EVENT_DOOR);
}

/// <summary>
/// Adds the player.
/// </summary>
/// <param name="plr">The PLR.</param>
void BattleGroundAB::AddPlayer(Player* plr)
{
    // Call parent class AddPlayer
    BattleGround::AddPlayer(plr);
    // Create score and add it to map, default values are set in the constructor
    BattleGroundABScore* sc = new BattleGroundABScore;
    m_PlayerScores[plr->GetObjectGuid()] = sc;
}

/// <summary>
/// Removes the player.
/// </summary>
/// <param name="">The .</param>
/// <param name="">The .</param>
void BattleGroundAB::RemovePlayer(Player* /*plr*/, ObjectGuid /*guid*/)
{
    // No implementation needed for now
}

/// <summary>
/// Handles the area trigger.
/// </summary>
/// <param name="source">The source.</param>
/// <param name="trigger">The trigger.</param>
bool BattleGroundAB::HandleAreaTrigger(Player* source, uint32 trigger)
{
    switch (trigger)
    {
    case 3948: // Arathi Basin Alliance Exit
        if (source->GetTeam() != ALLIANCE)
        {
            source->GetSession()->SendNotification(LANG_BATTLEGROUND_ONLY_ALLIANCE_USE);
        }
        else
        {
            source->LeaveBattleground();
        }
        break;
    case 3949: // Arathi Basin Horde Exit
        if (source->GetTeam() != HORDE)
        {
            source->GetSession()->SendNotification(LANG_BATTLEGROUND_ONLY_HORDE_USE);
        }
        else
        {
            source->LeaveBattleground();
        }
        break;
    default:
        return false;
    }
    return true;
}

/// <summary>
/// Creates the banner.
/// </summary>
/// <param name="node">The node.</param>
/// <param name="type">The type. 0-neutral, 1-contested, 3-occupied</param>
/// <param name="teamIndex">Index of the team. 0-ally, 1-horde</param>
/// <param name="delay">The delay.</param>
void BattleGroundAB::_CreateBanner(uint8 node, uint8 type, uint8 teamIndex, bool delay)
{
    // Just put it into the queue
    if (delay)
    {
        m_BannerTimers[node].timer = 2000;
        m_BannerTimers[node].type = type;
        m_BannerTimers[node].teamIndex = teamIndex;
        return;
    }

    // Cause the node-type is in the generic form
    // Please see in the header file for the ids
    if (type != BG_AB_NODE_TYPE_NEUTRAL)
    {
        type += teamIndex;
    }

    // Spawn the event
    SpawnEvent(node, type, true); // Will automatically despawn other events
}

/// <summary>
/// _s the get node name id.
/// </summary>
/// <param name="node">The node.</param>
/// <returns></returns>
int32 BattleGroundAB::_GetNodeNameId(uint8 node)
{
    switch (node)
    {
    case BG_AB_NODE_STABLES:    return LANG_BG_AB_NODE_STABLES;
    case BG_AB_NODE_BLACKSMITH: return LANG_BG_AB_NODE_BLACKSMITH;
    case BG_AB_NODE_FARM:       return LANG_BG_AB_NODE_FARM;
    case BG_AB_NODE_LUMBER_MILL: return LANG_BG_AB_NODE_LUMBER_MILL;
    case BG_AB_NODE_GOLD_MINE:  return LANG_BG_AB_NODE_GOLD_MINE;
    default:
        MANGOS_ASSERT(0);
    }
    return 0;
}

/// <summary>
/// Fills the initial world states.
/// </summary>
/// <param name="data">The data.</param>
/// <param name="count">The count.</param>
void BattleGroundAB::FillInitialWorldStates(WorldPacket& data, uint32& count)
{
    const uint8 plusArray[] = { 0, 2, 3, 0, 1 };

    // Node icons
    for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
    {
        FillInitialWorldState(data, count, BG_AB_OP_NODEICONS[node], m_Nodes[node] == 0);
    }

    // Node occupied states
    for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
        for (uint8 i = 1; i < BG_AB_NODES_MAX; ++i)
        {
            FillInitialWorldState(data, count, BG_AB_OP_NODESTATES[node] + plusArray[i], m_Nodes[node] == i);
        }

    // How many bases each team owns
    uint8 ally = 0, horde = 0;
    for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
        if (m_Nodes[node] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
        {
            ++ally;
        }
        else if (m_Nodes[node] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
        {
            ++horde;
        }

    FillInitialWorldState(data, count, BG_AB_OP_OCCUPIED_BASES_ALLY, ally);
    FillInitialWorldState(data, count, BG_AB_OP_OCCUPIED_BASES_HORDE, horde);

    // Team scores
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_MAX, BG_AB_MAX_TEAM_SCORE);
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_WARNING, BG_AB_WARNING_NEAR_VICTORY_SCORE);
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_ALLY, m_TeamScores[TEAM_INDEX_ALLIANCE]);
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_HORDE, m_TeamScores[TEAM_INDEX_HORDE]);

    // Other unknown
    FillInitialWorldState(data, count, 0x745, 0x2); // 37 1861 unk
}

/// <summary>
/// _s the send node update.
/// </summary>
/// <param name="node">The node.</param>
void BattleGroundAB::_SendNodeUpdate(uint8 node)
{
    // Send node owner state update to refresh map icons on client
    const uint8 plusArray[] = { 0, 2, 3, 0, 1 };

    if (m_prevNodes[node])
    {
        UpdateWorldState(BG_AB_OP_NODESTATES[node] + plusArray[m_prevNodes[node]], WORLD_STATE_REMOVE);
    }
    else
    {
        UpdateWorldState(BG_AB_OP_NODEICONS[node], WORLD_STATE_REMOVE);
    }

    UpdateWorldState(BG_AB_OP_NODESTATES[node] + plusArray[m_Nodes[node]], WORLD_STATE_ADD);

    // How many bases each team owns
    uint8 ally = 0, horde = 0;
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
        if (m_Nodes[i] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
        {
            ++ally;
        }
        else if (m_Nodes[i] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
        {
            ++horde;
        }

    UpdateWorldState(BG_AB_OP_OCCUPIED_BASES_ALLY, ally);
    UpdateWorldState(BG_AB_OP_OCCUPIED_BASES_HORDE, horde);
}

/// <summary>
/// _s the node occupied.
/// </summary>
/// <param name="node">The node.</param>
/// <param name="team">The team.</param>
void BattleGroundAB::_NodeOccupied(uint8 node, Team team)
{
    uint8 capturedNodes = 0;
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        if (m_Nodes[node] == GetTeamIndexByTeamId(team) + BG_AB_NODE_TYPE_OCCUPIED && !m_NodeTimers[i])
        {
            ++capturedNodes;
        }
    }
    if (capturedNodes >= 5)
    {
        CastSpellOnTeam(SPELL_AB_QUEST_REWARD_5_BASES, team);
    }
    if (capturedNodes >= 4)
    {
        CastSpellOnTeam(SPELL_AB_QUEST_REWARD_4_BASES, team);
    }
}

/* Invoked if a player used a banner as a gameobject */
/// <summary>
/// Handles the event when a player clicks on a flag.
/// </summary>
/// <param name="source">The player who clicked the flag.</param>
/// <param name="target_obj">The flag game object.</param>
void BattleGroundAB::EventPlayerClickedOnFlag(Player* source, GameObject* target_obj)
{
    // Check if the battleground is in progress
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        return;
    }

    // Get the event associated with the game object
    uint8 event = (sBattleGroundMgr.GetGameObjectEventIndex(target_obj->GetGUIDLow())).event1;
    if (event >= BG_AB_NODES_MAX) // Not a valid node
    {
        return;
    }
    BG_AB_Nodes node = BG_AB_Nodes(event);

    // Get the team index of the player
    PvpTeamIndex teamIndex = GetTeamIndexByTeamId(source->GetTeam());

    // Check if the player can use this banner
    if (!(m_Nodes[node] == 0 || teamIndex == m_Nodes[node] % 2))
    {
        return;
    }

    // Remove auras that interrupt PvP combat
    source->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
    uint32 sound;

    // If the node is neutral, change it to contested
    if (m_Nodes[node] == BG_AB_NODE_TYPE_NEUTRAL)
    {
        UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);
        m_prevNodes[node] = m_Nodes[node];
        m_Nodes[node] = teamIndex + 1;
        // Create a new contested banner
        _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex, true);
        _SendNodeUpdate(node);
        m_NodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

        // Send message to all players
        if (teamIndex == 0)
        {
            SendMessage2ToAll(LANG_BG_AB_NODE_CLAIMED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node), LANG_BG_ALLY);
        }
        else
        {
            SendMessage2ToAll(LANG_BG_AB_NODE_CLAIMED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node), LANG_BG_HORDE);
        }

        sound = BG_AB_SOUND_NODE_CLAIMED;
    }
    // If the node is contested
    else if ((m_Nodes[node] == BG_AB_NODE_STATUS_ALLY_CONTESTED) || (m_Nodes[node] == BG_AB_NODE_STATUS_HORDE_CONTESTED))
    {
        // If the last state is NOT occupied, change the node to enemy-contested
        if (m_prevNodes[node] < BG_AB_NODE_TYPE_OCCUPIED)
        {
            UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);
            m_prevNodes[node] = m_Nodes[node];
            m_Nodes[node] = teamIndex + BG_AB_NODE_TYPE_CONTESTED;
            // Create a new contested banner
            _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex, true);
            _SendNodeUpdate(node);
            m_NodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

            // Send message to all players
            if (teamIndex == TEAM_INDEX_ALLIANCE)
            {
                SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node));
            }
            else
            {
                SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node));
            }
        }
        // If contested, change back to occupied
        else
        {
            UpdatePlayerScore(source, SCORE_BASES_DEFENDED, 1);
            m_prevNodes[node] = m_Nodes[node];
            m_Nodes[node] = teamIndex + BG_AB_NODE_TYPE_OCCUPIED;
            // Create a new occupied banner
            _CreateBanner(node, BG_AB_NODE_TYPE_OCCUPIED, teamIndex, true);
            _SendNodeUpdate(node);
            m_NodeTimers[node] = 0;
            _NodeOccupied(node, (teamIndex == TEAM_INDEX_ALLIANCE) ? ALLIANCE : HORDE);

            // Send message to all players
            if (teamIndex == TEAM_INDEX_ALLIANCE)
            {
                SendMessage2ToAll(LANG_BG_AB_NODE_DEFENDED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node));
            }
            else
            {
                SendMessage2ToAll(LANG_BG_AB_NODE_DEFENDED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node));
            }
        }
        sound = (teamIndex == TEAM_INDEX_ALLIANCE) ? BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE : BG_AB_SOUND_NODE_ASSAULTED_HORDE;
    }
    // If the node is occupied, change to enemy-contested
    else
    {
        UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);
        m_prevNodes[node] = m_Nodes[node];
        m_Nodes[node] = teamIndex + BG_AB_NODE_TYPE_CONTESTED;
        // Create a new contested banner
        _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex, true);
        _SendNodeUpdate(node);
        m_NodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

        // Send message to all players
        if (teamIndex == TEAM_INDEX_ALLIANCE)
        {
            SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node));
        }
        else
        {
            SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node));
        }

        sound = (teamIndex == TEAM_INDEX_ALLIANCE) ? BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE : BG_AB_SOUND_NODE_ASSAULTED_HORDE;
    }

    // If the node is occupied again, send "X has taken the Y" message
    if (m_Nodes[node] >= BG_AB_NODE_TYPE_OCCUPIED)
    {
        if (teamIndex == TEAM_INDEX_ALLIANCE)
        {
            SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_ALLIANCE, NULL, LANG_BG_ALLY, _GetNodeNameId(node));
        }
        else
        {
            SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_HORDE, NULL, LANG_BG_HORDE, _GetNodeNameId(node));
        }
    }
    PlaySoundToAll(sound);
}

/// <summary>
/// Resets the battleground state.
/// </summary>
void BattleGroundAB::Reset()
{
    // Call parent class reset
    BattleGround::Reset();

    // Reset team scores and related variables
    for (uint8 i = 0; i < PVP_TEAM_COUNT; ++i)
    {
        m_TeamScores[i] = 0;
        m_lastTick[i] = 0;
        m_honorScoreTicks[i] = 0;
        m_ReputationScoreTics[i] = 0;
    }

    m_IsInformedNearVictory = false;
    bool isBGWeekend = BattleGroundMgr::IsBGWeekend(GetTypeID());
    m_honorTicks = isBGWeekend ? AB_WEEKEND_HONOR_INTERVAL : AB_NORMAL_HONOR_INTERVAL;
    m_ReputationTics = isBGWeekend ? AB_WEEKEND_REPUTATION_INTERVAL : AB_NORMAL_REPUTATION_INTERVAL;

    // Reset node-related variables
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        m_Nodes[i] = 0;
        m_prevNodes[i] = 0;
        m_NodeTimers[i] = 0;
        m_BannerTimers[i].timer = 0;

        // All nodes owned by neutral team at the beginning
        m_ActiveEvents[i] = BG_AB_NODE_TYPE_NEUTRAL;
    }
}

/// <summary>
/// Ends the battleground.
/// </summary>
/// <param name="winner">The winning team.</param>
void BattleGroundAB::EndBattleGround(Team winner)
{
    // Reward honor to the winning team
    if (winner == ALLIANCE)
    {
        RewardHonorToTeam(BG_AB_WinMatchHonor[GetBracketId()], ALLIANCE);
    }
    if (winner == HORDE)
    {
        RewardHonorToTeam(BG_AB_WinMatchHonor[GetBracketId()], HORDE);
    }

    // Call parent class EndBattleGround
    BattleGround::EndBattleGround(winner);
}

/// <summary>
/// Gets the closest graveyard for a player.
/// </summary>
/// <param name="player">The player.</param>
/// <returns>The closest graveyard entry.</returns>
WorldSafeLocsEntry const* BattleGroundAB::GetClosestGraveYard(Player* player)
{
    // Get the team index of the player
    PvpTeamIndex teamIndex = GetTeamIndexByTeamId(player->GetTeam());

    // Check if there are any occupied nodes for this team
    std::vector<uint8> nodes;
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
        if (m_Nodes[i] == teamIndex + 3)
        {
            nodes.push_back(i);
        }

    WorldSafeLocsEntry const* good_entry = NULL;
    // If there are occupied nodes, select the closest one to place the ghost
    if (!nodes.empty())
    {
        float plr_x = player->GetPositionX();
        float plr_y = player->GetPositionY();

        float mindist = 999999.0f;
        for (uint8 i = 0; i < nodes.size(); ++i)
        {
            WorldSafeLocsEntry const* entry = sWorldSafeLocsStore.LookupEntry(BG_AB_GraveyardIds[nodes[i]]);
            if (!entry)
            {
                continue;
            }
            float dist = (entry->x - plr_x) * (entry->x - plr_x) + (entry->y - plr_y) * (entry->y - plr_y);
            if (mindist > dist)
            {
                mindist = dist;
                good_entry = entry;
            }
        }
        nodes.clear();
    }
    // If no occupied nodes, place the ghost at the starting location
    if (!good_entry)
    {
        good_entry = sWorldSafeLocsStore.LookupEntry(BG_AB_GraveyardIds[teamIndex + 5]);
    }

    return good_entry;
}

/// <summary>
/// Updates the player score.
/// </summary>
/// <param name="source">The player.</param>
/// <param name="type">The type of score to update.</param>
/// <param name="value">The value to add to the score.</param>
void BattleGroundAB::UpdatePlayerScore(Player* source, uint32 type, uint32 value)
{
    // Find the player's score entry
    BattleGroundScoreMap::iterator itr = m_PlayerScores.find(source->GetObjectGuid());
    if (itr == m_PlayerScores.end()) // Player not found
    {
        return;
    }

    // Update the appropriate score type
    switch (type)
    {
    case SCORE_BASES_ASSAULTED:
        ((BattleGroundABScore*)itr->second)->BasesAssaulted += value;
        break;
    case SCORE_BASES_DEFENDED:
        ((BattleGroundABScore*)itr->second)->BasesDefended += value;
        break;
    default:
        BattleGround::UpdatePlayerScore(source, type, value);
        break;
    }
}

/// <summary>
/// Gets the premature finish winning team.
/// </summary>
/// <returns>The winning team.</returns>
Team BattleGroundAB::GetPrematureWinner()
{
    // Compare the scores of both teams
    int32 hordeScore = m_TeamScores[TEAM_INDEX_HORDE];
    int32 allianceScore = m_TeamScores[TEAM_INDEX_ALLIANCE];

    if (hordeScore > allianceScore)
    {
        return HORDE;
    }

    if (allianceScore > hordeScore)
    {
        return ALLIANCE;
    }

    // If the scores are equal, fall back to the number of players on each team
    return BattleGround::GetPrematureWinner();
}
