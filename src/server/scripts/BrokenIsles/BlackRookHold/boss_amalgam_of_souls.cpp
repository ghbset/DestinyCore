/*
 * Copyright (C) 2017-2018 AshamaneProject <https://github.com/AshamaneProject>
 * Improved with mechanics from TrinityCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AreaTrigger.h"
#include "AreaTriggerAI.h"
#include "black_rook_hold.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellScript.h"

enum AmalgamOfSoulsSpells
{
    SPELL_SWIRLING_SCYTHE           = 195254,
    SPELL_SWIRLING_SCYTHE_DAMAGE    = 196517,
    SPELL_REAP_SOUL                 = 194956,
    SPELL_SOUL_ECHOES               = 194966,
    SPELL_SOUL_ECHOES_CLONE_CASTER  = 194981,
    SPELL_SOUL_ECHOES_DAMAGE        = 194960,
    SPELL_SOULGORGE                 = 196930,
    SPELL_CALL_SOULS                = 196078,
    SPELL_CALL_SOULS_VISUAL         = 196925,
    SPELL_SOUL_BURST                = 196587,

    // Outro
    SPELL_SUMMON_MINIBOSS_A         = 196619,
    SPELL_SUMMON_MINIBOSS_B         = 196620,
    SPELL_SUMMON_MINIBOSS_C         = 196646,
};

enum AmalgamOfSoulsEvents
{
    EVENT_SWIRLING_SCYTHE = 1,
    EVENT_REAP_SOUL,
    EVENT_SOUL_ECHOES,
    EVENT_CHECK_ALIVE_SOULS,
    EVENT_SOUL_BURST,
    EVENT_CALL_SOULS,
};

enum AmalgamOfSoulsTexts
{
    SAY_AGGRO           = 0,
    SAY_SWIRLING_SCYTHE = 1,
    SAY_SOUL_ECHOES     = 2,
    SAY_REAP_SOUL       = 3,
    SAY_CALL_SOULS      = 4,
    SAY_SOUL_BURST      = 5,
};

enum AmalgamOfSoulsMisc
{
    POINT_START_CALL_SOULS    = 0,
    POINT_RESTLESS_SOUL       = 0,
    POINT_AMALGAM_OF_SOULS    = 1,

    NPC_SOUL_ECHO             = 99090,

    ACTION_SOUL_KILLED        = 1,
};

static const Position AmalgamOfSoulsPosition = { 3252.25f, 7581.75f, 12.884051f };

// 98542 - Amalgam of Souls
struct boss_amalgam_of_souls : public BossAI
{
    boss_amalgam_of_souls(Creature* creature) : BossAI(creature, DATA_AMALGAM_OF_SOULS), _callSoulsTriggered(false) { }

    void DespawnSoulEchoes() const
    {
        std::list<Creature*> soulEchoes;
        GetCreatureListWithEntryInGrid(soulEchoes, me, NPC_SOUL_ECHO, 100.0f);

        for (Creature* soulEcho : soulEchoes)
            soulEcho->DespawnOrUnsummon();
    }

    void Reset() override
    {
        _Reset();
        _callSoulsTriggered = false;
        restlessSoulsCount = 0;
    }

    void EnterCombat(Unit* who) override
    {
        BossAI::EnterCombat(who);
        Talk(SAY_AGGRO);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me, 1);

        events.ScheduleEvent(EVENT_SWIRLING_SCYTHE, 8100ms);
        events.ScheduleEvent(EVENT_SOUL_ECHOES, 16900ms);
        events.ScheduleEvent(EVENT_REAP_SOUL, 20200ms);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);

        summons.DespawnAll();
        DespawnSoulEchoes();
        _EnterEvadeMode();
        _DespawnAtEvade();
    }

    void MovementInform(uint32 /*type*/, uint32 id) override
    {
        if (id == POINT_START_CALL_SOULS)
        {
            Talk(SAY_CALL_SOULS);
            DoCastSelf(SPELL_CALL_SOULS);
            DoCastSelf(SPELL_CALL_SOULS_VISUAL, TRIGGERED_FULL_MASK);
            events.ScheduleEvent(EVENT_CHECK_ALIVE_SOULS, 1s);
        }
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage) override
    {
        if (!IsHeroic() || _callSoulsTriggered || !me->HealthBelowPctDamaged(50, damage))
            return;

        _callSoulsTriggered = true;

        DoStopAttack();
        me->SetReactState(REACT_PASSIVE);
        events.CancelEvent(EVENT_SWIRLING_SCYTHE);
        events.CancelEvent(EVENT_SOUL_ECHOES);
        events.CancelEvent(EVENT_REAP_SOUL);

        me->GetMotionMaster()->MovePoint(POINT_START_CALL_SOULS, me->GetHomePosition(), false);
    }

    void DoAction(int32 action) override
    {
        if (action != ACTION_SOUL_KILLED)
            return;

        if (--restlessSoulsCount == 0)
            me->RemoveAurasDueToSpell(SPELL_CALL_SOULS_VISUAL);
    }

    void JustDied(Unit* killer) override
    {
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        _JustDied();
        DespawnSoulEchoes();

        DoCastSelf(SPELL_SUMMON_MINIBOSS_A);
        DoCastSelf(SPELL_SUMMON_MINIBOSS_B);
        DoCastSelf(SPELL_SUMMON_MINIBOSS_C);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SWIRLING_SCYTHE:
                {
                    Talk(SAY_SWIRLING_SCYTHE);
                    DoCastVictim(SPELL_SWIRLING_SCYTHE);
                    events.Repeat(20600ms);
                    break;
                }
                case EVENT_REAP_SOUL:
                {
                    Talk(SAY_REAP_SOUL);
                    DoCastVictim(SPELL_REAP_SOUL);
                    events.Repeat(13400ms);
                    break;
                }
                case EVENT_SOUL_ECHOES:
                {
                    Talk(SAY_SOUL_ECHOES);
                    if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0))
                        DoCast(target, SPELL_SOUL_ECHOES);
                    events.Repeat(26700ms);
                    break;
                }
                case EVENT_CHECK_ALIVE_SOULS:
                {
                    if (me->FindNearestCreature(NPC_RESTLESS_SOUL, 100.0f))
                        events.Repeat(500ms);
                    else
                        events.ScheduleEvent(EVENT_SOUL_BURST, 5s);
                    break;
                }
                case EVENT_SOUL_BURST:
                {
                    Talk(SAY_SOUL_BURST);
                    me->RemoveAurasDueToSpell(SPELL_CALL_SOULS_VISUAL);
                    DoCastSelf(SPELL_SOUL_BURST);
                    me->SetReactState(REACT_AGGRESSIVE);
                    me->RemoveAurasDueToSpell(SPELL_SOULGORGE);

                    events.ScheduleEvent(EVENT_SWIRLING_SCYTHE, 14200ms);
                    events.ScheduleEvent(EVENT_SOUL_ECHOES, 23s);
                    events.ScheduleEvent(EVENT_REAP_SOUL, 26400ms);
                    break;
                }
                case EVENT_CALL_SOULS:
                {
                    DoCastAOE(SPELL_CALL_SOULS_VISUAL);

                    restlessSoulsCount = 7;
                    me->GetScheduler().Schedule(1s, 2s, [this](TaskContext context)
                    {
                        Position pos;
                        GetRandPosFromCenterInDist(me, 30.f, pos);
                        pos.m_positionZ = 20.0f;

                        me->SummonCreature(NPC_RESTLESS_SOUL, pos);

                        if (context.GetRepeatCounter() <= 6)
                            context.Repeat(1s, 2s);
                    });
                    break;
                }
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
    }

private:
    bool _callSoulsTriggered;
    uint8 restlessSoulsCount;
};

// 99090 - Soul Echoes Stalker
struct npc_aos_soul_echo : public ScriptedAI
{
    npc_aos_soul_echo(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        me->SetReactState(REACT_PASSIVE);
        me->setFaction(16);
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_IMMUNE_TO_PC);
        summoner->CastSpell(me, SPELL_SOUL_ECHOES_CLONE_CASTER, true);

        me->GetScheduler().Schedule(5s, [](TaskContext context)
        {
            GetContextUnit()->CastSpell(nullptr, SPELL_SOUL_ECHOES_DAMAGE, false);
            GetContextCreature()->DespawnOrUnsummon();
        });
    }
};

// 99664 - Restless Soul
struct npc_aos_restless_soul : public ScriptedAI
{
    npc_aos_restless_soul(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        me->SetSpeed(MOVE_FLIGHT, 1.5f);
        me->SetSpeed(MOVE_RUN,    1.5f);

        me->SetReactState(REACT_PASSIVE);
        me->GetMotionMaster()->MovePoint(POINT_RESTLESS_SOUL, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() + frand(10.0f, 12.0f), false);
    }

    void EnterCombat(Unit* /*who*/) override { }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        if (id == POINT_RESTLESS_SOUL)
            me->GetMotionMaster()->MovePoint(POINT_AMALGAM_OF_SOULS, AmalgamOfSoulsPosition);
        else if (id == POINT_AMALGAM_OF_SOULS)
        {
            me->DespawnOrUnsummon();
            me->CastSpell(nullptr, SPELL_SOULGORGE, true);

            if (CreatureAI* ai = GetSummonerAI())
                ai->DoAction(ACTION_SOUL_KILLED);
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (CreatureAI* ai = GetSummonerAI())
            ai->DoAction(ACTION_SOUL_KILLED);
    }

private:
    CreatureAI* GetSummonerAI()
    {
        if (TempSummon* meTempSummon = me->ToTempSummon())
            if (Unit* summoner = meTempSummon->GetSummoner())
                if (summoner->IsCreature() && summoner->IsAIEnabled)
                    return summoner->ToCreature()->AI();

        return nullptr;
    }
};

//AT : 9899
//Spell : 195254
struct at_aos_swirling_scythe : AreaTriggerAI
{
    at_aos_swirling_scythe(AreaTrigger* areatrigger) : AreaTriggerAI(areatrigger) { }

    void OnUnitEnter(Unit* unit) override
    {
        if (unit->IsPlayer())
            unit->CastSpell(unit, SPELL_SWIRLING_SCYTHE_DAMAGE, true);
    }
};

void AddSC_boss_amalgam_of_souls()
{
    RegisterCreatureAI(boss_amalgam_of_souls);
    RegisterCreatureAI(npc_aos_soul_echo);
    RegisterCreatureAI(npc_aos_restless_soul);

    RegisterAreaTriggerAI(at_aos_swirling_scythe);
}
