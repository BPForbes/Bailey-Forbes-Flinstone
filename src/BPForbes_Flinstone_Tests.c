/* BPForbes_Flinstone_Tests.c (moved into src/) */
#include <CUnit/Basic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>

/* Include CUnit header (assumes CUnit is installed) */
#include <CUnit/Basic.h>

/* Define UNIT_TEST so that interpreter.c uses its test?friendly version */
#ifndef UNIT_TEST
#define UNIT_TEST
#endif

/* Include project headers */
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "disk.h"
#include "cluster.h"
#include "fs.h"
#include "mem_domain.h"
#include "fs_service_glue.h"
#include "fs_facade.h"
#include "threadpool.h"
#include "interpreter.h"

/* Directly include interpreter.c so that its definitions are compiled into this test.
   (Ensure that interpreter.c checks UNIT_TEST so that interactive_shell() is a stub.)
*/
#include "interpreter.c"

/* Tests... (file content unchanged) */

int main(void)
{
    if (CUE_SUCCESS != CU_initialize_registry())
         return CU_get_error();

    CU_pSuite suite = CU_add_suite("Shell System Full Suite", suite_setup, suite_cleanup);
    if (NULL == suite) {
         CU_cleanup_registry();
         return CU_get_error();
    }

    /* ... register tests ... */

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}
