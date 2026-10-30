/* Included after the shared IL2CPP reflection helpers in ai_profile.c.
 * Real AI factions use 13 Team objects. The stock second Team is an aggregate
 * used solely by the original local HUD/end-of-round adapter, not for AI. */
typedef struct {uint32_t player;int registered;} SquadActor;
static struct {
 uint32_t manager,teams[13],allies[13];
 SquadActor actors[52];SquadRules rules;int ready,reported;
} squads;
static NxModuleMemory squad_trampolines;
static void *(*squad_spawn_original)(void *,void *);
static void (*squad_join_original)(void *,void *,void *);
static void (*squad_received_original)(void *,int,void *,void *);
static void (*squad_death_original)(void *,void *,void *);
static void (*squad_reset_original)(void *,void *);
static void (*squad_left_original)(void *,void *,void *);
static void (*squad_rank_original)(void *,void *);
static void (*squad_cards_original)(void *,int,int,void *);
static ZoneVector (*squad_spawn_next_original)(void *,int,void *,void *);
static int (*squad_lives_original)(void *,int,void *);
static void *(*squad_winners_original)(void *,void *);
static void *(*squad_losers_original)(void *,void *);
static uint32_t squad_population_data;
static int squad_population_original,squad_population_changed;
static void *squad_ref(void *object,const char *name) {
 if(!object)return NULL;
 void *f=api.field(api.klass(object),name),*v=NULL;
 if(!f || (api.flags(f)&0x10))return NULL;
 int k=api.kind(api.type(f));
 if(k!=0x12 && k!=0x15 && k!=0x1d && k!=0x14 && k!=0x1c && k!=0x0e)return NULL;
 api.read(object,f,&v);return v;
}
static int squad_store_ref(void *object,const char *name,void *value) {
 if(!object)return -1;
 void *f=api.field(api.klass(object),name);if(!f || (api.flags(f)&0x10))return -1;
 int k=api.kind(api.type(f));if(k!=0x12 && k!=0x15 && k!=0x1d && k!=0x14 && k!=0x1c && k!=0x0e)return -1;
 if(value) {
  void *declared=api.class_from_type(api.type(f));
  if(!declared || !api.assignable(declared,api.klass(value)))return -1;
 }
 api.barrier(object,(void **)((unsigned char *)object+api.field_offset(f)),value);return 0;
}
static int squad_int(void *object,const char *name,int *value,int write) {
 const Spec spec={name,8,0};void *f=checked_field(object,&spec);if(!f)return -1;
 if(write)api.write(object,f,value);else api.read(object,f,value);return 0;
}
static int squad_bool(void *object,const char *name,int *value) {
 const Spec spec={name,2,0};void *f=checked_field(object,&spec);if(!f)return -1;
 unsigned char b=0;api.read(object,f,&b);*value=b!=0;return 0;
}
static void *squad_singleton(const char *ns,const char *name,const char *member) {
 if(!profile_game_image)return NULL;
 void *k=api.class_named(profile_game_image,ns,name),*v=NULL;
 void *f=k ? api.field(k,member) : NULL;
 if(f && (api.flags(f)&0x10))api.static_read(f,&v);
 return v;
}
static int squad_mode_active(void) {
 void *m=squad_singleton("brijesh.CountryWar2","GameModeManager","Instance");
 if(!m)return 0;
 void *f=api.field(api.klass(m),"gameMode");int mode=-1;
 if(f)api.read(m,f,&mode);
 return mode==7; /* ClassicModeTeam only. */
}
static int squad_invoke(void *object,const char *name,int n,void **args,void **result) {
 if(!object)return -1;
 const void *m=api.method(api.klass(object),name,n);if(!m)return -1;
 void *exception=NULL,*v=api.invoke(m,object,args,&exception);
 if(exception) {if(output)fprintf(output,"SQUAD_API exception=%s\n",name);return -1;}
 if(result)*result=v;
 return 0;
}
static int squad_list_change(void *list,const char *operation,void *player) {
 void *args[]={player},*result=NULL;
 return squad_invoke(list,operation,1,args,&result);
}
static int squad_list_count(void *list) {
 void *r=NULL;if(squad_invoke(list,"get_Count",0,NULL,&r) || !r)return -1;
 void *p=api.unbox(r);int n=-1;if(p)memcpy(&n,p,4);return n;
}
static void *squad_team(unsigned id) {
 return id>=1 && id<=13 && squads.teams[id-1] ? api.target(squads.teams[id-1]) : NULL;
}
static void squad_clear(void) {
 for(unsigned i=0;i<52;++i)if(squads.actors[i].player)api.release(squads.actors[i].player);
 for(unsigned i=0;i<13;++i) {
  if(squads.teams[i])api.release(squads.teams[i]);
  if(squads.allies[i])api.release(squads.allies[i]);
 }
 if(squads.manager)api.release(squads.manager);
 memset(&squads,0,sizeof squads);
}
static void *squad_int_array(unsigned team,int allies) {
 const void *corlib=api.corlib();
 void *k=corlib ? api.class_named(corlib,"System","Int32") : NULL;
 void *a=k ? api.array_new(k,allies ? 1 : 12) : NULL;if(!a)return NULL;
 unsigned at=0;
 for(unsigned id=1;id<=13;++id)if((id==team)==allies) {
  int value=(int)id;if(api.array_length(a)!=(uintptr_t)(allies ? 1 : 12))return NULL;
  void *p=(unsigned char *)a+32+sizeof value*at++; /* ARM64 array vector ABI. */
  memcpy(p,&value,sizeof value);
 }
 return a;
}
static void squad_sync_lives(void *manager) {
 for(unsigned id=1;id<=13;++id) {
  int lives=(int)squad_rules_lives(&squads.rules,id);
  squad_int(squad_team(id),"_teamLifes",&lives,1);
 }
 int enemies=(int)squad_rules_enemy_lives(&squads.rules);
 squad_int(squad_ref(manager,"teamTwo"),"_teamLifes",&enemies,1);
}
static int squad_ensure(void *manager) {
 if(!manager || !squad_mode_active())return 0;
 void *current=squad_singleton("brijesh.CountryWar2","TeamManager","Instance");
 if(current && current!=manager)return 0; /* Ignore callbacks from an old scene. */
 if(squads.ready && api.target(squads.manager)==manager)return 1;
 if(squads.manager)squad_clear();
 void *local=squad_ref(manager,"teamOne"),*aggregate=squad_ref(manager,"teamTwo");
 if(!local || !aggregate)return 0;
 /* Do not silently absorb a partially registered two-team match. */
 if(squad_list_count(squad_ref(local,"<PlayerList>k__BackingField"))!=0 ||
    squad_list_count(squad_ref(aggregate,"<PlayerList>k__BackingField"))!=0) {
  if(output)fprintf(output,"SQUAD_SETUP status=WAIT_OR_REJECT reason=existing_two_team_roster\n");
  return 0;
 }
 squads.manager=api.weak(manager,0);squads.teams[0]=api.strong(local,0);
 if(!squads.manager || !squads.teams[0])goto fail;
 for(unsigned id=2;id<=13;++id) {
  void *t=api.object_new(api.klass(local));if(!t)goto fail;
  squads.teams[id-1]=api.strong(t,0);
  if(!squads.teams[id-1] || squad_invoke(t,".ctor",0,NULL,NULL))goto fail;
  int value=(int)id,lives=4;
  if(squad_int(t,"<TeamId>k__BackingField",&value,1) || squad_int(t,"_teamLifes",&lives,1))goto fail;
  char name[32];snprintf(name,sizeof name,"小队 %u",id);
  if(squad_store_ref(t,"<TeamName>k__BackingField",api.new_string(name)))goto fail;
  void *points=squad_ref(aggregate,"<spawnPoints>k__BackingField");
  if(points && squad_store_ref(t,"<spawnPoints>k__BackingField",points))goto fail;
 }
 /* Commit the existing local Team last, after all other allocations succeed. */
 for(unsigned i=0;i<13;++i) {
  unsigned id=i==12 ? 1 : i+2;
  void *a=squad_int_array(id,1);if(!a)goto fail;
  squads.allies[id-1]=api.strong(a,0);if(!squads.allies[id-1])goto fail;
  void *enemies=squad_int_array(id,0);if(!enemies)goto fail;
  if(squad_store_ref(squad_team(id),"<EnemyTeamIds>k__BackingField",enemies))goto fail;
 }
 squad_rules_reset(&squads.rules);squads.ready=1;squad_sync_lives(manager);
 if(output)fprintf(output,"SQUAD_SETUP status=READY squads=13 size=4 population=52 ai=51 backend=independent_team_objects\n");
 return 1;
fail:
 if(output)fprintf(output,"SQUAD_SETUP status=FAILED reason=allocation_or_metadata\n");
 squad_clear();return 0;
}
static int squad_find_player(void *player) {
 for(unsigned i=0;i<52;++i)if(squads.actors[i].player && api.target(squads.actors[i].player)==player)return (int)i;
 return -1;
}
static void squad_update_gun(void *gun) {
 if(!squads.ready || !squad_mode_active() || !gun)return;
 void *base=squad_ref(gun,"myAIBaseScript"),*player=squad_ref(base,"playerManager");
 int slot=squad_find_player(player);if(slot<0)return;
 unsigned id=(unsigned)slot/4+1;
 squad_store_ref(gun,"enemyTeams",squad_ref(squad_team(id),"<EnemyTeamIds>k__BackingField"));
}
static void squad_update_actor(void *player,unsigned id) {
 void *target=squad_ref(player,"targetScript");
 if(target) {
  squad_store_ref(target,"alliedTeamsIDs",api.target(squads.allies[id-1]));
  squad_store_ref(target,"enemyTeamsIDs",squad_ref(squad_team(id),"<EnemyTeamIds>k__BackingField"));
  void *base=squad_ref(target,"myAIBaseScript");
  squad_update_gun(squad_ref(base,"gunScript"));
 }
}
static void squad_report(void) {
 if(squads.reported>0 || squads.rules.registered!=52)return;
 unsigned counts[13]={0};int verified=1;
 for(unsigned i=0;i<52;++i) {
  void *p=api.target(squads.actors[i].player),*id=squad_ref(p,"playerId");int team=0;
  if(!p || squad_int(id,"_teamId",&team,0) || team!=(int)(i/4+1))verified=0;
  void *target=squad_ref(p,"targetScript");int target_team=0;
  if(!target || squad_int(target,"myTeamID",&target_team,0) || target_team!=(int)(i/4+1))verified=0;
  ++counts[i/4];
 }
 for(unsigned i=0;i<13;++i) {
  if(counts[i]!=4 || squad_list_count(squad_ref(squad_team(i+1),"<PlayerList>k__BackingField"))!=4)verified=0;
  void *a=squad_ref(squad_team(i+1),"<EnemyTeamIds>k__BackingField");
  if(!a || api.array_length(a)!=12)verified=0;
  else {
   unsigned seen=0;
   for(unsigned j=0;j<12;++j) {
    int id=0;memcpy(&id,(unsigned char *)a+32+4*j,4);
    if(id<1 || id>13 || id==(int)i+1 || (seen&(1u<<id)))verified=0;
    else seen|=1u<<id;
   }
  }
 }
 if(output && (verified || !squads.reported))fprintf(output,"SQUAD_ROSTER population=52 ai=51 squads=13 members=4x13 verified=%d local_members=4\n",verified);
 squads.reported=verified ? 1 : -1;
}
static void squad_poll(void) {
 if(!squads.ready || !squad_mode_active())return;
 squad_report();
}
static int squad_register(void *manager,void *player) {
 if(!squad_ensure(manager) || !player)return 0;
 int slot=squad_find_player(player);
 if(slot<0) {
  void *id=squad_ref(player,"playerId");int is_ai=0;
  if(squad_bool(id,"_isAi",&is_ai))return 0;
  uint32_t weak=api.weak(player,0);if(!weak)return 0;
  slot=squad_rules_join(&squads.rules,!is_ai);
  if(slot<0) {api.release(weak);if(output)fprintf(output,"SQUAD_REGISTER status=REJECTED reason=population_cap\n");return 1;}
  squads.actors[slot].player=weak;
 }
 unsigned team=(unsigned)slot/4+1;int value=(int)team;
 if(squad_int(squad_ref(player,"playerId"),"_teamId",&value,1))return 0;
 if(!squads.actors[slot].registered) {
  /* Reserve before invoking managed code: its callbacks may re-enter. */
  squads.actors[slot].registered=1;void *args[]={player};
  if(squad_invoke(squad_team(team),"RegisterPlayer",1,args,NULL)) {
   squads.actors[slot].registered=0;return 1;
  }
  if(team!=1) {
   void *list=squad_ref(squad_ref(manager,"teamTwo"),"<PlayerList>k__BackingField");
   if(squad_list_change(list,"Add",player) && output)fprintf(output,"SQUAD_AGGREGATE status=FAILED\n");
  }
  squad_update_actor(player,team);
  if(output)fprintf(output,"SQUAD_MEMBER slot=%d team=%u human=%d\n",slot,team,slot==0);
 }
 squad_sync_lives(manager);squad_report();return 1;
}
static void squad_apply_ranks(void) {
 for(unsigned i=0;i<52;++i)if(squads.actors[i].player) {
  int rank=squads.rules.rank[i/4];if(!rank)continue;
  void *player=api.target(squads.actors[i].player);if(!player)continue;
  int current=0;if(!squad_int(player,"survivalRank",&current,0) && current==rank)continue;
  squad_int(player,"survivalRank",&rank,1);
  void *args[]={&rank};squad_invoke(player,"SendSurvivalRank",1,args,NULL);
 }
}
static void squad_begin_new_roster(void) {
 if(!squads.ready)return;
 void *manager=api.target(squads.manager);
 if(!manager) {squad_clear();return;}
 void *current=squad_singleton("brijesh.CountryWar2","TeamManager","Instance");
 if(current && current!=manager) {squad_clear();return;}
 for(unsigned id=1;id<=13;++id)
  squad_invoke(squad_ref(squad_team(id),"<PlayerList>k__BackingField"),"Clear",0,NULL,NULL);
 squad_invoke(squad_ref(squad_ref(manager,"teamTwo"),"<PlayerList>k__BackingField"),"Clear",0,NULL,NULL);
 for(unsigned i=0;i<52;++i)if(squads.actors[i].player)api.release(squads.actors[i].player);
 memset(squads.actors,0,sizeof squads.actors);squad_rules_reset(&squads.rules);squads.reported=0;
 const Spec local_spec={"aalocalPlayerNiTeamChe",2,0};
 void *team=squad_team(1),*f=checked_field(team,&local_spec);unsigned char no=0;
 if(f)api.write(team,f,&no);
 squad_sync_lives(manager);
 if(output)fprintf(output,"SQUAD_NEW_ROSTER slots=52 reserved_human_slot=0\n");
}
static void *squad_spawn_players(void *self,void *method) {
 void *data=squad_singleton("hms.countryWar2","DataManager","Instance");int count=0;
 if(data && !squad_int(data,"noOfPlayersOnline",&count,0)) {
  if(squad_mode_active()) {
   squad_begin_new_roster();
   if(!squad_population_changed || api.target(squad_population_data)!=data) {
    if(squad_population_data)api.release(squad_population_data);
    squad_population_data=api.weak(data,0);squad_population_original=count;
   }
   int bots=51;squad_int(data,"noOfPlayersOnline",&bots,1);squad_population_changed=1;
   if(output)fprintf(output,"SQUAD_POPULATION bots=51 human=1 previous_bots=%d\n",count);
  } else if(squad_population_changed && api.target(squad_population_data)==data) {
   squad_int(data,"noOfPlayersOnline",&squad_population_original,1);squad_population_changed=0;
  }
 }
 return squad_spawn_original(self,method);
}
static void squad_join(void *self,void *player,void *method) {
 if(!squad_register(self,player))squad_join_original(self,player,method);
}
static void squad_received(void *self,int id,void *player,void *method) {
 if(!squad_register(self,player))squad_received_original(self,id,player,method);
}
static void *squad_get_team(void *self,int id,void *method) {
 (void)method;if(squad_ensure(self))return squad_team((unsigned)id);
 return id==1 ? squad_ref(self,"teamOne") : id==2 ? squad_ref(self,"teamTwo") : NULL;
}
static void squad_death(void *self,void *player,void *method) {
 if(!squad_ensure(self)) {squad_death_original(self,player,method);return;}
 int slot=squad_find_player(player);if(slot<0) {if(output)fprintf(output,"SQUAD_DEATH status=UNREGISTERED\n");return;}
 if(!squad_rules_death(&squads.rules,(unsigned)slot))return;
 squad_apply_ranks();
 unsigned team=(unsigned)slot/4+1;int native_id=team==1 ? 1 : 2,actual=(int)team;
 /* The original HUD adapter only understands local/all-opponent aggregates.
  * Temporarily adapt the deceased ID on this synchronous main-thread call.
  * TargetScript and all living actors retain their real 13 faction IDs. */
 int local=(int)squad_rules_lives(&squads.rules,1)+(team==1);
 int enemies=(int)squad_rules_enemy_lives(&squads.rules)+(team!=1);
 squad_int(squad_ref(self,"teamOne"),"_teamLifes",&local,1);
 squad_int(squad_ref(self,"teamTwo"),"_teamLifes",&enemies,1);
 void *id=squad_ref(player,"playerId");squad_int(id,"_teamId",&native_id,1);
 squad_death_original(self,player,method);
 squad_int(id,"_teamId",&actual,1);squad_sync_lives(self);squad_apply_ranks();
 if(output)fprintf(output,"SQUAD_DEATH slot=%d team=%u remaining=%u alive_squads=%u local_lives=%u enemy_lives=%u\n",slot,team,squad_rules_lives(&squads.rules,team),squad_rules_alive(&squads.rules),squad_rules_lives(&squads.rules,1),squad_rules_enemy_lives(&squads.rules));
}
static void squad_reset(void *self,void *method) {
 squad_reset_original(self,method);
 if(!squad_ensure(self))return;
 memset(squads.rules.dead,0,sizeof squads.rules.dead);memset(squads.rules.rank,0,sizeof squads.rules.rank);
 squad_sync_lives(self);
 if(output)fprintf(output,"SQUAD_RESET teams=13 lives_per_team=4\n");
}
static void squad_rank(void *self,void *method) {
 if(squad_ensure(self))squad_apply_ranks();else squad_rank_original(self,method);
}
static void squad_left(void *self,void *player,void *method) {
 if(!squad_ensure(self)) {squad_left_original(self,player,method);return;}
 int slot=squad_find_player(player);if(slot<0)return;
 if(!squads.rules.dead[slot])squad_death(self,player,NULL);
 void *args[]={player};squad_invoke(squad_team((unsigned)slot/4+1),"UnRegisterPlayer",1,args,NULL);
 if(slot/4!=0)squad_list_change(squad_ref(squad_ref(self,"teamTwo"),"<PlayerList>k__BackingField"),"Remove",player);
}
static void squad_cards(void *self,int id,int lives,void *method) {
 if(!squad_ensure(self)) {squad_cards_original(self,id,lives,method);return;}
 /* Ignore the old two-team broadcast's aggregate ID; do not overwrite actual
  * squad 2 with the combined life count of squads 2 through 13. */
 (void)id;(void)lives;squad_sync_lives(self);
}
static int squad_lives(void *self,int id,void *method) {
 if(squad_ensure(self))return (int)squad_rules_lives(&squads.rules,(unsigned)id);
 return squad_lives_original(self,id,method);
}
static ZoneVector squad_spawn_next(void *self,int id,void *player,void *method) {
 if(squad_ensure(self)) {
  void *t=squad_team((unsigned)id);ZoneVector result={0};
  void *args[]={squad_ref(t,"<PlayerList>k__BackingField"),player},*boxed=NULL;
  if(!squad_invoke(t,"GetNextSpawnPosition",2,args,&boxed) && boxed) {
   void *p=api.unbox(boxed);if(p) {memcpy(&result,p,sizeof result);return result;}
  }
  return squad_spawn_next_original(self,id==1 ? 1 : 2,player,method);
 }
 return squad_spawn_next_original(self,id,player,method);
}
static unsigned squad_best_alive(void) {
 unsigned best=1,lives=0;
 for(unsigned id=1;id<=13;++id)if(squad_rules_lives(&squads.rules,id)>lives) {
  best=id;lives=squad_rules_lives(&squads.rules,id);
 }
 return best;
}
static void *squad_winners(void *self,void *method) {
 if(squad_ensure(self))return squad_ref(squad_team(squad_best_alive()),"<PlayerList>k__BackingField");
 return squad_winners_original(self,method);
}
static void *squad_losers(void *self,void *method) {
 if(squad_ensure(self)) {
  void *team=squad_rules_lives(&squads.rules,1) ? squad_ref(self,"teamTwo") : squad_team(1);
  return squad_ref(team,"<PlayerList>k__BackingField");
 }
 return squad_losers_original(self,method);
}
static void squad_jump(unsigned char *dest,uintptr_t target) {
 const uint32_t code[]={0x58000050,0xd61f0200};memcpy(dest,code,8);memcpy(dest+8,&target,8);
}
typedef struct {size_t offset;uint32_t words[4];void *replacement;void **original;} SquadHook;
static int squad_prepare(AndroidElf *module,FILE *log) {
 if(!api.object_new || !api.array_new || !api.array_length || !api.corlib || !api.field_offset || !api.barrier || !api.strong || !api.new_string || !api.unbox || !api.class_from_type || !api.assignable)return -1;
 int tested=squad_rules_selftest();fprintf(log,"SQUAD_RULES_SELFTEST result=%d population=52 teams=13\n",tested);if(tested)return -1;
 const SquadHook hooks[]={
  {0x19f7600,{0xa9be4ff4,0xa9017bfd,0x910043fd,0xd0008a34},(void *)squad_spawn_players,(void **)&squad_spawn_original},
  {0x1753ce4,{0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_join,(void **)&squad_join_original},
  {0x1753f24,{0xd10103ff,0xa90157f6,0xa9024ff4,0xa9037bfd},(void *)squad_received,(void **)&squad_received_original},
  {0x1755254,{0xf81c0ff7,0xa90157f6,0xa9024ff4,0xa9037bfd},(void *)squad_death,(void **)&squad_death_original},
  {0x17546e0,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_reset,(void **)&squad_reset_original},
  {0x1754108,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_left,(void **)&squad_left_original},
  {0x17550f8,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_rank,(void **)&squad_rank_original},
  {0x1754f2c,{0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_cards,(void **)&squad_cards_original},
  {0x175459c,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_spawn_next,(void **)&squad_spawn_next_original},
  {0x1754454,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd},(void *)squad_lives,(void **)&squad_lives_original},
  {0x1754528,{0x7100083f,0x540000a0,0x7100043f,0x540000c1},(void *)squad_get_team,NULL},
  {0x1754dec,{0xa9be4ff4,0xa9017bfd,0x910043fd,0x90009f54},(void *)squad_winners,(void **)&squad_winners_original},
  {0x1754e8c,{0xa9be4ff4,0xa9017bfd,0x910043fd,0x90009f54},(void *)squad_losers,(void **)&squad_losers_original}
 };
 for(unsigned i=0;i<sizeof hooks/sizeof hooks[0];++i)
  if(hooks[i].offset+16>module->size || memcmp(module->image+hooks[i].offset,hooks[i].words,16)) {
   fprintf(log,"SQUAD_HOOK rejected_offset=%zx\n",hooks[i].offset);return -1;
  }
 if(nx_memory_prepare(&squad_trampolines,4096,log))return -1;
 for(unsigned i=0;i<sizeof hooks/sizeof hooks[0];++i)if(hooks[i].original) {
  unsigned char *p=(unsigned char *)squad_trampolines.staging+i*64;
  memcpy(p,hooks[i].words,16);
  uint32_t ins=hooks[i].words[3];
  if((ins&0x9f000000)==0x90000000) {
   int64_t imm=((ins>>5)&0x7ffff)*4+((ins>>29)&3);if(imm&(1<<20))imm-=(1<<21);
   uintptr_t original_page=(module->bias+hooks[i].offset+12)&~(uintptr_t)4095;
   uintptr_t address=original_page+(imm*4096);
   uint32_t literal=0x580000a0|(ins&31);memcpy(p+12,&literal,4);memcpy(p+32,&address,8);
  }
  squad_jump(p+16,module->bias+hooks[i].offset+16);
  *hooks[i].original=(void *)((uintptr_t)squad_trampolines.execution+i*64);
 }
 AndroidElf trampoline={0};trampoline.image=squad_trampolines.staging;trampoline.size=squad_trampolines.size;
 trampoline.bias=(uintptr_t)squad_trampolines.execution;trampoline.relocated=1;trampoline.phnum=1;
 trampoline.ph[0].p_type=PT_LOAD;trampoline.ph[0].p_flags=PF_R|PF_X;trampoline.ph[0].p_memsz=4096;
 if(nx_memory_publish(&squad_trampolines,&trampoline,log))return -1;
 for(unsigned i=0;i<sizeof hooks/sizeof hooks[0];++i)squad_jump(module->image+hooks[i].offset,(uintptr_t)hooks[i].replacement);
 fprintf(log,"SQUAD_HOOKS prepared=13 classic_team=52_players_13_squads device_validation=PENDING\n");return 0;
}
