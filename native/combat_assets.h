#pragma once
#include "combat_rules.h"
#include "combat_collision.h"
#include "delayed_hits.h"
#include <nlohmann/json.hpp>
#include <set>

namespace LS {
struct CombatAssets {CombatRuleSet rules;std::map<int,std::vector<CollisionWindow>> windows,delayedWindows;std::map<int,std::vector<DelayedHitRule>> delayedRules;};
inline CombatAssets ReadCombatAssets(const nlohmann::json& document,const std::string& mode,const std::map<int,Clip>& clips){
 using Json=nlohmann::json;
 if(document.at("schema_version")!=1||document.at("mode")!=mode||!document.at("motions").is_object()||
    document["motions"].empty()||document["motions"].size()>4096||!document.at("authored_windows").is_object())throw std::runtime_error("Invalid combat manifest");
 CombatAssets result;auto& rules=result.rules;const auto& p=document.at("parameters");
 const auto number=[&](const char* name){return p.at(name).get<float>();};
 rules.staminaMax=number("stamina_max");rules.staminaRegen=number("stamina_regen");rules.chargeDrain=number("charge_drain");rules.guardHitCost=number("guard_hit_cost");
 rules.bowShotCost=number("bow_shot_cost");rules.bowDodgeCost=number("bow_dodge_cost");rules.gaugeDecay=number("gauge_decay");rules.gaugeAutoGain=number("gauge_auto_gain");
 rules.gaugeAutoSeconds=number("gauge_auto_seconds");rules.ammoCapacity=p.at("ammo_capacity").get<int>();rules.bowChainSeconds=number("bow_chain_seconds");
 rules.numericScope=document.at("numeric_scope").get<std::string>();
 if(document.contains("bow_charge_seconds"))rules.bowChargeSeconds=document.at("bow_charge_seconds").get<std::vector<float>>();
 rules.helmFollowupSlot=document.at("helm_followup").at("slot").get<int>();rules.helmFollowupSeconds=document["helm_followup"].at("start_seconds").get<float>();
 if(mode=="longsword"){
  const auto& native=document.at("source_parameters");
  rules.foresightMinimumGauge=native.at("foresight_minimum_gauge").get<float>();rules.specialSheatheSeconds=native.at("special_sheathe_seconds").get<float>();
  rules.specialSheatheReadySeconds=native.at("special_sheathe_ready_seconds").get<float>();rules.gaugeLevelCost=native.at("gauge_level_cost").get<int>();
  rules.levelDurations=native.at("level_durations").get<std::array<float,4>>();rules.helmGaugeSeconds=native.at("helm_gauge_seconds").get<float>();
  rules.redHelmGaugeSeconds=native.at("red_helm_gauge_seconds").get<float>();rules.iaiGaugeSeconds=native.at("iai_gauge_seconds").get<float>();
  rules.iaiTimedGaugeSeconds=native.at("iai_timed_gauge_seconds").get<float>();rules.gaugeUpgradeNoDecaySeconds=native.at("gauge_upgrade_no_decay_seconds").get<float>();
  rules.levelDamageMultipliers=native.at("level_damage_multipliers").get<std::array<float,4>>();
 }
 const auto slotKey=[&](const std::string& value){size_t count=0;const int slot=std::stoi(value,&count);if(count!=value.size()||!clips.contains(slot))throw std::runtime_error("Unknown combat slot");return slot;};
 for(auto it=document["motions"].begin();it!=document["motions"].end();++it){
  const int slot=slotKey(it.key());const auto& row=it.value();MotionCombatRule rule;
  const auto readWindows=[&](const char* field,std::vector<CombatTimeWindow>& windows){const auto& rows=row.at(field);if(!rows.is_array()||rows.size()>128)throw std::runtime_error("Invalid combat windows");
   for(const auto& w:rows){if(!w.is_array()||w.size()!=2)throw std::runtime_error("Invalid combat time window");CombatTimeWindow window{w.at(0).get<float>(),w.at(1).get<float>()};
    if(!window.Valid()||window.end>clips.at(slot).duration()+1e-4f)throw std::runtime_error("Combat window exceeds source clip");windows.push_back(window);}};
  readWindows("outgoing",rule.outgoing);readWindows("defensive",rule.defensive);
  rule.spiritBlade=row.value("spirit_blade",false);rule.roundslash=row.value("roundslash",false);rule.foresight=row.value("foresight",false);rule.iai=row.value("iai",false);rule.iaiSpirit=row.value("iai_spirit",false);
  rule.helmThrust=row.value("helm_thrust",false);rule.helmFinish=row.value("helm_finish",false);rule.guard=row.value("guard",false);rule.lanceCounter=row.value("lance_counter",false);rule.tackle=row.value("tackle",false);
  rule.reload=row.value("reload",false);rule.continuousReload=row.value("continuous_reload",false);rule.projectile=row.value("projectile",false);
  rule.gaugeGain=row.at("gauge_gain").get<float>();rule.gaugeCost=row.at("gauge_cost").get<float>();rule.reloadAt=row.at("reload_at").get<float>();
  rule.gaugeGainByWindow=row.at("gauge_gain_by_window").get<std::vector<float>>();
  rule.motionValueByWindow=row.at("motion_value_by_window").get<std::vector<float>>();
  if(row.contains("motion_value_by_level_by_window"))rule.motionValueByLevelByWindow=row.at("motion_value_by_level_by_window").get<std::vector<std::array<float,4>>>();
  rule.counterShortSlot=row.value("counter_short_slot",0);rule.counterFullSlot=row.value("counter_full_slot",0);rule.counterFullSeconds=row.value("counter_full_seconds",0.f);
  rule.completionFollowupSlot=row.value("completion_followup_slot",0);
  rule.hitFollowupSlot=row.value("hit_followup_slot",0);
  rule.trueCharge=row.value("true_charge",false);rule.trueChargeFirstWindow=row.value("true_charge_first_window",size_t(0));rule.trueChargeFinalWindow=row.value("true_charge_final_window",size_t(1));
  rule.weakFinishMultiplier=row.value("weak_finish_multiplier",1.f);rule.weakFinishScope=row.value("weak_finish_scope",std::string("unknown"));
  for(const char* target:{"counter_short_slot","counter_full_slot","completion_followup_slot","hit_followup_slot"})if(row.value(target,0)&&!clips.contains(row.at(target).get<int>()))throw std::runtime_error("Unknown source combat followup");
  if(row.contains("timed_inputs"))for(const auto& branch:row.at("timed_inputs")){
   TimedInputBranch input;input.command=branch.at("command").get<std::string>();input.window={branch.at("begin").get<float>(),branch.at("end").get<float>()};
   input.timedSlot=branch.at("timed_slot").get<int>();input.normalSlot=branch.at("normal_slot").get<int>();
   input.inputGateBegin=branch.at("input_gate_begin").get<float>();input.inputGateEnd=branch.at("input_gate_end").get<float>();
   if(!clips.contains(input.timedSlot)||!clips.contains(input.normalSlot)||input.window.end>clips.at(slot).duration()+1e-4f||input.inputGateEnd>clips.at(slot).duration()+1e-4f)throw std::runtime_error("Invalid source timed input");
   rule.timedInputs.push_back(std::move(input));
  }
  if(row.contains("projectile_tiers"))for(const auto& tier:row.at("projectile_tiers")){
   ProjectileTier projectile;projectile.arrows=tier.at("arrows").get<int>();projectile.shellId=tier.at("shell_id").get<int>();
   projectile.motionValue=tier.at("motion_value").get<float>();projectile.speed=tier.at("speed").get<float>();projectile.range=tier.at("range").get<float>();projectile.radius=tier.at("radius").get<float>();rule.projectileTiers.push_back(projectile);
  }
  if(row.contains("projectile_markers")){rule.projectileMarkers=row.at("projectile_markers").get<std::vector<float>>();for(float marker:rule.projectileMarkers)if(marker>clips.at(slot).duration()+1e-4f)throw std::runtime_error("Projectile marker exceeds source clip");}
  rule.hitTimingScope=row.at("hit_timing_scope").get<std::string>();rule.defensiveTimingScope=row.at("defensive_timing_scope").get<std::string>();
  if(rule.reloadAt>clips.at(slot).duration()+1e-4f)throw std::runtime_error("Invalid reload marker");rules.motions.emplace(slot,std::move(rule));
 }
 if(mode=="longsword"){
  const auto& delay=document.at("delayed_hit");const auto& shell=delay.at("shell_geometry");
  const float first=delay.at("first_delay").get<float>(),between=delay.at("between_delay").get<float>(),last=delay.at("last_delay").get<float>();
  const size_t count=delay.at("hit_limit").get<size_t>();
  if(!std::isfinite(first)||first<0||!std::isfinite(between)||between<=0||!std::isfinite(last)||last<=0||count<1||count>32)throw std::runtime_error("Invalid source delayed timing");
  for(auto it=delay.at("triggers").begin();it!=delay["triggers"].end();++it){const int slot=slotKey(it.key());const auto* motion=rules.Find(slot);
   if(!motion||!motion->helmFinish||!it.value().is_array()||it.value().size()>16)throw std::runtime_error("Invalid source delayed trigger");
   for(const auto& row:it.value()){
    DelayedHitRule rule;rule.triggerWindow=row.at("window_id").get<uint32_t>();rule.begin=row.at("begin").get<float>();rule.end=row.at("end").get<float>();
    rule.pulseWindows=row.at("pulse_window_ids").get<std::vector<uint32_t>>();rule.offsets=row.at("pulse_offsets").get<std::vector<float>>();
    for(auto id:rule.pulseWindows){if(id>=motion->outgoing.size())throw std::runtime_error("Unknown source delayed MV window");std::array<float,4> values{};for(int level=0;level<4;++level)values[level]=motion->MotionValue(id,level);rule.motionValues.push_back(values);}
    rule.radius=shell.at("native_attack_range").get<float>()*shell.at("range_scale").get<float>()*shell.at("shell_hitbox_multiplier").get<float>();
    rule.stickSeconds=shell.at("modifiers").at("HitStickTime").get<float>();rule.authoredTriggerGeometry=row.at("trigger_contact_geometry_known").get<bool>();
    if(!rule.Valid()||rule.pulseWindows.size()!=count||rule.triggerWindow>=motion->outgoing.size()||std::abs(motion->outgoing[rule.triggerWindow].begin-rule.begin)>1e-4f||
       std::abs(motion->outgoing[rule.triggerWindow].end-rule.end)>1e-4f)throw std::runtime_error("Delayed trigger identity mismatch");
    for(size_t i=0;i<count;++i){const float expected=first+float(i)*between+(i+1==count?last-between:0);
     const auto id=rule.pulseWindows[i];
     if(std::abs(rule.offsets[i]-expected)>1e-4f||id>=motion->outgoing.size()||id==rule.triggerWindow)throw std::runtime_error("Delayed pulse identity mismatch");
     if(std::abs(motion->outgoing[id].begin-(rule.begin+expected))>1e-4f||std::abs(motion->outgoing[id].end-std::min(clips.at(slot).duration(),rule.end+expected))>1e-4f||motion->Gain(id)!=0)throw std::runtime_error("Delayed pulse window mismatch");}
    result.delayedWindows[slot].push_back({rule.begin,rule.end,1,uint64_t(rule.triggerWindow)+1,0,0,row.at("node_id").get<uint32_t>()});
    result.delayedRules[slot].push_back(std::move(rule));
   }
  }
 }
 for(auto it=document["authored_windows"].begin();it!=document["authored_windows"].end();++it){const int slot=slotKey(it.key());const auto* rule=rules.Find(slot);
  if(!rule||!it.value().is_array()||it.value().size()>128)throw std::runtime_error("Invalid authored collision rows");std::set<uint32_t> identifiers;
  for(const auto& row:it.value()){
   const uint32_t id=row.at("window_id").get<uint32_t>();const auto role=row.at("contact_role").get<std::string>();
   const float begin=row.at("begin").get<float>(),end=row.at("end").get<float>();
   if(!std::isfinite(begin)||!std::isfinite(end)||begin<0||end<=begin||!identifiers.insert(id).second||id>=rule->outgoing.size()||std::abs(rule->outgoing[id].begin-begin)>1e-4f||std::abs(rule->outgoing[id].end-end)>1e-4f)throw std::runtime_error("Authored collision window identity mismatch");
   // Friendly hit lanes and delayed shell triggers are not enemy blade hits.
   if(role=="ally_only"||role=="delayed_effect")continue;
   // The original SnS bank 3 contains a cutting Rush constraint attack. Its
   // named source bone is not decoded; lane 0 is our explicit weapon-sweep
   // approximation, never a claim of a source shield or native bone binding.
   uint32_t lane=0;if(role=="shield")lane=1;else if(role=="body")lane=2;else if(role=="constraint_attack"){
    if(mode!="sword_shield"||row.at("bank").get<int>()!=3||row.at("native_bone_binding_known").get<bool>()||!row.contains("source_move")||row["source_move"].at("Attack_Type").get<int>()!=1)throw std::runtime_error("Invalid source constraint attack");
   }else if(role!="weapon")throw std::runtime_error("Unknown combat contact role");
   result.windows[slot].push_back({begin,end,1,uint64_t(id)+1,lane,row.at("bank").get<uint32_t>(),row.at("node_id").get<uint32_t>()});
  }
 }
 if(!rules.Valid())throw std::runtime_error("Invalid combat parameters");
 if(mode=="longsword"&&(!clips.contains(rules.helmFollowupSlot)||rules.helmFollowupSeconds>=clips.at(rules.helmFollowupSlot).duration()))throw std::runtime_error("Invalid source helm followup");
 return result;
}
} // namespace LS
