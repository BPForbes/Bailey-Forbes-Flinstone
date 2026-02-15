#ifndef VM_DECODE_H
#define VM_DECODE_H

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

typedef struct vm_instr {
    vm_opcode_t op;
    int dst_reg;
    int src_reg;
    uint32_t imm;
    uint32_t mem_addr;
    int mem_size;
    unsigned int size;  /* instruction length in bytes */
} vm_instr_t;

int vm_decode(uint8_t *mem, uint32_t addr, vm_instr_t *out);

#endif /* VM_DECODE_H */
