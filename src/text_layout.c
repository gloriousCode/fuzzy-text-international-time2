#include "text_layout.h"

#include <string.h>

static int int_sqrt(int value)
{
  int result = 0;
  int bit = 1 << 14;

  while (bit > value) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    }
    else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return result;
}

static int abs_int(int value)
{
  return value < 0 ? -value : value;
}

TextMetrics text_metrics_for_font_choice(FontChoice font_choice)
{
  switch (font_choice) {
    case FONT_CHOICE_SHARP:
      return (TextMetrics) {
        .row_height = 32,
        .layer_height = 38,
        .top_margin = 2,
        .max_glyph_width = 18,
      };
    case FONT_CHOICE_COMPACT:
      return (TextMetrics) {
        .row_height = 28,
        .layer_height = 32,
        .top_margin = 0,
        .max_glyph_width = 16,
      };
    case FONT_CHOICE_TALL:
      return (TextMetrics) {
        .row_height = 42,
        .layer_height = 50,
        .top_margin = 0,
        .max_glyph_width = 22,
      };
    default:
      return (TextMetrics) {
        .row_height = 37,
        .layer_height = 50,
        .top_margin = 10,
        .max_glyph_width = 22,
      };
  }
}

TextFrame text_frame_for_line(int screen_width, int screen_height, bool round,
    FontChoice font_choice, int num_lines, int line_index)
{
  TextMetrics metrics = text_metrics_for_font_choice(font_choice);
  int y = (screen_height - (num_lines * metrics.row_height)) / 2 - metrics.top_margin;
  y += line_index * metrics.row_height;

  int x = 0;
  int width = screen_width;

  if (round) {
    int radius = screen_width < screen_height ? screen_width / 2 : screen_height / 2;
    int centre_y = screen_height / 2;
    int top_delta = abs_int(y - centre_y);
    int bottom_delta = abs_int((y + metrics.layer_height) - centre_y);
    int worst_delta = top_delta > bottom_delta ? top_delta : bottom_delta;
    int chord_value = (radius * radius) - (worst_delta * worst_delta);
    int chord_half_width = chord_value > 0 ? int_sqrt(chord_value) : 0;
    int inset = (screen_width / 2) - chord_half_width;
    int padding = screen_width / 30;

    x = inset > padding ? inset : padding;
    width = screen_width - (2 * x);
  }

  return (TextFrame) {
    .x = x,
    .y = y,
    .w = width,
    .h = metrics.layer_height,
  };
}

int text_estimated_width(FontChoice font_choice, const char *text)
{
  TextMetrics metrics = text_metrics_for_font_choice(font_choice);

  return (int)strlen(text) * metrics.max_glyph_width;
}

static int count_lines(char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE])
{
  int count = 0;

  for (int i = 0; i < FUZZY_TEXT_NUM_LINES; i++) {
    if (strlen(lines[i]) == 0) {
      break;
    }
    count++;
  }

  return count;
}

bool text_font_choice_fits(int screen_width, int screen_height, bool round,
    FontChoice font_choice,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE])
{
  int line_count = count_lines(lines);

  for (int i = 0; i < line_count; i++) {
    TextFrame frame = text_frame_for_line(screen_width, screen_height, round,
        font_choice, line_count, i);

    if (frame.x < 0 || frame.y < 0
        || frame.x + frame.w > screen_width
        || frame.y + frame.h > screen_height
        || text_estimated_width(font_choice, lines[i]) > frame.w) {
      return false;
    }
  }

  return true;
}

static FontChoice fallback_font_choice(FontChoice preferred, int attempt)
{
  static const FontChoice tall_fallbacks[] = {
    FONT_CHOICE_TALL,
    FONT_CHOICE_CLASSIC,
    FONT_CHOICE_SHARP,
    FONT_CHOICE_COMPACT,
  };
  static const FontChoice classic_fallbacks[] = {
    FONT_CHOICE_CLASSIC,
    FONT_CHOICE_SHARP,
    FONT_CHOICE_COMPACT,
  };
  static const FontChoice sharp_fallbacks[] = {
    FONT_CHOICE_SHARP,
    FONT_CHOICE_COMPACT,
  };

  switch (preferred) {
    case FONT_CHOICE_TALL:
      return attempt < (int)(sizeof(tall_fallbacks) / sizeof(tall_fallbacks[0]))
          ? tall_fallbacks[attempt]
          : FONT_CHOICE_COMPACT;
    case FONT_CHOICE_SHARP:
      return attempt < (int)(sizeof(sharp_fallbacks) / sizeof(sharp_fallbacks[0]))
          ? sharp_fallbacks[attempt]
          : FONT_CHOICE_COMPACT;
    case FONT_CHOICE_COMPACT:
      return FONT_CHOICE_COMPACT;
    default:
      return attempt < (int)(sizeof(classic_fallbacks) / sizeof(classic_fallbacks[0]))
          ? classic_fallbacks[attempt]
          : FONT_CHOICE_COMPACT;
  }
}

FontChoice text_font_choice_that_fits(int screen_width, int screen_height, bool round,
    FontChoice preferred,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE])
{
  for (int attempt = 0; attempt < FONT_CHOICE_COUNT; attempt++) {
    FontChoice font_choice = fallback_font_choice(preferred, attempt);
    if (text_font_choice_fits(screen_width, screen_height, round, font_choice, lines)) {
      return font_choice;
    }
  }

  return FONT_CHOICE_COMPACT;
}
