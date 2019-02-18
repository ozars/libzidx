#include<stdio.h>
#include<stdlib.h>

#include<check.h>
#include<streamlike.h>
#include<streamlike/file.h>

#include "utils.h"
#include "zidx.c"
#include "zidx_streamlike.h"

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
streamlike_t *comp_stream;
streamlike_t *zx_stream;
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

    ZX_LOG("Compressed/uncompressed test file size: %jd/%jd.",
           comp_size, uncomp_size);
#endif
}

void unchecked_teardown()
{
    int comp_file_deleted;

    free(uncomp_data);
    uncomp_data = NULL;
}

/* Core tests */

void setup_core()
{
    const char *msg = NULL;
    int zx_ret;

    ck_assert_msg(fseek(comp_file, 0, SEEK_SET) == 0,
                  "Couldn't rewind temporary compressed file.");

    comp_stream = sl_fopen2(comp_file);
    ck_assert_msg(comp_stream, "Couldn't initialize zidx file stream.");

    zx_index = zidx_index_create();
    ck_assert_msg(zx_index, "Couldn't allocate space for index.");

    zx_ret = zidx_index_init(zx_index, comp_stream);
    ck_assert_msg(zx_ret == 0, "Couldn't initialize zidx index.");

    zx_stream = sl_zx_open(zx_index);
    ck_assert_msg(zx_stream, "Couldn't initialize zidx streamlike object.");
}

void teardown_core()
{
    int zx_ret;

    zx_ret = zidx_index_destroy(zx_index);
    ck_assert_msg(zx_ret == 0, "Couldn't destroy zidx index.");

    free(zx_index);
    zx_index = NULL;

    ck_assert_msg(sl_fclose(comp_stream) == 0,
                  "Couldn't close streamlike file.");
    comp_stream = NULL;

    sl_zx_close(zx_stream);
    zx_stream = NULL;
}

#define template_comp_file_read(readf, context) \
    do { \
        int zx_ret; \
        uint8_t buffer[1024]; \
        uint8_t next_byte; \
        int file_completed; \
        int i; \
        long offset; \
        \
        ZX_LOG("TEST: Reading compressed file."); \
        \
        offset = 0; \
        file_completed = 0; \
        while (!file_completed) { \
            zx_ret = readf(context, buffer, sizeof(buffer)); \
            ck_assert_msg(zx_ret >= 0, "Error while reading file: %d.", zx_ret); \
            \
            if (zx_ret == 0) { \
                file_completed = 1; \
            } else { \
                for(i = 0; i < zx_ret; i++) \
                { \
                    next_byte = uncomp_data[offset]; \
                    ck_assert_msg(buffer[i] == next_byte, \
                                "Incorrect data at offset %ld, " \
                                "expected %u (0x%02X), got %u (0x%02X).", \
                                offset, next_byte, next_byte, buffer[i], \
                                buffer[i]); \
                    offset++; \
                } \
            } \
        } \
    } while(0)

START_TEST(test_comp_file_read)
{
    template_comp_file_read(zidx_read, zx_index);
}
END_TEST

START_TEST(test_comp_file_sl_read)
{
    template_comp_file_read(sl_read, zx_stream);
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

static
int sl_seek_wrapper(streamlike_t *stream, off_t offset)
{
    return sl_seek(stream, offset, SL_SEEK_SET);
}

#define template_two_seek_passes_over_file(readf, seekf, tellf, context) \
    do { \
        int ret; \
        int r_len; \
        uint8_t buffer[1024]; \
        int i; \
        long offset; \
        long step = 1023; \
        long last_offset; \
        \
        last_offset = zx_index->offset.uncomp; \
        \
        offset = last_offset - step; \
        while (offset > 0) { \
            ret = seekf(context, offset); \
            ck_assert_msg(ret == 0, "Seek returned %d at offset %ld", \
                                    ret, offset); \
            \
            r_len = readf(context, buffer, sizeof(buffer)); \
            ck_assert_msg(r_len >= 0, "Read returned %d at offset %ld", \
                                    r_len, offset); \
            \
            ck_assert_msg(memcmp(buffer, uncomp_data + offset, r_len) == 0, \
                                "Incorrect data at offset %ld, " \
                                "expected %u (0x%02X), got %u (0x%02X).", \
                                offset, uncomp_data[offset], \
                                uncomp_data[offset], buffer[0], buffer[i]); \
            \
            offset -= step; \
        } \
        \
        do { \
            offset = tellf(context) + step; \
            \
            ret = seekf(context, offset); \
            ck_assert_msg(ret == 0 || \
                        (offset >= last_offset && ret == ZX_ERR_STREAM_EOF), \
                        "Seek returned %d at offset %ld", ret, offset); \
            \
            r_len = readf(context, buffer, sizeof(buffer)); \
            ck_assert_msg(r_len >= 0, "Read returned %d at offset %ld", \
                                    ret, offset); \
            \
            ck_assert_msg(memcmp(buffer, uncomp_data + offset, r_len) == 0, \
                                "Incorrect data at offset %ld, " \
                                "expected %u (0x%02X), got %u (0x%02X).", \
                                offset, uncomp_data[offset], \
                                uncomp_data[offset], buffer[0], buffer[i]); \
        } while(r_len != 0); \
    } while(0)

#define template_comp_file_seek(readf, seekf, tellf, context) \
    do { \
        int ret; \
        int num_blocks = 0; \
        \
        ZX_LOG("TEST: Seeking compressed file with all possbile " \
               "checkpoints."); \
        \
        ret = zidx_build_index_ex(zx_index, comp_file_seek_callback, \
                                  &num_blocks); \
        ck_assert_msg(ret == ZX_RET_OK, "Error while building index (%d).", \
                                        ret); \
        \
        template_two_seek_passes_over_file(readf, seekf, tellf, context); \
    } while(0)

START_TEST(test_comp_file_seek)
{
    template_comp_file_seek(zidx_read, zidx_seek, zidx_tell, zx_index);
}
END_TEST

START_TEST(test_comp_file_sl_seek)
{
    template_comp_file_seek(sl_read, sl_seek_wrapper, sl_tell, zx_stream);
}
END_TEST

#define template_comp_file_seek_comp_space(readf, seekf, tellf, context) \
    do { \
        int ret; \
        int num_blocks = 0; \
        \
        ZX_LOG("TEST: Seeking compressed file with 1MB compressed spaces."); \
        \
        ret = zidx_build_index(zx_index, 1048576, 0); \
        ck_assert_msg(ret == ZX_RET_OK, "Error while building index (%d).", \
                                        ret); \
        \
        template_two_seek_passes_over_file(readf, seekf, tellf, context); \
    } while(0)

START_TEST(test_comp_file_seek_comp_space)
{
    template_comp_file_seek_comp_space(zidx_read, zidx_seek, zidx_tell,
                                       zx_index);
}
END_TEST

START_TEST(test_comp_file_sl_seek_comp_space)
{
    template_comp_file_seek_comp_space(sl_read, sl_seek_wrapper, sl_tell,
                                       zx_stream);
}
END_TEST

#define template_comp_file_seek_uncomp_space(readf, seekf, tellf, context) \
    do { \
        int ret; \
        int num_blocks = 0; \
        \
        ZX_LOG("TEST: Seeking compressed file with 1MB uncompressed spaces."); \
        \
        ret = zidx_build_index(zx_index, 1048576, 1); \
        ck_assert_msg(ret == ZX_RET_OK, "Error while building index (%d).", \
                                        ret); \
        \
        template_two_seek_passes_over_file(readf, seekf, tellf, context); \
    } while(0)

START_TEST(test_comp_file_seek_uncomp_space)
{
    template_comp_file_seek_uncomp_space(zidx_read, zidx_seek, zidx_tell,
                                         zx_index);
}
END_TEST

START_TEST(test_comp_file_sl_seek_uncomp_space)
{
    template_comp_file_seek_uncomp_space(sl_read, sl_seek_wrapper, sl_tell,
                                         zx_stream);
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

    streamlike_t *index_stream = NULL;
    zidx_index *new_index;
    streamlike_t *new_stream;
    zidx_checkpoint *new_ckp;
    zidx_checkpoint *old_ckp;

    ZX_LOG("TEST: Importing/exporting index.");

    zx_ret = zidx_build_index_ex(zx_index, comp_file_seek_callback,
                                 &num_blocks);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Error while building index (%d).",
                  zx_ret);

    /* TODO: Use tmpnam instead. */
    index_stream = sl_fopen("index_file.tmp", "wb+");
    ck_assert_msg(index_stream, "Couldn't open index file.");

    zx_ret = zidx_export(zx_index, index_stream);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Couldn't export index (%d).", zx_ret);

    new_index = zidx_index_create();
    ck_assert_msg(new_index, "Couldn't create new index.");

    new_stream = sl_fopen2(comp_file);
    ck_assert_msg(new_stream, "Couldn't create new stream.");

    zx_ret = zidx_index_init(new_index, new_stream);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Couldn't initialize index (%d).",
                                       zx_ret);

    ck_assert_msg(sl_seek(index_stream, 0, SL_SEEK_SET) == 0,
                  "Couldn't rewind file.");

    zx_ret = zidx_import(new_index, index_stream);
    ck_assert_msg(zx_ret == ZX_RET_OK, "Couldn't import from file (%d).",
                                       zx_ret);

    ck_assert_msg(new_index->list_count == zx_index->list_count,
                  "Couldn't match the number of elements on new (%d) and old "
                  "(%d) list.", new_index->list_count, zx_index->list_count);

    ck_assert_msg(new_index->compressed_size == zx_index->compressed_size,
                  "Couldn't match compressed sizes on new (%jd) and old (%jd) "
                  "list.", (intmax_t)new_index->compressed_size,
                  (intmax_t)zx_index->compressed_size);

    ck_assert_msg(new_index->uncompressed_size == zx_index->uncompressed_size,
                  "Couldn't match uncompressed sizes on new (%jd) and old "
                  "(%jd) list.", (intmax_t)new_index->uncompressed_size,
                  (intmax_t)zx_index->uncompressed_size);

    for (i = 0; i < new_index->list_count; i++)
    {
        new_ckp = &new_index->list[i];
        old_ckp = &zx_index->list[i];
        ck_assert_msg(new_ckp->window_length == old_ckp->window_length,
                      "Couldn't match window lengths at checkpoint %d.", i);

        ck_assert_msg(new_ckp->offset.comp == old_ckp->offset.comp,
                      "Couldn't match compressed offsets at checkpoint %d.",
                      i);

        ck_assert_msg(new_ckp->offset.uncomp == old_ckp->offset.uncomp,
                      "Couldn't match uncompressed offsets at checkpoint %d.",
                      i);

        ck_assert_msg(new_ckp->offset.comp_bits_count
                            == old_ckp->offset.comp_bits_count,
                      "Couldn't match boundary bits count at checkpoint %d.",
                      i);

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
        zx_ret = zidx_seek(new_index, offset);
        ck_assert_msg(zx_ret == 0, "Seek returned %d at offset %ld",
                                   zx_ret, offset);

        r_len = zidx_read(new_index, buffer, sizeof(buffer));
        ck_assert_msg(r_len >= 0, "Read returned %d at offset %ld",
                                  zx_ret, offset);

        ck_assert_msg(memcmp(buffer, uncomp_data + offset, r_len) == 0,
                              "Incorrect data at offset %ld, "
                              "expected %u (0x%02X), got %u (0x%02X).",
                              offset, uncomp_data[offset], uncomp_data[offset],
                              buffer[0], buffer[i]);

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

    /* Core test cases. */
    tc_core = tcase_create("Core");

    /* They can be a bit long. */
    tcase_set_timeout(tc_core, ZX_TEST_LONG_TIMEOUT);

    tcase_add_unchecked_fixture(tc_core, unchecked_setup, unchecked_teardown);
    tcase_add_checked_fixture(tc_core, setup_core, teardown_core);

    tcase_add_test(tc_core, test_comp_file_read);
    tcase_add_test(tc_core, test_comp_file_sl_read);
    tcase_add_test(tc_core, test_comp_file_seek);
    tcase_add_test(tc_core, test_comp_file_sl_seek);
    tcase_add_test(tc_core, test_comp_file_seek_comp_space);
    tcase_add_test(tc_core, test_comp_file_sl_seek_comp_space);
    tcase_add_test(tc_core, test_comp_file_seek_uncomp_space);
    tcase_add_test(tc_core, test_comp_file_sl_seek_uncomp_space);
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
