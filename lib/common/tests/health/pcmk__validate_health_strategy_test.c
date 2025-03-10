/*
 * Copyright 2022 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

// Test functions

static void
valid_strategy(void **state) {
    assert_true(pcmk__validate_health_strategy("none"));
    assert_true(pcmk__validate_health_strategy("None"));
    assert_true(pcmk__validate_health_strategy("NONE"));
    assert_true(pcmk__validate_health_strategy("NoNe"));
    assert_true(pcmk__validate_health_strategy("migrate-on-red"));
    assert_true(pcmk__validate_health_strategy("only-green"));
    assert_true(pcmk__validate_health_strategy("progressive"));
    assert_true(pcmk__validate_health_strategy("custom"));
}

static void
invalid_strategy(void **state) {
    assert_false(pcmk__validate_health_strategy(NULL));
    assert_false(pcmk__validate_health_strategy(""));
    assert_false(pcmk__validate_health_strategy("none to speak of"));
    assert_false(pcmk__validate_health_strategy("customized"));
}

int
main(int argc, char **argv) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(valid_strategy),
        cmocka_unit_test(invalid_strategy),
    };

    cmocka_set_message_output(CM_OUTPUT_TAP);
    return cmocka_run_group_tests(tests, NULL, NULL);
}
