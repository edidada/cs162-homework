/**
 * Client binary.
 */

#include "kv_store_client.h"

#define HOST "localhost"

CLIENT* clnt_connect(char* host) {
  CLIENT* clnt = clnt_create(host, KVSTORE, KVSTORE_V1, "udp");
  if (clnt == NULL) {
    clnt_pcreateerror(host);
    exit(1);
  }
  return clnt;
}

int example(int input) {
  CLIENT *clnt = clnt_connect(HOST);

  int ret;
  int *result;

  result = example_1(&input, clnt);
  if (result == (int *)NULL) {
    clnt_perror(clnt, "call failed");
    exit(1);
  }
  ret = *result;
  xdr_free((xdrproc_t)xdr_int, (char *)result);

  clnt_destroy(clnt);
  
  return ret;
}

char* echo(char* input) {
  CLIENT *clnt = clnt_connect(HOST);

  char* ret;

  char **result = echo_1(&input, clnt);
  if (!result) { clnt_perror(clnt, "echo failed"); exit(1); }
  ret = strdup(*result);
  xdr_free((xdrproc_t)xdr_wrapstring, (char *)result);
  if (!ret) { perror("strdup"); exit(1); }

  clnt_destroy(clnt);
  
  return ret;
}

void put(buf key, buf value) {
  CLIENT *clnt = clnt_connect(HOST);

  put_request request = {key, value};
  if (!put_1(&request, clnt)) { clnt_perror(clnt, "put failed"); exit(1); }

  clnt_destroy(clnt);
}

buf* get(buf key) {
  CLIENT *clnt = clnt_connect(HOST);

  buf* ret;

  /* rpcgen owns its static result descriptor; transfer payload ownership
     to this stable wrapper descriptor, which callers release with xdr_free. */
  static buf owned;
  buf *result = get_1(&key, clnt);
  if (!result) { clnt_perror(clnt, "get failed"); exit(1); }
  xdr_free((xdrproc_t)xdr_buf, (char *)&owned);
  owned = *result;
  result->buf_len = 0; result->buf_val = NULL;
  ret = &owned;

  clnt_destroy(clnt);
  
  return ret;
}
