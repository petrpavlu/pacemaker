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

#include <glib.h>

static void
null_input_table(void **state)
{
    assert_null(pcmk__str_table_dup(NULL));
}

static void
empty_input_table(void **state)
{
    GHashTable *tbl = pcmk__strkey_table(free, free);
    GHashTable *copy = NULL;

    copy = pcmk__str_table_dup(tbl);
    assert_int_equal(g_hash_table_size(copy), 0);

    g_hash_table_destroy(tbl);
    g_hash_table_destroy(copy);
}

static void
regular_input_table(void **state)
{
    GHashTable *tbl = pcmk__strkey_table(free, free);
    GHashTable *copy = NULL;

    g_hash_table_insert(tbl, strdup("abc"), strdup("123"));
    g_hash_table_insert(tbl, strdup("def"), strdup("456"));
    g_hash_table_insert(tbl, strdup("ghi"), strdup("789"));

    copy = pcmk__str_table_dup(tbl);
    assert_int_equal(g_hash_table_size(copy), 3);

    assert_string_equal(g_hash_table_lookup(tbl, "abc"), "123");
    assert_string_equal(g_hash_table_lookup(tbl, "def"), "456");
    assert_string_equal(g_hash_table_lookup(tbl, "ghi"), "789");

    g_hash_table_destroy(tbl);
    g_hash_table_destroy(copy);
}

int main(int argc, char **argv) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(null_input_table),
        cmocka_unit_test(empty_input_table),
        cmocka_unit_test(regular_input_table),
    };

    cmocka_set_message_output(CM_OUTPUT_TAP);
    return cmocka_run_group_tests(tests, NULL, NULL);
}
