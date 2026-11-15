#include "kv_store_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  CLIENT *tcp=clnt_create("localhost",KVSTORE,KVSTORE_V1,"tcp");assert(tcp);
  char key_data[64];snprintf(key_data,sizeof key_data,"process-%ld",(long)getpid());
  buf key={strlen(key_data),key_data};
  size_t sizes[]={0,1,4096,65536,1048576};
  for(unsigned j=0;j<sizeof sizes/sizeof *sizes;++j) {
    size_t n=sizes[j];char *data=malloc(n?n:1);assert(data);
    for(size_t i=0;i<n;++i)data[i]=(char)(i*31+j);
    put_request request={key,{n,data}};assert(put_1(&request,tcp));
    buf *reply=get_1(&key,tcp);assert(reply && reply->buf_len==n);
    if(n)assert(!memcmp(reply->buf_val,data,n));
    xdr_free((xdrproc_t)xdr_buf,(char *)reply);free(data);
  }
  clnt_destroy(tcp);puts("TCP sizes 0/1/4096/65536/1048576 passed");return 0;
}
