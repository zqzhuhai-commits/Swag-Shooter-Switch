#include "squad_rules.h"
void *memset(void *p,int v,size_t n) {unsigned char *d=p;for(size_t i=0;i<n;++i)d[i]=(unsigned char)v;return p;}
void *memcpy(void *p,const void *q,size_t n) {unsigned char *d=p;const unsigned char *s=q;for(size_t i=0;i<n;++i)d[i]=s[i];return p;}
int run_squad_rules_tests(void) {
 int result=squad_rules_selftest();if(result)return result;
 for(unsigned human_after=0;human_after<52;++human_after) {
  SquadRules s;squad_rules_reset(&s);
  for(unsigned ai=0;ai<51;++ai) {
   if(ai==human_after && squad_rules_join(&s,1)!=0)return 10;
   if(squad_rules_join(&s,0)!=(int)ai+1)return 11;
  }
  if(human_after==51 && squad_rules_join(&s,1)!=0)return 12;
  if(s.registered!=52)return 13;
  for(unsigned i=0;i<4;++i)if(!squad_rules_death(&s,i))return 14;
  if(s.rank[0]!=13 || squad_rules_alive(&s)!=12 || squad_rules_enemy_lives(&s)!=48)return 15;
  for(unsigned i=4;i<48;++i)squad_rules_death(&s,i);
  if(s.rank[12]!=1 || squad_rules_alive(&s)!=1 || squad_rules_lives(&s,13)!=4)return 16;
 }
 return 0;
}
