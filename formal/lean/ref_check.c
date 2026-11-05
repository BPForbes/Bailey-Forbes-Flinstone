/* Readable hand translation of formal/lean/FlWqCode.lean, used ONLY as a check.
 *
 * It is never linked into the kernel or any test: the kernel has exactly one implementation, the C that Lean
 * generated (kernel/core/sched/wq_model_gen.inc). This program compares the two over every input.
 *
 * Build and run (make check-formal does this, with the sanitizers on):
 *   gcc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-sanitize-recover=undefined \
 *       formal/lean/ref_check.c -o /tmp/ref_check && /tmp/ref_check
 *
 * fl_wq_state_t: 1 PENDING 2 RUNNING 3 DONE 4 FAILED 5 EXPIRED 6 HELD 7 QUEUED 8 SKIPPED.
 */
#include <stdint.h>
#include <stdio.h>

#include "../../kernel/core/sched/wq_model_gen.inc" /* defines static fl_wq_transition_allowed / fl_wq_state_terminal */

/* Lean: transitionAllowed (f t : UInt8) : UInt8. Same chain, same literals, same order. */
static uint8_t ref_transition_allowed(uint8_t from, uint8_t to)
{
    if (from == 1 && (to == 7 || to == 6 || to == 5)) {
        return 1; /* FlWqCode.lean:13  PENDING -> QUEUED, HELD, EXPIRED */
    } else if (from == 7 && (to == 2 || to == 1 || to == 5 || to == 4)) {
        return 1; /* FlWqCode.lean:14  QUEUED -> RUNNING, PENDING, EXPIRED, FAILED */
    } else if (from == 2 && (to == 3 || to == 1 || to == 4)) {
        return 1; /* FlWqCode.lean:15  RUNNING -> DONE, PENDING, FAILED */
    } else if (from == 6 && (to == 1 || to == 8)) {
        return 1; /* FlWqCode.lean:16  HELD -> PENDING, SKIPPED */
    } else {
        return 0; /* FlWqCode.lean:17  every other pair, including numbers that are no state */
    }
}

/* Lean: stateTerminal (c : UInt8) : UInt8. */
static uint8_t ref_state_terminal(uint8_t c)
{
    if (c == 3 || c == 4 || c == 5 || c == 8) {
        return 1; /* FlWqCode.lean:22  DONE, FAILED, EXPIRED, SKIPPED */
    } else {
        return 0; /* FlWqCode.lean:22  PENDING, RUNNING, HELD, QUEUED, and numbers that are no state */
    }
}

int main(void)
{
    for (unsigned from = 0; from < 256; from++) {
        for (unsigned to = 0; to < 256; to++) {
            uint8_t hand = ref_transition_allowed((uint8_t)from, (uint8_t)to);
            uint8_t gen = fl_wq_transition_allowed((uint8_t)from, (uint8_t)to);
            if (hand != gen) {
                printf("ref_check: MISMATCH transition_allowed(from=%u, to=%u): hand-written %u, generated %u\n",
                       from, to, (unsigned)hand, (unsigned)gen);
                return 1;
            }
        }
    }
    for (unsigned c = 0; c < 256; c++) {
        uint8_t hand = ref_state_terminal((uint8_t)c);
        uint8_t gen = fl_wq_state_terminal((uint8_t)c);
        if (hand != gen) {
            printf("ref_check: MISMATCH state_terminal(%u): hand-written %u, generated %u\n", c, (unsigned)hand,
                   (unsigned)gen);
            return 1;
        }
    }
    printf("ref_check: hand-written C equals the generated C for all 65536 pairs and 256 states\n");
    return 0;
}
