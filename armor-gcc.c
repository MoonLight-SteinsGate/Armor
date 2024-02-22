/*
   armor-gcc - wrapper for GCC and clang
   ----------------------------------------------

   Written and maintained by Tai Yue <yuetai17@nudt.edu.cn>

   Copyright 2013, 2014, 2015 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

 */

#define AFL_MAIN

#include "config.h"
#include "types.h"
#include "debug.h"
#include "alloc-inl.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static u8*  as_path;                /* Path to the AFL 'as' wrapper      */
static u8** cc_params;              /* Parameters passed to the real CC  */
static u32  cc_par_cnt = 1;         /* Param count, including argv0      */
static u8   be_quiet,               /* Quiet mode                        */
            clang_mode;             /* Invoked as afl-clang*?            */

static u8*  obj_path;               /* Path to runtime libraries         */
/* Try to find our "fake" GNU assembler in AFL_PATH or at the location derived
   from argv[0]. If that fails, abort. */

static void find_as(u8* argv0) {

  u8 *slash, *tmp;

  slash = strrchr(argv0, '/');

  if (slash) {

    u8 *dir;

    *slash = 0;
    dir = ck_strdup(argv0);
    *slash = '/';

    tmp = alloc_printf("%s/armor-as", dir);

    if (!access(tmp, X_OK)) {
      as_path = dir;
      ck_free(tmp);
      return;
    }

    ck_free(tmp);
    ck_free(dir);

  }

  FATAL("Unable to find armor wrapper binary for 'as'. Please set armor_PATH");
 
}

/* Try to find the runtime libraries. If that fails, abort. */

static void find_obj(u8* argv0) {

  u8 *slash, *tmp;

  slash = strrchr(argv0, '/');

  if (slash) {

    u8 *dir;

    *slash = 0;
    dir = ck_strdup(argv0);
    *slash = '/';
    
    tmp = alloc_printf("%s/armor.o", dir);

    if (!access(tmp, R_OK)) {
      obj_path = dir;
      free(tmp);
      return;
    }

    free(tmp);
    free(dir);

  }

  printf("Unable to find 'armor-loop.o' or 'armor.so'.");
  exit(0);

}


/* Copy argv to cc_params, making the necessary edits. */

static void edit_params(u32 argc, char** argv) {

  u8 fortify_set = 0, asan_set = 0;
  u8 *name;

#if defined(__FreeBSD__) && defined(__x86_64__)
  u8 m32_set = 0;
#endif

  cc_params = ck_alloc((argc + 128) * sizeof(u8*));

  name = strrchr(argv[0], '/');
  if (!name) name = argv[0]; else name++;

  if (!strncmp(name, "clang", 5)) {

    clang_mode = 1;

    setenv(CLANG_ENV_VAR, "1", 1);

    if (!strcmp(name, "clang++")) {
      u8* alt_cxx = getenv("ARMOR_CXX");
      cc_params[0] = alt_cxx ? alt_cxx : (u8*)"clang++";
    } else {
      u8* alt_cc = getenv("ARMOR_CC");
      cc_params[0] = alt_cc ? alt_cc : (u8*)"clang";
    }

  } else {

    /* With GCJ and Eclipse installed, you can actually compile Java! The
       instrumentation will work (amazingly). Alas, unhandled exceptions do
       not call abort(), so afl-fuzz would need to be modified to equate
       non-zero exit codes with crash conditions when working with Java
       binaries. Meh. */

#ifdef __APPLE__

    if (!strcmp(name, "g++")) cc_params[0] = getenv("ARMOR_CXX");
    else if (!strcmp(name, "gcj")) cc_params[0] = getenv("ARMOR_GCJ");
    else if (!strcmp(name, "gfortran")) cc_params[0] = getenv("ARMOR_GFORTRAN");
    else cc_params[0] = getenv("ARMOR_CC");

    if (!cc_params[0]) {

      SAYF("\n" cLRD "[-] " cRST
           "On Apple systems, 'gcc' is usually just a wrapper for clang. Please use the\n"
           "    'afl-clang' utility instead of 'afl-gcc'. If you really have GCC installed,\n"
           "    set ARMOR_CC or ARMOR_CXX to specify the correct path to that compiler.\n");

      FATAL("ARMOR_CC or ARMOR_CXX required on MacOS X");

    }

#else

    if (!strcmp(name, "g++")) {
      u8* alt_cxx = getenv("ARMOR_CXX");
      cc_params[0] = alt_cxx ? alt_cxx : (u8*)"g++";
    } else if (!strcmp(name, "gcj")) {
      u8* alt_cc = getenv("ARMOR_GCJ");
      cc_params[0] = alt_cc ? alt_cc : (u8*)"gcj";
    } else if (!strcmp(name, "gfortran")) {
      u8* alt_cc = getenv("ARMOR_CFORTRAN");
      cc_params[0] = alt_cc ? alt_cc : (u8*)"gfortran";
    }
    else {
      u8* alt_cc = getenv("ARMOR_CC");
      cc_params[0] = alt_cc ? alt_cc : (u8*)"gcc";
    }

#endif /* __APPLE__ */

  }

  while (--argc) {
    u8* cur = *(++argv);

    if (!strncmp(cur, "-B", 2)) {

      if (!be_quiet) WARNF("-B is already set, overriding");

      if (!cur[2] && argc > 1) { argc--; argv++; }
      continue;

    }

    if (!strcmp(cur, "-integrated-as")) continue;

    if (!strcmp(cur, "-pipe")) continue;

#if defined(__FreeBSD__) && defined(__x86_64__)
    if (!strcmp(cur, "-m32")) m32_set = 1;
#endif

    cc_params[cc_par_cnt++] = cur;

  }

  cc_params[cc_par_cnt++] = "-B";
  cc_params[cc_par_cnt++] = as_path;

  // cc_params[cc_par_cnt++] = "-L";
  // cc_params[cc_par_cnt++] = as_path;

  // cc_params[cc_par_cnt++] = "-larmor";

  cc_params[cc_par_cnt++] = "-ffixed-x9";

  // cc_params[cc_par_cnt++] = "-fPIC";

  // cc_params[cc_par_cnt++] = alloc_printf("%s/armor.o", as_path);

  cc_params[cc_par_cnt] = NULL;

}


/* Main entry point */

int main(int argc, char** argv) {

  if (isatty(2) && !getenv("ARMOR_QUIET")) {

    SAYF(cCYA "armor-cc " cBRI VERSION cRST " by <yuetai17@nudt.edu.cn>\n");

  } else be_quiet = 1;

  if (argc < 2) {

    SAYF("\n"
         "This is a helper application for afl-fuzz. It serves as a drop-in replacement\n"
         "for gcc or clang, letting you recompile third-party code with the required\n"
         "runtime instrumentation. A common use pattern would be one of the following:\n\n"

         "  CC=%s/armor-gcc ./configure\n"
         "  CXX=%s/armor-g++ ./configure\n\n"

         "You can specify custom next-stage toolchain via AFL_CC, AFL_CXX, and AFL_AS.\n"
         "Setting AFL_HARDEN enables hardening optimizations in the compiled code.\n\n",
         BIN_PATH, BIN_PATH);

    exit(1);

  }

  find_as(argv[0]);
  // find_obj(argv[0]);
  // printf("%s\n", obj_path);
  edit_params(argc, argv);

  // for(u32 i = 0; i < cc_par_cnt; i++)
  //   printf("%s\n", cc_params[i]);

  execvp(cc_params[0], (char**)cc_params);

  FATAL("Oops, failed to execute '%s' - check your PATH", cc_params[0]);

  return 0;

}
