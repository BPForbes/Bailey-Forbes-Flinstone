#include "vm_decode.h"
#include "mem_asm.h"

/* Decode structure: primary opcode switch + range handlers + ModRM/SIB.
 * ModRM: mod=11 reg only; mod=00/01/10 memory with optional disp.
 * SIB: when r/m=4; index*scale + base + disp. */

static void modrm_clear(vm_modrm_t *m) {
    if (m) asm_mem_zero(m, sizeof(*m));
}

static void sib_clear(vm_sib_t *s) {
    if (s) asm_mem_zero(s, sizeof(*s));
}

#define VM_DECODE_SAFE 16
/* Parse ModRM at addr; returns total bytes consumed (1 + disp). Supports mod=11 (reg) and disp8/32. */
static int decode_modrm(uint8_t *mem, uint32_t addr, size_t mem_size, vm_modrm_t *out, vm_sib_t *sib_out) {
    if (mem_size < 6 || addr + 6 > mem_size) return -1;
    uint8_t b = mem[addr];
    out->valid = 1;
    out->mod = (b >> 6) & 3;
    out->reg = (b >> 3) & 7;
    out->rm = b & 7;
    out->disp = 0;
    out->disp_bytes = 0;
    sib_clear(sib_out);

    int n = 1;
    if (out->mod == 1) {
        out->disp = (int32_t)(int8_t)mem[addr + 1];
        out->disp_bytes = 1;
        n = 2;
    } else if (out->mod == 2) {
        out->disp = (uint32_t)mem[addr+1] | ((uint32_t)mem[addr+2]<<8) | ((uint32_t)mem[addr+3]<<16) | ((uint32_t)mem[addr+4]<<24);
        out->disp_bytes = 4;
        n = 5;
    } else if (out->mod == 0 && out->rm == 5) {
        out->disp = (uint32_t)mem[addr+1] | ((uint32_t)mem[addr+2]<<8) | ((uint32_t)mem[addr+3]<<16) | ((uint32_t)mem[addr+4]<<24);
        out->disp_bytes = 4;
        n = 5;
    }
    if (out->mod != 3 && out->rm == 4) {
        uint8_t s = mem[addr + n];
        sib_out->valid = 1;
        sib_out->scale = 1 << ((s >> 6) & 3);
        sib_out->index = (s >> 3) & 7;
        sib_out->base = s & 7;
        n++;
        if (out->mod == 0 && sib_out->base == 5) {
            out->disp = (uint32_t)mem[addr+n] | ((uint32_t)mem[addr+n+1]<<8) | ((uint32_t)mem[addr+n+2]<<16) | ((uint32_t)mem[addr+n+3]<<24);
            out->disp_bytes = 4;
            n += 4;
        }
    }
    return n;
}

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

int vm_decode(uint8_t *mem, uint32_t addr, size_t mem_size, vm_instr_t *out) {
    if (!mem || !out || mem_size < 2) return -1;
    if (mem[addr] == 0x0F) {
        /* Two-byte opcode */
        uint8_t b1 = addr + 1 < mem_size ? mem[addr + 1] : 0;
        if (b1 == 0x20) { /* MOV r32, CRn */
            int n = decode_modrm(mem, addr + 2, mem_size, &out->modrm, &out->sib);
            if (n < 0 || out->modrm.mod != 3) return -1;
            out->op = VM_OP_MOV_CR;
            out->dst_reg = out->modrm.reg;
            out->src_reg = out->modrm.rm;
            out->imm = 0;  /* 0 = read CR into reg */
            out->size = 2 + n;
            return 0;
        }
        if (b1 == 0x22) { /* MOV CRn, r32 */
            int n = decode_modrm(mem, addr + 2, mem_size, &out->modrm, &out->sib);
            if (n < 0 || out->modrm.mod != 3) return -1;
            out->op = VM_OP_MOV_CR;
            out->dst_reg = out->modrm.reg;
            out->src_reg = out->modrm.rm;
            out->imm = 1;  /* 1 = write reg to CR */
            out->size = 2 + n;
            return 0;
        }
        return -1;
    }
    out->op = VM_OP_UNKNOWN;
    out->dst_reg = out->src_reg = -1;
    out->imm = 0;
    out->mem_addr = 0;
    out->mem_size = 0;
    out->size = 1;
    modrm_clear(&out->modrm);
    sib_clear(&out->sib);

    uint8_t b0 = mem[addr];
    uint8_t b1 = addr + 1 < mem_size ? mem[addr + 1] : 0;

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
    case 0x3C: /* CMP al, imm8 */
        out->op = VM_OP_CMP;
        out->dst_reg = 0;
        out->imm = b1;
        out->size = 2;
        return 0;
    case 0xA8: /* TEST al, imm8 */
        out->op = VM_OP_TEST;
        out->dst_reg = 0;
        out->imm = b1;
        out->size = 2;
        return 0;
    case 0x74: /* JZ rel8 */
        out->op = VM_OP_JZ;
        out->imm = (int8_t)b1;
        out->size = 2;
        return 0;
    case 0x75: /* JNZ rel8 */
        out->op = VM_OP_JNZ;
        out->imm = (int8_t)b1;
        out->size = 2;
        return 0;
    case 0x01: { /* ADD r/m32, r32 */
        int n = decode_modrm(mem, addr + 1, mem_size, &out->modrm, &out->sib);
        if (n < 0 || out->modrm.mod != 3) return -1;
        out->op = VM_OP_ADD;
        out->dst_reg = out->modrm.rm;
        out->src_reg = out->modrm.reg;
        out->size = 1 + n;
        return 0;
    }
    case 0x03: { /* ADD r32, r/m32 */
        int n = decode_modrm(mem, addr + 1, mem_size, &out->modrm, &out->sib);
        if (n < 0 || out->modrm.mod != 3) return -1;
        out->op = VM_OP_ADD;
        out->dst_reg = out->modrm.reg;
        out->src_reg = out->modrm.rm;
        out->size = 1 + n;
        return 0;
    }
    case 0x29: { /* SUB r/m32, r32 */
        int n = decode_modrm(mem, addr + 1, mem_size, &out->modrm, &out->sib);
        if (n < 0 || out->modrm.mod != 3) return -1;
        out->op = VM_OP_SUB;
        out->dst_reg = out->modrm.rm;
        out->src_reg = out->modrm.reg;
        out->size = 1 + n;
        return 0;
    }
    case 0x2B: { /* SUB r32, r/m32 */
        int n = decode_modrm(mem, addr + 1, mem_size, &out->modrm, &out->sib);
        if (n < 0 || out->modrm.mod != 3) return -1;
        out->op = VM_OP_SUB;
        out->dst_reg = out->modrm.reg;
        out->src_reg = out->modrm.rm;
        out->size = 1 + n;
        return 0;
    }
    case 0x83: { /* ADD/SUB r/m32, imm8 - /r is opcode ext */
        int n = decode_modrm(mem, addr + 1, mem_size, &out->modrm, &out->sib);
        if (n < 0 || out->modrm.mod != 3) return -1;
        if (addr + 1 + n + 1 > mem_size) return -1;
        uint8_t ext = out->modrm.reg;
        if (ext == 0) out->op = VM_OP_ADD;
        else if (ext == 5) out->op = VM_OP_SUB;
        else return -1;
        out->dst_reg = out->modrm.rm;
        out->src_reg = -1;
        out->imm = (int32_t)(int8_t)mem[addr + 1 + n];
        out->size = 1 + n + 1;
        return 0;
    }
    default:
        break;
    }

    if (b0 >= 0x40 && b0 <= 0x47) { /* INC r32 */
        out->op = VM_OP_INC;
        out->dst_reg = b0 - 0x40;
        out->size = 1;
        return 0;
    }
    if (b0 >= 0x48 && b0 <= 0x4F) { /* DEC r32 */
        out->op = VM_OP_DEC;
        out->dst_reg = b0 - 0x48;
        out->size = 1;
        return 0;
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
