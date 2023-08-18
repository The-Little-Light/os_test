#include "stdio.h"
int main(void) {
printf("prog_no_arg from disk\n");
while(1);
return 0;
}
// sreg
// lb 0x0804900
// lb 0xc000e912
// info tab
// xp 物理 x线性
// u/20 0x8049000
/*

push 0x00000020           ; 6a20
<bochs:16> 
Next at t=879268446
(0) [0x00000000225b] 0008:c000225b (unk. ctxt): call dword ptr ds:0xc0015f80 ;


*/