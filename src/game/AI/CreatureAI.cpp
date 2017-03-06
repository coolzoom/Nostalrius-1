/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
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
 */

#include "CreatureAI.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Spell.h"

CreatureAI::~CreatureAI()
{
}

void CreatureAI::AttackedBy(Unit* attacker)
{
    if (!m_creature->getVictim())
        AttackStart(attacker);
}

CanCastResult CreatureAI::CanCastSpell(Unit* pTarget, const SpellEntry *pSpell, bool isTriggered)
{
    if (!pTarget)
        return CAST_FAIL_OTHER;
    // If not triggered, we check
    if (!isTriggered)
    {
        // State does not allow
        if (m_creature->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
            return CAST_FAIL_STATE;

        if (pSpell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE && (m_creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED) || m_creature->IsSpellProhibited(pSpell)))
            return CAST_FAIL_STATE;

        if (pSpell->PreventionType == SPELL_PREVENTION_TYPE_PACIFY && m_creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED))
            return CAST_FAIL_STATE;

        // Check for power (also done by Spell::CheckCast())
        if (m_creature->GetPower((Powers)pSpell->powerType) < Spell::CalculatePowerCost(pSpell, m_creature))
            return CAST_FAIL_POWER;
    }

    if (pSpell->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY)
        return CAST_OK;

    if (const SpellRangeEntry *pSpellRange = sSpellRangeStore.LookupEntry(pSpell->rangeIndex))
    {
        if (pTarget != m_creature)
        {
            // pTarget is out of range of this spell (also done by Spell::CheckCast())
            float fDistance = m_creature->GetCombatDistance(pTarget);

            if (fDistance > pSpellRange->maxRange)
                return CAST_FAIL_TOO_FAR;

            float fMinRange = pSpellRange->minRange;

            if (fMinRange && fDistance < fMinRange)
                return CAST_FAIL_TOO_CLOSE;
        }

        return CAST_OK;
    }
    else
        return CAST_FAIL_OTHER;
}

CanCastResult CreatureAI::DoCastSpellIfCan(Unit* pTarget, uint32 uiSpell, uint32 uiCastFlags, ObjectGuid uiOriginalCasterGUID)
{
    if (!pTarget)
        return CAST_FAIL_OTHER;

    Unit* pCaster = m_creature;

    if (uiCastFlags & CAST_FORCE_TARGET_SELF)
        pCaster = pTarget;

    // Allowed to cast only if not casting (unless we interrupt ourself) or if spell is triggered
    if (!pCaster->IsNonMeleeSpellCasted(false) || (uiCastFlags & (CAST_TRIGGERED | CAST_INTERRUPT_PREVIOUS)))
    {
        if (const SpellEntry* pSpell = sSpellMgr.GetSpellEntry(uiSpell))
        {
            // If cast flag CAST_AURA_NOT_PRESENT is active, check if target already has aura on them
            if (uiCastFlags & CAST_AURA_NOT_PRESENT)
            {
                if (pTarget->HasAura(uiSpell))
                    return CAST_FAIL_TARGET_AURA;
            }

            // Check if cannot cast spell
            if (!(uiCastFlags & (CAST_FORCE_TARGET_SELF | CAST_FORCE_CAST)))
            {
                CanCastResult castResult = CanCastSpell(pTarget, pSpell, uiCastFlags & CAST_TRIGGERED);

                if (castResult != CAST_OK)
                    return castResult;
            }

            // Interrupt any previous spell
            if ((uiCastFlags & CAST_INTERRUPT_PREVIOUS) && pCaster->IsNonMeleeSpellCasted(false))
                pCaster->InterruptNonMeleeSpells(false);

            pCaster->CastSpell(pTarget, pSpell, uiCastFlags & CAST_TRIGGERED, nullptr, nullptr, uiOriginalCasterGUID);
            return CAST_OK;
        }
        else
        {
            sLog.outErrorDb("DoCastSpellIfCan by creature entry %u attempt to cast spell %u but spell does not exist.", m_creature->GetEntry(), uiSpell);
            return CAST_FAIL_OTHER;
        }
    }
    else
        return CAST_FAIL_IS_CASTING;
}

void CreatureAI::SetGazeOn(Unit *target)
{
    if (m_creature->canAttack(target))
    {
        AttackStart(target);
        m_creature->SetReactState(REACT_PASSIVE);
    }
}

bool CreatureAI::UpdateVictimWithGaze()
{
    if (!m_creature->isInCombat())
        return false;

    if (m_creature->HasReactState(REACT_PASSIVE))
    {
        if (m_creature->getVictim())
            return true;
        else
            m_creature->SetReactState(REACT_AGGRESSIVE);
    }

    if (m_creature->SelectHostileTarget())
        if (Unit *victim = m_creature->getVictim())
            AttackStart(victim);
    return m_creature->getVictim();
}

bool CreatureAI::UpdateCombatState()
{
    if (!m_creature->isInCombat())
        return false;

    if (!m_creature->HasReactState(REACT_PASSIVE))
    {
        if (m_creature->SelectHostileTarget())
            if (Unit *victim = m_creature->getVictim())
                AttackStart(victim);
        return m_creature->getVictim();
    }

    if (m_creature->getThreatManager().isThreatListEmpty())
    {
        EnterEvadeMode();
        return false;
    }

    return true;
}

bool CreatureAI::UpdateVictim()
{
    if (!m_creature->isInCombat())
        return false;

    if (!m_creature->HasReactState(REACT_PASSIVE))
    {
        if (m_creature->SelectHostileTarget())
            if (Unit *victim = m_creature->getVictim())
                AttackStart(victim);
        return m_creature->getVictim();
    }

    if (m_creature->getThreatManager().isThreatListEmpty())
    {
        EnterEvadeMode();
        return false;
    }

    return true;
}


void CreatureAI::DoCast(Unit* victim, uint32 spellId, bool triggered)
{
    if (!victim || (m_creature->IsNonMeleeSpellCasted(false) && !triggered))
        return;

    m_creature->CastSpell(victim, spellId, triggered);
}

void CreatureAI::DoCastVictim(uint32 spellId, bool triggered)
{
    m_creature->CastSpell(m_creature->getVictim(), spellId, triggered);
}

void CreatureAI::DoCastAOE(uint32 spellId, bool triggered)
{
    if (!triggered && m_creature->IsNonMeleeSpellCasted(false))
        return;

    m_creature->CastSpell((Unit*)nullptr, spellId, triggered);
}

bool CreatureAI::DoMeleeAttackIfReady()
{
    return m_creature->UpdateMeleeAttackingState();
}

void CreatureAI::EnterEvadeMode()
{
    if (!m_creature->isAlive())
    {
        DEBUG_FILTER_LOG(LOG_FILTER_AI_AND_MOVEGENSS, "Creature stopped attacking, he is dead [guid=%u]", m_creature->GetGUIDLow());
        m_creature->CombatStop(true);
        m_creature->DeleteThreatList();
        return;
    }

    if (!m_creature->isCharmed())
    {
        m_creature->RemoveAurasAtReset();

        // Remove ChaseMovementGenerator from MotionMaster stack list, and add HomeMovementGenerator instead
        if (m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            m_creature->GetMotionMaster()->MoveTargetedHome();
    }

    m_creature->DeleteThreatList();
    m_creature->CombatStop(true);
    m_creature->SetLootRecipient(nullptr);
}

// Distract creature, if player gets too close while stealthed/prowling
void CreatureAI::TriggerAlert(Unit const* who)
{
    // If there's no target, or target isn't a player do nothing
    if (!who || who->GetTypeId() != TYPEID_PLAYER)
        return;

    // If this unit isn't an NPC, is already distracted, is in combat, is confused, stunned or fleeing, do nothing
    if (m_creature->GetTypeId() != TYPEID_UNIT || m_creature->isInCombat() || m_creature->hasUnitState(UNIT_STAT_NO_FREE_MOVE))
        return;

    // Only alert for hostiles!
    if (m_creature->IsCivilian() || m_creature->HasReactState(REACT_PASSIVE) || !m_creature->IsValidAttackTarget(who))
        return;

    // 10 sec cooldown for stealth warning
    if (WorldTimer::getMSTimeDiffToNow(m_uLastAlertTime) < 10000)
        return;

    // Send alert sound (if any) for this creature
    m_creature->SendAIReaction(AI_REACTION_ALERT);

    // Face the unit (stealthed player) and set distracted state for 5 seconds
    m_creature->StopMoving();
    m_creature->SetFacingTo(m_creature->GetAngle(who->GetPositionX(), who->GetPositionY()));
    m_creature->GetMotionMaster()->MoveDistract(5 * IN_MILLISECONDS);
    m_uLastAlertTime = WorldTimer::getMSTime();
}
