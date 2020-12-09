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
  // fprintf(stderr, "Return to: %lX\n", addr);
  uintptr_t value;
  int ret = mambo_ht_get(&valid_addresses, (uintptr_t)addr, &value);
  if(ret != 0){;
    fprintf(stderr, "Encountered invalid return to %lX\n", addr);
    exit(EXIT_FAILURE);
  }
}

// When executing a return instruction check if the returna address
// is valid
int check_returns_pre_inst_handler(mambo_context *ctx) {
  mambo_branch_type type = mambo_get_branch_type(ctx);

  if(type & BRANCH_RETURN){
    // fprintf(stderr, "Branch from %lX", mambo_get_source_addr(ctx));
    emit_push(ctx, (1 << reg0));
    emit_mov(ctx, reg0, lr);
    emit_safe_fcall(ctx, check_address, 1);
    emit_pop(ctx, (1 << reg0));
  }
}

// Before starting the main thread build a hashtable with all of the valid 
// function addresses from the output of obj_dump
int check_returns_pre_thread(mambo_context *ctx) {
  int ret;
  if(is_first_thread){
    // Initalize the hashtable
    ret = mambo_ht_init(&valid_addresses, 10000, 0, 70, true);
    assert(ret == 0);

    // Load valid return addresses from a file
    FILE* fd = fopen ("/home/ubuntu/mambo/plugins/check_returns/return_addresses.txt", "r");
    
    if(fd == NULL)
    {
      printf("couldnt open %s\n", "return_addresses.txt");
      exit(EXIT_FAILURE);
    }
 
    // Parse the file and insert all valid return addresses into a hash table
    int i;
    while (!feof (fd))
    {
      fscanf (fd, "%d", &i);
      uintptr_t value;
      
      ret = mambo_ht_get(&valid_addresses, i, &value);
      if(ret != 0){
        ret = mambo_ht_add(&valid_addresses, i, 1);
        assert(ret == 0);
      }
      // printf ("%d\n", i);     
    }

    fclose (fd);        
    is_first_thread = false;
  }
}

__attribute__((constructor)) void check_returns_init_plugin() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  is_first_thread = true;

  printf("\n--- MAMBO check returns ---\n\n");

  int ret = mambo_register_pre_inst_cb(ctx, &check_returns_pre_inst_handler);
  assert(ret == MAMBO_SUCCESS);
  
  ret = mambo_register_pre_thread_cb(ctx, &check_returns_pre_thread);
  assert(ret == MAMBO_SUCCESS);
}