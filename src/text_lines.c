#include "text_lines.h"

#include <string.h>

#define LINE_APPEND_MARGIN 0
#define LINE_APPEND_LIMIT (FUZZY_TEXT_LINE_LENGTH - LINE_APPEND_MARGIN)

static void copy_line(char line[FUZZY_TEXT_BUFFER_SIZE], const char *start)
{
  strncpy(line, start, FUZZY_TEXT_LINE_LENGTH);
  line[FUZZY_TEXT_LINE_LENGTH] = '\0';
}

void time_to_lines(Language lang, int hours, int minutes, int seconds,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE], char format[])
{
  int length = FUZZY_TEXT_NUM_LINES * FUZZY_TEXT_BUFFER_SIZE + 1;
  char timeStr[length];
  time_to_words(lang, hours, minutes, seconds, timeStr, length);

  for (int i = 0; i < FUZZY_TEXT_NUM_LINES; i++)
  {
    lines[i][0] = '\0';
  }

  char *start = timeStr;
  char *end = strstr(start, " ");
  int line = 0;
  while (end != NULL && line < FUZZY_TEXT_NUM_LINES) {
    if (*start == '*' && end - start > 1)
    {
      format[line] = 'b';
      start++;
    }
    else
    {
      format[line] = ' ';
    }

    if (format[line] == ' ' && *(end + 1) != '*'
        && end - start < LINE_APPEND_LIMIT - 1)
    {
      char *try = strstr(end + 1, " ");
      if (try != NULL && try - start <= LINE_APPEND_LIMIT)
      {
        end = try;
      }
    }

    *end = '\0';
    copy_line(lines[line++], start);

    start = end + 1;
    end = strstr(start, " ");
  }
}

void date_to_lines(Language lang, int day, int date, int month,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE], char format[])
{
  int length = FUZZY_TEXT_NUM_LINES * FUZZY_TEXT_BUFFER_SIZE + 1;
  char dateStr[length];

  for (int i = 0; i < FUZZY_TEXT_NUM_LINES; i++)
  {
    lines[i][0] = '\0';
    format[i] = ' ';
  }
  format[0] = 'b';

  date_to_words(lang, day, date, month, dateStr, length);

  char *start = dateStr;
  char *end = strstr(start, " ");
  int line = 0;
  while (end != NULL && line < FUZZY_TEXT_NUM_LINES) {
    char *try = strstr(end + 1, " ");
    if (try != NULL && try - start <= LINE_APPEND_LIMIT)
    {
      end = try;
    }

    *end = '\0';
    copy_line(lines[line++], start);

    start = end + 1;
    end = strstr(start, " ");
  }
}
