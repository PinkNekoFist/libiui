/*
 * Initialization & Configuration Tests
 *
 * Tests for iui_init(), iui_config_t validation, memory requirements,
 * and bounds/limit enforcement.
 */

#include "common.h"

/* Initialization Tests */

static void test_init_null_buffer(void)
{
    TEST(init_null_buffer);
    iui_config_t config = {
        .buffer = NULL,
        .font_height = 16.0f,
        .renderer =
            {
                .draw_box = mock_draw_box,
                .draw_text = mock_draw_text,
                .set_clip_rect = mock_set_clip,
                .text_width = mock_text_width,
            },
    };
    ASSERT_FALSE(iui_config_is_valid(&config));
    PASS();
}

static void test_init_zero_font_height(void)
{
    TEST(init_zero_font_height);
    void *buffer = malloc(iui_min_memory_size());
    iui_config_t config = {
        .buffer = buffer,
        .font_height = 0.0f,
        .renderer =
            {
                .draw_box = mock_draw_box,
                .draw_text = mock_draw_text,
                .set_clip_rect = mock_set_clip,
                .text_width = mock_text_width,
            },
    };
    ASSERT_FALSE(iui_config_is_valid(&config));
    iui_context *ctx = iui_init(&config);
    ASSERT_NULL(ctx);
    free(buffer);
    PASS();
}

static void test_init_negative_font_height(void)
{
    TEST(init_negative_font_height);
    void *buffer = malloc(iui_min_memory_size());
    iui_config_t config = {
        .buffer = buffer,
        .font_height = -10.0f,
        .renderer =
            {
                .draw_box = mock_draw_box,
                .draw_text = mock_draw_text,
                .set_clip_rect = mock_set_clip,
                .text_width = mock_text_width,
            },
    };
    ASSERT_FALSE(iui_config_is_valid(&config));
    iui_context *ctx = iui_init(&config);
    ASSERT_NULL(ctx);
    free(buffer);
    PASS();
}

static void test_init_missing_draw_box(void)
{
    TEST(init_missing_draw_box);
    void *buffer = malloc(iui_min_memory_size());
    iui_config_t config = {
        .buffer = buffer,
        .font_height = 16.0f,
        .renderer =
            {
                .draw_box = NULL,
                .draw_text = mock_draw_text,
                .set_clip_rect = mock_set_clip,
                .text_width = mock_text_width,
            },
    };
    ASSERT_FALSE(iui_config_is_valid(&config));
    iui_context *ctx = iui_init(&config);
    ASSERT_NULL(ctx);
    free(buffer);
    PASS();
}

static void test_init_missing_set_clip(void)
{
    TEST(init_missing_set_clip);
    void *buffer = malloc(iui_min_memory_size());
    iui_config_t config = {
        .buffer = buffer,
        .font_height = 16.0f,
        .renderer =
            {
                .draw_box = mock_draw_box,
                .draw_text = mock_draw_text,
                .set_clip_rect = NULL,
                .text_width = mock_text_width,
            },
    };
    ASSERT_FALSE(iui_config_is_valid(&config));
    iui_context *ctx = iui_init(&config);
    ASSERT_NULL(ctx);
    free(buffer);
    PASS();
}

static void test_def_is_valid(void)
{
    TEST(def_is_valid);
    void *buffer = malloc(iui_min_memory_size());

    iui_config_t valid_config =
        iui_make_config(buffer,
                        (iui_renderer_t) {
                            .draw_box = mock_draw_box,
                            .draw_text = mock_draw_text,
                            .set_clip_rect = mock_set_clip,
                            .text_width = mock_text_width,
                        },
                        16.0f, NULL);
    ASSERT_TRUE(iui_config_is_valid(&valid_config));

    /* NULL def pointer */
    ASSERT_FALSE(iui_config_is_valid(NULL));

    /* Zero font height */
    iui_config_t zero_font_config = valid_config;
    zero_font_config.font_height = 0.0f;
    ASSERT_FALSE(iui_config_is_valid(&zero_font_config));

    /* Missing draw_text without vector_callbacks */
    iui_config_t no_text_config = valid_config;
    no_text_config.renderer.draw_text = NULL;
    ASSERT_FALSE(iui_config_is_valid(&no_text_config));

    free(buffer);
    PASS();
}

static void test_min_memory_size(void)
{
    TEST(min_memory_size);
    size_t size = iui_min_memory_size();
    ASSERT_TRUE(size >= 1024);
    ASSERT_TRUE(size < 65536);
    PASS();
}

/* Bounds & Limit Enforcement Tests */

static void test_id_stack_underflow(void)
{
    TEST(id_stack_underflow);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 200, 200, 0);

    iui_pop_id(ctx);
    iui_pop_id(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_window_limit(void)
{
    TEST(window_limit);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);

    char name[32];
    for (int i = 0; i < IUI_MAX_WINDOWS; i++) {
        snprintf(name, sizeof(name), "Window%d", i);
        iui_begin_window(ctx, name, (float) (i * 10), (float) (i * 10), 100,
                         100, 0);
        iui_end_window(ctx);
    }

    iui_begin_window(ctx, "Overflow", 0, 0, 100, 100, 0);
    iui_end_window(ctx);

    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_row_items_limit(void)
{
    TEST(row_items_limit);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 800, 600, 0);

    float widths[32];
    for (int i = 0; i < 32; i++)
        widths[i] = -1.0f;
    iui_row(ctx, 32, widths, 30.0f);

    for (int i = 0; i < 32; i++)
        iui_layout_next(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_flex_items_limit(void)
{
    TEST(flex_items_limit);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 800, 600, 0);

    float sizes[32];
    for (int i = 0; i < 32; i++)
        sizes[i] = -1.0f;
    iui_flex(ctx, 32, sizes, 30.0f, 4.0f);

    for (int i = 0; i < 32; i++)
        iui_flex_next(ctx);

    iui_flex_end(ctx);
    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* String Buffer Safety Tests */

static void test_text_format_overflow(void)
{
    TEST(text_format_overflow);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    char long_str[4096];
    memset(long_str, 'A', sizeof(long_str) - 1);
    long_str[sizeof(long_str) - 1] = '\0';

    iui_text(ctx, IUI_ALIGN_LEFT, "%s", long_str);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_slider_format_overflow(void)
{
    TEST(slider_format_overflow);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    char long_fmt[256];
    memset(long_fmt, '%', sizeof(long_fmt) - 2);
    long_fmt[sizeof(long_fmt) - 2] = 'f';
    long_fmt[sizeof(long_fmt) - 1] = '\0';

    float value = 50.0f;
    iui_slider(ctx, "Test", 0.0f, 100.0f, 1.0f, &value, long_fmt);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_text_width_vec_edge_cases(void)
{
    TEST(text_width_vec_edge_cases);

    float w = iui_text_width_vec("", 16.0f);
    ASSERT_NEAR(w, 0.0f, 0.001f);

    w = iui_text_width_vec("Hello", 16.0f);
    ASSERT_TRUE(w > 0.0f);

    w = iui_text_width_vec("Hello", 0.0f);
    ASSERT_TRUE(w >= 0.0f);

    w = iui_text_width_vec("A", 0.001f);
    ASSERT_TRUE(w >= 0.0f);

    PASS();
}

/* Test Suite Runner */

void run_init_tests(void)
{
    SECTION_BEGIN("Initialization & Definition");
    test_init_null_buffer();
    test_init_zero_font_height();
    test_init_negative_font_height();
    test_init_missing_draw_box();
    test_init_missing_set_clip();
    test_def_is_valid();
    test_min_memory_size();
    SECTION_END();
}

void run_bounds_tests(void)
{
    SECTION_BEGIN("Bounds & Limit Enforcement");
    test_id_stack_underflow();
    test_window_limit();
    test_row_items_limit();
    test_flex_items_limit();
    SECTION_END();
}

void run_string_tests(void)
{
    SECTION_BEGIN("String Buffer Safety");
    test_text_format_overflow();
    test_slider_format_overflow();
    test_text_width_vec_edge_cases();
    SECTION_END();
}
