#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <locale.h>
#include <libelf.h>
#include <gelf.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../../plugins.h"

#include "../../dbm.h"
#include "../../elf/elf_loader.h"

//------------------------------------------------------------------------------
// Global Variables
//------------------------------------------------------------------------------
bool is_first_thread;
mambo_ht_t valid_addresses;


//------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------
//Function used to check if the branch address is a valid function address
void check_address(void* addr){
  // fprintf(stderr, "Branch to: %lX\n", addr);
  uintptr_t value;
  int ret = mambo_ht_get(&valid_addresses, (uintptr_t)addr, &value);
  if(ret != 0){
    printf("Encountered invalid branch to %lX\n", addr);
    // exit(EXIT_FAILURE);
  }
}

// When executing a function call check if the branch goes to a valid function
// address
int branch_check_pre_inst_handler(mambo_context *ctx) {
  mambo_branch_type type = mambo_get_branch_type(ctx);
  if(type & BRANCH_CALL){
    // printf("Branch Call\n");
    emit_push(ctx, (1 << reg0));
    mambo_calc_br_target(ctx, reg0);
    emit_safe_fcall(ctx, check_address, 1);
    emit_pop(ctx, (1 << reg0));
  }
}

// Before starting the main thread build a hashtable with all of the valid 
// function addresses 
int brach_check_pre_thread(mambo_context *ctx) {
  int ret;
  if(is_first_thread){
    // Initalize the hashtable
    ret = mambo_ht_init(&valid_addresses, 1000, 0, 70, true);
    assert(ret == 0);

    char *file_name = global_data.argv[1];	// filename
    int fd; 		                            // File Descriptor
    
    if((fd = open(file_name, O_RDWR)) < 0)
    {
      printf("couldnt open %s\n", file_name);
      exit(EXIT_FAILURE);
    }

    Elf *elf = elf_begin(fd, ELF_C_READ, NULL);
    // Parse the ELF file
    if (elf != NULL) {
      GElf_Shdr shdr;
      GElf_Sym sym;
      Elf_Scn *scn = NULL;

      while((scn = elf_nextscn(elf, scn)) != NULL) {
        gelf_getshdr(scn, &shdr);
        // Stop at the Symbol table
        if(shdr.sh_type == SHT_SYMTAB) {

          Elf_Data *edata = elf_getdata(scn, NULL);
          assert(edata != NULL);

          int sym_count = shdr.sh_size / shdr.sh_entsize;
          // Insert all function addresses to the symbol table
          for (int i = 0; i < sym_count; i++) {
            gelf_getsym(edata, i, &sym);
            if(ELF32_ST_TYPE(sym.st_info) == STT_FUNC){
              uintptr_t value;
              ret = mambo_ht_get(&valid_addresses, sym.st_value, &value);
              if(ret != 0){
                ret = mambo_ht_add(&valid_addresses, sym.st_value, 1);
                assert(ret == 0);
              }
            }
          }
        } // shdr.sh_type == SHT_SYMTAB
      } // while scn iterator
    }
    is_first_thread = false;
  }
}

int branch_check_exit_handler(mambo_context *ctx){
  uintptr_t value;

  for(int index = 0; index <= 5242880; index++){
    int ret = mambo_ht_get(&valid_addresses, index, &value);
    if(ret == 0);
      // printf("%08X  %d\n", index, value);
  }
}

__attribute__((constructor)) void branch_check_init_plugin() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  is_first_thread = true;

  printf("\n--- MAMBO branch check ---\n\n");

  int ret = mambo_register_pre_inst_cb(ctx, &branch_check_pre_inst_handler);
  assert(ret == MAMBO_SUCCESS);
  
  ret = mambo_register_pre_thread_cb(ctx, &brach_check_pre_thread);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_register_exit_cb(ctx, &branch_check_exit_handler);
  assert(ret == MAMBO_SUCCESS);
}