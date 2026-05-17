/*
 * **Px** (*P1–P9*) contract prelude — include this (or **contract_foundations.h**)
 * at the top of every future header under **contracts/extensions/** (or elsewhere)
 * so **P0 stays the single inherited base** and dependency order never inverts.
 *
 * This file intentionally adds no new types; it exists as a stable, greppable
 * entry point for “Px contract depends on P0 only through here.”
 * **contract_compile_ext.h** is pulled in via **contract_foundations.h** and
 * documents compile-time extension markers (**FL_CONTRACT_HAS_***) and the
 * **FL_CONTRACT_COMPILE_EXTENSIONS** master switch.
 */
#ifndef FL_CONTRACT_EXTEND_H
#define FL_CONTRACT_EXTEND_H

#include "contract_foundations.h"

#ifndef FL_CONTRACT_P0_FOUNDATIONS_REV
#error "contract_foundations.h must define FL_CONTRACT_P0_FOUNDATIONS_REV"
#endif

#endif /* FL_CONTRACT_EXTEND_H */
