
.macro  push, xreg1, xreg2
        stp     \xreg1, \xreg2, [sp, #-16]!
.endm

.macro  pop, xreg1, xreg2
        ldp     \xreg1, \xreg2, [sp], #16
.endm

.balign 4

.global hyp_print2
hyp_print2: 

   push x0, x1
   push x2, x3
   push x4, x5
   push x6, x7
   push x8, x9
   push x30, x29

   mov x29, x0
   ldr x1 ,[x29, 128] 
   ldr x2 ,[x29, 136]
   ldr x3 ,[x29, 144]
   ldr x4 ,[x29, 152]
   ldr x5 ,[x29, 160]
   ldr x6 ,[x29, 168]
   ldr x7 ,[x29, 176]
#
# Now the floating points
   ldr x9,  [x29, 184]
   fmov d0, x9
   ldr x9,  [x29, 192]
   fmov d1, x9
   ldr x9,  [x29, 200]
   fmov d2, x9
   ldr x9,  [x29, 208]
   fmov d3, x9
   ldr x9,  [x29, 216]
   fmov d4, x9
   ldr x9,  [x29, 224]
   fmov d5, x9
   ldr x9,  [x29, 232]
   fmov d6, x9
   ldr x9,  [x29, 240]
   fmov d7, x9

   bl printf

   pop x30, x29
   pop x8, x9
   pop x6, x7
   pop x4, x5
   pop x2, x3
   pop x0, x1

   ret
