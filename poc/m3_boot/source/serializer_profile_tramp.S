.syntax unified
.arm
.section .text.m3_serializer_tramp,"ax",%progbits
.macro TRAMP n, mask, frame
.align 2
.global m3_serializer_tramp_\n
.type m3_serializer_tramp_\n,%function
m3_serializer_tramp_\n:
 .word \mask
 add fp,sp,#\frame
 ldr pc,[pc,#-4]
 .word 0
.size m3_serializer_tramp_\n,.-m3_serializer_tramp_\n
.endm
TRAMP 0,0xe92d4ff0,28
TRAMP 1,0xe92d4ff0,28
TRAMP 2,0xe92d4bf0,24
TRAMP 3,0xe92d4ff0,28
TRAMP 4,0xe92d4df0,24
TRAMP 5,0xe92d4df0,24
.section .note.GNU-stack,"",%progbits
