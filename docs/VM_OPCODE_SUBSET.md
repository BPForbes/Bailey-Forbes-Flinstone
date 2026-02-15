# VM Opcode Subset Policy

## Policy

Expand CPU **only as needed** for guest programs. Prefer:
1. **Strict subset + matching guest**: Write guest to use implemented opcodes.
2. **Add opcodes** when guest code requires them; document in baseline.

## Current Subset

See `docs/baseline/vm_opcodes.txt` for full list.

**Flow**: NOP, HLT, JMP, JZ, JNZ, RET, INT, IRET  
**Data**: MOV (r8/r32 + imm), PUSH, POP, STOSB  
**Arithmetic**: ADD, SUB, INC, DEC  
**Compare**: CMP, TEST (al, imm8)  
**I/O**: IN, OUT  
**System**: MOV CR0/CR3  

## Expansion

- Add opcodes when a concrete guest needs them.
- Use ModRM infrastructure for new reg/mem forms.
- Update baseline doc on each addition.
