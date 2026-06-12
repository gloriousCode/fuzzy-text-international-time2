#pragma once

#include "num2words.h"
#include "text_layout.h"

void time_to_lines(Language lang, int hours, int minutes, int seconds,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE], char format[]);
void date_to_lines(Language lang, int day, int date, int month,
    char lines[FUZZY_TEXT_NUM_LINES][FUZZY_TEXT_BUFFER_SIZE], char format[]);
