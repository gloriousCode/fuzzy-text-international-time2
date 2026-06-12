#pragma once

#include <stdbool.h>

#define FUZZY_TEXT_NUM_LINES 4
#define FUZZY_TEXT_LINE_LENGTH 12
#define FUZZY_TEXT_BUFFER_SIZE (FUZZY_TEXT_LINE_LENGTH + 2)

typedef enum {
  FONT_CHOICE_CLASSIC = 0,
  FONT_CHOICE_SHARP = 1,
  FONT_CHOICE_COMPACT = 2,
  FONT_CHOICE_TALL = 3,
  FONT_CHOICE_SMALL = 4,
  FONT_CHOICE_COUNT
} FontChoice;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} TextFrame;

typedef struct {
  int row_height;
  int layer_height;
  int top_margin;
  int max_glyph_width;
} TextMetrics;

TextMetrics text_metrics_for_font_choice(FontChoice font_choice);
TextFrame text_frame_for_line(int screen_width, int screen_height, bool round,
    FontChoice font_choice, int num_lines, int line_index);
int text_estimated_width(FontChoice font_choice, const char *text);
bool text_font_choice_fits(int screen_width, int screen_height, bool round,
    FontChoice font_choice,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE]);
FontChoice text_font_choice_that_fits(int screen_width, int screen_height, bool round,
    FontChoice preferred,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE]);
