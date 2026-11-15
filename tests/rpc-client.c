#include "kv_store_client.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
  char key_data[] = {'a',0,'b', (char)255};
  char value_data[] = {0,1,2,0,(char)255};
  buf key = {sizeof key_data, key_data}, value = {sizeof value_data, value_data};
  for (int i=0;i<500;++i) {
    char message[64]; snprintf(message,sizeof message,"echo-%d",i);
    char *reply=echo(message); assert(!strcmp(reply,message)); free(reply);
    value_data[1]=(char)i; put(key,value);
    buf *got=get(key);assert(got && got->buf_len==sizeof value_data);
    assert(!memcmp(got->buf_val,value_data,sizeof value_data));
    xdr_free((xdrproc_t)xdr_buf,(char *)got);
  }
  buf empty={0,NULL};put(empty,value);buf *got=get(empty);
  assert(got->buf_len==sizeof value_data);xdr_free((xdrproc_t)xdr_buf,(char *)got);
  put(key,empty);got=get(key);assert(got->buf_len==0);xdr_free((xdrproc_t)xdr_buf,(char *)got);
  char missing_data[]="absent";buf missing={sizeof missing_data,missing_data};got=get(missing);
  assert(got->buf_len==0);xdr_free((xdrproc_t)xdr_buf,(char *)got);
  char *reply=echo("");assert(reply && !*reply);free(reply);
  puts("500 binary overwrite/read/echo cycles and empty/missing cases passed");
}
