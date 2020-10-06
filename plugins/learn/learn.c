#ifdef PLUGINS_NEW

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <locale.h>
#include "../../plugins.h"


int learn_exit_handler(mambo_context *ctx) {
  printf("\n-- MAMBO EXIT --\n\n");
}

int learn_syscall_handler(mambo_context *ctx){
	long unsigned int syscall_num;
	mambo_syscall_get_no(ctx, &syscall_num);
	if(syscall_num == 214)
		printf("\n-- MAMBO FIRST SYSCALL --\n");
}

__attribute__((constructor)) void learn_init_plugin() {
	mambo_context *ctx = mambo_register_plugin();
	assert(ctx != NULL);
	
	mambo_register_pre_syscall_cb(ctx, &learn_syscall_handler);

	mambo_register_exit_cb(ctx, &learn_exit_handler);
}
#endif