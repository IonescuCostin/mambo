/*
  This file is part of MAMBO, a low-overhead dynamic binary modification tool:
      https://github.com/beehive-lab/mambo

  Copyright 2017-2019 The University of Manchester

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <libelf.h>
#include <stdlib.h>
#include <stdio.h>
#include <gelf.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../dbm.h"
#include "elf_loader.h"

void function_watch_lock_funcs(watched_functions_t *self) {
  int ret = pthread_mutex_lock(&self->funcs_lock);
  assert(ret == 0);
}

void function_watch_lock_funcps(watched_functions_t *self) {
  int ret = pthread_mutex_lock(&self->funcps_lock);
  assert(ret == 0);
}

void function_watch_unlock_funcs(watched_functions_t *self) {
  int ret = pthread_mutex_unlock(&self->funcs_lock);
  assert(ret == 0);
}

void function_watch_unlock_funcps(watched_functions_t *self) {
  int ret = pthread_mutex_unlock(&self->funcps_lock);
  assert(ret == 0);
}

int function_watch_init(watched_functions_t *self) {
  int ret = pthread_mutex_init(&self->funcs_lock, NULL);
  assert(ret == 0);
  ret = pthread_mutex_init(&self->funcps_lock, NULL);
  assert(ret == 0);
}

int function_watch_search(watched_functions_t *self, char *name) {
  for (int i = 0; i < self->func_count; i++) {
    if (strcmp(name, self->funcs[i].name) == 0) {
      return 1;
    }
  }
  return 0;
}

int function_watch_add(watched_functions_t *self, char *name, int plugin_id,
                       mambo_callback pre_callback, mambo_callback post_callback) {
  function_watch_lock_funcs(self);

  if (function_watch_search(self, name) > 0) return -101;

  int idx = self->func_count++;
  if (idx >= MAX_WATCHED_FUNCS) return -102;

  self->funcs[idx].name = name;
  self->funcs[idx].plugin_id = plugin_id;
  self->funcs[idx].pre_callback = pre_callback;
  self->funcs[idx].post_callback = post_callback;

  function_watch_unlock_funcs(self);

  return 0;
}

/* Memory barriers used in function modifying funcps because the
   mutex doesn't protect from reading */
int function_watch_addp(watched_functions_t *self, watched_func_t *func, void *addr) {
  function_watch_lock_funcps(self);

  int idx = self->funcp_count;
  if (idx >= MAX_WATCHED_FUNC_PTRS) return -2;

  self->funcps[idx].func = func;
  self->funcps[idx].addr = addr;
  asm volatile("DMB SY" ::: "memory");
  self->funcp_count++;

  function_watch_unlock_funcps(self);

  return 0;
}

int function_watch_try_addp(watched_functions_t *self, char *name, void *addr) {
  function_watch_lock_funcs(self);

  for (int i = 0; i < self->func_count; i++) {
    if (strcmp(name, self->funcs[i].name) == 0) {
      function_watch_addp(self, &self->funcs[i], addr);
    }
  }

  function_watch_unlock_funcs(self);
}

int function_watch_delete_addp(watched_functions_t *self, int i) {
  int last = self->funcp_count-1;
  if (i > last) {
    return -1;
  }

  if (i < last) {
    self->funcps[i].addr = NULL;
    asm volatile("DMB SY" ::: "memory");

    self->funcps[i].func = self->funcps[last].func;
    asm volatile("DMB SY" ::: "memory");

    self->funcps[i].addr = self->funcps[last].addr;
    asm volatile("DMB SY" ::: "memory");
  }

  self->funcp_count = last;
  asm volatile("DMB SY" ::: "memory");

  return 0;
}

int function_watch_addp_invalidate(watched_functions_t *self, void *addr, size_t size) {
  function_watch_lock_funcps(self);

  for (int i = 0; i < self->funcp_count; i++) {
    if (self->funcps[i].addr >= addr && self->funcps[i].addr < (addr + size)) {
      function_watch_delete_addp(self, i);
    }
  }
  function_watch_unlock_funcps(self);
}

int function_watch_parse_elf(watched_functions_t *self, Elf *elf, void *base_addr) {
  Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  GElf_Sym sym;
  ELF_EHDR *ehdr = ELF_GETEHDR(elf);
  if (ehdr == NULL) {
    printf("Error reading the ELF executable header: %s\n", elf_errmsg(-1));
    return -1;
  }
  if (ehdr->e_type == ET_EXEC) {
    base_addr = NULL;
  }

  while((scn = elf_nextscn(elf, scn)) != NULL) {
    gelf_getshdr(scn, &shdr);
    if(shdr.sh_type == SHT_SYMTAB || shdr.sh_type == SHT_DYNSYM) {
      Elf_Data *edata = elf_getdata(scn, NULL);
      assert(edata != NULL);
      int sym_count = shdr.sh_size / shdr.sh_entsize;

      for (int i = 0; i < sym_count; i++) {
        gelf_getsym(edata, i, &sym);
        if (sym.st_value != 0 && ELF32_ST_TYPE(sym.st_info) == STT_FUNC) {
          char *sym_name = elf_strptr(elf, shdr.sh_link, sym.st_name);
          assert(sym_name != NULL);
          function_watch_try_addp(self, sym_name, base_addr + sym.st_value);
        }
      }
    } // shdr.sh_type == SHT_SYMTAB
  } // while scn iterator
  return 0;
}
