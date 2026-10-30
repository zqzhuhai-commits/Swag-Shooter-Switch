/* Movement-only override. Keep targeting/combat state intact and return its
 * original targetVector after the native navigation submission. */
static struct {ZoneVector center,current_center;float radius,current_radius;int valid,failure;uint64_t next_tick;} zone_ai;
static void (*zone_move_original)(void *,void *);
static void (*zone_sprint)(void *,void *);
static unsigned char (*zone_sample)(const ZoneVector *,void *,float,int);
static unsigned zone_route_reports;
typedef struct {uint32_t player;unsigned seen,outside,zero_wait,death_requested,was_alive;float health;Bot route;} ZoneActor;
static ZoneActor zone_actors[128];
static unsigned zone_scan;
static unsigned zone_submitted;
static unsigned zone_actor_round;
static uint64_t zone_actor_tick;
static void (*zone_late_original)(void *,void *);
static ZoneActor *zone_watch(void *player);
static void zone_observe_ai(void *base) {
 void *player=object_field(base,"playerManager");int ai=0,alive=0;
 if(!player || squad_bool(player,"_isAlive",&alive) || !alive)return;
 if(squad_bool(player,"_isAi",&ai) || !ai)squad_bool(object_field(player,"playerId"),"_isAi",&ai);
 if(!ai)return;
 ZoneActor *w=zone_watch(player);if(!w)return;
 if(!w->route.base || api.target(w->route.base)!=base) {
  if(w->route.base)api.release(w->route.base);
  w->route.base=api.weak(base,0);
 }
}
static void *zone_playing_game(void) {
 void *game=squad_singleton("brijesh.CountryWar2","GameManager","_instance");
 void *field=game ? api.field(api.klass(game),"gameState") : NULL;int state=-1;
 if(!field || (api.flags(field)&0x10))return NULL;
 int kind=api.kind(api.type(field));if(kind!=8 && kind!=17)return NULL;
 api.read(game,field,&state);return state==1 ? game : NULL;
}
static int zone_finite(ZoneVector v) {return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z);}
static float zone_distance_squared(ZoneVector a,ZoneVector b) {
 float x=a.x-b.x,z=a.z-b.z;return x*x+z*z;
}
static int zone_navigation_ready(void *base) {
 void *nav=object_field(base,"navI"),*agent=object_field(nav,"agent"),*boxed=NULL;
 if(!nav || !agent || squad_invoke(agent,"get_isActiveAndEnabled",0,NULL,&boxed) || !boxed)return 0;
 unsigned char *enabled=api.unbox(boxed);if(!enabled || !*enabled)return 0;
 boxed=NULL;if(squad_invoke(nav,"IsOnNavMesh",0,NULL,&boxed) || !boxed)return 0;
 unsigned char *on=api.unbox(boxed);return on && *on;
}
static void zone_ai_poll(void) {
 uint64_t now=armGetSystemTick(),freq=armGetSystemTickFreq();
 if(now<zone_ai.next_tick)return;
 zone_ai.next_tick=now+freq/2;zone_ai.valid=0;zone_ai.failure=1;
 if(!profile_game_image || !zone_playing_game())return;
 void *mode=squad_singleton("brijesh.CountryWar2","GameModeManager","Instance");
 const void *applicable=mode ? api.method(api.klass(mode),"get_RestrictedAreaApplicable",0) : NULL;
 void *mode_exception=NULL,*enabled=applicable ? api.invoke(applicable,mode,NULL,&mode_exception) : NULL;
 unsigned char *has_circle=enabled && !mode_exception ? api.unbox(enabled) : NULL;
 void *area=zone_area_handle ? api.target(zone_area_handle) : NULL;
 if(has_circle && !*has_circle)return;
 if((!has_circle || !*has_circle) && !area)return;
 if(!area)area=squad_singleton("brijesh.CountryWar2","RestrictedArea","Instance");
 int online=0;zone_ai.failure=2;
 if(!area || squad_bool(area,"_isOnlineScene",&online) || online)return;
 zone_ai.failure=3;
 /* The native inside/outside test reads a cached center/radius. Refresh that
  * cache from the current visible transforms even if its update was skipped. */
 if(!object_field(area,"radiusPoint") || squad_invoke(area,"OnUpdate",0,NULL,NULL))return;
 const Spec radius={"radiusSquare",12,0};
 void *rf=checked_field(area,&radius);if(!rf)return;
 float squared=0;ZoneVector current;api.read(area,rf,&squared);
 if(!isfinite(squared) || squared<=0.0001f)return;
 zone_ai.failure=4;
 const void *getter=api.method(api.klass(area),"get_transform",0);if(!getter)return;
 void *exception=NULL,*transform=api.invoke(getter,area,NULL,&exception);ZoneVector center;
 if(exception || !transform || zone_vector_property(transform,"get_position",&center,0) || !zone_finite(center))return;
 if(zone_vector_property(transform,"get_localScale",&current,0) || !zone_finite(current) || current.x<=0)return;
 float r=sqrtf(squared);
 zone_ai.current_center=center;zone_ai.current_radius=r;
 void *next=object_field(area,"nextAreaTransform");ZoneVector next_center,next_scale;
 if(next && !zone_vector_property(next,"get_position",&next_center,0) &&
    !zone_vector_property(next,"get_localScale",&next_scale,0) && zone_finite(next_center) && zone_finite(next_scale) &&
    next_scale.x>0 && next_scale.x<=current.x) {
  float nr=r*next_scale.x/current.x;
  /* Reject an uninitialized preview or a next circle outside the current one. */
  if(nr>0.01f && sqrtf(zone_distance_squared(center,next_center))+nr<=r*1.02f) {center=next_center;r=nr;}
 }
 if(!zone_sample && api.resolve_icall)zone_sample=(void *)api.resolve_icall("UnityEngine.AI.NavMesh::SamplePosition_Injected(UnityEngine.Vector3&,UnityEngine.AI.NavMeshHit&,System.Single,System.Int32)");
 zone_ai.center=center;zone_ai.radius=r;zone_ai.valid=1;zone_ai.failure=0;
}
static int zone_ai_destination(Bot *bot,void *base,ZoneVector *destination) {
 if(!zone_ai.valid || !zone_sample)return 0;
 uint64_t now=armGetSystemTick();
 if(now>=bot->route_next_tick) {
  bot->route_next_tick=now+armGetSystemTickFreq()*3/2;bot->route_forced=0;
  void *transform=object_field(base,"myTransform");ZoneVector position;
  if(!transform || zone_vector_property(transform,"get_position",&position,0) || !zone_finite(position))return 0;
  float distance=zone_distance_squared(position,zone_ai.center),r=zone_ai.radius;
  if(!isfinite(distance) || distance<=r*r*0.81f)return 0;
  /* Spread destinations along the entry side, safely inside the circle. */
  float fraction=0.60f+(bot->personality-0.9f)*0.5f;
  float ratio=r*fraction/sqrtf(distance);
  ZoneVector wanted={zone_ai.center.x+(position.x-zone_ai.center.x)*ratio,position.y,
                     zone_ai.center.z+(position.z-zone_ai.center.z)*ratio};
  _Alignas(16) unsigned char hit[64]={0};
  unsigned char found=zone_sample(&wanted,hit,20.0f,-1);
  if(!found) {wanted.x=zone_ai.center.x;wanted.z=zone_ai.center.z;found=zone_sample(&wanted,hit,100.0f,-1);}
  if(!found)return 0;
  ZoneVector point;memcpy(&point,hit,sizeof point);
  if(!zone_finite(point) || zone_distance_squared(point,zone_ai.center)>r*r*0.81f)return 0;
  memcpy(bot->route_position,&point,sizeof point);bot->route_forced=1;
  if(output && zone_route_reports<12) {
   fprintf(output,"AI_ZONE_ROUTE radius=%.1f distance=%.1f destination=%.1f,%.1f,%.1f navmesh=1\n",r,sqrtf(distance),point.x,point.y,point.z);++zone_route_reports;
  }
 }
 if(!bot->route_forced)return 0;
 memcpy(destination,bot->route_position,sizeof *destination);return 1;
}
static int zone_ai_submit(void *base,void *method) {
 if(!zone_ai.valid)return 0;
 if(!zone_navigation_ready(base))return 0;
 int alive=0;
 if(squad_bool(object_field(base,"playerManager"),"_isAlive",&alive) || !alive)return 0;
 Bot *bot=NULL;
 for(unsigned i=0;i<128;++i)if(bots[i].base && api.target(bots[i].base)==base) {bot=&bots[i];break;}
 if(!bot)for(unsigned i=0;i<128;++i)
  if(zone_actors[i].route.base && api.target(zone_actors[i].route.base)==base) {bot=&zone_actors[i].route;break;}
 int busy=0;
 if(!bot || (!squad_bool(base,"usingDynamicObject",&busy) && busy))return 0;
 if((!squad_bool(base,"inParkour",&busy) && busy) || (!squad_bool(base,"isDodging",&busy) && busy))return 0;
 ZoneVector destination;
 if(!zone_ai_destination(bot,base,&destination))return 0;
 void *behaviour=object_field(base,"currentBehaviour"),*nav=object_field(base,"navI");
 const Spec target={"targetVector",17,0};void *field=checked_field(behaviour,&target);
 if(!field || !nav)return 0;
 ZoneVector previous;api.read(behaviour,field,&previous);api.write(behaviour,field,&destination);
 zone_sprint(base,NULL);zone_move_original(base,method);api.write(behaviour,field,&previous);
 return 1;
}
static void zone_ai_move(void *base,void *method) {
 if(!zone_ai_submit(base,method))zone_move_original(base,method);
}
static void zone_ai_late(void *base,void *method) {
 zone_late_original(base,method);
 zone_observe_ai(base);
 /* Submit after the combat/cover update, which may have overwritten the path. */
 if(zone_ai.valid)zone_ai_submit(base,NULL);
}
static ZoneActor *zone_watch(void *player) {
 ZoneActor *empty=NULL;
 for(unsigned i=0;i<128;++i) {
  ZoneActor *w=&zone_actors[i];
  if(w->player && api.target(w->player)==player) {w->seen=zone_scan;return w;}
  if(!empty && (!w->player || !api.target(w->player)))empty=w;
 }
 if(!empty)return NULL;
 if(empty->player)api.release(empty->player);
 if(empty->route.base)api.release(empty->route.base);
 memset(empty,0,sizeof *empty);empty->player=api.weak(player,0);empty->seen=zone_scan;
 empty->health=-1;empty->route.personality=1.0f;
 return empty->player ? empty : NULL;
}
static void zone_reconcile_death(void *player) {
 int alive=1;if(squad_bool(player,"_isAlive",&alive) || alive || !squads.ready)return;
 int slot=squad_find_player(player);
 if(slot>=0 && !squads.rules.dead[slot]) {
  void *manager=api.target(squads.manager);if(manager)squad_death(manager,player,NULL);
 }
}
static void zone_actor_visit(void *player,void *area,unsigned *outside_count,unsigned *damaged) {
 int ai=0,alive=0;if(!player)return;
 if(squad_bool(player,"_isAi",&ai) || !ai)squad_bool(object_field(player,"playerId"),"_isAi",&ai);
 if(!ai)return;
 ZoneActor *w=zone_watch(player);if(!w)return;
 if(squad_bool(player,"_isAlive",&alive) || !alive) {if(w->was_alive)zone_reconcile_death(player);return;}
 void *health=object_field(player,"playerHealth"),*hf,*mf;
 const Spec hp={"_health",12,0},maximum={"maxHealth",12,0};
 hf=checked_field(health,&hp);mf=checked_field(health,&maximum);float value=0,max=0;
 if(!hf || !mf)return;
 api.read(health,hf,&value);api.read(health,mf,&max);
 if(!isfinite(value) || !isfinite(max) || max<=0)return;
 if(value<=0) {
  /* Zero during prefab/aircraft initialization is not evidence of a death. */
  if(!w->was_alive)return;
  /* Only complete the native death flow after health really reached zero. */
  if(++w->zero_wait>=2 && !w->death_requested) {
   unsigned char headshot=0;void *args[]={&headshot};
   if(!squad_invoke(player,"MakePlayerDead",1,args,NULL))w->death_requested=1;
   zone_reconcile_death(player);
   if(output)fprintf(output,"AI_ZONE_ZERO_HEALTH death_requested=%u\n",w->death_requested);
  }
  return;
 }
 w->was_alive=1;
 if(w->zero_wait || w->death_requested) {w->outside=0;w->health=-1;w->death_requested=0;}
 w->zero_wait=0;
 void *base=object_field(object_field(player,"targetScript"),"myAIBaseScript");
 if(base && (!w->route.base || api.target(w->route.base)!=base)) {
  if(w->route.base)api.release(w->route.base);
  w->route.base=api.weak(base,0);w->route.route_next_tick=0;
 }
 void *transform=base ? object_field(base,"myTransform") : NULL;
 if(!transform) {
  const void *getter=api.method(api.klass(player),"get_transform",0);void *ex=NULL;
  if(getter)transform=api.invoke(getter,player,NULL,&ex);
  if(ex)transform=NULL;
 }
 ZoneVector position;if(!transform || zone_vector_property(transform,"get_position",&position,0) || !zone_finite(position))return;
 void *args[]={&position},*boxed=NULL;
 if(squad_invoke(area,"AmIInSide",1,args,&boxed) || !boxed)return;
 unsigned char *inside=api.unbox(boxed);if(!inside)return;
 if(*inside) {w->outside=0;w->health=value;return;}
 ++*outside_count;++w->outside;
 if(base && zone_ai.valid && zone_navigation_ready(base)) {
  int busy=0;ZoneVector destination;
  if((squad_bool(base,"usingDynamicObject",&busy) || !busy) &&
     (squad_bool(base,"inParkour",&busy) || !busy) &&
     (squad_bool(base,"isDodging",&busy) || !busy) && zone_ai_destination(&w->route,base,&destination)) {
   void *nav=object_field(base,"navI"),*on_mesh=NULL;
   if(nav && !squad_invoke(nav,"IsOnNavMesh",0,NULL,&on_mesh) && on_mesh) {
    unsigned char *on=api.unbox(on_mesh);
    if(on && *on) {
     void *agent=object_field(nav,"agent");unsigned char stopped=0;void *resume[]={&stopped};
     if(agent)squad_invoke(agent,"set_isStopped",1,resume,NULL);
     void *route_args[]={&destination},*accepted=NULL;
     if(!squad_invoke(nav,"SetDestination",1,route_args,&accepted) && accepted) {
      unsigned char *ok=api.unbox(accepted);
      if(ok && *ok) {zone_sprint(base,NULL);++zone_submitted;}
     }
    }
   }
  }
 }
 /* Allow the original tick to work. Fill in only after two outside samples
  * without a health decrease, rather than stacking on native zone damage. */
 if(zone_damage_fallback(w->outside,w->health>=0,value<w->health-0.01f)) {
  unsigned phase=zone_phase ? zone_phase-1 : 0;if(phase>5)phase=5;
  float amount=max*(float)zone_damage_percent(phase)/100.0f,next=fmaxf(0,value-amount);
  /* Reset attribution to an environmental/self source using the native API. */
  squad_invoke(health,"ReceiveTornadoDamageReceived",0,NULL,NULL);
  void *set_args[]={&next};
  if(!squad_invoke(health,"set_Health",1,set_args,NULL)) {
   api.read(health,hf,&value);++*damaged;
   if(output && *damaged<=3)fprintf(output,"AI_ZONE_DAMAGE phase=%u amount=%.2f health=%.2f\n",phase+1,amount,value);
   if(value<=0)squad_invoke(health,"SendZeroHealth",0,NULL,NULL);
   zone_reconcile_death(player);
  }
 }
 w->health=value;
}
static unsigned zone_collect_registered(void **players,unsigned capacity) {
 unsigned count=0;if(!players || !capacity)return 0;
 if(squads.ready && squad_mode_active()) {
  for(unsigned i=0;i<52 && count<capacity;++i) {
   void *p=squads.actors[i].player ? api.target(squads.actors[i].player) : NULL;
   if(p)players[count++]=p;
  }
  return count; /* The current squad roster is authoritative, including during spawn. */
 }
 for(unsigned i=0;i<128 && count<capacity;++i) {
  void *p=zone_actors[i].player ? api.target(zone_actors[i].player) : NULL;if(!p)continue;
  int duplicate=0;for(unsigned j=0;j<count;++j)if(players[j]==p)duplicate=1;
  if(!duplicate)players[count++]=p;
 }
 for(unsigned i=0;i<128 && count<capacity;++i)if(bots[i].base) {
  void *base=api.target(bots[i].base),*p=object_field(base,"playerManager");if(!p)continue;
  int duplicate=0;for(unsigned j=0;j<count;++j)if(players[j]==p)duplicate=1;
  if(!duplicate)players[count++]=p;
 }
 return count;
}
static void zone_actors_poll(void) {
 uint64_t now=armGetSystemTick();if(now<zone_actor_tick)return;
 zone_actor_tick=now+armGetSystemTickFreq();++zone_scan;
 if(zone_actor_round!=zone_round_serial) {
  for(unsigned i=0;i<128;++i) {
   if(zone_actors[i].player)api.release(zone_actors[i].player);
   if(zone_actors[i].route.base)api.release(zone_actors[i].route.base);
  }
  memset(zone_actors,0,sizeof zone_actors);zone_actor_round=zone_round_serial;
 }
 void *game=zone_playing_game();
 void *area=zone_area_handle ? api.target(zone_area_handle) : NULL;
 if(!area && zone_ai.valid)area=squad_singleton("brijesh.CountryWar2","RestrictedArea","Instance");
 if(!game || !area) {
  if(output && zone_scan%10==0)fprintf(output,"AI_ZONE_STATUS blocked=NO_ACTIVE_GAME_OR_AREA game=%d area=%d snapshot_failure=%d\n",game!=NULL,area!=NULL,zone_ai.failure);
  return;
 }
 void *mode=squad_singleton("brijesh.CountryWar2","GameModeManager","Instance"),*result=NULL;
 if(!mode || squad_invoke(mode,"get_RestrictedAreaApplicable",0,NULL,&result) || !result)return;
 unsigned char *circle=api.unbox(result);if(!circle || !*circle)return;
 const Spec radius={"radiusSquare",12,0};void *rf=checked_field(area,&radius);float r2=0;
 if(rf)api.read(area,rf,&r2);
 if(!isfinite(r2) || r2<=0.0001f)return;
 unsigned outside=0,damaged=0;
 /* GameManager.playerList is a dictionary in this build. Never call its
  * indexer with an integer address as though it were List<PlayerManager>. */
 void *players[128]={0};unsigned count=zone_collect_registered(players,128);
 for(unsigned i=0;i<count;++i)zone_actor_visit(players[i],area,&outside,&damaged);
 for(unsigned i=0;i<128;++i)if(zone_actors[i].player && zone_actors[i].seen!=zone_scan) {
  api.release(zone_actors[i].player);if(zone_actors[i].route.base)api.release(zone_actors[i].route.base);
  memset(&zone_actors[i],0,sizeof zone_actors[i]);
 }
 if(output && (zone_scan%5==0 || damaged))fprintf(output,"AI_ZONE_STATUS actors=%u outside=%u fallback_damage=%u geometry=%d snapshot_failure=%d navmesh=%d routes_submitted=%u radius=%.2f\n",count,outside,damaged,zone_ai.valid,zone_ai.failure,zone_sample!=NULL,zone_submitted,sqrtf(r2));
}
