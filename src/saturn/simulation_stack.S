/* Run the CPU-only simulation callback on its caller-owned Work RAM stack.
 * r8 and PR stay on the executor's original stack; the C body preserves r8.
 * SH-2 GCC passes the fifth argument at the incoming stack pointer. */
    .section .text.t3_simulation_task_entry,"ax"
    .align 2
    .global _t3_simulation_task_entry
    .type _t3_simulation_task_entry,@function
_t3_simulation_task_entry:
    mov.l r8,@-r15
    sts.l pr,@-r15
    mov r15,r8
    mov.l @(8,r8),r0
    mov.l .Lsimulation_stack_top,r15
    mov.l r0,@-r15
    mov.l .Lsimulation_body,r0
    jsr @r0
    nop
    mov r8,r15
    lds.l @r15+,pr
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lsimulation_stack_top:
    .long _g_t3_simulation_stack+8192
.Lsimulation_body:
    .long _t3_simulation_task_body
    .size _t3_simulation_task_entry,.-_t3_simulation_task_entry
