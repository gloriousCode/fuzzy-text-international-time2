#include "strings-en_US.h"

const char* const HOURS_EN_US[] = {
  // AM hours
  "twelve",
  "one",
  "two",
  "three",
  "four",
  "five",
  "six",
  "seven",
  "eight",
  "nine",
  "ten",
  "eleven",

  // PM hours
  "twelve",
  "one",
  "two",
  "three",
  "four",
  "five",
  "six",
  "seven",
  "eight",
  "nine",
  "ten",
  "eleven"
};

/**
 * The string "$1" will be replaced with the current hour (e.g., "three"
 * at 3:45).  The string "$2" will be replaced with the *next* hour
 * (e.g., "four" at 3:45).
 *
 * A "*" character before a word makes that word bold.
 */
const char* const RELS_EN_US[] = {
  "*$1 o'clock",
  "five after *$1",
  "ten after *$1",
  "quarter after *$1",
  "twenty after *$1",
  "twenty five after *$1",
  "half past *$1",
  "twenty five to *$2",
  "twenty to *$2",
  "quarter to *$2",
  "ten to *$2",
  "five to *$2"
};

const char* const MONTHS_EN_US[] = {
  "jan",
  "feb",
  "mar",
  "apr",
  "may",
  "jun",
  "jul",
  "aug",
  "sep",
  "oct",
  "nov",
  "dec"
};

const char* const DAYS_EN_US[] = {
  "sun",
  "mon",
  "tue",
  "wed",
  "thur",
  "fri",
  "sat"
};
