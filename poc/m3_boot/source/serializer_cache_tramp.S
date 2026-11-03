.syntax unified
.arm
.section .text.m3_attribute_cache_tramp,"ax",%progbits
.align 2
.global m3_attribute_cache_tramp
.type m3_attribute_cache_tramp,%function
m3_attribute_cache_tramp:
 .word 0xe92d4ff0
 add fp,sp,#28
 ldr pc,[pc,#-4]
 .word 0
.size m3_attribute_cache_tramp,.-m3_attribute_cache_tramp
.section .note.GNU-stack,"",%progbits
