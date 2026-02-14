#include "vm_decode.h"

static int decode_mov_r8_imm8(uint8_t *mem, uint32_t addr, vm_instr_t *out) {
    uint8_t op = mem[addr];
    int reg = op - 0xB0;
    if (reg < 0 || reg > 7) return -1;
    out->op = VM_OP_MOV;
    out->dst_reg = reg;
    out->src_reg = -1;
    out->imm = mem[addr + 1];
    out->size = 2;
    return 0;
}

static int decode_mov_r32_imm32(uint8_t *mem, uint32_t addr, vm_instr_t *out) {
    int reg = mem[addr] - 0xB8;
    if (reg < 0 || reg > 7) return -1;
    out->op = VM_OP_MOV;
    out->dst_reg = reg;
    out->src_reg = -1;
    out->imm = (uint32_t)mem[addr+1] | ((uint32_t)mem[addr+2]<<8) | ((uint32_t)mem[addr+3]<<16) | ((uint32_t)mem[addr+4]<<24);
    out->size = 5;
    return 0;
}

int vm_decode(uint8_t *mem, uint32_t addr, vm_instr_t *out) {
    if (!mem || !out) return -1;
    out->op = VM_OP_UNKNOWN;
    out->dst_reg = out->src_reg = -1;
    out->imm = 0;
    out->mem_addr = 0;
    out->mem_size = 0;
    out->size = 1;

    uint8_t b0 = mem[addr];
    uint8_t b1 = addr + 1 < 0x1000000 ? mem[addr + 1] : 0;

    switch (b0) {
    case 0x90: /* NOP */
        out->op = VM_OP_NOP;
        return 0;
    case 0xF4: /* HLT */
        out->op = VM_OP_HLT;
        return 0;
    case 0xE4: /* IN al, imm8 */
        out->op = VM_OP_IN;
        out->imm = b1;
        out->size = 2;
        return 0;
    case 0xE6: /* OUT imm8, al */
        out->op = VM_OP_OUT;
        out->imm = b1;
        out->size = 2;
        return 0;
    case 0xEB: /* JMP rel8 */
        out->op = VM_OP_JMP;
        out->imm = (int8_t)b1;
        out->size = 2;
        return 0;
    case 0xC3: /* RET near */
        out->op = VM_OP_RET;
        return 0;
    case 0xCD: /* INT imm8 */
        out->op = VM_OP_INT;
        out->imm = b1;
        out->size = 2;
        return 0;
    case 0xCF: /* IRET */
        out->op = VM_OP_IRET;
        return 0;
    case 0xAA: /* STOSB */
        out->op = VM_OP_STOSB;
        return 0;
    default:
        break;
    }

    if (b0 >= 0xB0 && b0 <= 0xB7) {
        return decode_mov_r8_imm8(mem, addr, out);
    }
    if (b0 >= 0xB8 && b0 <= 0xBF) {
        return decode_mov_r32_imm32(mem, addr, out);
    }
    if (b0 >= 0x50 && b0 <= 0x57) { /* PUSH r32 */
        out->op = VM_OP_PUSH;
        out->dst_reg = b0 - 0x50;
        return 0;
    }
    if (b0 >= 0x58 && b0 <= 0x5F) { /* POP r32 */
        out->op = VM_OP_POP;
        out->dst_reg = b0 - 0x58;
        return 0;
    }

    return -1;
}
