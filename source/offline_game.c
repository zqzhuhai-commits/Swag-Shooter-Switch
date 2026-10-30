#include "offline_game.h"
#include <string.h>
/* Inspected Assembly-CSharp codegen module for the bundled ARM64 game.
 * Apply only to writable staging, checking every token and original word
 * before any mutation. Game resources on disk remain untouched. */
#define B(from,to) (0x14000000u | (((to)-(from))/4u & 0x03ffffffu))
#define BL(from,to) (0x94000000u | (((to)-(from))/4u & 0x03ffffffu))
typedef struct { size_t offset; uint32_t before,after; } Patch;
typedef void (*PurchaseComplete)(void *,void *,void *);
static PurchaseComplete purchase_complete[5];
static unsigned char (*same_product)(void *,void *,void *);
static unsigned purchase_busy;
static FILE *purchase_log;
static void **purchase_class_slot;
static void *(*purchase_field)(void *,const char *);
static unsigned (*purchase_field_flags)(void *);
static void (*purchase_static)(void *,void *);
static void *(*shop_string_new)(const char *);
static uint32_t (*shop_keep_string)(void *,unsigned char);
static void (*shop_write_reference)(void *,void **,void *);
static void *shop_price;
static uint32_t shop_price_handle;
static const size_t product_fields[]={24,32,40,56,48};
static void *shop_owner(void) {
 if(!purchase_class_slot || !*purchase_class_slot) return NULL;
 void *field=purchase_field(*purchase_class_slot,"Instance"),*owner=NULL;
 if(field && (purchase_field_flags(field)&0x10)) purchase_static(field,&owner);
 return owner;
}
/* Run on the attached Unity main thread before input delivery. SelectedProduct
 * overwrites the resource Text with ProductData.localizedPrice at entry+8;
 * initialize that actual source rather than repairing only the prefab label. */
void offline_shop_poll(FILE *log) {
 if(!shop_string_new || !shop_keep_string || !shop_write_reference) return;
 void *owner=shop_owner();if(!owner) return;
 if(!shop_price) {
  void *text=shop_string_new("\xe5\x85\x8d\xe8\xb4\xb9\xe8\x8e\xb7\xe5\x8f\x96");
  if(!text) return;
  shop_price_handle=shop_keep_string(text,0);if(!shop_price_handle) return;
  shop_price=text;
 }
 unsigned changes=0;
 for(unsigned category=0;category<5;++category) {
  unsigned char *list=NULL,*items=NULL;int count=0;uintptr_t length=0;
  memcpy(&list,(unsigned char *)owner+product_fields[category],8);
  if(!list) continue;
  memcpy(&items,list+16,8);memcpy(&count,list+24,4);
  if(!items || count<0 || count>1024) continue;
  memcpy(&length,items+24,8);if((uintptr_t)count>length || length>4096) continue;
  for(int i=0;i<count;++i) {
   void **slot=(void **)(items+32+(size_t)i*32+8);
   if(*slot!=shop_price) { shop_write_reference(items,slot,shop_price);++changes; }
  }
 }
 if(changes) fprintf(log,"SHOP_PRICE_LABELS repaired=%u categories=5 source=product_price_string\n",changes);
}
/* Bundled IL2CPP List<ProductData> stores 32-byte value types; verified by
 * ProcessPurchase's ldp q1,q0 and IndexOf code. Dispatch its original handlers
 * with the matching product, without creating store receipts or SDK objects. */
static void local_purchase(void *owner,void *id,void *method) {
 (void)method;
 if(!id || purchase_busy) return;
 /* The old callee did not use this, so optimized callers may omit it. Fetch
  * the real component from its metadata-checked singleton instead. */
 owner=shop_owner();if(!owner) return;
 purchase_busy=1;
 for(unsigned category=0;category<5;++category) {
  unsigned char *list=NULL,*items=NULL;int count=0;uintptr_t length=0;
  memcpy(&list,(unsigned char *)owner+product_fields[category],8);
  if(!list) continue;
  memcpy(&items,list+16,8);memcpy(&count,list+24,4);
  if(!items || count<0 || count>1024) continue;
  memcpy(&length,items+24,8);
  if((uintptr_t)count>length || length>4096) continue;
  for(int i=0;i<count;++i) {
   unsigned char *entry=items+32+(size_t)i*32;void *product_id=NULL;
   memcpy(&product_id,entry,8);
   if(!product_id || !same_product(id,product_id,NULL)) continue;
   if(category==0) {
    unsigned char data[32] __attribute__((aligned(16)));memcpy(data,entry,32);
    purchase_complete[category](owner,data,NULL);
   } else purchase_complete[category](owner,id,NULL);
   if(purchase_log) fprintf(purchase_log,"LOCAL_PURCHASE category=%u status=ORIGINAL_HANDLER_RETURNED\n",category);
   purchase_busy=0;return;
  }
 }
 if(purchase_log) fprintf(purchase_log,"LOCAL_PURCHASE status=UNKNOWN_PRODUCT\n");
 purchase_busy=0;
}
static const Patch patches[]={
 /* Local labels are authoritative; remote localized prices are unavailable. */
 {0x1853480,0xd10383ff,0xd65f03c0},
 {0x1854270,0xf81e0ff3,0xd65f03c0},
 /* Only shop entrance gates; do not invent global network availability. */
 {0x1747228,0x36000960,0xd503201f},
 {0x178bdac,0x36000500,0xd503201f},
 /* Both reward availability queries return true without querying the SDK. */
 {0x1772898,0xa9bf7bfd,0x52800020},
 {0x177289c,0x910003fd,0xd65f03c0},
 /* Four-argument overload: existing success-delegate path (x20) restores
  * its own stack and invokes the entry's actual reward exactly once. */
 {0x17729a8,0xaa1f03e0,B(0x17729a8,0x17729e0)},
 /* Legacy zero/one-argument overloads: original reward event followed by
  * original close event. Preserve this/LR; reward event clears its delegate. */
 {0x177291c,0xa9bf7bfd,0xa9bf7bf3},
 {0x1772920,0x910003fd,0xaa0003f3},
 {0x1772924,0xaa1f03e0,0xaa1f03e1},
 {0x1772928,0x94039daa,BL(0x1772928,0x17725e8)},
 {0x177292c,0xb4000180,0xaa1303e0},
 {0x1772930,0xaa1f03e1,0xaa1f03e1},
 {0x1772934,0x9403a157,0xa8c17bf3},
 {0x1772938,0x360000e0,B(0x1772938,0x17724a8)},
};
int offline_game_apply(AndroidElf *module,FILE *log) {
 if(!module || module->published || !module->relocated || module->size<0x2a7ba20+0x5a4b*8) return -1;
 const struct {unsigned token;size_t address;} methods[]={
  {0x2f71,0x17471e8},{0x2ce8,0x178bd6c},{0x3287,0x1772898},
  {0x3289,0x17728e0},{0x328a,0x177291c},{0x328b,0x1772960},
  {0x327d,0x17725e8},{0x3279,0x17724a8},{0x1011,0x1853308}
 };
 for(unsigned i=0;i<sizeof methods/sizeof methods[0];++i) {
  uintptr_t value=0;
  memcpy(&value,module->image+0x2a7ba20+(methods[i].token-1)*8,8);
  if(value!=module->bias+methods[i].address) goto reject;
 }
 const uint32_t purchase_before[]={0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd};
 if(memcmp(module->image+0x1853308,purchase_before,sizeof purchase_before)) goto reject;
 const size_t handlers[]={0x1854608,0x18548b4,0x1854ce8,0x1854e4c,0x1854fa0};
 for(unsigned i=0;i<5;++i) {
  uintptr_t method=0;memcpy(&method,module->image+0x2a7ba20+(0x1019+i-1)*8,8);
  if(method!=module->bias+handlers[i]) goto reject;
 }
 uintptr_t class_slot=0;memcpy(&class_slot,module->image+0x2908a28,8);
 if(class_slot<module->bias || class_slot>=module->bias+module->size) goto reject;
 purchase_field=(void *)elf_export(module,"il2cpp_class_get_field_from_name");
 purchase_field_flags=(void *)elf_export(module,"il2cpp_field_get_flags");
 purchase_static=(void *)elf_export(module,"il2cpp_field_static_get_value");
 if(!purchase_field || !purchase_field_flags || !purchase_static) goto reject;
 shop_string_new=(void *)elf_export(module,"il2cpp_string_new");
 shop_keep_string=(void *)elf_export(module,"il2cpp_gchandle_new");
 shop_write_reference=(void *)elf_export(module,"il2cpp_gc_wbarrier_set_field");
 if(!shop_string_new || !shop_keep_string || !shop_write_reference) goto reject;
 purchase_class_slot=(void **)class_slot;
 purchase_log=log;
 for(unsigned i=0;i<sizeof patches/sizeof patches[0];++i) {
  uint32_t word; memcpy(&word,module->image+patches[i].offset,4);
  if(word!=patches[i].before) goto reject;
 }
 for(unsigned i=0;i<sizeof patches/sizeof patches[0];++i)
  memcpy(module->image+patches[i].offset,&patches[i].after,4);
 for(unsigned i=0;i<5;++i) purchase_complete[i]=(PurchaseComplete)(module->bias+handlers[i]);
 same_product=(void *)(module->bias+0x10ea348);
 /* ldr x16, PC+8; br x16; absolute native callback address. */
 const uint32_t jump[]={0x58000050,0xd61f0200};uintptr_t target=(uintptr_t)local_purchase;
 memcpy(module->image+0x1853308,jump,8);memcpy(module->image+0x1853310,&target,8);
 fprintf(log,"OFFLINE_GAME_PATCH shops=2 reward_overloads=3 original_success_callbacks=1 status=APPLIED\n");
 return 0;
reject:
 fprintf(log,"OFFLINE_GAME_PATCH status=REJECTED reason=game_code_mismatch\n");
 return -1;
}
