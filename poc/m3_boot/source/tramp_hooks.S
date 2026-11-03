/* tramp_hooks.S -- permanent ARM trampolines for the InitDataPersistent call
 * targets. The first two instructions of both targets are
 * "push {r4,r5,r6,r7,fp,lr}" and "add fp,sp,#16" (verified on
 * libil2cpp.so 0x2a4f928 / 0x2a3aa10); the last word is patched at run time
 * with target+8. 0x2a4f928 is the method body of UnityEngine.Resources.Load;
 * 0x2a3aa10 is the second call target (not yet named). Keeping the entry
 * hooked avoids restoring/re-patching code while IL2CPP is starting up,
 * which hangs under yuzu. MIT. */
.syntax unified
.arm
.section .text.m3_tramp,"ax",%progbits
.align 2
.global m3_tramp_h1
.type m3_tramp_h1, %function
m3_tramp_h1:
  .word 0xe92d48f0          /* push {r4,r5,r6,r7,fp,lr} */
  .word 0xe28db010          /* add fp, sp, #16 */
  ldr pc, [pc, #-4]
  .word 0                   /* patched: helper1 + 8 */
.size m3_tramp_h1, .-m3_tramp_h1

.global m3_tramp_h2
.type m3_tramp_h2, %function
m3_tramp_h2:
  .word 0xe92d48f0          /* push {r4,r5,r6,r7,fp,lr} */
  .word 0xe28db010          /* add fp, sp, #16 */
  ldr pc, [pc, #-4]
  .word 0                   /* patched: helper2 + 8 */
.size m3_tramp_h2, .-m3_tramp_h2

.global m3_tramp_nullchk
.type m3_tramp_nullchk, %function
m3_tramp_nullchk:
  .word 0xe3a00001          /* mov r0, #1 */
  .word 0xe3510000          /* cmp r1, #0 */
  ldr pc, [pc, #-4]
  .word 0                   /* patched: null-check helper + 8 */
.size m3_tramp_nullchk, .-m3_tramp_nullchk

.global m3_tramp_persistent
.type m3_tramp_persistent, %function
m3_tramp_persistent:
  .word 0xe92d4c70          /* push {r4,r5,r6,sl,fp,lr} */
  .word 0xe28db010          /* add fp, sp, #16 */
  ldr pc, [pc, #-4]
  .word 0                   /* patched: MonoDataPersistent.Init + 8 */
.size m3_tramp_persistent, .-m3_tramp_persistent

.global m3_tramp_zip
.type m3_tramp_zip, %function
m3_tramp_zip:
  .word 0xe92d4df0          /* push {r4,r5,r6,r7,r8,sl,fp,lr} */
  .word 0xe28db018          /* add fp, sp, #24 */
  ldr pc, [pc, #-4]
  .word 0                   /* patched: ZipConstants.ConvertToString + 8 */
.size m3_tramp_zip, .-m3_tramp_zip

.global m3_tramp_account
.type m3_tramp_account, %function
m3_tramp_account:
  .word 0xe92d4c70          /* push {r4,r5,r6,sl,fp,lr} */
  .word 0xe28db010          /* add fp, sp, #16 */
  ldr pc, [pc, #-4]
  .word 0                   /* patched: GeneralLocalDataItem.get_Account + 8 */
.size m3_tramp_account, .-m3_tramp_account

.global m3_tramp_gc_alloc
.type m3_tramp_gc_alloc, %function
m3_tramp_gc_alloc:
  .word 0xe92d47f0          /* push {r4,r5,r6,r7,r8,r9,r10,lr} */
  .word 0xe1a04000          /* mov r4,r0 */
  ldr pc, [pc, #-4]
  .word 0                   /* GC_generic_malloc + 8 */
.size m3_tramp_gc_alloc, .-m3_tramp_gc_alloc

.global m3_tramp_camera
.type m3_tramp_camera, %function
m3_tramp_camera:
  .word 0xe92d4ff0
  .word 0xe28db01c
  ldr pc, [pc, #-4]
  .word 0                   /* MonoMainCamera.LateUpdate + 8 */
.size m3_tramp_camera, .-m3_tramp_camera

.global m3_tramp_resource_preload
.type m3_tramp_resource_preload, %function
m3_tramp_resource_preload:
  push {r4, r5, r6, lr}
  mov r6, r1
  ldr pc, [pc, #-4]
.global m3_tramp_resource_preload_resume
m3_tramp_resource_preload_resume:
  .word 0
.size m3_tramp_resource_preload, .-m3_tramp_resource_preload

/* Mid-function diagnostic: preserve every GPR, flags and all VFP registers. */
.global m3_trace_resource_slot
.type m3_trace_resource_slot, %function
m3_trace_resource_slot:
  push {r0-r12, lr}
  mrs r12, cpsr
  push {r12, lr}
  vmrs r12, fpscr
  str r12, [sp, #4]
  vpush {d0-d15}
  vpush {d16-d31}
  add r0, sp, #320
  mov r1, r4
  mov r2, r9
  mov r3, r8
  bl m3_resource_slot_check
  vpop {d16-d31}
  vpop {d0-d15}
  pop {r12, lr}
  vmsr fpscr, lr
  msr APSR_nzcvq, r12
  pop {r0-r12, lr}
  add fp, sp, #16
  add r2, sp, #52
  ldr pc, [pc, #-4]
.global m3_trace_resource_resume
m3_trace_resource_resume:
  .word 0
.size m3_trace_resource_slot, .-m3_trace_resource_slot

.macro RESOURCE_LIFETIME name, phase
.global \name
.type \name, %function
\name:
  push {r0-r12, lr}
  mrs r12, cpsr
  push {r12, lr}
  vmrs r12, fpscr
  str r12, [sp, #4]
  vpush {d0-d15}
  vpush {d16-d31}
  mov r2, r0
  add r0, sp, #320
  mov r1, r4
  mov r3, #\phase
  bl m3_resource_lifetime
  vpop {d16-d31}
  vpop {d0-d15}
  pop {r12, lr}
  vmsr fpscr, lr
  msr APSR_nzcvq, r12
  pop {r0-r12, lr}
.if \phase == 0
  str r0, [r4]
  mov r0, r6
.elseif \phase >= 5
  mov r0, #0
  str r0, [r4]
.elseif \phase == 3
  ldr r1, [sp, #0x18]
  ldr r2, [r6, r7]
.elseif \phase == 4
  /* Exact constant from original PC-relative literal Unity+40edc4. */
  movw r0, #0xf758
  movt r0, #0xac
  ldr r1, [sp, #0x1c]
.else
  add r0, sp, #0x1c
  add r1, sp, #0x30
.endif
  ldr pc, [pc, #-4]
.global \name\()_resume
\name\()_resume:
  .word 0
.size \name, .-\name
.endm
RESOURCE_LIFETIME m3_resource_created, 0
RESOURCE_LIFETIME m3_resource_middle, 1
RESOURCE_LIFETIME m3_resource_last, 2
RESOURCE_LIFETIME m3_resource_joined, 3
RESOURCE_LIFETIME m3_resource_checked, 4
RESOURCE_LIFETIME m3_resource_sweep, 5
RESOURCE_LIFETIME m3_resource_unload, 6

/* Checked, PC-independent entry prologues populated by entry_flow.c. */
.global m3_tramp_entry_login
.type m3_tramp_entry_login, %function
m3_tramp_entry_login:
  .space 16
.size m3_tramp_entry_login, .-m3_tramp_entry_login
.global m3_tramp_entry_goto
.type m3_tramp_entry_goto, %function
m3_tramp_entry_goto:
  .space 16
.size m3_tramp_entry_goto, .-m3_tramp_entry_goto
.global m3_tramp_entry_reply
.type m3_tramp_entry_reply, %function
m3_tramp_entry_reply:
  .space 16
.size m3_tramp_entry_reply, .-m3_tramp_entry_reply
.global m3_tramp_entry_callback
.type m3_tramp_entry_callback, %function
m3_tramp_entry_callback:
  .space 16
.size m3_tramp_entry_callback, .-m3_tramp_entry_callback
.global m3_tramp_entry_stub
.type m3_tramp_entry_stub, %function
m3_tramp_entry_stub:
  .space 16
.size m3_tramp_entry_stub, .-m3_tramp_entry_stub

.global m3_tramp_entry_wheel
.type m3_tramp_entry_wheel, %function
m3_tramp_entry_wheel:
    .space 16
.size m3_tramp_entry_wheel, .-m3_tramp_entry_wheel

.global m3_tramp_entry_floor
.type m3_tramp_entry_floor, %function
m3_tramp_entry_floor:
    .space 16
.size m3_tramp_entry_floor, .-m3_tramp_entry_floor

.global m3_tramp_entry_login_reply
.type m3_tramp_entry_login_reply, %function
m3_tramp_entry_login_reply:
    .space 16
.size m3_tramp_entry_login_reply, .-m3_tramp_entry_login_reply

.global m3_tramp_entry_agreement
.type m3_tramp_entry_agreement, %function
m3_tramp_entry_agreement:
    .space 16
.size m3_tramp_entry_agreement, .-m3_tramp_entry_agreement

.global m3_tramp_entry_timeout
.type m3_tramp_entry_timeout, %function
m3_tramp_entry_timeout:
    .space 16
.size m3_tramp_entry_timeout, .-m3_tramp_entry_timeout

.global m3_tramp_entry_click
.type m3_tramp_entry_click, %function
m3_tramp_entry_click:
.space 16
.size m3_tramp_entry_click, .-m3_tramp_entry_click
