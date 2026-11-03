.syntax unified
.arm
.section .text.m3_table_tramp,"ax",%progbits
.align 2
.global m3_tramp_table_invoke
.type m3_tramp_table_invoke,%function
m3_tramp_table_invoke:
  ldr ip,[r0,#12]
  ldrh ip,[ip,#176]
  ldr pc,[pc,#-4]
  .word 0
.size m3_tramp_table_invoke,.-m3_tramp_table_invoke
.section .note.GNU-stack,"",%progbits
