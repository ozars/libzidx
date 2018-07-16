#include<stdio.h>
#include<stdlib.h>

#include<check.h>

#include "utils.h"
#include "zidx.h"

#ifndef RANDOM_SEED
#define RANDOM_SEED (0UL)
#endif

#ifndef COMP_FILE_LENGTH
#define COMP_FILE_LENGTH (100 * (1 << 20))
#endif

const char *comp_file_path;
zidx_comp_stream *comp_stream;

void unchecked_setup()
{
    comp_file_path = get_random_compressed_file(RANDOM_SEED, COMP_FILE_LENGTH);

    ck_assert_msg(comp_file_path, "Couldn't create temporary compressed file.");
}

void unchecked_teardown()
{
    int comp_file_deleted;
    comp_file_deleted = remove(comp_file_path);

    ck_assert_msg(comp_file_deleted == 0, "Couldn't remove temporary compressed "
                                          "file.");
}

void setup()
{
    int zidx_ret;
    FILE *f;
    const char *msg;

    f = fopen(comp_file_path, "rb");
    if (f == NULL) {
        msg = "Couldn't open compressed file.";
        goto fail;
    }

    comp_stream = malloc(sizeof(zidx_comp_stream));
    if (comp_stream == NULL) {
        msg = "Couldn't allocate space form zidx compressed stream.";
        goto fail;
    }

    zidx_ret = zidx_comp_file_init(comp_stream, f);
    if (zidx_ret != 0) {
        msg = "Couldn't initialize zidx file stream.";
        goto fail;
    }

    return;

fail:
    if (f) fclose(f);
    free(comp_stream);
    ck_abort_msg(msg);
}

void teardown()
{
    FILE *f;

    f = comp_stream->context;

    free(comp_stream);

    ck_assert_msg(fclose(f) == 0, "File couldn't be closed.");
}

START_TEST(test_comp_file_init)
{
    ck_assert(comp_stream->read    == zidx_raw_file_read);
    ck_assert(comp_stream->seek    == zidx_raw_file_seek);
    ck_assert(comp_stream->tell    == zidx_raw_file_tell);
    ck_assert(comp_stream->length  == zidx_raw_file_length);
    ck_assert(comp_stream->eof     == zidx_raw_file_eof);
    ck_assert(comp_stream->error   == zidx_raw_file_error);
    ck_assert(comp_stream->context != NULL);
}
END_TEST

START_TEST(test_comp_file_read)
{
}
END_TEST

Suite* libzidx_test_suite()
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("libzidx");

    tc_core = tcase_create("Core");

    tcase_add_unchecked_fixture(tc_core, unchecked_setup, unchecked_teardown);
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_comp_file_init);
    tcase_add_test(tc_core, test_comp_file_read);

    suite_add_tcase(s, tc_core);

    return s;
}

int main()
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = libzidx_test_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_VERBOSE);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
