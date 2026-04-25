/* x86_64 GAS — IDT with 256 entries and IRQ0 (PIT) handler
 *
 * Gate format: 64-bit interrupt gate, CS=0x08, DPL=0, present (type 0x8E).
 *
 * ISR stubs for vectors 0-47:
 *   Exceptions without error code: push 0 (dummy), push vector, jmp common.
 *   Exceptions with error code (8,10-14,17,29,30): push vector, jmp common.
 *   IRQ lines (32-47): push 0, push vector, jmp common.
 *
 * Vectors 48-255: filled with default_stub which routes through the same
 *   dispatcher path used by named stubs.
 *
 * isr_common_stub:
 *   Saves caller-save regs, calls x86_idt_dispatch(vector) (C function in
 *   timer_driver.c / pic_driver.c), restores regs, iretqs.
 *
 * idt_install():
 *   Iterates all 256 vectors: 0-47 get their specific stub via a static
 *   address table; 48-255 get default_stub.  Loads IDTR via LIDT.
 */

.section .note.GNU-stack,"",@progbits

/* ------------------------------------------------------------------ */
/* IDT storage                                                         */
/* ------------------------------------------------------------------ */
.section .bss
.align 16
.globl idt_table
idt_table:
    .space 256 * 16

.section .data
.align 2
idt_ptr:
    .word 256 * 16 - 1
    .quad idt_table

/* ------------------------------------------------------------------ */
/* ISR stubs 0-47                                                      */
/* ------------------------------------------------------------------ */
.text

.macro ISR_NOERR num
.globl isr_stub_\num
isr_stub_\num:
    pushq $0
    pushq $\num
    jmp   isr_common_stub
.endm

.macro ISR_ERR num
.globl isr_stub_\num
isr_stub_\num:
    pushq $\num
    jmp   isr_common_stub
.endm

/* CPU exceptions */
ISR_NOERR 0;  ISR_NOERR 1;  ISR_NOERR 2;  ISR_NOERR 3
ISR_NOERR 4;  ISR_NOERR 5;  ISR_NOERR 6;  ISR_NOERR 7
ISR_ERR   8;  ISR_NOERR 9;  ISR_ERR   10; ISR_ERR   11
ISR_ERR   12; ISR_ERR   13; ISR_ERR   14; ISR_NOERR 15
ISR_NOERR 16; ISR_ERR   17; ISR_NOERR 18; ISR_NOERR 19
ISR_NOERR 20; ISR_NOERR 21; ISR_NOERR 22; ISR_NOERR 23
ISR_NOERR 24; ISR_NOERR 25; ISR_NOERR 26; ISR_NOERR 27
ISR_NOERR 28; ISR_ERR   29; ISR_ERR   30; ISR_NOERR 31
/* IRQ 0-15 (PIC-remapped to vectors 32-47) */
ISR_NOERR 32; ISR_NOERR 33; ISR_NOERR 34; ISR_NOERR 35
ISR_NOERR 36; ISR_NOERR 37; ISR_NOERR 38; ISR_NOERR 39
ISR_NOERR 40; ISR_NOERR 41; ISR_NOERR 42; ISR_NOERR 43
ISR_NOERR 44; ISR_NOERR 45; ISR_NOERR 46; ISR_NOERR 47

/* Vectors 48-255: each gets its own stub with correct vector number. */
ISR_NOERR 48;  ISR_NOERR 49;  ISR_NOERR 50;  ISR_NOERR 51
ISR_NOERR 52;  ISR_NOERR 53;  ISR_NOERR 54;  ISR_NOERR 55
ISR_NOERR 56;  ISR_NOERR 57;  ISR_NOERR 58;  ISR_NOERR 59
ISR_NOERR 60;  ISR_NOERR 61;  ISR_NOERR 62;  ISR_NOERR 63
ISR_NOERR 64;  ISR_NOERR 65;  ISR_NOERR 66;  ISR_NOERR 67
ISR_NOERR 68;  ISR_NOERR 69;  ISR_NOERR 70;  ISR_NOERR 71
ISR_NOERR 72;  ISR_NOERR 73;  ISR_NOERR 74;  ISR_NOERR 75
ISR_NOERR 76;  ISR_NOERR 77;  ISR_NOERR 78;  ISR_NOERR 79
ISR_NOERR 80;  ISR_NOERR 81;  ISR_NOERR 82;  ISR_NOERR 83
ISR_NOERR 84;  ISR_NOERR 85;  ISR_NOERR 86;  ISR_NOERR 87
ISR_NOERR 88;  ISR_NOERR 89;  ISR_NOERR 90;  ISR_NOERR 91
ISR_NOERR 92;  ISR_NOERR 93;  ISR_NOERR 94;  ISR_NOERR 95
ISR_NOERR 96;  ISR_NOERR 97;  ISR_NOERR 98;  ISR_NOERR 99
ISR_NOERR 100; ISR_NOERR 101; ISR_NOERR 102; ISR_NOERR 103
ISR_NOERR 104; ISR_NOERR 105; ISR_NOERR 106; ISR_NOERR 107
ISR_NOERR 108; ISR_NOERR 109; ISR_NOERR 110; ISR_NOERR 111
ISR_NOERR 112; ISR_NOERR 113; ISR_NOERR 114; ISR_NOERR 115
ISR_NOERR 116; ISR_NOERR 117; ISR_NOERR 118; ISR_NOERR 119
ISR_NOERR 120; ISR_NOERR 121; ISR_NOERR 122; ISR_NOERR 123
ISR_NOERR 124; ISR_NOERR 125; ISR_NOERR 126; ISR_NOERR 127
ISR_NOERR 128; ISR_NOERR 129; ISR_NOERR 130; ISR_NOERR 131
ISR_NOERR 132; ISR_NOERR 133; ISR_NOERR 134; ISR_NOERR 135
ISR_NOERR 136; ISR_NOERR 137; ISR_NOERR 138; ISR_NOERR 139
ISR_NOERR 140; ISR_NOERR 141; ISR_NOERR 142; ISR_NOERR 143
ISR_NOERR 144; ISR_NOERR 145; ISR_NOERR 146; ISR_NOERR 147
ISR_NOERR 148; ISR_NOERR 149; ISR_NOERR 150; ISR_NOERR 151
ISR_NOERR 152; ISR_NOERR 153; ISR_NOERR 154; ISR_NOERR 155
ISR_NOERR 156; ISR_NOERR 157; ISR_NOERR 158; ISR_NOERR 159
ISR_NOERR 160; ISR_NOERR 161; ISR_NOERR 162; ISR_NOERR 163
ISR_NOERR 164; ISR_NOERR 165; ISR_NOERR 166; ISR_NOERR 167
ISR_NOERR 168; ISR_NOERR 169; ISR_NOERR 170; ISR_NOERR 171
ISR_NOERR 172; ISR_NOERR 173; ISR_NOERR 174; ISR_NOERR 175
ISR_NOERR 176; ISR_NOERR 177; ISR_NOERR 178; ISR_NOERR 179
ISR_NOERR 180; ISR_NOERR 181; ISR_NOERR 182; ISR_NOERR 183
ISR_NOERR 184; ISR_NOERR 185; ISR_NOERR 186; ISR_NOERR 187
ISR_NOERR 188; ISR_NOERR 189; ISR_NOERR 190; ISR_NOERR 191
ISR_NOERR 192; ISR_NOERR 193; ISR_NOERR 194; ISR_NOERR 195
ISR_NOERR 196; ISR_NOERR 197; ISR_NOERR 198; ISR_NOERR 199
ISR_NOERR 200; ISR_NOERR 201; ISR_NOERR 202; ISR_NOERR 203
ISR_NOERR 204; ISR_NOERR 205; ISR_NOERR 206; ISR_NOERR 207
ISR_NOERR 208; ISR_NOERR 209; ISR_NOERR 210; ISR_NOERR 211
ISR_NOERR 212; ISR_NOERR 213; ISR_NOERR 214; ISR_NOERR 215
ISR_NOERR 216; ISR_NOERR 217; ISR_NOERR 218; ISR_NOERR 219
ISR_NOERR 220; ISR_NOERR 221; ISR_NOERR 222; ISR_NOERR 223
ISR_NOERR 224; ISR_NOERR 225; ISR_NOERR 226; ISR_NOERR 227
ISR_NOERR 228; ISR_NOERR 229; ISR_NOERR 230; ISR_NOERR 231
ISR_NOERR 232; ISR_NOERR 233; ISR_NOERR 234; ISR_NOERR 235
ISR_NOERR 236; ISR_NOERR 237; ISR_NOERR 238; ISR_NOERR 239
ISR_NOERR 240; ISR_NOERR 241; ISR_NOERR 242; ISR_NOERR 243
ISR_NOERR 244; ISR_NOERR 245; ISR_NOERR 246; ISR_NOERR 247
ISR_NOERR 248; ISR_NOERR 249; ISR_NOERR 250; ISR_NOERR 251
ISR_NOERR 252; ISR_NOERR 253; ISR_NOERR 254; ISR_NOERR 255

/* ------------------------------------------------------------------ */
/* Address table for all 256 vectors (0-255)                           */
/* ------------------------------------------------------------------ */
.section .rodata
.align 8
stub_table:
    .quad isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
    .quad isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
    .quad isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
    .quad isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
    .quad isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
    .quad isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
    .quad isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
    .quad isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    .quad isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35
    .quad isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39
    .quad isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43
    .quad isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47
    .quad isr_stub_48,  isr_stub_49,  isr_stub_50,  isr_stub_51
    .quad isr_stub_52,  isr_stub_53,  isr_stub_54,  isr_stub_55
    .quad isr_stub_56,  isr_stub_57,  isr_stub_58,  isr_stub_59
    .quad isr_stub_60,  isr_stub_61,  isr_stub_62,  isr_stub_63
    .quad isr_stub_64,  isr_stub_65,  isr_stub_66,  isr_stub_67
    .quad isr_stub_68,  isr_stub_69,  isr_stub_70,  isr_stub_71
    .quad isr_stub_72,  isr_stub_73,  isr_stub_74,  isr_stub_75
    .quad isr_stub_76,  isr_stub_77,  isr_stub_78,  isr_stub_79
    .quad isr_stub_80,  isr_stub_81,  isr_stub_82,  isr_stub_83
    .quad isr_stub_84,  isr_stub_85,  isr_stub_86,  isr_stub_87
    .quad isr_stub_88,  isr_stub_89,  isr_stub_90,  isr_stub_91
    .quad isr_stub_92,  isr_stub_93,  isr_stub_94,  isr_stub_95
    .quad isr_stub_96,  isr_stub_97,  isr_stub_98,  isr_stub_99
    .quad isr_stub_100, isr_stub_101, isr_stub_102, isr_stub_103
    .quad isr_stub_104, isr_stub_105, isr_stub_106, isr_stub_107
    .quad isr_stub_108, isr_stub_109, isr_stub_110, isr_stub_111
    .quad isr_stub_112, isr_stub_113, isr_stub_114, isr_stub_115
    .quad isr_stub_116, isr_stub_117, isr_stub_118, isr_stub_119
    .quad isr_stub_120, isr_stub_121, isr_stub_122, isr_stub_123
    .quad isr_stub_124, isr_stub_125, isr_stub_126, isr_stub_127
    .quad isr_stub_128, isr_stub_129, isr_stub_130, isr_stub_131
    .quad isr_stub_132, isr_stub_133, isr_stub_134, isr_stub_135
    .quad isr_stub_136, isr_stub_137, isr_stub_138, isr_stub_139
    .quad isr_stub_140, isr_stub_141, isr_stub_142, isr_stub_143
    .quad isr_stub_144, isr_stub_145, isr_stub_146, isr_stub_147
    .quad isr_stub_148, isr_stub_149, isr_stub_150, isr_stub_151
    .quad isr_stub_152, isr_stub_153, isr_stub_154, isr_stub_155
    .quad isr_stub_156, isr_stub_157, isr_stub_158, isr_stub_159
    .quad isr_stub_160, isr_stub_161, isr_stub_162, isr_stub_163
    .quad isr_stub_164, isr_stub_165, isr_stub_166, isr_stub_167
    .quad isr_stub_168, isr_stub_169, isr_stub_170, isr_stub_171
    .quad isr_stub_172, isr_stub_173, isr_stub_174, isr_stub_175
    .quad isr_stub_176, isr_stub_177, isr_stub_178, isr_stub_179
    .quad isr_stub_180, isr_stub_181, isr_stub_182, isr_stub_183
    .quad isr_stub_184, isr_stub_185, isr_stub_186, isr_stub_187
    .quad isr_stub_188, isr_stub_189, isr_stub_190, isr_stub_191
    .quad isr_stub_192, isr_stub_193, isr_stub_194, isr_stub_195
    .quad isr_stub_196, isr_stub_197, isr_stub_198, isr_stub_199
    .quad isr_stub_200, isr_stub_201, isr_stub_202, isr_stub_203
    .quad isr_stub_204, isr_stub_205, isr_stub_206, isr_stub_207
    .quad isr_stub_208, isr_stub_209, isr_stub_210, isr_stub_211
    .quad isr_stub_212, isr_stub_213, isr_stub_214, isr_stub_215
    .quad isr_stub_216, isr_stub_217, isr_stub_218, isr_stub_219
    .quad isr_stub_220, isr_stub_221, isr_stub_222, isr_stub_223
    .quad isr_stub_224, isr_stub_225, isr_stub_226, isr_stub_227
    .quad isr_stub_228, isr_stub_229, isr_stub_230, isr_stub_231
    .quad isr_stub_232, isr_stub_233, isr_stub_234, isr_stub_235
    .quad isr_stub_236, isr_stub_237, isr_stub_238, isr_stub_239
    .quad isr_stub_240, isr_stub_241, isr_stub_242, isr_stub_243
    .quad isr_stub_244, isr_stub_245, isr_stub_246, isr_stub_247
    .quad isr_stub_248, isr_stub_249, isr_stub_250, isr_stub_251
    .quad isr_stub_252, isr_stub_253, isr_stub_254, isr_stub_255

/* ------------------------------------------------------------------ */
/* isr_common_stub                                                     */
/* Stack on entry:                                                     */
/*   [rsp+0 ] vector  (pushed by ISR macro)                           */
/*   [rsp+8 ] error code (0 or real)                                  */
/*   [rsp+16] CPU interrupt frame (rip, cs, rflags, rsp, ss)          */
/*
 * XMM/AVX register preservation:
 * This stub saves only general-purpose registers (rax-r11).
 * XMM/YMM/ZMM registers are NOT saved or restored.
 *
 * REQUIREMENT: The C dispatcher (x86_idt_dispatch) and all exception/IRQ
 * handlers MUST be compiled with -mno-sse -mno-mmx -mno-avx -mno-80387
 * to prevent the compiler from emitting SSE/AVX instructions that would
 * clobber vector registers.
 *
 * If XMM/AVX preservation is needed in future, modify this stub to save/restore
 * the full XMM state (or use XSAVE/XRSTOR) around the dispatcher call.
 */
/* ------------------------------------------------------------------ */
.text
isr_common_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    /* Pass vector (rdi) and error_code (rsi) to x86_idt_dispatch.
     * After pushing 10 GPRs, stack layout (from rsp upward):
     *   rsp+0:  r11, r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax (10*8=80 bytes)
     *   rsp+80: vector
     *   rsp+88: error_code
     *   rsp+96: CPU interrupt frame (rip, cs, rflags, rsp, ss)
     * For #PF, CR2 must be read inside the C dispatcher. */
    movq  80(%rsp), %rdi   /* vector -> rdi (first arg) */
    movq  88(%rsp), %rsi   /* error_code -> rsi (second arg) */
    subq  $8, %rsp         /* align stack to 16 bytes before call */
    call  x86_idt_dispatch
    addq  $8, %rsp

    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp   /* discard vector + error code */
    iretq

/* ------------------------------------------------------------------ */
/* idt_set_gate helper                                                 */
/* rdi=vector  rsi=handler_addr                                       */
/* ------------------------------------------------------------------ */
idt_set_gate:
    movq  %rdi, %rax
    shlq  $4, %rax
    leaq  idt_table(%rip), %rcx
    addq  %rax, %rcx

    movw  %si, 0(%rcx)                /* offset[15:0] */
    movw  $0x08, 2(%rcx)              /* kernel CS */
    movb  $0x00, 4(%rcx)              /* IST=0 */
    movb  $0x8E, 5(%rcx)              /* P=1, DPL=0, type=0xE */
    movq  %rsi, %rax
    shrq  $16, %rax
    movw  %ax, 6(%rcx)               /* offset[31:16] */
    movq  %rsi, %rax
    shrq  $32, %rax
    movl  %eax, 8(%rcx)              /* offset[63:32] */
    movl  $0, 12(%rcx)               /* reserved */
    ret

/* ------------------------------------------------------------------ */
/* idt_install                                                         */
/* ------------------------------------------------------------------ */
.globl idt_install
idt_install:
    pushq %rbx
    pushq %r12

    /* Install all 256 vectors from stub_table */
    xorl  %ebx, %ebx
    leaq  stub_table(%rip), %r12
.Lloop:
    movq  (%r12,%rbx,8), %rsi
    movq  %rbx, %rdi
    call  idt_set_gate
    incl  %ebx
    cmpl  $256, %ebx
    jl    .Lloop

    lidt  idt_ptr(%rip)   /* RIP-relative avoids R_X86_64_32S in PIE builds */

    popq  %r12
    popq  %rbx
    ret
