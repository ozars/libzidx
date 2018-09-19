#include<stdio.h>
#include<stdlib.h>

#include<check.h>

#include "utils.h"
#include "zidx.c"

#ifndef ZX_TEST_RANDOM_SEED
#define ZX_TEST_RANDOM_SEED (0UL)
#endif

#ifndef ZX_TEST_COMP_FILE_LENGTH
#define ZX_TEST_COMP_FILE_LENGTH (10 * (1 << 20))
#endif

#ifndef ZX_TEST_LONG_TIMEOUT
#define ZX_TEST_LONG_TIMEOUT (60)
#endif

FILE *comp_file;
zidx_stream *comp_stream;
zidx_index *zx_index;
uint8_t *uncomp_data;

void unchecked_setup()
{
    uncomp_data = malloc(ZX_TEST_COMP_FILE_LENGTH);
    ck_assert_msg(uncomp_data, "Couldn't allocate space for temporary data.");

    intmax_t comp_size;
    intmax_t uncomp_size = ZX_TEST_COMP_FILE_LENGTH;

    comp_file = get_random_compressed_file(ZX_TEST_RANDOM_SEED,
                                           ZX_TEST_COMP_FILE_LENGTH,
                                           uncomp_data);

    ck_assert_msg(comp_file, "Couldn't create temporary compressed file.");

#ifdef ZX_DEBUG
    ck_assert_msg(fseek(comp_file, 0, SEEK_END) == 0,
                  "Couldn't seek on temporary compressed file.");

    comp_size = ftell(comp_file);
    ck_assert_msg(comp_size != -1,
                  "Couldn't get position in temporary compressed file.");

    ck_assert_msg(fseek(comp_file, 0, SEEK_SET) == 0,
                  "Couldn't seek on temporary compressed file.");

    ZX_LOG("Compressed/uncompressed test file size: %jd/%jd.\n",
           comp_size, uncomp_size);
#endif
}

void unchecked_teardown()
{
    int comp_file_deleted;

    free(uncomp_data);
    uncomp_data = NULL;
}

/* Stream API tests */

void setup_stream_api()
{
    int zx_ret;
    const char *msg;

    comp_stream = malloc(sizeof(zidx_stream));
    ck_assert_msg(comp_stream, "Couldn't allocate space form zidx compressed "
                               "stream.");

    ck_assert_msg(fseek(comp_file, 0, SEEK_SET) == 0,
                  "Couldn't rewind temporary compressed file.");

    comp_stream = zidx_stream_from_file(comp_file);
    ck_assert_msg(comp_stream != NULL, "Couldn't initialize zidx file stream.");
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
    ck_assert(comp_stream->write   == zidx_raw_file_write);
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
    int zx_ret;
    zidx_checkpoint *ckp;

    if (!last_block) {
        (*num_blocks)++;
    }

    ckp = zidx_create_checkpoint();
    ck_assert(ckp != NULL);
    zx_ret = zidx_fill_checkpoint(index, ckp, offset);
    ck_assert(zx_ret == 0);

    zx_ret = zidx_add_checkpoint(index, ckp);
    ck_assert(zx_ret == 0);

    return 0;
}

void make_two_seek_passes_over_file()
{
    int zx_ret;
    int r_len;
    uint8_t buffer[1024];
    int i;
    long offset;
    long step = 1023;
    long last_offset;

    last_offset = zx_index->offset.uncomp;

    offset = last_offset - step;
    while (offset > 0) {
        zx_ret = zidx_seek(zx_index, offset, ZX_SEEK_SET);
        ck_assert_msg(zx_ret == 0, "Seek returned %d at offset %ld", zx_ret, offset);

        r_len = zidx_read(zx_index, buffer, sizeof(buffer));
        ck_assert_msg(r_len >= 0, "Read returned %d at offset %ld", zx_ret, offset);

        ck_assert_msg(memcmp(buffer, uncomp_data + offset, r_len) == 0,
                              "Incorrect data at offset %ld, "
                              "expected %u (0x%02X), got %u (0x%02X).",
                              offset, uncomp_data[offset], uncomp_data[offset], buffer[0],
                              buffer[i]);

        offset -= step;
    }

    do {
        offset = zidx_tell(zx_index) + step;

        zx_ret = zidx_seek(zx_index, offset, ZX_SEEK_SET);
        ck_assert_msg(zx_ret == 0 ||
                      (offset >= last_offset && zx_ret == ZX_ERR_STREAM_EOF),
                      "Seek returned %d at offset %ld", zx_ret, offset);

        r_len = zidx_read(zx_index, buffer, sizeof(buffer));
        ck_assert_msg(r_len >= 0, "Read returned %d at offset %ld", zx_ret, offset);

        ck_assert_msg(memcmp(buffer, uncomp_data + offset, r_len) == 0,
                              "Incorrect data at offset %ld, "
                              "expected %u (0x%02X), got %u (0x%02X).",
                              offset, uncomp_data[offset], uncomp_data[offset], buffer[0],
                              buffer[i]);
    } while(r_len != 0);

}

START_TEST(test_comp_file_seek)
{
    int zx_ret;
    int num_blocks = 0;

    ZX_LOG("TEST: Seeking compressed file with all possbile checkpoints.\n");

    zx_ret = zidx_build_index_ex(zx_index, comp_file_seek_callback, &num_blocks);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Error while building index (%d).", zx_ret);

    make_two_seek_passes_over_file();
}
END_TEST

START_TEST(test_comp_file_seek_comp_space)
{
    int zx_ret;
    int num_blocks = 0;

    ZX_LOG("TEST: Seeking compressed file with 1MB compressed spaces.\n");

    zx_ret = zidx_build_index(zx_index, 1048576, 0);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Error while building index (%d).", zx_ret);

    make_two_seek_passes_over_file();
}
END_TEST

START_TEST(test_comp_file_seek_uncomp_space)
{
    int zx_ret;
    int num_blocks = 0;

    ZX_LOG("TEST: Seeking compressed file with 1MB uncompressed spaces.\n");

    zx_ret = zidx_build_index(zx_index, 1048576, 1);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Error while building index (%d).", zx_ret);

    make_two_seek_passes_over_file();
}
END_TEST

START_TEST(test_export_import)
{
    int zx_ret;
    int r_len;
    uint8_t buffer[1024];
    int file_completed;
    int i;
    long offset;
    long step = 1024;
    int num_blocks = 0;

    FILE *index_file = NULL;
    zidx_index *new_index;
    zidx_stream *new_stream;
    zidx_checkpoint *new_ckp;
    zidx_checkpoint *old_ckp;

    zx_ret = zidx_build_index_ex(zx_index, comp_file_seek_callback, &num_blocks);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Error while building index (%d).",
                  zx_ret);

    index_file = fopen("index_file.tmp", "wb+"); /* TODO: Use tmpfile instead. */
    ck_assert_msg(index_file, "Couldn't open index file.");

    zx_ret = zidx_export(zx_index, index_file);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Couldn't export index (%d).", zx_ret);

    new_index = zidx_index_create();
    ck_assert_msg(new_index, "Couldn't create new index.");

    new_stream = zidx_stream_from_file(comp_file);
    ck_assert_msg(new_stream, "Couldn't create new stream.");

    zx_ret = zidx_index_init(new_index, new_stream);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Couldn't initialize index (%d).", zx_ret);

    ck_assert_msg(fseek(index_file, 0, SEEK_SET) == 0, "Couldn't rewind file.", zx_ret);

    zx_ret = zidx_import(new_index, index_file);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Couldn't import from file (%d).", zx_ret);

    ck_assert_msg(new_index->list_count == zx_index->list_count,
                  "Couldn't match the number of elements on new (%d) and old (%d) list.",
                  new_index->list_count, zx_index->list_count);

    ck_assert_msg(new_index->compressed_size == zx_index->compressed_size,
                  "Couldn't match compressed sizes on new (%jd) and old (%jd) list.",
                  (intmax_t)new_index->compressed_size,
                  (intmax_t)zx_index->compressed_size);

    ck_assert_msg(new_index->uncompressed_size == zx_index->uncompressed_size,
                  "Couldn't match uncompressed sizes on new (%jd) and old (%jd) list.",
                  (intmax_t)new_index->uncompressed_size,
                  (intmax_t)zx_index->uncompressed_size);

    for (i = 0; i < new_index->list_count; i++)
    {
        new_ckp = &new_index->list[i];
        old_ckp = &zx_index->list[i];
        ck_assert_msg(new_ckp->window_length == old_ckp->window_length,
                      "Couldn't match window lengths at checkpoint %d.", i);

        ck_assert_msg(new_ckp->offset.comp == old_ckp->offset.comp,
                      "Couldn't match compressed offsets at checkpoint %d.", i);

        ck_assert_msg(new_ckp->offset.uncomp == old_ckp->offset.uncomp,
                      "Couldn't match uncompressed offsets at checkpoint %d.", i);

        ck_assert_msg(new_ckp->offset.comp_bits_count
                            == old_ckp->offset.comp_bits_count,
                      "Couldn't match boundary bits count at checkpoint %d.", i);

        ck_assert_msg(new_ckp->offset.comp_byte == old_ckp->offset.comp_byte,
                      "Couldn't match boundary byte at checkpoint %d.", i);

        if (new_ckp->window_length > 0) {
            ck_assert_msg(!memcmp(new_ckp->window_data, old_ckp->window_data,
                                  new_ckp->window_length),
                          "Couldn't match window data at checkpoint %d.", i);
        } else {
            ck_assert_msg(new_ckp->window_data == NULL,
                          "New checkpoint %d window should be NULL.", i);
            ck_assert_msg(old_ckp->window_data == NULL,
                          "Old checkpoint %d window should be NULL.", i);
        }
    }

    offset = zx_index->offset.uncomp - step;
    while (offset > 0) {
        zx_ret = zidx_seek(new_index, offset, ZX_SEEK_SET);
        ck_assert_msg(zx_ret == 0, "Seek returned %d at offset %ld", zx_ret, offset);

        r_len = zidx_read(new_index, buffer, sizeof(buffer));
        ck_assert_msg(r_len >= 0, "Read returned %d at offset %ld", zx_ret, offset);

        ck_assert_msg(memcmp(buffer, uncomp_data + offset, r_len) == 0,
                              "Incorrect data at offset %ld, "
                              "expected %u (0x%02X), got %u (0x%02X).",
                              offset, uncomp_data[offset], uncomp_data[offset], buffer[0],
                              buffer[i]);

        offset -= step;
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
    tcase_add_test(tc_core, test_comp_file_seek_comp_space);
    tcase_add_test(tc_core, test_comp_file_seek_uncomp_space);
    tcase_add_test(tc_core, test_export_import);

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
