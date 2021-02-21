#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include "../plugins.h"

__attribute__((constructor)) void branch_count_init_plugin() {
  mambo_context *ctx = mambo_register_plugin();
  assert(ctx != NULL);
}
