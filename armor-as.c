/*
   american fuzzy lop - wrapper for GNU as
   ---------------------------------------

   Written and maintained by Michal Zalewski <lcamtuf@google.com>

   Copyright 2013, 2014, 2015 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   The sole purpose of this wrapper is to preprocess assembly files generated
   by GCC / clang and inject the instrumentation bits included from afl-as.h. It
   is automatically invoked by the toolchain when compiling programs using
   afl-gcc / afl-clang.

   Note that it's an explicit non-goal to instrument hand-written assembly,
   be it in separate .s files or in __asm__ blocks. The only aspiration this
   utility has right now is to be able to skip them gracefully and allow the
   compilation process to continue.

   That said, see experimental/clang_asm_normalize/ for a solution that may
   allow clang users to make things work even with hand-crafted assembly. Just
   note that there is no equivalent for GCC.

 */

#define AFL_MAIN

#include "config.h"
#include "types.h"
#include "debug.h"
#include "alloc-inl.h"

#include "armor-as.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>

#include <sys/wait.h>
#include <sys/time.h>

#define MAX_OFFSET  0x7ff0
// #define MAX_OFFSET  0x6ff0

#define MIN_CALL_DISTANCE (1 << 18)

static u8** as_params;          /* Parameters passed to the real 'as'   */

static u8*  input_file;         /* Originally specified input file      */
static u8*  modified_file;      /* Instrumented file for the real 'as'  */
static u8*  python_file;        /* Instrumented file for the real 'as'  */
static u8   be_quiet,           /* Quiet mode (no stderr output)        */
            clang_mode,         /* Running in clang mode?               */
            pass_thru,          /* Just pass data through?              */
            just_version,       /* Just show version?                   */
            start_calculate,
            contain_main,
            contain_jmp,
            contain_switch,
            insert_after,
            func_start,
            insert_light_x9,
            sanitizer;          /* Using ASAN / MSAN                    */

static u32  inst_ratio = 100,   /* Instrumentation probability (%)      */
            cond_branch,
            as_par_cnt = 1;     /* Number of params to 'as'             */

static u8*  unique_ID;          /* Unique ID of armor basic block     */

static u32  function_nums = 0,
            func_name_len = 0;
static u8** function_name;
static u32* skip_transfer_func;

static u32  current_offset = 0,
            call_distance = 0;

static u8  func_name[512];
/* If we don't find --32 or --64 in the command line, default to 
   instrumentation for whichever mode we were compiled with. This is not
   perfect, but should do the trick for almost all use cases. */

static u8   use_64bit = 1;

#ifdef __APPLE__
#  error "Sorry, 32-bit Apple platforms are not supported."
#endif /* __APPLE__ */

// static u32 analyse_ins(u8* line) {
//   if(line[0] == '\t' && line[1] != '.' && line[1] != 'b'){
//     if((strstr(line, "uxtw") || strstr(line, "uxth") || strstr(line, "uxtb") ||
//       strstr(line, "sxtw") || strstr(line, "sxth") || strstr(line, "sxtb")) && strstr(line, "add")){
//         return 1;
//       }
//   }
//   return 0;
// }

static u8* insert_armor(u8* line) {
    // if(func_start == 1 && line[0] == '\t'){
    //   func_start = 0;
    //   if(!strncmp(line+1, "stp", 3) && strstr(line, "x30")){
    //     insert_light_x9 = 1;
    //   }
    // }

    if((!strncmp(line+1, "stp", 3)|| !strncmp(line+1, "str", 3)) && strstr(line, "x30"))
      insert_light_x9 = 1;
    if((!strncmp(line+1, "ldp", 3)|| !strncmp(line+1, "ldr", 3)) && strstr(line, "x30"))
      insert_light_x9 = 0;

    if(line[0] == '\t' && (!strncmp(line+1, "ret", 3) || !strncmp(line+1, "br", 2) || !strncmp(line+1, "blr", 3))){
      u8* res;
      u32 i, len = strlen(line);
      u8 ins[len];
      u8 target[len];
      u8 prefix[len];
      memset(ins, '\0', sizeof(ins));
      memset(target, '\0', sizeof(target));
      memset(prefix, '\0', sizeof(prefix));
      for(i = 1; i < len; i++) {
        if (line[i] == '\t' || line[i] == ' ') {
          strncpy(ins, line+1, i-1);
          break;
        }
      }
      for(i = len-1; i >=0; i--) {
        if(line[i] == '\t' || line[i] == ' ') {
          strncpy(target, line+i+1, len - (i+2));
          strncpy(prefix, line, i+1);
          break;
        }
      }
      if (!strncmp(line, "\tbr\t", 4)) {
        res = alloc_printf("%s", call_armor_light);
        if (start_calculate)
          current_offset += 20;
        if (contain_main)
          call_distance += 20;
        return res;
      }
      else if (!strncmp(line, "\tblr\t", 5)){
        res = alloc_printf("%s", call_armor_light);
        if (start_calculate)
          current_offset += 20;
        if (contain_main)
          call_distance += 20;
        return res;
      }
      else if (!strncmp(line+1, "ret", 3)){
        // if (insert_light_x9)
        //   res = alloc_printf("%s", direct_call_armor_light);
        // else
        //   res = alloc_printf("%s", call_armor_light);
        res = alloc_printf("%s", call_armor_light);
        if (start_calculate)
          current_offset += 20;
        if (contain_main)
          call_distance += 20;
        return res;
      }
      // else if (!strncmp(line, "\tblr\t", 5) && strcmp(target, "x9") != 0) {
      //   res = alloc_printf("%s", call_armor_light);
      //   if (start_calculate)
      //     current_offset += 20;
      //   if (contain_main)
      //     call_distance += 20;
      //   return res;
      // }
      else{
        return NULL;
      }
    }
    else{
      return NULL;
    }
}


/* Examine and modify parameters to pass to 'as'. Note that the file name
   is always the last parameter passed by GCC, so we exploit this property
   to keep the code simple. */

static void edit_params(int argc, char** argv) {

  u8 *tmp_dir = getenv("TMPDIR"), *armor_as = getenv("ARMOR_AS");
  u32 i;

#ifdef __APPLE__

  u8 use_clang_as = 0;

  /* On MacOS X, the Xcode cctool 'as' driver is a bit stale and does not work
     with the code generated by newer versions of clang that are hand-built
     by the user. See the thread here: http://goo.gl/HBWDtn.

     To work around this, when using clang and running without AFL_AS
     specified, we will actually call 'clang -c' instead of 'as -q' to
     compile the assembly file.

     The tools aren't cmdline-compatible, but at least for now, we can
     seemingly get away with this by making only very minor tweaks. Thanks
     to Nico Weber for the idea. */

  if (clang_mode && !armor_as) {

    use_clang_as = 1;

    armor_as = getenv("ARMOR_CC");
    if (!armor_as) armor_as = getenv("ARMOR_CXX");
    if (!armor_as) armor_as = "clang";

  }

#endif /* __APPLE__ */

  /* Although this is not documented, GCC also uses TEMP and TMP when TMPDIR
     is not set. We need to check these non-standard variables to properly
     handle the pass_thru logic later on. */

  if (!tmp_dir) tmp_dir = getenv("TEMP");
  if (!tmp_dir) tmp_dir = getenv("TMP");
  if (!tmp_dir) tmp_dir = "/tmp";

  as_params = ck_alloc((argc + 32) * sizeof(u8*));

  as_params[0] = armor_as ? armor_as : (u8*)"as";

  as_params[argc] = 0;

  for (i = 1; i < argc - 1; i++) {

    if (!strcmp(argv[i], "--64")) use_64bit = 1;
    else if (!strcmp(argv[i], "--32")) use_64bit = 0;

#ifdef __APPLE__

    /* The Apple case is a bit different... */

    if (!strcmp(argv[i], "-arch") && i + 1 < argc) {

      if (!strcmp(argv[i + 1], "x86_64")) use_64bit = 1;
      else if (!strcmp(argv[i + 1], "i386"))
        FATAL("Sorry, 32-bit Apple platforms are not supported.");

    }

    /* Strip options that set the preference for a particular upstream
       assembler in Xcode. */

    if (clang_mode && (!strcmp(argv[i], "-q") || !strcmp(argv[i], "-Q")))
      continue;

#endif /* __APPLE__ */

    as_params[as_par_cnt++] = argv[i];

  }

#ifdef __APPLE__

  /* When calling clang as the upstream assembler, append -c -x assembler
     and hope for the best. */

  if (use_clang_as) {

    as_params[as_par_cnt++] = "-c";
    as_params[as_par_cnt++] = "-x";
    as_params[as_par_cnt++] = "assembler";

  }

#endif /* __APPLE__ */

  input_file = argv[argc - 1];

  if (input_file[0] == '-') {

    if (!strcmp(input_file + 1, "-version")) {
      just_version = 1;
      modified_file = input_file;
      goto wrap_things_up;
    }

    if (input_file[1]) FATAL("Incorrect use (not called through afl-gcc?)");
      else input_file = NULL;

  } else {

    /* Check if this looks like a standard invocation as a part of an attempt
       to compile a program, rather than using gcc on an ad-hoc .s file in
       a format we may not understand. This works around an issue compiling
       NSS. */

    if (strncmp(input_file, tmp_dir, strlen(tmp_dir)) &&
        strncmp(input_file, "/var/tmp/", 9) &&
        strncmp(input_file, "/tmp/", 5)) pass_thru = 1;

  }

  modified_file = alloc_printf("%s/.armor-%s.s", tmp_dir, unique_ID);    
  // printf("%s\n%s\n", input_file, modified_file);                       

wrap_things_up:

  as_params[as_par_cnt++] = modified_file;
  as_params[as_par_cnt]   = NULL;

}

/* Add the nop instruction to reach the min call instance. */

// static void add_nop(void) {
//     u32 i = 0;
//     for(i = 0; i < MIN_CALL_DISTANCE - call_distance; i++) {
      
//     }

// }

/* Process input file, generate modified_file. Insert instrumentation in all
   the appropriate places. */

static void add_instrumentation(void) {

  static u8 line[MAX_LINE];

  // printf("Start ins\n");

  FILE* inf;
  FILE* outf;
  s32 outfd;
  u32 ins_lines = 0;
  u8 *armor_name, *nop_ins;

  u32 armor_nums = 0;

  u8  instr_ok = 0, skip_csect = 0, skip_next_label = 0,
      skip_intel = 0, skip_app = 0, instrument_next = 0,
      add_armor = 0, has_armor = 0, add_fork = 0;

#ifdef __APPLE__

  u8* colon_pos;

#endif /* __APPLE__ */

  if (input_file) {

    inf = fopen(input_file, "r");
    if (!inf) PFATAL("Unable to read '%s'", input_file);

  } else inf = stdin;

  while (fgets(line, MAX_LINE, inf)){
    if (!strncmp(line + 1, ".type", 5) && !strncmp(line + strlen(line) - 10, "%function", 9)) {
        function_nums++;
    }
  }
  
  function_name = ck_alloc((function_nums + 32) * sizeof(u8*));
  skip_transfer_func = ck_alloc((function_nums + 32) * sizeof(u32));
  rewind(inf);
  function_nums = 0;
  // printf("search func\n");
  while (fgets(line, MAX_LINE, inf)){
    if (!strncmp(line + 1, ".type", 5) && !strncmp(line + strlen(line) - 10, "%function", 9)) {
        memset(func_name, '\0', sizeof(func_name));
        u32 str_start = 0, str_end = 0, i;
        for(i = 6; i < strlen(line) - 10; i++){
          if((line[i] != '\t' && line[i] != ' ') && !str_start){
            str_start = i;
          }
          if(line[i] == ',' && !str_end) {
            str_end = i;
            break;
          }
        }
        strncpy(func_name, line+str_start, str_end-str_start);
        if(!strncmp(func_name, "main", 4) && (str_end-str_start) == 4) {
          contain_main = 1;
        }
        function_name[function_nums++] = alloc_printf("%s", func_name);
    }
  }
  rewind(inf);

  unlink(modified_file);
  outfd = open(modified_file, O_WRONLY | O_EXCL | O_CREAT, 0600);

  if (outfd < 0) PFATAL("Unable to write to '%s'", modified_file);

  outf = fdopen(outfd, "w");
  // printf("add ins\n");
  if (!outf) PFATAL("fdopen() failed");  
  function_nums = 0;
  while (fgets(line, MAX_LINE, inf)) {

    /* Output the actual line, call it a day in pass-thru mode. */
    // printf(line);
    if (!strncmp(line, "\t.file", 6) && !add_fork && contain_main){
      fputs(line, outf);
      call_distance = 0;
      add_fork = 1;
      continue;
    }

    if (line[0] == '\t' && line[1] != '.' && contain_main) {
      call_distance += 4;
    }

    /* All right, this is where the actual fun begins. For one, we only want to
       instrument the .text section. So, let's keep track of that in processed
       files - and let's set instr_ok accordingly. */

    if (line[0] == '\t' && line[1] == '.') {

      /* OpenBSD puts jump tables directly inline with the code, which is
         a bit annoying. They use a specific format of p2align directives
         around them, so we use that as a signal. */

      if (!strncmp(line + 2, "section\t", 8) ||
          !strncmp(line + 2, "section ", 8) ||
          !strncmp(line + 2, "bss\n", 4) ||
          !strncmp(line + 2, "data\n", 5)) {
        instr_ok = 0;
      }
      
      if (!strncmp(line + 2, "text\n", 5) ||
          !strncmp(line + 2, "section\t.text", 13) ||
          !strncmp(line + 2, "section\t__TEXT,__text", 21) ||
          !strncmp(line + 2, "section __TEXT,__text", 21)) {
        instr_ok = 1;
      }

      if (!strncmp(line + 1, ".type", 5) && !strncmp(line + strlen(line) - 10, "%function", 9)) {
        memset(func_name, '\0', sizeof(func_name));
        u32 str_start = 0, str_end = 0, i;
        for(i = 6; i < strlen(line) - 10; i++){
          if((line[i] != '\t' && line[i] != ' ') && !str_start){
            str_start = i;
          }
          if(line[i] == ',' && !str_end) {
            str_end = i;
            break;
          }
        }
        func_name_len = str_end-str_start;
        strncpy(func_name, line+str_start, str_end-str_start);
        has_armor = 0;
        add_armor = 0;
        cond_branch = 0;
        current_offset = 0;
        function_nums++;
        // printf(line + strlen(line) - 10);
      }

    }

    /* Detect off-flavor assembly (rare, happens in gdb). When this is
       encountered, we set skip_csect until the opposite directive is
       seen, and we do not instrument. */

    if (strstr(line, ".code")) {

      if (strstr(line, ".code32")) skip_csect = use_64bit;
      if (strstr(line, ".code64")) skip_csect = !use_64bit;

    }

    /* Detect syntax changes, as could happen with hand-written assembly.
       Skip Intel blocks, resume instrumentation when back to AT&T. */

    if (strstr(line, ".intel_syntax")) skip_intel = 1;
    if (strstr(line, ".att_syntax")) skip_intel = 0;

    /* Detect and skip ad-hoc __asm__ blocks, likewise skipping them. */

    if (line[0] == '#' || line[1] == '#') {

      if (strstr(line, "#APP")) skip_app = 1;
      if (strstr(line, "#NO_APP")) skip_app = 0;

    }

    /* If we're in the right mood for instrumenting, check for function
       names or conditional labels. This is a bit messy, but in essence,
       we want to catch:

         ^main:      - function entry point (always instrumented)
         ^.L0:       - GCC branch label
         ^.LBB0_0:   - clang branch label (but only in clang mode)
         ^\tjnz foo  - conditional branches

       ...but not:

         ^# BB#0:    - clang comments
         ^ # BB#0:   - ditto
         ^.Ltmp0:    - clang non-branch labels
         ^.LC0       - GCC non-branch labels
         ^.LBB0_0:   - ditto (when in GCC mode)
         ^\tjmp foo  - non-conditional jumps

       Additionally, clang and GCC on MacOS X follow a different convention
       with no leading dots on labels, hence the weird maze of #ifdefs
       later on.

     */
    if (!strncmp(line, "\t.size\t", 7) && !strncmp(line + 7, func_name, func_name_len))
      add_armor = 1;

    /* Conditional branch instruction (jnz, etc). We append the instrumentation
       right after the branch (to instrument the not-taken path) and at the
       branch destination label (handled later on). */
    // printf("ready to ins\n");
    if (instr_ok && !skip_app && function_nums > 0) {
      if(!strncmp(line, func_name, func_name_len) && !strncmp(line + func_name_len, ":\n", 2)){
          func_start = 1;
          insert_light_x9 = 0;
          fputs(line, outf);
          if((!strncmp(func_name, "main", 4) && func_name_len == 4) ){
            fputs(call_armor_main, outf);
            call_distance += 20;
          }
          continue;
      }
      insert_after = 0;
      u8* temp = insert_armor(line);
      if(temp != NULL){
        fputs(temp, outf);
        ins_lines++;
      }
      if(insert_after)
        continue;
      fputs(line, outf);
      if (start_calculate)
        current_offset += 4;
      if (contain_main)
        call_distance += 4;
    }
    else{
      fputs(line, outf);
    }

  }
  // if(contain_main) {
  //   // printf("call_distance:%u\tmin_distance:%u\n", call_distance, MIN_CALL_DISTANCE);
  //   if(call_distance < MIN_CALL_DISTANCE) {
  //     nop_ins = alloc_printf("\t.text\n");
  //     fputs(nop_ins, outf);
  //     ck_free(nop_ins);
  //     nop_ins = alloc_printf("\tnop\n");
  //     u32 i = 0;
  //     for(i = 0; i < MIN_CALL_DISTANCE - call_distance; i += 4){
  //       fputs(nop_ins, outf);
  //     }
  //     ck_free(nop_ins);
  //   }
  //   fputs(armor_func, outf);
  // }
  if (input_file) fclose(inf);
  fclose(outf);
  // printf("complete ins\n");
  // printf("Print ok %u\n", be_quiet);
  if (!be_quiet) {

    if (!ins_lines) WARNF("No instrumentation targets found%s.",
                          pass_thru ? " (pass-thru mode)" : "");
    else OKF("Instrumented %u locations (%s-bit, %s mode, ratio %u%%).",
             ins_lines, use_64bit ? "64" : "32",
             getenv("AFL_HARDEN") ? "hardened" : 
             (sanitizer ? "ASAN/MSAN" : "non-hardened"),
             inst_ratio);
 
  }

  // printf("End ins\n");

}


/* Main entry point */

int main(int argc, char** argv) {
  s32 pid;
  u32 rand_seed;
  int status;
  u8* inst_ratio_str = getenv("AFL_INST_RATIO");

  struct timeval tv;
  struct timezone tz;

  clang_mode = !!getenv(CLANG_ENV_VAR);

  if (isatty(2) && !getenv("armor_QUIET")) {

    SAYF(cCYA "armor-as " cBRI VERSION cRST " by <yuetai17@nudt.edu.cn>\n");
 
  } else be_quiet = 1;

  if (argc < 2) {

    SAYF("\n"
         "This is a helper application for afl-fuzz. It is a wrapper around GNU 'as',\n"
         "executed by the toolchain whenever using afl-gcc or afl-clang. You probably\n"
         "don't want to run this program directly.\n\n"

         "Rarely, when dealing with extremely complex projects, it may be advisable to\n"
         "set AFL_INST_RATIO to a value less than 100 in order to reduce the odds of\n"
         "instrumenting every discovered branch.\n\n");

    exit(1);

  }

  gettimeofday(&tv, &tz);

  rand_seed = tv.tv_sec ^ tv.tv_usec ^ getpid();

  srandom(rand_seed);

  // unique_ID = alloc_printf("%u%u%u%u", getpid(), rand_seed, random(), random());
  unique_ID = alloc_printf("%u%u", getpid(), (u32)time(NULL));

  // for(u32 i = 0; i < argc; i++)
  //   printf("%s\n", argv[i]);

  edit_params(argc, argv);

  // for(u32 i = 0; i < as_par_cnt; i++)
  //   printf("%s\n", as_params[i]);

  if (inst_ratio_str) {

    if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || inst_ratio > 100) 
      FATAL("Bad value of AFL_INST_RATIO (must be between 0 and 100)");

  }
  
  char programPath[256];
  strcpy(programPath, argv[0]);  // 获取程序路径
  
  // 提取目录部分
  char *lastSlash = strrchr(programPath, '/');
  if (lastSlash != NULL) {
      *(lastSlash + 1) = '\0';  // 添加字符串结束符，截断文件名部分
  }
  python_file = alloc_printf("python %s/ta_instrument.py %s %s", programPath, input_file, modified_file);
  // python_file = alloc_printf("%s/instrument.py", programPath);

  // printf("input file: %s %s %s\n", python_file, input_file, modified_file);

  // FILE* pipe = popen(python_file, "r");
  // if (pipe == NULL) {
  //     printf("Failed to open pipe.\n");
  //     return 1;
  // }

  // char buffer[128];
  // while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
  //     printf("%s", buffer);
  // }

  // pclose(pipe);

  // if (!just_version) add_instrumentation();
  if (!just_version) system(python_file);

  // execlp("python", "python", python_file, input_file, modified_file);

  // printf("after file: %s %s %s\n", python_file, input_file, modified_file);
  if (!(pid = fork())) {
    execvp(as_params[0], (char**)as_params);
    FATAL("Oops, failed to execute '%s' - check your PATH", as_params[0]);

  }

  if (pid < 0) PFATAL("fork() failed");

  if (waitpid(pid, &status, 0) <= 0) PFATAL("waitpid() failed");

  if (!getenv("AFL_KEEP_ASSEMBLY")) unlink(modified_file);

  exit(WEXITSTATUS(status));

}

