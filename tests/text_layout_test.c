#include "../src/text_layout.h"
#include "../src/text_lines.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>

typedef struct {
  const char *name;
  int width;
  int height;
  bool round;
} ScreenCase;

static const ScreenCase screen_cases[] = {
  { "aplite", 144, 168, false },
  { "basalt", 144, 168, false },
  { "chalk", 180, 180, true },
  { "diorite", 144, 168, false },
  { "emery", 200, 228, false },
  { "flint", 144, 168, false },
  { "gabbro", 260, 260, true },
};

static const char *current_label = "";
static int current_language;
static int current_hour;
static int current_minute;
static int current_day;
static int current_date;
static int current_month;

static void print_crash_context(int signal)
{
  fprintf(stderr,
      "signal=%d label=%s language=%d hour=%d minute=%d day=%d date=%d month=%d\n",
      signal, current_label, current_language, current_hour, current_minute,
      current_day, current_date, current_month);
  exit(128 + signal);
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

static bool frame_fits_screen(TextFrame frame, const ScreenCase *screen)
{
  return frame.x >= 0
      && frame.y >= 0
      && frame.x + frame.w <= screen->width
      && frame.y + frame.h <= screen->height;
}

static void assert_lines_fit(const ScreenCase *screen, FontChoice preferred_font_choice,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE], const char *label)
{
  int line_count = count_lines(lines);
  FontChoice font_choice = text_font_choice_that_fits(screen->width, screen->height,
      screen->round, preferred_font_choice, lines);

  for (int i = 0; i < line_count; i++) {
    TextFrame frame = text_frame_for_line(screen->width, screen->height,
        screen->round, font_choice, line_count, i);
    int text_width = text_estimated_width(font_choice, lines[i]);

    if (!frame_fits_screen(frame, screen) || text_width > frame.w) {
      fprintf(stderr,
          "%s preferred_font=%d rendered_font=%d line=%d text='%s' text_width=%d frame=%d,%d,%d,%d screen=%s\n",
          label, preferred_font_choice, font_choice, i, lines[i], text_width,
          frame.x, frame.y, frame.w, frame.h, screen->name);
    }

    assert(frame_fits_screen(frame, screen));
    assert(text_width <= frame.w);
  }
}

static void assert_time_lines_fit(void)
{
  char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE];
  char format[FUZZY_TEXT_NUM_LINES];

  for (int language = CA; language <= SV; language++) {
    current_language = language;
    for (int hour = 0; hour < 24; hour++) {
      current_hour = hour;
      for (int minute = 0; minute < 60; minute += 5) {
        current_label = "time";
        current_minute = minute;
        time_to_lines((Language)language, hour, minute, 0, lines, format);
        for (int screen = 0; screen < (int)(sizeof(screen_cases) / sizeof(screen_cases[0])); screen++) {
          for (int font = 0; font < FONT_CHOICE_COUNT; font++) {
            assert_lines_fit(&screen_cases[screen], (FontChoice)font, lines, "time");
          }
        }
      }
    }
  }
}

static void assert_date_lines_fit(void)
{
  char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE];
  char format[FUZZY_TEXT_NUM_LINES];

  for (int language = CA; language <= SV; language++) {
    current_language = language;
    for (int day = 0; day < 7; day++) {
      current_day = day;
      for (int month = 0; month < 12; month++) {
        current_month = month;
        for (int date = 1; date <= 31; date++) {
          current_label = "date";
          current_date = date;
          date_to_lines((Language)language, day, date, month, lines, format);
          for (int screen = 0; screen < (int)(sizeof(screen_cases) / sizeof(screen_cases[0])); screen++) {
            for (int font = 0; font < FONT_CHOICE_COUNT; font++) {
              assert_lines_fit(&screen_cases[screen], (FontChoice)font, lines, "date");
            }
          }
        }
      }
    }
  }
}

static void assert_time_lines_mark_hour_bold(void)
{
  char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE];
  char format[FUZZY_TEXT_NUM_LINES];

  for (int language = CA; language <= SV; language++) {
    current_language = language;
    for (int hour = 0; hour < 24; hour++) {
      current_hour = hour;
      for (int minute = 0; minute < 60; minute += 5) {
        current_label = "bold";
        current_minute = minute;
        time_to_lines((Language)language, hour, minute, 0, lines, format);

        bool has_bold_line = false;
        int line_count = count_lines(lines);
        for (int i = 0; i < line_count; i++) {
          if (format[i] == 'b') {
            has_bold_line = true;
            break;
          }
        }

        if (!has_bold_line) {
          fprintf(stderr, "missing bold language=%d hour=%d minute=%d lines=", language, hour, minute);
          for (int i = 0; i < line_count; i++) {
            fprintf(stderr, "[%s:%c]", lines[i], format[i]);
          }
          fprintf(stderr, "\n");
        }

        assert(has_bold_line);
      }
    }
  }
}

static void assert_high_resolution_time_uses_large_fonts(void)
{
  char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE];
  char format[FUZZY_TEXT_NUM_LINES];
  const ScreenCase *emery = &screen_cases[4];
  const ScreenCase *gabbro = &screen_cases[6];

  for (int hour = 0; hour < 24; hour++) {
    current_hour = hour;
    for (int minute = 0; minute < 60; minute += 30) {
      current_label = "high-resolution-font";
      current_minute = minute;
      time_to_lines(EN_US, hour, minute, 0, lines, format);

      FontChoice emery_font = text_font_choice_that_fits(emery->width, emery->height,
          emery->round, FONT_CHOICE_CLASSIC, lines);
      FontChoice gabbro_font = text_font_choice_that_fits(gabbro->width, gabbro->height,
          gabbro->round, FONT_CHOICE_CLASSIC, lines);

      if (emery_font != FONT_CHOICE_LARGE || gabbro_font != FONT_CHOICE_LARGE) {
        fprintf(stderr, "unexpected high-resolution fallback hour=%d minute=%d emery=%d gabbro=%d\n",
            hour, minute, emery_font, gabbro_font);
      }

      assert(emery_font == FONT_CHOICE_LARGE);
      assert(gabbro_font == FONT_CHOICE_LARGE);
    }
  }
}

static void assert_high_resolution_short_lines_preserve_large_fonts(void)
{
  char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE];
  char format[FUZZY_TEXT_NUM_LINES];
  const ScreenCase *gabbro = &screen_cases[6];

  time_to_lines_with_limit(EN_GB, 1, 15, 0, 7, lines, format);

  assert(strcmp(lines[0], "quarter") == 0);
  assert(strcmp(lines[1], "past") == 0);
  assert(strcmp(lines[2], "one") == 0);
  assert(format[2] == 'b');
  assert(text_font_choice_that_fits(gabbro->width, gabbro->height,
      gabbro->round, FONT_CHOICE_CLASSIC, lines) == FONT_CHOICE_LARGE);
}

int main(void)
{
  signal(SIGSEGV, print_crash_context);

  assert_time_lines_fit();
  assert_date_lines_fit();
  assert_time_lines_mark_hour_bold();
  assert_high_resolution_time_uses_large_fonts();
  assert_high_resolution_short_lines_preserve_large_fonts();

  puts("text layout compatibility checks passed");
  return 0;
}
