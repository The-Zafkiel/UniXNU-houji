/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <string.h>
#include "HoujiRootFs26.h"
/* Simulate VFS registration order and callback outcomes, not filesystem I/O. */
static unsigned mock_calls,hfs_calls;
static int root(int native_root,int hfs_error) {
 const char *order[]={"devfs","apfs","mockfs","hfs"};
 for(unsigned i=0;i<sizeof(order)/sizeof(order[0]);i++) {
  if(!hjrfs26_candidate(native_root,order[i]))continue;
  if(!strcmp(order[i],"mockfs")){++mock_calls;return 0;}
  if(!strcmp(order[i],"hfs")){++hfs_calls;if(!hfs_error)return 0;}
 }
 return 1;
}
int main(void) {
 assert(hjrfs26_candidate(1,"hfs"));
 for(const char **name=(const char*[]){"", "h", "hf", "hfsx", "HFS", "apfs", "mockfs",0};*name;name++)
  assert(!hjrfs26_candidate(1,*name));
 assert(!hjrfs26_candidate(1,0));
 assert(!root(0,0) && mock_calls==1 && !hfs_calls);
 mock_calls=hfs_calls=0;assert(!root(1,0) && !mock_calls && hfs_calls==1);
 mock_calls=hfs_calls=0;assert(root(1,22) && !mock_calls && hfs_calls==1);
}
