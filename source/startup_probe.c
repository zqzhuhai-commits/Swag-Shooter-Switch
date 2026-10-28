#include "startup_probe.h"
#include <switch.h>
#include <string.h>
static FILE *output;
static char names[128][128];
static unsigned used;
void startup_log(FILE *log) { output=log; }
static void blocked(unsigned index) __attribute__((noreturn));
static void blocked(unsigned index) { fprintf(output,"ENGINE_START_BLOCKED called_missing_api=%s game_running=0\n",names[index]); fflush(output); svcExitProcess(); }
static void trap_0(void) __attribute__((noreturn));
static void trap_0(void) { blocked(0); }
static void trap_1(void) __attribute__((noreturn));
static void trap_1(void) { blocked(1); }
static void trap_2(void) __attribute__((noreturn));
static void trap_2(void) { blocked(2); }
static void trap_3(void) __attribute__((noreturn));
static void trap_3(void) { blocked(3); }
static void trap_4(void) __attribute__((noreturn));
static void trap_4(void) { blocked(4); }
static void trap_5(void) __attribute__((noreturn));
static void trap_5(void) { blocked(5); }
static void trap_6(void) __attribute__((noreturn));
static void trap_6(void) { blocked(6); }
static void trap_7(void) __attribute__((noreturn));
static void trap_7(void) { blocked(7); }
static void trap_8(void) __attribute__((noreturn));
static void trap_8(void) { blocked(8); }
static void trap_9(void) __attribute__((noreturn));
static void trap_9(void) { blocked(9); }
static void trap_10(void) __attribute__((noreturn));
static void trap_10(void) { blocked(10); }
static void trap_11(void) __attribute__((noreturn));
static void trap_11(void) { blocked(11); }
static void trap_12(void) __attribute__((noreturn));
static void trap_12(void) { blocked(12); }
static void trap_13(void) __attribute__((noreturn));
static void trap_13(void) { blocked(13); }
static void trap_14(void) __attribute__((noreturn));
static void trap_14(void) { blocked(14); }
static void trap_15(void) __attribute__((noreturn));
static void trap_15(void) { blocked(15); }
static void trap_16(void) __attribute__((noreturn));
static void trap_16(void) { blocked(16); }
static void trap_17(void) __attribute__((noreturn));
static void trap_17(void) { blocked(17); }
static void trap_18(void) __attribute__((noreturn));
static void trap_18(void) { blocked(18); }
static void trap_19(void) __attribute__((noreturn));
static void trap_19(void) { blocked(19); }
static void trap_20(void) __attribute__((noreturn));
static void trap_20(void) { blocked(20); }
static void trap_21(void) __attribute__((noreturn));
static void trap_21(void) { blocked(21); }
static void trap_22(void) __attribute__((noreturn));
static void trap_22(void) { blocked(22); }
static void trap_23(void) __attribute__((noreturn));
static void trap_23(void) { blocked(23); }
static void trap_24(void) __attribute__((noreturn));
static void trap_24(void) { blocked(24); }
static void trap_25(void) __attribute__((noreturn));
static void trap_25(void) { blocked(25); }
static void trap_26(void) __attribute__((noreturn));
static void trap_26(void) { blocked(26); }
static void trap_27(void) __attribute__((noreturn));
static void trap_27(void) { blocked(27); }
static void trap_28(void) __attribute__((noreturn));
static void trap_28(void) { blocked(28); }
static void trap_29(void) __attribute__((noreturn));
static void trap_29(void) { blocked(29); }
static void trap_30(void) __attribute__((noreturn));
static void trap_30(void) { blocked(30); }
static void trap_31(void) __attribute__((noreturn));
static void trap_31(void) { blocked(31); }
static void trap_32(void) __attribute__((noreturn));
static void trap_32(void) { blocked(32); }
static void trap_33(void) __attribute__((noreturn));
static void trap_33(void) { blocked(33); }
static void trap_34(void) __attribute__((noreturn));
static void trap_34(void) { blocked(34); }
static void trap_35(void) __attribute__((noreturn));
static void trap_35(void) { blocked(35); }
static void trap_36(void) __attribute__((noreturn));
static void trap_36(void) { blocked(36); }
static void trap_37(void) __attribute__((noreturn));
static void trap_37(void) { blocked(37); }
static void trap_38(void) __attribute__((noreturn));
static void trap_38(void) { blocked(38); }
static void trap_39(void) __attribute__((noreturn));
static void trap_39(void) { blocked(39); }
static void trap_40(void) __attribute__((noreturn));
static void trap_40(void) { blocked(40); }
static void trap_41(void) __attribute__((noreturn));
static void trap_41(void) { blocked(41); }
static void trap_42(void) __attribute__((noreturn));
static void trap_42(void) { blocked(42); }
static void trap_43(void) __attribute__((noreturn));
static void trap_43(void) { blocked(43); }
static void trap_44(void) __attribute__((noreturn));
static void trap_44(void) { blocked(44); }
static void trap_45(void) __attribute__((noreturn));
static void trap_45(void) { blocked(45); }
static void trap_46(void) __attribute__((noreturn));
static void trap_46(void) { blocked(46); }
static void trap_47(void) __attribute__((noreturn));
static void trap_47(void) { blocked(47); }
static void trap_48(void) __attribute__((noreturn));
static void trap_48(void) { blocked(48); }
static void trap_49(void) __attribute__((noreturn));
static void trap_49(void) { blocked(49); }
static void trap_50(void) __attribute__((noreturn));
static void trap_50(void) { blocked(50); }
static void trap_51(void) __attribute__((noreturn));
static void trap_51(void) { blocked(51); }
static void trap_52(void) __attribute__((noreturn));
static void trap_52(void) { blocked(52); }
static void trap_53(void) __attribute__((noreturn));
static void trap_53(void) { blocked(53); }
static void trap_54(void) __attribute__((noreturn));
static void trap_54(void) { blocked(54); }
static void trap_55(void) __attribute__((noreturn));
static void trap_55(void) { blocked(55); }
static void trap_56(void) __attribute__((noreturn));
static void trap_56(void) { blocked(56); }
static void trap_57(void) __attribute__((noreturn));
static void trap_57(void) { blocked(57); }
static void trap_58(void) __attribute__((noreturn));
static void trap_58(void) { blocked(58); }
static void trap_59(void) __attribute__((noreturn));
static void trap_59(void) { blocked(59); }
static void trap_60(void) __attribute__((noreturn));
static void trap_60(void) { blocked(60); }
static void trap_61(void) __attribute__((noreturn));
static void trap_61(void) { blocked(61); }
static void trap_62(void) __attribute__((noreturn));
static void trap_62(void) { blocked(62); }
static void trap_63(void) __attribute__((noreturn));
static void trap_63(void) { blocked(63); }
static void trap_64(void) __attribute__((noreturn));
static void trap_64(void) { blocked(64); }
static void trap_65(void) __attribute__((noreturn));
static void trap_65(void) { blocked(65); }
static void trap_66(void) __attribute__((noreturn));
static void trap_66(void) { blocked(66); }
static void trap_67(void) __attribute__((noreturn));
static void trap_67(void) { blocked(67); }
static void trap_68(void) __attribute__((noreturn));
static void trap_68(void) { blocked(68); }
static void trap_69(void) __attribute__((noreturn));
static void trap_69(void) { blocked(69); }
static void trap_70(void) __attribute__((noreturn));
static void trap_70(void) { blocked(70); }
static void trap_71(void) __attribute__((noreturn));
static void trap_71(void) { blocked(71); }
static void trap_72(void) __attribute__((noreturn));
static void trap_72(void) { blocked(72); }
static void trap_73(void) __attribute__((noreturn));
static void trap_73(void) { blocked(73); }
static void trap_74(void) __attribute__((noreturn));
static void trap_74(void) { blocked(74); }
static void trap_75(void) __attribute__((noreturn));
static void trap_75(void) { blocked(75); }
static void trap_76(void) __attribute__((noreturn));
static void trap_76(void) { blocked(76); }
static void trap_77(void) __attribute__((noreturn));
static void trap_77(void) { blocked(77); }
static void trap_78(void) __attribute__((noreturn));
static void trap_78(void) { blocked(78); }
static void trap_79(void) __attribute__((noreturn));
static void trap_79(void) { blocked(79); }
static void trap_80(void) __attribute__((noreturn));
static void trap_80(void) { blocked(80); }
static void trap_81(void) __attribute__((noreturn));
static void trap_81(void) { blocked(81); }
static void trap_82(void) __attribute__((noreturn));
static void trap_82(void) { blocked(82); }
static void trap_83(void) __attribute__((noreturn));
static void trap_83(void) { blocked(83); }
static void trap_84(void) __attribute__((noreturn));
static void trap_84(void) { blocked(84); }
static void trap_85(void) __attribute__((noreturn));
static void trap_85(void) { blocked(85); }
static void trap_86(void) __attribute__((noreturn));
static void trap_86(void) { blocked(86); }
static void trap_87(void) __attribute__((noreturn));
static void trap_87(void) { blocked(87); }
static void trap_88(void) __attribute__((noreturn));
static void trap_88(void) { blocked(88); }
static void trap_89(void) __attribute__((noreturn));
static void trap_89(void) { blocked(89); }
static void trap_90(void) __attribute__((noreturn));
static void trap_90(void) { blocked(90); }
static void trap_91(void) __attribute__((noreturn));
static void trap_91(void) { blocked(91); }
static void trap_92(void) __attribute__((noreturn));
static void trap_92(void) { blocked(92); }
static void trap_93(void) __attribute__((noreturn));
static void trap_93(void) { blocked(93); }
static void trap_94(void) __attribute__((noreturn));
static void trap_94(void) { blocked(94); }
static void trap_95(void) __attribute__((noreturn));
static void trap_95(void) { blocked(95); }
static void trap_96(void) __attribute__((noreturn));
static void trap_96(void) { blocked(96); }
static void trap_97(void) __attribute__((noreturn));
static void trap_97(void) { blocked(97); }
static void trap_98(void) __attribute__((noreturn));
static void trap_98(void) { blocked(98); }
static void trap_99(void) __attribute__((noreturn));
static void trap_99(void) { blocked(99); }
static void trap_100(void) __attribute__((noreturn));
static void trap_100(void) { blocked(100); }
static void trap_101(void) __attribute__((noreturn));
static void trap_101(void) { blocked(101); }
static void trap_102(void) __attribute__((noreturn));
static void trap_102(void) { blocked(102); }
static void trap_103(void) __attribute__((noreturn));
static void trap_103(void) { blocked(103); }
static void trap_104(void) __attribute__((noreturn));
static void trap_104(void) { blocked(104); }
static void trap_105(void) __attribute__((noreturn));
static void trap_105(void) { blocked(105); }
static void trap_106(void) __attribute__((noreturn));
static void trap_106(void) { blocked(106); }
static void trap_107(void) __attribute__((noreturn));
static void trap_107(void) { blocked(107); }
static void trap_108(void) __attribute__((noreturn));
static void trap_108(void) { blocked(108); }
static void trap_109(void) __attribute__((noreturn));
static void trap_109(void) { blocked(109); }
static void trap_110(void) __attribute__((noreturn));
static void trap_110(void) { blocked(110); }
static void trap_111(void) __attribute__((noreturn));
static void trap_111(void) { blocked(111); }
static void trap_112(void) __attribute__((noreturn));
static void trap_112(void) { blocked(112); }
static void trap_113(void) __attribute__((noreturn));
static void trap_113(void) { blocked(113); }
static void trap_114(void) __attribute__((noreturn));
static void trap_114(void) { blocked(114); }
static void trap_115(void) __attribute__((noreturn));
static void trap_115(void) { blocked(115); }
static void trap_116(void) __attribute__((noreturn));
static void trap_116(void) { blocked(116); }
static void trap_117(void) __attribute__((noreturn));
static void trap_117(void) { blocked(117); }
static void trap_118(void) __attribute__((noreturn));
static void trap_118(void) { blocked(118); }
static void trap_119(void) __attribute__((noreturn));
static void trap_119(void) { blocked(119); }
static void trap_120(void) __attribute__((noreturn));
static void trap_120(void) { blocked(120); }
static void trap_121(void) __attribute__((noreturn));
static void trap_121(void) { blocked(121); }
static void trap_122(void) __attribute__((noreturn));
static void trap_122(void) { blocked(122); }
static void trap_123(void) __attribute__((noreturn));
static void trap_123(void) { blocked(123); }
static void trap_124(void) __attribute__((noreturn));
static void trap_124(void) { blocked(124); }
static void trap_125(void) __attribute__((noreturn));
static void trap_125(void) { blocked(125); }
static void trap_126(void) __attribute__((noreturn));
static void trap_126(void) { blocked(126); }
static void trap_127(void) __attribute__((noreturn));
static void trap_127(void) { blocked(127); }
static void (*const traps[])(void)={trap_0,trap_1,trap_2,trap_3,trap_4,trap_5,trap_6,trap_7,trap_8,trap_9,trap_10,trap_11,trap_12,trap_13,trap_14,trap_15,trap_16,trap_17,trap_18,trap_19,trap_20,trap_21,trap_22,trap_23,trap_24,trap_25,trap_26,trap_27,trap_28,trap_29,trap_30,trap_31,trap_32,trap_33,trap_34,trap_35,trap_36,trap_37,trap_38,trap_39,trap_40,trap_41,trap_42,trap_43,trap_44,trap_45,trap_46,trap_47,trap_48,trap_49,trap_50,trap_51,trap_52,trap_53,trap_54,trap_55,trap_56,trap_57,trap_58,trap_59,trap_60,trap_61,trap_62,trap_63,trap_64,trap_65,trap_66,trap_67,trap_68,trap_69,trap_70,trap_71,trap_72,trap_73,trap_74,trap_75,trap_76,trap_77,trap_78,trap_79,trap_80,trap_81,trap_82,trap_83,trap_84,trap_85,trap_86,trap_87,trap_88,trap_89,trap_90,trap_91,trap_92,trap_93,trap_94,trap_95,trap_96,trap_97,trap_98,trap_99,trap_100,trap_101,trap_102,trap_103,trap_104,trap_105,trap_106,trap_107,trap_108,trap_109,trap_110,trap_111,trap_112,trap_113,trap_114,trap_115,trap_116,trap_117,trap_118,trap_119,trap_120,trap_121,trap_122,trap_123,trap_124,trap_125,trap_126,trap_127};
uintptr_t startup_missing(const char *name) { for(unsigned i=0;i<used;++i) if(!strcmp(names[i],name)) return (uintptr_t)traps[i]; if(used==128 || strlen(name)>=128) return 0; strcpy(names[used],name); fprintf(output,"ENGINE_DIAGNOSTIC_TRAP symbol=%s not_implemented=1\n",name); return (uintptr_t)traps[used++]; }
