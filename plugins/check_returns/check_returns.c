#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <locale.h>
#include <libelf.h>
#include <gelf.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>

#include "../../plugins.h"
#include "../../dbm.h"
#include "../../elf/elf_loader.h"

//------------------------------------------------------------------------------
// Global Variables
//------------------------------------------------------------------------------
bool is_first_thread;
mambo_ht_t valid_addresses;
int count;


//------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------
//Function used to check if the branch address is a valid function address
void check_address(void* addr){

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
    emit_push(ctx, (1 << reg0));
    emit_mov(ctx, reg0, lr);
    emit_safe_fcall(ctx, check_address, 1);
    emit_pop(ctx, (1 << reg0));
  }
}

// Before starting the main thread build a hashtable with all of the valid 
// function addresses from the output of obj_dump
int check_returns_pre_thread(mambo_context *ctx) {
  if(is_first_thread){
    // Initalize the hashtable
    int ret = mambo_ht_init(&valid_addresses, 10000, 0, 70, true);
    assert(ret == 0);
    
    // Dissasamble the binary
    char *file_name = global_data.argv[1];
    char cmd[strlen(file_name) + 30];
    strcpy(cmd, "objdump --disassemble-all ");
    strcat(cmd, file_name);

    FILE *objdump_out;
    char buffer[1001];

    // Open dissasembly output for reading
    objdump_out = popen(cmd, "r");
    if (objdump_out == NULL) {
      printf("Failed to run dissasamble\n" );
      exit(EXIT_FAILURE);
    }

    // Parse the output and scan for BL or BLR instructions
    while (fgets(buffer, sizeof(buffer), objdump_out) != NULL) {
      if(strstr(buffer, "\tbl\t") != NULL || strstr(buffer, "\tblr\t") != NULL){
        // Extract the address
        char *token = strtok(buffer, " \t");
        token[strlen(token) - 1] = '\0';
        uint32_t ret_addr = (int)strtol(token, NULL, 16) + 4;
        
        uintptr_t value;      
        ret = mambo_ht_get(&valid_addresses, ret_addr, &value);
        if(ret != 0){
          ret = mambo_ht_add(&valid_addresses, ret_addr, 1);
          assert(ret == 0);
        }
      }
    } // while


    // Close
    pclose(objdump_out);
    is_first_thread = false;
  }
} // check_returns_pre_thread


// Virtual memory operation callbacks
int check_returns_vm_op_handler(mambo_context *ctx) {
  vm_op_t type = mambo_get_vm_op(ctx);

  int addr = mambo_get_vm_addr(ctx);
  int size = mambo_get_vm_size(ctx);
  int filedes = mambo_get_vm_filedes(ctx);
  int prot = mambo_get_vm_prot(ctx);

  if((type == VM_MAP) && (filedes > 0) && ((prot & PROT_EXEC) == PROT_EXEC)){
    printf("%d \n", filedes);
    printf("%d \n", prot&PROT_EXEC);
    printf("%X \n", mambo_get_vm_flags(ctx));
    printf("addr: %X\nsize: %d\n\n", addr, size);
  }
}

// Exit callback
int check_returns_exit_handler(mambo_context *ctx) {
  printf("VM_OP counter %d\n", count);
}

__attribute__((constructor)) void check_returns_init_plugin() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  is_first_thread = true;

  printf("\n--- MAMBO check returns ---\n\n");

  int ret = mambo_register_pre_inst_cb(ctx, &check_returns_pre_inst_handler);
  assert(ret == MAMBO_SUCCESS);

  count = 0;
  ret = mambo_register_vm_op_cb(ctx, &check_returns_vm_op_handler);
  assert(ret == MAMBO_SUCCESS);
  
  ret = mambo_register_pre_thread_cb(ctx, &check_returns_pre_thread);
  assert(ret == MAMBO_SUCCESS);

  ret = mambo_register_exit_cb(ctx, &check_returns_exit_handler);
  assert(ret == MAMBO_SUCCESS);
}