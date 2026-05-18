/**
 * Compile-time **contract extensions** (optional helpers and markers).
 *
 * **Model (matches “optional contract.c + HAS_* marker” teaching pattern):**
 *   - Optional translation units or headers define **predetermined** helpers
 *     (extra I/O discipline, shared primitives, small wrappers).
 *   - They also define **`FL_CONTRACT_HAS_<Feature>`** (1) when present so call
 *     sites can use `#if defined(FL_CONTRACT_HAS_<Feature>)` … `#else` … for a
 *     minimal baseline path.
 *   - Nothing here is required to build the core tree; extensions **extend**
 *     behaviour when compiled in.
 *
 * **Later P0–P9 work:** when full phase contracts land, **treat each marker as a
 * gate** for code that must follow the same **data I/O**, **primitive**, and
 * **function** rules as the normative module-contract narrative (see
 * **docs/ROADMAP.md** module contracts + **Appendix D** for bare-metal paths).
 * Prefer one marker per cohesive extension package, not per line of code.
 *
 * **Build:** pass **-DFL_CONTRACT_COMPILE_EXTENSIONS=1** (and optional
 * **-DFL_CONTRACT_HAS_…=1** from generated **config** or **CMake**) when enabling
 * packaged extensions in a TU or target. Default remains off so behaviour is
 * unchanged unless opted in.
 */
#ifndef FL_CONTRACT_COMPILE_EXT_H
#define FL_CONTRACT_COMPILE_EXT_H

#ifndef FL_CONTRACT_COMPILE_EXTENSIONS
/**
 * Master switch for packaged compile-time extensions (0 = off).
 * Turn on per-target from the build when a subtree opts into extension helpers.
 */
#define FL_CONTRACT_COMPILE_EXTENSIONS 0
#endif

#endif /* FL_CONTRACT_COMPILE_EXT_H */
