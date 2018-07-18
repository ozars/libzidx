#include<stdio.h>
#include<stdlib.h>

#include<check.h>

#include "utils.h"
#include "zidx.h"

#ifndef ZX_TEST_RANDOM_SEED
#define ZX_TEST_RANDOM_SEED (0UL)
#endif

#ifndef ZX_TEST_COMP_FILE_LENGTH
#define ZX_TEST_COMP_FILE_LENGTH (10 * (1 << 20))
#endif

#ifndef ZX_TEST_LONG_TIMEOUT
#define ZX_TEST_LONG_TIMEOUT (30)
#endif

const char *comp_file_path;
zidx_comp_stream *comp_stream;
zidx_index *zx_index;
uint8_t *uncomp_data;

void unchecked_setup()
{
    uncomp_data = malloc(ZX_TEST_COMP_FILE_LENGTH);
    ck_assert_msg(uncomp_data, "Couldn't allocate space for temporary data.");

    comp_file_path = get_random_compressed_file(ZX_TEST_RANDOM_SEED,
                                                ZX_TEST_COMP_FILE_LENGTH,
                                                uncomp_data);

    ck_assert_msg(comp_file_path, "Couldn't create temporary compressed file.");
}

void unchecked_teardown()
{
    int comp_file_deleted;
    comp_file_deleted = remove(comp_file_path);

    ck_assert_msg(comp_file_deleted == 0, "Couldn't remove temporary compressed "
                                          "file.");

    free(uncomp_data);
    uncomp_data = NULL;
}

/* Stream API tests */

void setup_stream_api()
{
    int zx_ret;
    FILE *f;
    const char *msg;

    f = fopen(comp_file_path, "rb");
    ck_assert_msg(f, "Couldn't open compressed file.");

    comp_stream = malloc(sizeof(zidx_comp_stream));
    ck_assert_msg(comp_stream, "Couldn't allocate space form zidx compressed "
                               "stream.");

    zx_ret = zidx_comp_file_init(comp_stream, f);
    ck_assert_msg(zx_ret == 0, "Couldn't initialize zidx file stream.");
}

void teardown_stream_api()
{
    FILE *f;

    f = comp_stream->context;

    free(comp_stream);
    comp_stream = NULL;

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

/* Core tests */

void setup_core()
{
    const char *msg = NULL;
    int zx_ret;

    setup_stream_api();

    zx_index = malloc(sizeof(zidx_index));
    ck_assert_msg(zx_index, "Couldn't allocate space for index.");

    zx_ret = zidx_index_init(zx_index, comp_stream);
    ck_assert_msg(zx_ret == 0, "Couldn't initialize zidx index.");
}

void teardown_core()
{
    int zx_ret;

    zx_ret = zidx_index_destroy(zx_index);
    ck_assert_msg(zx_ret == 0, "Couldn't destroy zidx index.");

    free(zx_index);
    zx_index = NULL;

    teardown_stream_api();
}

START_TEST(test_comp_file_read)
{
    int zx_ret;
    uint8_t buffer[1024];
    uint8_t next_byte;
    int file_completed;
    int i;
    long offset;

    offset = 0;
    file_completed = 0;
    while (!file_completed) {
        zx_ret = zidx_read(zx_index, buffer, sizeof(buffer));
        ck_assert_msg(zx_ret >= 0, "Error while reading file: %d.", zx_ret);

        if (zx_ret == 0) {
            file_completed = 1;
        } else {
            for(i = 0; i < zx_ret; i++)
            {
                next_byte = uncomp_data[offset];
                ck_assert_msg(buffer[i] == next_byte,
                              "Incorrect data at offset %ld, "
                              "expected %u (0x%02X), got %u (0x%02X).",
                              offset, next_byte, next_byte, buffer[i],
                              buffer[i]);
                offset++;
            }
        }
    }
}
END_TEST

int comp_file_seek_callback(void *context,
                            zidx_index *index,
                            zidx_checkpoint_offset *offset,
                            int last_block)
{
    int *num_blocks = (int*) context;

    if (!last_block) {
        num_blocks++;
    }

    printf("%ld:%d (%02x) %ld %d\n", offset->comp,
                                     offset->comp_bits_count,
                                     offset->comp_byte,
                                     offset->uncomp,
                                     last_block);

    return 0;
}

START_TEST(test_comp_file_seek)
{
    int zx_ret;
    uint8_t buffer[1024];
    uint8_t next_byte;
    int file_completed;
    int i;
    long offset;
    int num_blocks = 0;

    offset = 0;
    file_completed = 0;
    while (!file_completed) {
        zx_ret = zidx_read_advanced(zx_index,
                                    buffer,
                                    sizeof(buffer),
                                    comp_file_seek_callback,
                                    &num_blocks);
        ck_assert_msg(zx_ret >= 0, "Error while reading file: %d.", zx_ret);

        if (zx_ret == 0) {
            file_completed = 1;
        }
    }

}
END_TEST

Suite* libzidx_test_suite()
{
    Suite *s;
    TCase *tc_stream_api;
    TCase *tc_core;

    s = suite_create("libzidx");

    /* Stream API test cases. */
    tc_stream_api = tcase_create("Stream API");

    tcase_add_unchecked_fixture(tc_stream_api, unchecked_setup,
                                unchecked_teardown);
    tcase_add_checked_fixture(tc_stream_api, setup_stream_api,
                              teardown_stream_api);

    tcase_add_test(tc_stream_api, test_comp_file_init);

    suite_add_tcase(s, tc_stream_api);

    /* Core test cases. */
    tc_core = tcase_create("Core");

    /* They can be a bit long. */
    tcase_set_timeout(tc_core, ZX_TEST_LONG_TIMEOUT);

    tcase_add_unchecked_fixture(tc_core, unchecked_setup, unchecked_teardown);
    tcase_add_checked_fixture(tc_core, setup_core, teardown_core);

    tcase_add_test(tc_core, test_comp_file_read);
    tcase_add_test(tc_core, test_comp_file_seek);

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

    srunner_run_all(sr, CK_ENV);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
