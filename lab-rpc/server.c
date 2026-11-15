/**
 * Server binary.
 */

#include "kv_store.h"
#include <glib.h>
#include <memory.h>
#include <netinet/in.h>
#include <rpc/pmap_clnt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef SIG_PF
#define SIG_PF void (*)(int)
#endif

static GHashTable *store;

extern void kvstore_1(struct svc_req *, SVCXPRT *);

/* Set up and run RPC server. */
int main(int argc, char **argv) {
  register SVCXPRT *transp;

  pmap_unset(KVSTORE, KVSTORE_V1);

  transp = svcudp_create(RPC_ANYSOCK);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create udp service.");
    exit(1);
  }
  if (!svc_register(transp, KVSTORE, KVSTORE_V1, kvstore_1, IPPROTO_UDP)) {
    fprintf(stderr, "%s", "unable to register (KVSTORE, KVSTORE_V1, udp).");
    exit(1);
  }

  transp = svctcp_create(RPC_ANYSOCK, 0, 0);
  if (transp == NULL) {
    fprintf(stderr, "%s", "cannot create tcp service.");
    exit(1);
  }
  if (!svc_register(transp, KVSTORE, KVSTORE_V1, kvstore_1, IPPROTO_TCP)) {
    fprintf(stderr, "%s", "unable to register (KVSTORE, KVSTORE_V1, tcp).");
    exit(1);
  }

  store = g_hash_table_new_full(g_bytes_hash, g_bytes_equal,
                                (GDestroyNotify)g_bytes_unref, (GDestroyNotify)g_bytes_unref);

  svc_run();
  fprintf(stderr, "%s", "svc_run returned");
  exit(1);
  /* NOTREACHED */
}

/* Example server-side RPC stub. */
int *example_1_svc(int *argp, struct svc_req *rqstp) {
  static int result;

  result = *argp + 1;

  return &result;
}

char **echo_1_svc(char **argp, struct svc_req *rqstp) {
  static char *result;
  (void)rqstp;
  free(result); result = strdup(*argp);
  return result ? &result : NULL;
}
void *put_1_svc(put_request *argp, struct svc_req *rqstp) {
  static char result;
  (void)rqstp;
  GBytes *key = g_bytes_new(argp->key.buf_val, argp->key.buf_len);
  GBytes *value = g_bytes_new(argp->value.buf_val, argp->value.buf_len);
  g_hash_table_replace(store, key, value);
  return &result;
}
buf *get_1_svc(buf *argp, struct svc_req *rqstp) {
  static buf result;
  (void)rqstp;
  free(result.buf_val); result.buf_val = NULL; result.buf_len = 0;
  GBytes *key = g_bytes_new(argp->buf_val, argp->buf_len);
  GBytes *value = g_hash_table_lookup(store, key);
  g_bytes_unref(key);
  if (value) {
    gsize length;
    const void *data = g_bytes_get_data(value, &length);
    if (length) {
      result.buf_val = malloc(length);
      if (!result.buf_val) return NULL;
      memcpy(result.buf_val, data, length); result.buf_len = length;
    }
  }
  return &result;
}
