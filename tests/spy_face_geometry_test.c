#include "../src/spy_face_geometry.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
  const char *name;
  int width;
  int height;
  bool round;
} SpyFaceScreenCase;

static bool rect_fits_screen(SpyFaceRect rect, int width, int height)
{
  return rect.x >= 0
      && rect.y >= 0
      && rect.x + rect.w <= width
      && rect.y + rect.h <= height;
}

static void test_spy_face_geometry_for_bounds(void)
{
  const SpyFaceScreenCase cases[] = {
    { "basalt", 144, 168, false },
    { "emery", 200, 228, false },
    { "gabbro", 260, 260, true },
  };

  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
    SpyFaceGeometry geometry = spy_face_geometry_for_bounds(cases[i].width,
        cases[i].height, cases[i].round);

    assert(geometry.screen_width == cases[i].width);
    assert(geometry.screen_height == cases[i].height);
    assert(geometry.outer_radius > geometry.inner_radius);
    assert(geometry.ring_width == geometry.outer_radius - geometry.inner_radius);
    assert(geometry.centre_x - geometry.outer_radius >= 0);
    assert(geometry.centre_x + geometry.outer_radius <= cases[i].width);
    assert(geometry.centre_y - geometry.outer_radius >= 0);
    assert(geometry.centre_y + geometry.outer_radius <= cases[i].height);
    assert(rect_fits_screen(geometry.display, cases[i].width, cases[i].height));
    assert(rect_fits_screen(geometry.left_bar, cases[i].width, cases[i].height));
    assert(rect_fits_screen(geometry.right_bar, cases[i].width, cases[i].height));
  }
}

static void test_spy_face_geometry_scales_for_gabbro(void)
{
  SpyFaceGeometry geometry = spy_face_geometry_for_bounds(260, 260, true);

  assert(geometry.centre_x == 130);
  assert(geometry.centre_y == 130);
  assert(geometry.outer_radius == 122);
  assert(geometry.inner_radius == 96);
  assert(geometry.display.w == 166);
  assert(geometry.display.h == 120);
  assert(geometry.left_bar.h == geometry.right_bar.h);
  assert(geometry.left_bar.x < geometry.display.x);
  assert(geometry.right_bar.x + geometry.right_bar.w > geometry.display.x + geometry.display.w);
}

static void test_spy_face_lit_segments_for_percent(void)
{
  assert(spy_face_lit_segments_for_percent(-10, SPY_FACE_SEGMENT_COUNT) == 0);
  assert(spy_face_lit_segments_for_percent(0, SPY_FACE_SEGMENT_COUNT) == 0);
  assert(spy_face_lit_segments_for_percent(1, SPY_FACE_SEGMENT_COUNT) == 1);
  assert(spy_face_lit_segments_for_percent(50, SPY_FACE_SEGMENT_COUNT) == 4);
  assert(spy_face_lit_segments_for_percent(100, SPY_FACE_SEGMENT_COUNT) == 8);
  assert(spy_face_lit_segments_for_percent(140, SPY_FACE_SEGMENT_COUNT) == 8);
  assert(spy_face_lit_segments_for_percent(60, 0) == 0);
}

int main(void)
{
  test_spy_face_geometry_for_bounds();
  test_spy_face_geometry_scales_for_gabbro();
  test_spy_face_lit_segments_for_percent();

  puts("spy face geometry checks passed");
  return 0;
}
