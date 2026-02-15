#ifndef VM_DECODE_H
#define VM_DECODE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    VM_OP_NOP,
    VM_OP_HLT,
    VM_OP_IN,
    VM_OP_OUT,
    VM_OP_MOV,
    VM_OP_ADD,
    VM_OP_SUB,
    VM_OP_INC,
    VM_OP_DEC,
    VM_OP_CMP,
    VM_OP_PUSH,
    VM_OP_POP,
    VM_OP_JMP,
    VM_OP_JZ,
    VM_OP_JNZ,
    VM_OP_INT,
    VM_OP_IRET,
    VM_OP_RET,
    VM_OP_STOSB,
    VM_OP_UNKNOWN,
} vm_opcode_t;

typedef enum {
    VM_OPND_NONE,
    VM_OPND_REG8,
    VM_OPND_REG32,
    VM_OPND_IMM8,
    VM_OPND_IMM16,
    VM_OPND_IMM32,
    VM_OPND_MEM,
} vm_operand_type_t;

/* ModRM: mod[1:0] reg[2:0] r/m[2:0]. mod=11 => reg operands. */
typedef struct vm_modrm {
    unsigned valid : 1;
    unsigned mod   : 2;   /* 0=mem no disp, 1=mem+disp8, 2=mem+disp32, 3=reg */
    unsigned reg   : 3;   /* reg field: reg num or opcode ext */
    unsigned rm    : 3;   /* r/m field: reg num or addressing mode */
    int32_t  disp;        /* displacement (disp8 sign-extended or disp32) */
    unsigned disp_bytes;  /* 0, 1, or 4 */
} vm_modrm_t;

/* SIB: scale[1:0] index[2:0] base[2:0]. Used when r/m=4. */
typedef struct vm_sib {
    unsigned valid : 1;
    unsigned scale : 2;   /* 1,2,4,8 */
    unsigned index : 3;
    unsigned base  : 3;
} vm_sib_t;

typedef struct vm_instr {
    vm_opcode_t op;
    int dst_reg;
    int src_reg;
    uint32_t imm;
    uint32_t mem_addr;
    int mem_size;
    unsigned int size;  /* instruction length in bytes */
    vm_modrm_t modrm;
    vm_sib_t sib;
} vm_instr_t;

int vm_decode(uint8_t *mem, uint32_t addr, size_t mem_size, vm_instr_t *out);

#endif /* VM_DECODE_H */
