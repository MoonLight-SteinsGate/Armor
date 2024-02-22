/*
   american fuzzy lop - injectable parts
   -------------------------------------

   Written and maintained by Michal Zalewski <lcamtuf@google.com>

   Forkserver design by Jann Horn <jannhorn@googlemail.com>

   Copyright 2013, 2014, 2015 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   This file houses the assembly-level instrumentation injected into fuzzed
   programs. The instrumentation stores XORed pairs of data: identifiers of the
   currently executing branch and the one that executed immediately before.

   TL;DR: the instrumentation does shm_trace_map[cur_loc ^ prev_loc]++

   The code is designed for 32-bit and 64-bit x86 systems. Both modes should
   work everywhere except for Apple systems. Apple does relocations differently
   from everybody else, so since their OSes have been 64-bit for a longer while,
   I didn't go through the mental effort of porting the 32-bit code.

   In principle, similar code should be easy to inject into any well-behaved
   binary-only code (e.g., using DynamoRIO). Conditional jumps offer natural
   targets for instrumentation, and should offer comparable probe density.

 */

#include "config.h"
#include "types.h"

/* 
   ------------------
   Performances notes
   ------------------

   Contributions to make this code faster are appreciated! Here are some
   rough notes that may help with the task:

   - Only the trampoline_fmt and the non-setup __afl_maybe_log code paths are
     really worth optimizing; the setup / fork server stuff matters a lot less
     and should be mostly just kept readable.

   - We're aiming for modern CPUs with out-of-order execution and large
     pipelines; the code is mostly follows intuitive, human-readable
     instruction ordering, because "textbook" manual reorderings make no
     substantial difference.

   - Interestingly, instrumented execution isn't a lot faster if we store a
     variable pointer to the setup, log, or return routine and then do a reg
     call from within trampoline_fmt. It does speed up non-instrumented
     execution quite a bit, though, since that path just becomes
     push-call-ret-pop.

   - There is also not a whole lot to be gained by doing SHM attach at a
     fixed address instead of retrieving __afl_area_ptr. Although it allows us
     to have a shorter log routine inserted for conditional jumps and jump
     labels (for a ~10% perf gain), there is a risk of bumping into other
     allocations created by the program or by tools such as ASAN.

   - popf is *awfully* slow, which is why we're doing the lahf / sahf +
     overflow test trick. Unfortunately, this forces us to taint eax / rax, but
     this dependency on a commonly-used register still beats the alternative of
     using pushf / popf.

     One possible optimization is to avoid touching flags by using a circular
     buffer that stores just a sequence of current locations, with the XOR stuff
     happening offline. Alas, this doesn't seem to have a huge impact:

     https://groups.google.com/d/msg/afl-users/MsajVf4fRLo/2u6t88ntUBIJ

   - Preforking one child a bit sooner, and then waiting for the "go" command
     from within the child, doesn't offer major performance gains; fork() seems
     to be relatively inexpensive these days. Preforking multiple children does
     help, but badly breaks the "~1 core per fuzzer" design, making it harder to
     scale up. Maybe there is some middle ground.

   Perhaps of note: in the 64-bit version for all platforms except for Apple,
   the instrumentation is done slightly differently than on 32-bit, with
   __afl_prev_loc and __afl_area_ptr being local to the object file (.lcomm),
   rather than global (.comm). This is to avoid GOTRELPC lookups in the critical
   code path, which AFAICT, are otherwise unavoidable if we want gcc -shared to
   work; simple relocations between .bss and .text won't work on most 64-bit
   platforms in such a case.

   (Fun fact: on Apple systems, .lcomm can segfault the linker.)

   The side effect is that state transitions are measured in a somewhat
   different way, with previous tuple being recorded separately within the scope
   of every .c file. This should have no impact in any practical sense.

   Another side effect of this design is that getenv() will be called once per
   every .o file when running in non-instrumented mode; and since getenv() tends
   to be optimized in funny ways, we need to be very careful to save every
   oddball register it may touch.

 */

// static const u8* avoid_trace = 
//   "\n"
//   "\t.bss\n"
//   "\t.align\t2\n"
//   "\t.type\tarmor_fork_nums, %object\n"
//   "\t.size\tarmor_fork_nums, 4\n"
//   "armor_fork_nums:\n"
//   "\t.zero\t4\n"
//   "\t.section\t.rodata\n"
//   "\t.align\t3\n"
//   ".LC0_avoid_trace:\n"
//   "\t.string\t\"Armor: fork failed!\"\n"
//   "\t.text\n"
//   "\t.align\t2\n"
//   "\t.global\tavoid_trace\n"
//   "\t.type\tavoid_trace, %function\n"
// "avoid_trace:\n"
// ".LFB20_avoid_trace:\n"
//   "\tstp\tx29, x30, [sp, -32]!\n"
//   "\tadd\tx29, sp, 0\n"
//   "\tadrp\tx0, armor_fork_nums\n"
//   "\tadd\tx0, x0, :lo12:armor_fork_nums\n"
//   "\tldr\tw1, [x0]\n"
//   "\tcmp\tw1, 4\n"
//   "\tbls\t.L11_avoid_trace\n"
//   "\tldp\tx29, x30, [sp], 32\n"
//   "\tret\n"
// ".L11_avoid_trace:\n"
//   "\tadd\tw1, w1, 1\n"
//   "\tstr\tw1, [x0]\n"
//   "\tbl\tfork\n"
//   "\tcmp\tw0, wzr\n"
//   "\tblt\t.L12_avoid_trace\n"
//   "\tbne\t.L4_avoid_trace\n"
//   "\tldp\tx29, x30, [sp], 32\n"
//   "\tret\n"
// ".L12_avoid_trace:\n"
//   "\tadrp\tx0, .LC0_avoid_trace\n"
//   "\tadd\tx0, x0, :lo12:.LC0_avoid_trace\n"
//   "\tbl\tputs\n"
// ".L4_avoid_trace:\n"
//   "\tadd\tx0, x29, 16\n"
//   "\tbl\twait\n"
//   "\tmov\tw0, 0\n"
//   "\tbl\texit\n"
// ".LFE20_avoid_trace:\n"
//   "\t.size\tavoid_trace, .-avoid_trace\n";

static const u8* avoid_trace = 
  "\t.align\t3\n"
  ".LC0_avoid_trace:\n"
  "\t.string\t\"Armor: fork failed!\"\n"
  "\t.text\n"
  "\t.align\t2\n"
  "\t.global\tavoid_trace\n"
  "\t.type\tavoid_trace, %function\n"
  "avoid_trace:\n"
  ".LFB20_avoid_trace:\n"
  "\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
  "\tstp\tx29, x30, [sp, -32]!\n"
  "\tadd\tx29, sp, 0\n"
  "\tbl\tfork\n"
  "\tcmp\tw0, wzr\n"
  "\tblt\t.L6_avoid_trace\n"
  "\tbne\t.L3_avoid_trace\n"
  "\tldp\tx29, x30, [sp], 32\n"
  "\tret\n"
  ".L6_avoid_trace:\n"
  "\tadrp\tx0, .LC0_avoid_trace\n"
  "\tadd\tx0, x0, :lo12:.LC0_avoid_trace\n"
  "\tbl\tputs\n"
  ".L3_avoid_trace:\n"
  "\tadd\tx0, x29, 16\n"
  "\tbl\twait\n"
  "\tldr\tw0, [x29, 16]\n"
  "\tbl\texit\n"
  ".LFE20_avoid_trace:\n"
  "\t.size\tavoid_trace, .-avoid_trace\n";

static const u8* armor_func = 
// "\t.bss\n"
// "\t.align\t3\n"
// "\t.type\tarmor_save_reg, %object\n"
// "\t.size\tarmor_save_reg, 80\n"
// "armor_save_reg:\n"
// "\t.zero\t80\n"
// "\t.comm\tarmor_save_reg,80,8\n"
// "\t.data\n"
// "\t.align\t3\n"
// ".LOOP_NUMS_armor_light = . + 0\n"
// "\t.type\tarmor_loop_nums, %object\n"
// "\t.size\tarmor_loop_nums, 4\n"
// "armor_loop_nums:\n"
// "\t.word\t10\n"
// "\t.align\t3\n"
// ".LC1_armor_light:\n"
// "\t.string\t\"loop_nums:%u\\n\"\n"
// "\t.zero\t2\n"
// ".LC0_armor_main:\n"
// "\t.string\t\"Armor:timeout!\"\n"
// "\t.zero\t1\n"
"\t.comm\tarmor_save_reg,80,8\n"
"\t.data\n"
"\t.align\t3\n"
".LC0_armor_main:\n"
"\t.string\t\"Armor:timeout!\\nThe consumed bytes:%llu\\n\"\n"
"\t.text\n"
"\t.align\t3\n"
"\t.global\tarmor_func_1\n"
"\t.type\tarmor_func_1, %function\n"
"armor_func_1:\n"
".LFB20_armor_func_1:\n"
"\tret\n"
"\tnop\n"
".LFE20_armor_func_1:\n"
"\t.size\tarmor_func_1, .-armor_func_1\n"
"\t.align\t3\n"
"\t.global\tarmor_func_2\n"
"\t.type\tarmor_func_2, %function\n"
"armor_func_2:\n"
".LFB21_armor_func_2:\n"
"\tret\n"
"\tnop\n"
".LFE21_armor_func_2:\n"
"\t.size\tarmor_func_2, .-armor_func_2\n"
"\t.align\t3\n"
"\t.global\tarmor_func_3\n"
"\t.type\tarmor_func_3, %function\n"
"armor_func_3:\n"
".LFB22_armor_func_3:\n"
"\tret\n"
"\tnop\n"
".LFE22_armor_func_3:\n"
"\t.size\tarmor_func_3, .-armor_func_3\n"
;

#ifndef BASE_LINE

static const u8* armor_light = 

"\t.text\n"
"\t.align\t2\n"
"\t.p2align 3,,7\n"
"\t.global\tarmor_light\n"
"\t.type\tarmor_light, %function\n"
"armor_light:\n"
".LFB23_armor_light:\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstp x0, x1, [x9, 8]\n"
"\tstp x2, x3, [x9, 24]\n"
"\tstp x4, x5, [x9, 40]\n"
"\tstr x30, [x9, 56]\n"
"\tmrs x4, cntvct_el0\n"
"\tmrs x0, cntfrq_el0\n"
"\tldr\tx1, [x9,72]\n"
"\tldr\tx3, [x9,64]\n"
"\tsub\tx1, x4, x1\n"
"\tlsl\tx1, x1, 29\n"
"\tudiv\tx0, x1, x0\n"
"\tadd\tx0, x0, 83\n"
"\tcmp\tx0, 65536\n"
"\tmov\tx1, 65536\n"
"\tcsel\tx1, x0, x1, ls\n"
"\tudiv\tx1, x1, x3\n"
#ifdef LOOP_INC
"\tadd\tx1, x1, 1\n"
#else
#ifdef LOOP_DES
"\tsub\tx1, x1, 1\n"
#else
"\tnop\n"
#endif
#endif
// +1
//"\tadd\tx1, x1, 1\n"
// "\tnop\n"
"\tcbz\tx1, .L5_armor_light\n"
"\tmov\tw0, 0\n"
"\tadrp\tx2, armor_func_1\n"
"\tadd\tx2, x2, :lo12:armor_func_1\n"
"\tadd\tx3, x2, 8\n"
"\tadd\tx4, x3, 8\n"
"\t.p2align 3,,7\n"
".L6_armor_light:\n"
"\tblr\tx2\n"
"\tblr\tx3\n"
"\tblr\tx4\n"
"\tadd\tw0, w0, 1\n"
"\tcmp\tx1, x0, uxtw\n"
"\tbhi\t.L6_armor_light\n"
".L5_armor_light:\n"
"\tmrs x0, cntvct_el0\n"
"\tstr\tx0, [x9,72]\n"
"\tldp x0, x1, [x9, 8]\n"
"\tldp x2, x3, [x9, 24]\n"
"\tldp x4, x5, [x9, 40]\n"
"\tldr x30, [x9, 56]\n"
"\tret\n"
".LFE23_armor_light:\n"
"\t.size\tarmor_light, .-armor_light\n";

static const u8* armor_fork = 

"\t.text\n"
"\t.align\t2\n"
"\t.p2align 3,,7\n"
"\t.global\tarmor_fork\n"
"\t.type\tarmor_fork, %function\n"
"armor_fork:\n"
".LFB23_armor_fork:\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstp x0, x1, [x9, 8]\n"
"\tstp x2, x3, [x9, 24]\n"
"\tstp x4, x5, [x9, 40]\n"
"\tstr x30, [x9, 56]\n"
"\tbl avoid_trace\n"
"\tldp x0, x1, [x9, 8]\n"
"\tldp x2, x3, [x9, 24]\n"
"\tldp x4, x5, [x9, 40]\n"
"\tldr x30, [x9, 56]\n"
"\tret\n"
".LFE23_armor_fork:\n"
"\t.size\tarmor_fork, .-armor_fork\n";

static const u8* armor_main = 

"\t.text\n"
"\t.align\t2\n"
"\t.p2align 3,,7\n"
"\t.global\tarmor_main\n"
"\t.type\tarmor_main, %function\n"
"armor_main:\n"
".LFB24_armor_main:\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstp x0, x1, [x9, 8]\n"
"\tstp x2, x3, [x9, 24]\n"
"\tstp x4, x5, [x9, 40]\n"
"\tstr x30, [x9, 56]\n"
"\tmrs x1, cntvct_el0\n"
"\tmrs x3, cntfrq_el0\n"
"\tmov\tw0, 1024\n"
"\tadrp\tx2, armor_func_1\n"
"\tadd\tx2, x2, :lo12:armor_func_1\n"
"\tadd\tx4, x2, 8\n"
"\tadd\tx5, x4, 8\n"
"\t.p2align 3,,7\n"
".L13_armor_main:\n"
"\tblr\tx2\n"
"\tblr\tx4\n"
"\tblr\tx5\n"
"\tsubs\tw0, w0, #1\n"
"\tbne\t.L13_armor_main\n"
"\tmrs x0, cntvct_el0\n"
"\tsub\tx1, x0, x1\n"
"\tlsl\tx1, x1, 29\n"
"\tudiv\tx1, x1, x3\n"
"\tlsr\tx1, x1, 10\n"
"\tstr\tx0, [x9,72]\n"
"\tcmp\tx1, 26\n"
"\tbhi\t.L17_armor_main\n"
"\tmov\tx0, 36\n"
"\tsub\tx1, x0, x1\n"
"\tmov\tx0, 65536\n"
"\tstr\tx1, [x9,64]\n"
"\tudiv\tx0, x0, x1\n"
"\tmov\tw1,\twzr\n"
"\t.p2align 3,,7\n"
".L16_armor_main:\n"
"\tblr\tx2\n"
"\tblr\tx4\n"
"\tblr\tx5\n"
"\tadd\tw1, w1, 1\n"
"\tcmp\tx0, x1, uxtw\n"
"\tbhi\t.L16_armor_main\n"
"\tmrs x0, cntvct_el0\n"
"\tstr\tx0, [x9,72]\n"
"\tldp x0, x1, [x9, 8]\n"
"\tldp x2, x3, [x9, 24]\n"
"\tldp x4, x5, [x9, 40]\n"
"\tldr x30, [x9, 56]\n"
"\tret\n"
".L17_armor_main:\n"
"\tadrp\tx0, .LC0_armor_main\n"
"\tadd\tx0, x0, :lo12:.LC0_armor_main\n"
"\tbl\tprintf\n"
"\tmov\tw0, -123\n"
"\tbl\texit\n"
".LFE24_armor_main:\n"
"\t.size\tarmor_main, .-armor_main\n";


#else

static const u8* armor_light = 

"\t.text\n"
"\t.align\t2\n"
"\t.p2align 3,,7\n"
"\t.global\tarmor_light\n"
"\t.type\tarmor_light, %function\n"
"armor_light:\n"
".LFB23_armor_light:\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstp x0, x1, [x9, 8]\n"
"\tstp x2, x3, [x9, 24]\n"
"\tstp x4, x5, [x9, 40]\n"
"\tstr x30, [x9, 56]\n"
"\tadrp\tx2, .L6_armor_light\n"
"\tadd\tx2, x2, :lo12:.L6_armor_light\n"
"\tbr\tx2\n"
".L6_armor_light:\n"
"\tmov\tx2, 4\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\t.p2align 3,,7\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
".L5_armor_light:\n"
"\tnop\n"
"\tnop\n"
"\tldp x0, x1, [x9, 8]\n"
"\tldp x2, x3, [x9, 24]\n"
"\tldp x4, x5, [x9, 40]\n"
"\tldr x30, [x9, 56]\n"
"\tret\n"
".LFE23_armor_light:\n"
"\t.size\tarmor_light, .-armor_light\n";

static const u8* armor_fork = 

"\t.text\n"
"\t.align\t2\n"
"\t.p2align 3,,7\n"
"\t.global\tarmor_fork\n"
"\t.type\tarmor_fork, %function\n"
"armor_fork:\n"
".LFB23_armor_fork:\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstp x0, x1, [x9, 8]\n"
"\tstp x2, x3, [x9, 24]\n"
"\tstp x4, x5, [x9, 40]\n"
"\tstr x30, [x9, 56]\n"
"\tbl avoid_trace\n"
"\tldp x0, x1, [x9, 8]\n"
"\tldp x2, x3, [x9, 24]\n"
"\tldp x4, x5, [x9, 40]\n"
"\tldr x30, [x9, 56]\n"
"\tret\n"
".LFE23_armor_fork:\n"
"\t.size\tarmor_fork, .-armor_fork\n";

static const u8* armor_main = 

"\t.text\n"
"\t.align\t2\n"
"\t.p2align 3,,7\n"
"\t.global\tarmor_main\n"
"\t.type\tarmor_main, %function\n"
"armor_main:\n"
".LFB24_armor_main:\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstp x0, x1, [x9, 8]\n"
"\tstp x2, x3, [x9, 24]\n"
"\tstp x4, x5, [x9, 40]\n"
"\tstr x30, [x9, 56]\n"
"\tadrp\tx2, .L13_armor_main\n"
"\tadd\tx2, x2, :lo12:.L13_armor_main\n"
"\tbr\tx2\n"
".L13_armor_main:\n"
"\tmov\tx2, 4\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\t.p2align 3,,7\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\t.p2align 3,,7\n"
".L16_armor_main:\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tnop\n"
"\tldp x0, x1, [x9, 8]\n"
"\tldp x2, x3, [x9, 24]\n"
"\tldp x4, x5, [x9, 40]\n"
"\tldr x30, [x9, 56]\n"
"\tret\n"
".L17_armor_main:\n"
"\tadrp\tx0, .LC0_armor_main\n"
"\tadd\tx0, x0, :lo12:.LC0_armor_main\n"
"\tbl\tprintf\n"
"\tmov\tw0, -123\n"
"\tbl\texit\n"
".LFE24_armor_main:\n"
"\t.size\tarmor_main, .-armor_main\n";

#endif

// static const u8* call_armor_main = 
// "\tadrp\tx9, save_reg\n"
// "\tadd\tx9, x9, :lo12:save_reg\n"
// "\tstr\tx30, [x9]\n"
// "\tbl __armor_main\n"
// "\tldr\tx30, [x9]\n";

static const u8* call_armor_main = 
"\tmov\tx9, x30\n"
"\tbl __armor_main\n"
"\tmov\tx30, x9\n";

static const u8* call_armor_light = 
"\tmov\tx9, x30\n"
"\tbl __armor_light\n"
"\tmov\tx30, x9\n";

static const u8* direct_call_armor_light = 
"\tbl __armor_light\n";

static const u8* tail_call_armor_light = 
"\tb __armor_light\n";

static const u8* call_armor_fork = 
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tstr\tx30, [x9]\n"
"\tbl armor_main\n"
"\tbl armor_fork\n"
"\tadrp\tx9, armor_save_reg\n"
"\tadd\tx9, x9, :lo12:armor_save_reg\n"
"\tldr\tx30, [x9]\n";