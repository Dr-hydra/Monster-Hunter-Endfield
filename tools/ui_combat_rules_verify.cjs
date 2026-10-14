'use strict';
const assert=require('node:assert/strict'),W=require('../ui/workshop.js');
const defaults=W.configuration({});
assert.equal(defaults.target_lock,true);assert.equal(defaults.target_camera,'focus');assert.equal(defaults.combat_rules,true);
assert.deepEqual(W.controlKeys.map(key=>defaults[key]),[81,17,9]);assert.equal(defaults.target_range,30);
const custom=W.configuration({target_camera:'target',lock_key:85,focus_key:18,cycle_key:88,target_range:42.5,combat_rules:false});
const restored=W.preset(JSON.stringify({format:'mhe.preset',version:2,name:'镜头预设',config:custom})).config;
for(const field of ['target_lock','target_camera','lock_key','focus_key','cycle_key','target_range','combat_rules'])assert.equal(restored[field],custom[field]);
for(const mode of W.modes){
 const config=W.configuration({...custom,mode:mode.id,keys:W.presetKeys(mode.id,'mouse3')});
 for(const field of W.controlKeys)assert.equal(config[field],custom[field]);
 assert.equal(W.controlKeyConflicts(config).length,0);
}
const legacy=W.preset({config:{spirit_key:17}}).config;
assert.equal(legacy.keys.spirit,17);assert.equal(legacy.focus_key,17);
assert(W.controlKeyWarnings(legacy).some(message=>message.includes('动作键优先')));
assert.equal(W.keyConflicts(legacy).length,0); // Old bindings survive, with an explicit warning.
assert.equal(W.controlKeyWarnings({...legacy,target_lock:false}).length,0);
for(const key of W.controlKeys){for(const value of [0,255,1.5,27,119,120])assert.throws(()=>W.configuration({[key]:value}));}
assert.throws(()=>W.configuration({lock_key:17}));assert.throws(()=>W.configuration({focus_key:9}));
assert.throws(()=>W.configuration({toggle_key:81}));
for(const value of [4.99,80.01,NaN])assert.throws(()=>W.configuration({target_range:value}));
assert.throws(()=>W.configuration({target_camera:'homing'}));assert.throws(()=>W.configuration({target_lock:1}));assert.throws(()=>W.configuration({combat_rules:'yes'}));
assert.equal(W.configuration({target_lock:false,combat_rules:false,target_range:80}).target_range,80);
assert.equal(W.configuration({...custom,host_future_field:{retained:true}}).host_future_field.retained,true);
console.log('Combat UI configuration: independent controls, six modes, old Ctrl migration, conflict priority, range/reserved keys and preset roundtrip passed');
