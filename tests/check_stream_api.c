#include<check.h>
#include<stdlib.h>

#include "zidx.h"

START_TEST(test_comp_file_init)
{
    void *context;
    zidx_comp_stream *comp_stream;

    context     = (void *) 0xBEEF;
    comp_stream = malloc(sizeof(zidx_comp_stream));

    ck_assert(zidx_comp_file_init(comp_stream, context) == 0);

    ck_assert(comp_stream->read    == zidx_raw_file_read);
    ck_assert(comp_stream->seek    == zidx_raw_file_seek);
    ck_assert(comp_stream->tell    == zidx_raw_file_tell);
    ck_assert(comp_stream->length  == zidx_raw_file_length);
    ck_assert(comp_stream->eof     == zidx_raw_file_eof);
    ck_assert(comp_stream->error   == zidx_raw_file_error);
    ck_assert(comp_stream->context == context);

    free(comp_stream);
}
END_TEST

START_TEST(test_comp_file_read)
{
    // TODO
}
END_TEST

Suite* stream_api_suite()
{
    Suite *s;
    TCase *tc_stream_api;

    s = suite_create("libzidx");

    tc_stream_api = tcase_create("Stream API");

    tcase_add_test(tc_stream_api, test_comp_file_init);
    tcase_add_test(tc_stream_api, test_comp_file_read);

    suite_add_tcase(s, tc_stream_api);

    return s;
}

int main()
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = stream_api_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_VERBOSE);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
