/*
 * Layout System Tests
 *
 * Tests for row, flex, and grid layout systems.
 */

#include "common.h"

/* Row Layout Tests */

static void test_row_null_widths(void)
{
    TEST(row_null_widths);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_row(ctx, 4, NULL, 30.0f);

    iui_rect_t r1 = iui_layout_next(ctx);
    iui_rect_t r2 = iui_layout_next(ctx);
    iui_rect_t r3 = iui_layout_next(ctx);
    iui_rect_t r4 = iui_layout_next(ctx);

    ASSERT_NEAR(r1.width, r2.width, 1.0f);
    ASSERT_NEAR(r2.width, r3.width, 1.0f);
    ASSERT_NEAR(r3.width, r4.width, 1.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_spacing_snap(void)
{
    TEST(spacing_snap);
    float result = iui_spacing_snap(10.3f);
    /* 10.3 / 4 = 2.575, roundf(2.575) = 3, 3 * 4 = 12 */
    ASSERT_NEAR(result, 12.0f, 0.001f);

    result = iui_spacing_snap(10.0f);
    /* 10.0 / 4 = 2.5, roundf(2.5) = 3 (rounds away from zero on halfway), 3 * 4
     * = 12 */
    ASSERT_NEAR(result, 12.0f, 0.001f);

    result = iui_spacing_snap(2.0f);
    /* 2.0 / 4 = 0.5, roundf(0.5) = 1 (rounds away from zero on halfway), 1 * 4
     * = 4 */
    ASSERT_NEAR(result, 4.0f, 0.001f);

    PASS();
}

static void test_row_mixed_widths(void)
{
    TEST(row_mixed_widths);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    float widths[] = {100.0f, -1.0f, -2.0f};
    iui_row(ctx, 3, widths, 30.0f);

    iui_rect_t r1 = iui_layout_next(ctx);
    iui_rect_t r2 = iui_layout_next(ctx);
    iui_rect_t r3 = iui_layout_next(ctx);

    ASSERT_NEAR(r1.width, 100.0f, 1.0f);
    ASSERT_NEAR(r3.width, r2.width * 2.0f, 2.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Flex Layout Tests */


static void test_flex_equal_distribution(void)
{
    TEST(flex_equal_distribution);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_flex(ctx, 3, NULL, 30.0f, 0.0f);

    iui_rect_t r1 = iui_flex_next(ctx);
    iui_rect_t r2 = iui_flex_next(ctx);
    iui_rect_t r3 = iui_flex_next(ctx);

    ASSERT_NEAR(r1.width, r2.width, 1.0f);
    ASSERT_NEAR(r2.width, r3.width, 1.0f);

    iui_flex_end(ctx);
    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_flex_fixed_center(void)
{
    TEST(flex_fixed_center);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    float sizes[] = {-1.0f, 100.0f, -2.0f};
    iui_flex(ctx, 3, sizes, 30.0f, 0.0f);

    iui_flex_next(ctx);
    iui_rect_t center = iui_flex_next(ctx);
    iui_flex_next(ctx);

    ASSERT_NEAR(center.width, 100.0f, 1.0f);

    iui_flex_end(ctx);
    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_flex_column_layout(void)
{
    TEST(flex_column_layout);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    float sizes[] = {30.0f, -1.0f, 30.0f};
    iui_flex_column(ctx, 3, sizes, 200.0f, 0.0f);

    iui_rect_t top = iui_flex_next(ctx);
    iui_rect_t middle = iui_flex_next(ctx);
    iui_rect_t bottom = iui_flex_next(ctx);

    ASSERT_NEAR(top.height, 30.0f, 1.0f);
    ASSERT_NEAR(bottom.height, 30.0f, 1.0f);
    ASSERT_TRUE(middle.height > 100.0f);
    ASSERT_TRUE(middle.y > top.y);
    ASSERT_TRUE(bottom.y > middle.y);

    iui_flex_end(ctx);
    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Grid Layout Tests */

static void test_grid_basic(void)
{
    TEST(grid_basic);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    reset_counters();
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_grid_begin(ctx, 3, 50.0f, 30.0f, 5.0f);

    for (int i = 0; i < 9; i++) {
        iui_button(ctx, "X", IUI_ALIGN_CENTER);
        iui_grid_next(ctx);
    }

    iui_grid_end(ctx);
    iui_end_window(ctx);
    iui_end_frame(ctx);

    ASSERT_TRUE(g_draw_box_calls >= 9);

    free(buffer);
    PASS();
}

static void test_grid_zero_cols(void)
{
    TEST(grid_zero_cols);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_grid_begin(ctx, 0, 50.0f, 30.0f, 5.0f);
    iui_grid_next(ctx);
    iui_grid_end(ctx);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Window Auto-sizing Tests */

static void test_window_autosize_expands_to_content(void)
{
    TEST(window_autosize_expands_to_content);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    /* Frame 1: Window starts at 300px, content requires 450px */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Auto", 0, 0, 300, 200, IUI_WINDOW_AUTO_SIZE);

    /* Report content requirement larger than window */
    iui_require_content_width(ctx, 450.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    /* After frame 1: min_width should be updated to fit content */
    /* content (450) + padding*4 (32) = 482, but also consider title min */
    iui_window *w = &ctx->windows[0];
    ASSERT_TRUE(w->min_width >= 450.0f); /* Should accommodate content */

    /* Frame 2: Window should auto-expand */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Auto", 0, 0, 300, 200, IUI_WINDOW_AUTO_SIZE);

    /* Width should have expanded to min_width */
    ASSERT_TRUE(w->width >= 450.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_window_autosize_only_grows(void)
{
    TEST(window_autosize_only_grows);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    /* Frame 1: Start at 400px, report small content */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "NoShrink", 0, 0, 400, 200, IUI_WINDOW_AUTO_SIZE);

    iui_require_content_width(ctx, 100.0f); /* Small content */

    iui_end_window(ctx);
    iui_end_frame(ctx);

    iui_window *w = &ctx->windows[0];
    float width_after_frame1 = w->width;

    /* Frame 2: Window should NOT shrink */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "NoShrink", 0, 0, 400, 200, IUI_WINDOW_AUTO_SIZE);

    /* Width should remain at original size (400), not shrink */
    ASSERT_TRUE(w->width >= 400.0f);
    ASSERT_NEAR(w->width, width_after_frame1, 1.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_window_no_autosize_ignores_content_width(void)
{
    TEST(window_no_autosize_ignores_content_width);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    /* Frame 1: Window without auto-size flags */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Manual", 0, 0, 300, 200, 0); /* No auto-size flags */

    iui_require_content_width(ctx, 500.0f); /* Large content */

    iui_end_window(ctx);
    iui_end_frame(ctx);

    iui_window *w = &ctx->windows[0];

    /* Frame 2: Width should NOT expand (no auto-size) */
    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Manual", 0, 0, 300, 200, 0);

    /* Width should remain at 300, not expand */
    ASSERT_NEAR(w->width, 300.0f, 1.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

static void test_grid_reports_width_requirement(void)
{
    TEST(grid_reports_width_requirement);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Grid", 0, 0, 200, 200, IUI_WINDOW_AUTO_SIZE);

    /* 4 cols × 60px + 3 gaps × 5px = 255px required */
    iui_grid_begin(ctx, 4, 60.0f, 30.0f, 5.0f);
    iui_grid_end(ctx);

    /* Check that content width was reported */
    float expected = 4.0f * 60.0f + 3.0f * 5.0f; /* 255 */
    ASSERT_TRUE(ctx->window_content_min_width >= expected - 1.0f);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Layout Edge Cases */
static void test_layout_next_outside_row(void)
{
    TEST(layout_next_outside_row);
    void *buffer = malloc(iui_min_memory_size());
    iui_context *ctx = create_test_context(buffer, false);
    ASSERT_NOT_NULL(ctx);

    iui_begin_frame(ctx, 1.0f / 60.0f);
    iui_begin_window(ctx, "Test", 0, 0, 400, 300, 0);

    iui_rect_t r = iui_layout_next(ctx);

    ASSERT_TRUE(r.width > 0);
    ASSERT_TRUE(r.height > 0);

    iui_end_window(ctx);
    iui_end_frame(ctx);

    free(buffer);
    PASS();
}

/* Test Suite Runner */
void run_layout_tests(void)
{
    SECTION_BEGIN("Layout System");
    test_row_null_widths();
    test_row_mixed_widths();
    test_flex_equal_distribution();
    test_flex_fixed_center();
    test_flex_column_layout();
    test_grid_basic();
    test_grid_zero_cols();
    test_layout_next_outside_row();
    test_spacing_snap();
    /* Window auto-sizing tests */
    test_window_autosize_expands_to_content();
    test_window_autosize_only_grows();
    test_window_no_autosize_ignores_content_width();
    test_grid_reports_width_requirement();
    SECTION_END();
}
