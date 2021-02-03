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
// Parameters
//------------------------------------------------------------------------------

#define NUM_INSTRUCTIONS 9

//------------------------------------------------------------------------------
// Global Variables
//------------------------------------------------------------------------------
queue* inst_q     = NULL;
queue* ht_cleanup = NULL;

mambo_ht_t switch_ht;

int switch_inst[NUM_INSTRUCTIONS] = {A64_ADD_SUB_IMMED, A64_B_COND, A64_LDP_STP, A64_ADR, A64_ADD_SUB_IMMED, A64_ADD_SUB_IMMED, A64_LDR_STR_REG, A64_ADR, A64_ADD_SUB_EXT_REG};

//------------------------------------------------------------------------------
// Functions
//------------------------------------------------------------------------------

void build_hash_tables(mambo_context *ctx){
  //Iterate through the instruction sequence to extract information
  q_node* iter = inst_q->head;
  uint32_t sf, op, s, shift, imm12, rn, rd, immlo, immhi;

  a64_ADD_SUB_immed_decode_fields(iter->addr, &sf, &op, &s, &shift, &imm12, &rn, &rd);
  uint32_t num_cases = imm12;

  for(int index = 0; index < 3; index++)
    iter = iter->next;

  a64_ADR_decode_fields (iter->addr, &op, &immlo, &immhi, &rd);
  uint64_t table_base = (immhi<<2) + immlo;
  table_base = (table_base*4096) + (uint32_t)iter->addr;
  table_base &= ~0xFFF;
  iter = iter->next;
  a64_ADD_SUB_immed_decode_fields(iter->addr, &sf, &op, &s, &shift, &imm12, &rn, &rd);
  table_base += imm12;

  for(int index = 0; index < 3; index++)
    iter = iter->next;

  a64_ADR_decode_fields (iter->addr, &op, &immlo, &immhi, &rd);
  uint64_t case_base = (immhi<<2) + immlo;
  case_base = case_base + (uint32_t)iter->addr;

  // Add all valid branch addresses to a hash table
  mambo_ht_t* addr_ht = malloc(sizeof(mambo_ht_t));
  int ret = mambo_ht_init(addr_ht, 20, 0, 70, true);
  enqueue(ht_cleanup, 0, addr_ht);
  for(int index = 0; index < num_cases; index++){
    uint8_t *tbl_pointer = (void *) table_base + index;
    uintptr_t a = *tbl_pointer<<2;
    // printf("%X\n", *tbl_pointer);
    ret = mambo_ht_add(addr_ht, (uintptr_t)(case_base + a), 1);
    assert(ret == 0);
  }
  printf("ADDR %lX\n", addr_ht);
  ret = mambo_ht_add(&switch_ht, (uintptr_t)mambo_get_source_addr(ctx), (uintptr_t)addr_ht);
  assert(ret == 0);

  printf("Jump table size: %u\n",   num_cases);
  printf("Jump table base: %X\n",   table_base);
  printf("Switch cases base: %X\n", case_base);
}// build_hash_tables

void check_br_address(void* b_addr, mambo_context *ctx){
  printf("checking br\n");
  mambo_ht_t* addr_ht;
  int ret = mambo_ht_get(&switch_ht, (uintptr_t)mambo_get_source_addr(ctx), &addr_ht);
  if(ret != 0){
    printf("Switch statement at %lX not found\n", mambo_get_source_addr(ctx));
    exit(EXIT_FAILURE);
  }else{
    uintptr_t ret_value;
    ret = mambo_ht_get(addr_ht, b_addr, &ret_value);
    if(ret != 0){
      printf("%lX not a vaild branch address!\n", b_addr);
      exit(EXIT_FAILURE);
    }
  }
}// check_br_address


int check_switch_pre_inst_handler(mambo_context *ctx) {

  // Keep track of the previous 8 instructions
  if(inst_q->size < NUM_INSTRUCTIONS)
    enqueue(inst_q, mambo_get_inst(ctx), mambo_get_source_addr(ctx));
  else{
    // Check if the insturctions match the pattern for a switch statement
    q_node* iter = inst_q->head;
    bool switch_match = true;
    for(int index = 0; index < NUM_INSTRUCTIONS; index++){
      if(switch_inst[index] != iter->data)
        switch_match = false;
      iter = iter->next;
    }
    //Check if the current insturction is a branch
    if(ctx->code.inst != A64_BR)
      switch_match = false;

    //If a switch statement has been detected
    if(switch_match == true){
      //Check if the same statement has been executed before
      uintptr_t ret_value = 0;
      mambo_ht_t* ret_ht; 
      int ret = mambo_ht_get(&switch_ht, (uintptr_t)mambo_get_source_addr(ctx), &ret_value);

      //If not build the address hash tables
      if(ret != 0){
        build_hash_tables(ctx);
        printf("New Switch statement found at: %X\n", mambo_get_source_addr(ctx));
      }else{
        printf("prievously executed Switch statement at: %X\n", mambo_get_source_addr(ctx));
      }

      //Check the validity of the branch address
      emit_push(ctx, (1 << reg0) | (1 << reg1));
      emit_set_reg_ptr(ctx, reg1, ctx);
      // mambo_calc_br_target(ctx, reg1);
      emit_safe_fcall(ctx, check_br_address, 2);
      emit_pop(ctx, (1 << reg0) | (1 << reg1));

      printf("\n");
      // print_queue(inst_q);
    }// switch match
    dequeue(inst_q);
    enqueue(inst_q, mambo_get_inst(ctx), mambo_get_source_addr(ctx));      
  }
}// check_switch_pre_inst_handler

// Cleanup
int check_switch_exit_handler(mambo_context *ctx) {
  // Free the memory taken up by the individual hash tables for each address.
  q_node* iter = ht_cleanup->head;
  while(iter != NULL){
    free(iter->addr);
    iter = iter->next;
  }

  // Free the memory used by queues
  ht_cleanup = free_queue(ht_cleanup);
  inst_q     = free_queue(inst_q);
}// check_switch_exit_handler



__attribute__((constructor)) void check_returns_init_plugin() {

  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);

  printf("\n--- MAMBO validate switch ---\n\n");

  inst_q = init_queue();

  int ret = mambo_register_pre_inst_cb(ctx, &check_switch_pre_inst_handler);
  assert(ret == MAMBO_SUCCESS);
  
  ret = mambo_ht_init(&switch_ht, 10, 0, 70, true);
  assert(ret == 0);
  ht_cleanup = init_queue();

  // ret = mambo_register_vm_op_cb(ctx, &check_returns_vm_op_handler);
  // assert(ret == MAMBO_SUCCESS);
  
  // ret = mambo_register_pre_thread_cb(ctx, &check_returns_pre_thread);
  // assert(ret == MAMBO_SUCCESS);

  ret = mambo_register_exit_cb(ctx, &check_switch_exit_handler);
  assert(ret == MAMBO_SUCCESS);
}