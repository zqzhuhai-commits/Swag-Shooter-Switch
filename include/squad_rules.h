#ifndef SWAG_SQUAD_RULES_H
#define SWAG_SQUAD_RULES_H
#include <stdint.h>
#include <string.h>
enum { SQUAD_TEAMS=13, SQUAD_SIZE=4, SQUAD_PLAYERS=52 };
typedef struct {
 uint8_t used[SQUAD_PLAYERS],dead[SQUAD_PLAYERS],rank[SQUAD_TEAMS];
 unsigned registered;
} SquadRules;
static inline void squad_rules_reset(SquadRules *s) { memset(s,0,sizeof *s); }
static inline int squad_rules_join(SquadRules *s,int human) {
 unsigned first=human ? 0 : 1,last=human ? 1 : SQUAD_PLAYERS;
 for(unsigned i=first;i<last;++i)if(!s->used[i]) {
  s->used[i]=1;++s->registered;return (int)i;
 }
 return -1;
}
static inline unsigned squad_rules_lives(const SquadRules *s,unsigned team) {
 if(team<1 || team>SQUAD_TEAMS)return 0;
 unsigned lives=SQUAD_SIZE;
 for(unsigned i=(team-1)*SQUAD_SIZE;i<team*SQUAD_SIZE;++i)lives-=s->dead[i]!=0;
 return lives;
}
/* UI population never counts slots that have not actually registered. */
static inline unsigned squad_rules_present_lives(const SquadRules *s,unsigned team) {
 if(team<1 || team>SQUAD_TEAMS)return 0;
 unsigned n=0;
 for(unsigned i=(team-1)*SQUAD_SIZE;i<team*SQUAD_SIZE;++i)n+=s->used[i] && !s->dead[i];
 return n;
}
static inline unsigned squad_rules_alive(const SquadRules *s) {
 unsigned alive=0;
 for(unsigned i=1;i<=SQUAD_TEAMS;++i)alive+=squad_rules_lives(s,i)>0;
 return alive;
}
static inline int squad_rules_death(SquadRules *s,unsigned slot) {
 if(slot>=SQUAD_PLAYERS || !s->used[slot] || s->dead[slot])return 0;
 s->dead[slot]=1;unsigned team=slot/SQUAD_SIZE+1;
 if(!squad_rules_lives(s,team))s->rank[team-1]=(uint8_t)(squad_rules_alive(s)+1);
 if(s->registered==SQUAD_PLAYERS && squad_rules_alive(s)==1)
  for(unsigned i=1;i<=SQUAD_TEAMS;++i)if(squad_rules_lives(s,i))s->rank[i-1]=1;
 return 1;
}
static inline unsigned squad_rules_enemy_lives(const SquadRules *s) {
 unsigned alive=0;for(unsigned i=2;i<=SQUAD_TEAMS;++i)alive+=squad_rules_lives(s,i);
 return alive;
}
/* No provisional winner: an incomplete roster or multiple living factions
 * can never award a squad victory. Local elimination may show a loss. */
static inline unsigned squad_rules_winner(const SquadRules *s) {
 if(s->registered!=SQUAD_PLAYERS || squad_rules_alive(s)!=1)return 0;
 for(unsigned id=1;id<=SQUAD_TEAMS;++id)if(squad_rules_lives(s,id))return id;
 return 0;
}
static inline int squad_rules_can_finish(const SquadRules *s) {
 return s->registered==SQUAD_PLAYERS &&
  (!squad_rules_lives(s,1) || squad_rules_winner(s)!=0);
}
/* Executed on the console before installing hooks; also usable by a host runner. */
static inline int squad_rules_selftest(void) {
 SquadRules s;squad_rules_reset(&s);
 for(unsigned i=1;i<SQUAD_PLAYERS;++i)if(squad_rules_join(&s,0)!=(int)i)return -1;
 if(squad_rules_join(&s,1)!=0 || s.registered!=52 || squad_rules_join(&s,0)!=-1)return -2;
 for(unsigned i=1;i<=13;++i)if(squad_rules_lives(&s,i)!=4)return -3;
 for(unsigned i=4;i<52;++i) {
  if(!squad_rules_death(&s,i) || squad_rules_death(&s,i))return -4;
  if(i<51 && s.rank[0])return -5;
 }
 if(squad_rules_alive(&s)!=1 || squad_rules_enemy_lives(&s) || s.rank[0]!=1 || s.rank[1]!=13 || s.rank[12]!=2)return -6;
 squad_rules_reset(&s);if(s.registered || s.dead[0] || s.rank[0])return -7;
 return 0;
}
#endif
