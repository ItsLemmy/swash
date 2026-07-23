#include "editor-utils.h"

#include <glib.h>

static void
test_size_presets(void)
{
  g_assert_cmpfloat(swash_tool_size_preset(SWASH_TOOL_BRUSH, 0.5), ==, 3.0);
  g_assert_cmpfloat(swash_tool_size_preset(SWASH_TOOL_BRUSH, 1.0), ==, 6.0);
  g_assert_cmpfloat(swash_tool_size_preset(SWASH_TOOL_BRUSH, 2.0), ==, 12.0);
  g_assert_cmpfloat(swash_tool_size_preset(SWASH_TOOL_TEXT, 0.5), ==, 12.0);
  g_assert_cmpfloat(swash_tool_size_preset(SWASH_TOOL_TEXT, 2.0), ==, 48.0);
}

static void
test_crop_rect(void)
{
  double left = 90.2;
  double top = 60.8;
  double right = 10.5;
  double bottom = 20.1;
  int width;
  int height;

  swash_crop_rect_normalize(&left, &top, &right, &bottom);
  g_assert_cmpfloat(left, ==, 10.5);
  g_assert_cmpfloat(top, ==, 20.1);
  g_assert_cmpfloat(right, ==, 90.2);
  g_assert_cmpfloat(bottom, ==, 60.8);

  swash_crop_rect_dimensions(left, top, right, bottom, &width, &height);
  g_assert_cmpint(width, ==, 81);
  g_assert_cmpint(height, ==, 41);
}

static void
test_shortcut_conflicts(void)
{
  g_assert_true(swash_accelerators_conflict("<Primary>c", "<Control>C"));
  g_assert_false(swash_accelerators_conflict("<Primary>c", "c"));
  g_assert_false(swash_accelerators_conflict("", "<Primary>c"));
  g_assert_false(swash_accelerators_conflict("not-an-accelerator", "<Primary>c"));
}

static void
test_stroke_sampling(void)
{
  const SwashPoint origin = { 0.0, 0.0 };
  const SwashPoint nearby = { 2.0, 1.0 };
  const SwashPoint distant = { 3.0, 0.0 };
  const SwashPoint straight = { 6.0, 0.1 };
  const SwashPoint corner = { 3.0, 3.0 };
  const SwashPoint reverse = { 1.0, 0.0 };

  g_assert_false(swash_point_is_far_enough(&origin, &nearby, 2.5));
  g_assert_true(swash_point_is_far_enough(&origin, &distant, 2.5));
  g_assert_true(swash_point_can_simplify(&origin, &distant, &straight, 0.75));
  g_assert_false(swash_point_can_simplify(&origin, &distant, &corner, 0.75));
  g_assert_false(swash_point_can_simplify(&origin, &distant, &reverse, 0.75));
}

int
main(int argc,
     char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/editor/size-presets", test_size_presets);
  g_test_add_func("/editor/crop-rect", test_crop_rect);
  g_test_add_func("/editor/shortcut-conflicts", test_shortcut_conflicts);
  g_test_add_func("/editor/stroke-sampling", test_stroke_sampling);
  return g_test_run();
}
