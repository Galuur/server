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

#include "ReactorAI.h"
#include "Creature.h"
#include "Map.h"
#include "Log.h"

#define REACTOR_VISIBLE_RANGE (26.46f)

int
ReactorAI::Permissible(const Creature* creature)
{
    if ((creature->GetCreatureInfo()->ExtraFlags & CREATURE_FLAG_EXTRA_NO_AGGRO) || creature->IsNeutralToAll())
    {
        return PERMIT_BASE_REACTIVE;
    }

    return PERMIT_BASE_NO;
}

void
ReactorAI::MoveInLineOfSight(Unit*)
{
}

void
ReactorAI::AttackStart(Unit* p)
{
    if (!p)
    {
        return;
    }

    if (m_creature->Attack(p, true))
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Tag unit GUID: %u (TypeId: %u) as a victim", p->GetGUIDLow(), p->GetTypeId());
        i_victimGuid = p->GetObjectGuid();
        m_creature->AddThreat(p);

        m_creature->SetInCombatWith(p);
        p->SetInCombatWith(m_creature);

        HandleMovementOnAttackStart(p);
    }
}

bool
ReactorAI::IsVisible(Unit*) const
{
    return false;
}

void
ReactorAI::UpdateAI(const uint32 diff)
{
    // update i_victimGuid if i_creature.getVictim() !=0 and changed
    if (!m_creature->SelectHostileTarget() || !m_creature->getVictim())
    {
        return;
    }

    i_victimGuid = m_creature->getVictim()->GetObjectGuid();

    if (!m_CreatureSpells.empty())
    {
        UpdateSpellsList(diff);
    }

    DoMeleeAttackIfReady();
}

void
ReactorAI::EnterEvadeMode()
{
    if (!m_creature->IsAlive())
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature stopped attacking, he is dead [guid=%u]", m_creature->GetGUIDLow());
        m_creature->GetMotionMaster()->MovementExpired();
        m_creature->GetMotionMaster()->MoveIdle();
        i_victimGuid.Clear();
        m_creature->CombatStop(true);
        m_creature->DeleteThreatList();
        return;
    }

    Unit* victim = m_creature->GetMap()->GetUnit(i_victimGuid);

    if (!victim)
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature stopped attacking, no victim [guid=%u]", m_creature->GetGUIDLow());
    }
    else if (victim->HasStealthAura())
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature stopped attacking, victim is in stealth [guid=%u]", m_creature->GetGUIDLow());
    }
    else if (victim->IsTaxiFlying())
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature stopped attacking, victim is in flight [guid=%u]", m_creature->GetGUIDLow());
    }
    else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature stopped attacking, victim %s [guid=%u]", victim->IsAlive() ? "out run him" : "is dead", m_creature->GetGUIDLow());
    }

    m_creature->RemoveAllAurasOnEvade();
    m_creature->DeleteThreatList();
    i_victimGuid.Clear();
    m_creature->CombatStop(true);
    m_creature->SetLootRecipient(NULL);

    // Remove ChaseMovementGenerator from MotionMaster stack list, and add HomeMovementGenerator instead
    if (m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
    {
        m_creature->GetMotionMaster()->MoveTargetedHome();
    }

    // Reset back to default spells template. This also resets timers.
    SetSpellsList(m_creature->GetCreatureInfo()->SpellListId);
}
