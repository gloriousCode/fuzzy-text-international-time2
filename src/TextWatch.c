#include <pebble.h>

#include "num2words.h"
#include "text_layout.h"
#include "text_lines.h"

#define DEBUG 0

#define NUM_LINES FUZZY_TEXT_NUM_LINES
#define LINE_LENGTH FUZZY_TEXT_LINE_LENGTH
#define BUFFER_SIZE FUZZY_TEXT_BUFFER_SIZE

#define INVERT_KEY 0
#define TEXT_ALIGN_KEY 1
#define LANGUAGE_KEY 2
#define FONT_CHOICE_KEY 3
#define FOREGROUND_COLOR_KEY 4
#define BACKGROUND_COLOR_KEY 5
#define SETTINGS_SCHEMA_KEY 97
#define TEST_HOUR_KEY 98
#define TEST_MINUTE_KEY 99

#define SETTINGS_SCHEMA_VERSION 2

#define TEXT_ALIGN_CENTER 0
#define TEXT_ALIGN_LEFT 1
#define TEXT_ALIGN_RIGHT 2

// The time it takes for a layer to slide in or out.
#define ANIMATION_DURATION 400
// Delay between the layers animations, from top to bottom
#define ANIMATION_STAGGER_TIME 150
// Delay from the start of the current layer going out until the next layer slides in
#define ANIMATION_OUT_IN_DELAY 100

static int text_align = TEXT_ALIGN_CENTER;
static int foreground_color = 0xFFFFFF;
static int background_color = 0x000000;
static FontChoice font_choice = FONT_CHOICE_CLASSIC;
static FontChoice render_font_choice = FONT_CHOICE_CLASSIC;
static Language lang = EN_GB;
static GFont custom_font_large_light;
static GFont custom_font_large_bold;
static GFont custom_font_medium_light;
static GFont custom_font_medium_bold;

static Window *window;
static GRect screen_bounds;

typedef struct {
	TextLayer *currentLayer;
	TextLayer *nextLayer;
	char lineStr1[BUFFER_SIZE];
	char lineStr2[BUFFER_SIZE];
	PropertyAnimation *animation1;
	PropertyAnimation *animation2;
} Line;

static Line lines[NUM_LINES];

static struct tm *t;
static struct tm test_time;
static bool use_test_time = false;
static bool pending_test_hour = false;
static bool pending_test_minute = false;
static int requested_test_hour = 0;
static int requested_test_minute = 0;

static int currentNLines;
static bool forceDisplayUpdate = false;

static bool showTime = true;
static int dateTimeout = 0;

static void destroy_property_animation(PropertyAnimation **animation)
{
	if (*animation == NULL) {
		return;
	}

	animation_unschedule(property_animation_get_animation(*animation));
	property_animation_destroy(*animation);
	*animation = NULL;
}

// Animation handler
static void animationStoppedHandler(struct Animation *animation, bool finished, void *context)
{
	TextLayer *current = (TextLayer *)context;
	GRect rect = layer_get_frame((Layer *)current);
	rect.origin.x = screen_bounds.size.w;
	layer_set_frame((Layer *)current, rect);
}

// Animate line
static void makeAnimationsForLayer(Line *line, int delay)
{
	TextLayer *current = line->currentLayer;
	TextLayer *next = line->nextLayer;

	// Destroy old animations
	destroy_property_animation(&line->animation1);
	destroy_property_animation(&line->animation2);

	// Configure animation for current layer to move out
	GRect rect = layer_get_frame((Layer *)current);
	rect.origin.x =  -screen_bounds.size.w;
	line->animation1 = property_animation_create_layer_frame((Layer *)current, NULL, &rect);
	if (line->animation1 == NULL) {
		return;
	}
	Animation *animation1 = property_animation_get_animation(line->animation1);
	animation_set_duration(animation1, ANIMATION_DURATION);
	animation_set_delay(animation1, delay);
	animation_set_curve(animation1, AnimationCurveEaseIn); // Accelerate

	// Configure animation for current layer to move in
	GRect rect2 = layer_get_frame((Layer *)next);
	rect2.origin.x = (screen_bounds.size.w - rect2.size.w) / 2;
	line->animation2 = property_animation_create_layer_frame((Layer *)next, NULL, &rect2);
	if (line->animation2 == NULL) {
		destroy_property_animation(&line->animation1);
		return;
	}
	Animation *animation2 = property_animation_get_animation(line->animation2);
	animation_set_duration(animation2, ANIMATION_DURATION);
	animation_set_delay(animation2, delay + ANIMATION_OUT_IN_DELAY);
	animation_set_curve(animation2, AnimationCurveEaseOut); // Deaccelerate

	// Set a handler to rearrange layers after animation is finished
	animation_set_handlers(animation2, (AnimationHandlers) {
		.stopped = (AnimationStoppedHandler)animationStoppedHandler
	}, current);

	// Start the animations
	animation_schedule(animation1);
	animation_schedule(animation2);
}

static void updateLayerText(TextLayer* layer, char* text)
{
	const char* layerText = text_layer_get_text(layer);
	strncpy((char*)layerText, text, BUFFER_SIZE);
	((char*)layerText)[BUFFER_SIZE - 1] = '\0';
	// To mark layer dirty
	text_layer_set_text(layer, layerText);
    //layer_mark_dirty(&layer->layer);
}

// Update line
static void updateLineTo(Line *line, char *value, int delay)
{
	updateLayerText(line->nextLayer, value);
	makeAnimationsForLayer(line, delay);

	// Swap current/next layers
	TextLayer *tmp = line->nextLayer;
	line->nextLayer = line->currentLayer;
	line->currentLayer = tmp;
}

// Check to see if the current line needs to be updated
static bool needToUpdateLine(Line *line, char *nextValue)
{
	const char *currentStr = text_layer_get_text(line->currentLayer);

	if (strcmp(currentStr, nextValue) != 0) {
		return true;
	}
	return false;
}

static int count_text_lines(char text[NUM_LINES][BUFFER_SIZE])
{
	int count = 0;

	for (int i = 0; i < NUM_LINES; i++) {
		if (strlen(text[i]) == 0) {
			break;
		}
		count++;
	}

	return count;
}

static GTextAlignment lookup_text_alignment(int align_key)
{
	GTextAlignment alignment;
	switch (align_key)
	{
		case TEXT_ALIGN_LEFT:
			alignment = GTextAlignmentLeft;
			break;
		case TEXT_ALIGN_RIGHT:
			alignment = GTextAlignmentRight;
			break;
		default:
			alignment = GTextAlignmentCenter;
			break;
	}
	return alignment;
}

static int valid_text_align(int align_key)
{
	switch (align_key) {
		case TEXT_ALIGN_LEFT:
		case TEXT_ALIGN_RIGHT:
		case TEXT_ALIGN_CENTER:
			return align_key;
		default:
			return TEXT_ALIGN_CENTER;
	}
}

static Language valid_language(Language language)
{
	switch (language) {
		case CA:
		case DE:
		case EN_GB:
		case EN_US:
		case ES:
		case FR:
		case NO:
		case SV:
			return language;
		default:
			return EN_GB;
	}
}

static GColor colour_from_rgb(int rgb)
{
#ifdef PBL_COLOR
	return GColorFromRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
#else
	int red = (rgb >> 16) & 0xFF;
	int green = (rgb >> 8) & 0xFF;
	int blue = rgb & 0xFF;
	return (red * 30 + green * 59 + blue * 11) >= 12800 ? GColorWhite : GColorBlack;
#endif
}

static GColor background_colour(void)
{
	return colour_from_rgb(background_color);
}

static GColor foreground_colour(void)
{
#ifdef PBL_COLOR
	return colour_from_rgb(foreground_color);
#else
	GColor foreground = colour_from_rgb(foreground_color);
	GColor background = background_colour();
	if (gcolor_equal(foreground, background)) {
		return gcolor_equal(background, GColorBlack) ? GColorWhite : GColorBlack;
	}
	return foreground;
#endif
}

static GFont font_for_choice(FontChoice choice, bool bold)
{
	switch (choice)
	{
		case FONT_CHOICE_LARGE:
			if (bold) {
				return custom_font_large_bold ? custom_font_large_bold : fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
			}
			return custom_font_large_light ? custom_font_large_light : fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
		case FONT_CHOICE_MEDIUM:
			if (bold) {
				return custom_font_medium_bold ? custom_font_medium_bold : fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
			}
			return custom_font_medium_light ? custom_font_medium_light : fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
		case FONT_CHOICE_SHARP:
			return fonts_get_system_font(bold ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_28);
		case FONT_CHOICE_COMPACT:
			return fonts_get_system_font(bold ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_24);
		case FONT_CHOICE_TALL:
			return fonts_get_system_font(bold ? FONT_KEY_BITHAM_42_BOLD : FONT_KEY_BITHAM_42_LIGHT);
		case FONT_CHOICE_SMALL:
			return fonts_get_system_font(bold ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18);
		default:
			return fonts_get_system_font(bold ? FONT_KEY_BITHAM_42_BOLD : FONT_KEY_BITHAM_42_LIGHT);
	}
}

static void load_custom_fonts(void)
{
	if (custom_font_large_light == NULL) {
		custom_font_large_light = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_54));
	}
	if (custom_font_large_bold == NULL) {
		custom_font_large_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_54));
	}
	if (custom_font_medium_light == NULL) {
		custom_font_medium_light = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_48));
	}
	if (custom_font_medium_bold == NULL) {
		custom_font_medium_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_48));
	}
}

static void unload_custom_font(GFont *font)
{
	if (*font != NULL) {
		fonts_unload_custom_font(*font);
		*font = NULL;
	}
}

static void unload_custom_fonts(void)
{
	unload_custom_font(&custom_font_large_light);
	unload_custom_font(&custom_font_large_bold);
	unload_custom_font(&custom_font_medium_light);
	unload_custom_font(&custom_font_medium_bold);
}

// Configure bold line of text
static void configureBoldLayer(TextLayer *textlayer)
{
	text_layer_set_font(textlayer, font_for_choice(render_font_choice, true));
	text_layer_set_text_color(textlayer, foreground_colour());
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
	text_layer_set_overflow_mode(textlayer, GTextOverflowModeTrailingEllipsis);
}

// Configure light line of text
static void configureLightLayer(TextLayer *textlayer)
{
	text_layer_set_font(textlayer, font_for_choice(render_font_choice, false));
	text_layer_set_text_color(textlayer, foreground_colour());
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
	text_layer_set_overflow_mode(textlayer, GTextOverflowModeTrailingEllipsis);
}

static void apply_window_colours(void)
{
	window_set_background_color(window, background_colour());
}

static void apply_layer_styles(void)
{
	for (int i = 0; i < NUM_LINES; i++)
	{
		configureLightLayer(lines[i].currentLayer);
		configureLightLayer(lines[i].nextLayer);
		layer_mark_dirty(text_layer_get_layer(lines[i].currentLayer));
		layer_mark_dirty(text_layer_get_layer(lines[i].nextLayer));
	}
	forceDisplayUpdate = true;
}

static bool should_use_high_resolution_layout(void);

static bool render_font_fits(FontChoice choice, char text[NUM_LINES][BUFFER_SIZE],
		char format[])
{
	int line_count = count_text_lines(text);
	const GTextAlignment alignment = lookup_text_alignment(text_align);

	for (int i = 0; i < line_count; i++) {
		TextFrame frame = text_frame_for_line(screen_bounds.size.w, screen_bounds.size.h,
			PBL_IF_ROUND_ELSE(true, false), choice, line_count, i);
		if (frame.x < 0 || frame.y < 0
				|| frame.x + frame.w > screen_bounds.size.w
				|| frame.y + frame.h > screen_bounds.size.h) {
			return false;
		}

		GFont font = font_for_choice(choice, format[i] == 'b');
		GSize content_size = graphics_text_layout_get_content_size(text[i], font,
			GRect(0, 0, screen_bounds.size.w * 4, frame.h * 2),
			GTextOverflowModeWordWrap, alignment);
		if (content_size.w > frame.w || content_size.h > frame.h) {
			return false;
		}
	}

	return true;
}

static FontChoice fallback_font_for_attempt(FontChoice preferred, int attempt)
{
	static const FontChoice high_resolution_large_fallbacks[] = {
		FONT_CHOICE_LARGE,
		FONT_CHOICE_MEDIUM,
		FONT_CHOICE_CLASSIC,
		FONT_CHOICE_SHARP,
		FONT_CHOICE_COMPACT,
		FONT_CHOICE_SMALL,
	};
	static const FontChoice high_resolution_compact_fallbacks[] = {
		FONT_CHOICE_MEDIUM,
		FONT_CHOICE_LARGE,
		FONT_CHOICE_COMPACT,
		FONT_CHOICE_SHARP,
		FONT_CHOICE_SMALL,
	};
	static const FontChoice tall_fallbacks[] = {
		FONT_CHOICE_TALL,
		FONT_CHOICE_CLASSIC,
		FONT_CHOICE_SHARP,
		FONT_CHOICE_COMPACT,
		FONT_CHOICE_SMALL,
	};
	static const FontChoice classic_fallbacks[] = {
		FONT_CHOICE_CLASSIC,
		FONT_CHOICE_SHARP,
		FONT_CHOICE_COMPACT,
		FONT_CHOICE_SMALL,
	};
	static const FontChoice sharp_fallbacks[] = {
		FONT_CHOICE_SHARP,
		FONT_CHOICE_COMPACT,
		FONT_CHOICE_SMALL,
	};
	static const FontChoice compact_fallbacks[] = {
		FONT_CHOICE_COMPACT,
		FONT_CHOICE_SMALL,
	};

	if (should_use_high_resolution_layout()) {
		if (preferred == FONT_CHOICE_COMPACT) {
			return attempt < (int)(sizeof(high_resolution_compact_fallbacks) / sizeof(high_resolution_compact_fallbacks[0]))
				? high_resolution_compact_fallbacks[attempt]
				: FONT_CHOICE_SMALL;
		}
		return attempt < (int)(sizeof(high_resolution_large_fallbacks) / sizeof(high_resolution_large_fallbacks[0]))
			? high_resolution_large_fallbacks[attempt]
			: FONT_CHOICE_SMALL;
	}

	switch (preferred) {
		case FONT_CHOICE_TALL:
			return attempt < (int)(sizeof(tall_fallbacks) / sizeof(tall_fallbacks[0]))
				? tall_fallbacks[attempt]
				: FONT_CHOICE_SMALL;
		case FONT_CHOICE_SHARP:
			return attempt < (int)(sizeof(sharp_fallbacks) / sizeof(sharp_fallbacks[0]))
				? sharp_fallbacks[attempt]
				: FONT_CHOICE_SMALL;
		case FONT_CHOICE_COMPACT:
			return attempt < (int)(sizeof(compact_fallbacks) / sizeof(compact_fallbacks[0]))
				? compact_fallbacks[attempt]
				: FONT_CHOICE_SMALL;
		default:
			return attempt < (int)(sizeof(classic_fallbacks) / sizeof(classic_fallbacks[0]))
				? classic_fallbacks[attempt]
				: FONT_CHOICE_SMALL;
	}
}

static FontChoice choose_render_font(char text[NUM_LINES][BUFFER_SIZE],
		char format[])
{
	for (int attempt = 0; attempt < FONT_CHOICE_COUNT; attempt++) {
		FontChoice choice = fallback_font_for_attempt(font_choice, attempt);
		if (render_font_fits(choice, text, format)) {
			return choice;
		}
	}

	return FONT_CHOICE_SMALL;
}

static bool should_use_high_resolution_layout(void)
{
	return screen_bounds.size.w >= 200 && screen_bounds.size.h >= 200;
}

static int font_visual_rank(FontChoice choice)
{
	switch (choice) {
		case FONT_CHOICE_LARGE:
			return 0;
		case FONT_CHOICE_MEDIUM:
			return 1;
		case FONT_CHOICE_TALL:
			return 2;
		case FONT_CHOICE_CLASSIC:
			return 3;
		case FONT_CHOICE_SHARP:
			return 4;
		case FONT_CHOICE_COMPACT:
			return 5;
		default:
			return 6;
	}
}

static void copy_text_lines(char destination[NUM_LINES][BUFFER_SIZE],
		char destination_format[], char source[NUM_LINES][BUFFER_SIZE],
		char source_format[])
{
	for (int i = 0; i < NUM_LINES; i++) {
		strncpy(destination[i], source[i], BUFFER_SIZE);
		destination[i][BUFFER_SIZE - 1] = '\0';
		destination_format[i] = source_format[i];
	}
}

static void choose_time_lines(Language language, int hours, int minutes, int seconds,
		char text[NUM_LINES][BUFFER_SIZE], char format[])
{
	static const int high_resolution_limits[] = { 7, 8, 9, 10, FUZZY_TEXT_LINE_LENGTH };
	char candidate[NUM_LINES][BUFFER_SIZE];
	char candidate_format[NUM_LINES];
	FontChoice best_font = FONT_CHOICE_SMALL;
	int best_line_count = 0;
	bool found = false;

	if (!should_use_high_resolution_layout()) {
		time_to_lines(language, hours, minutes, seconds, text, format);
		render_font_choice = choose_render_font(text, format);
		return;
	}

	for (int i = 0; i < (int)(sizeof(high_resolution_limits) / sizeof(high_resolution_limits[0])); i++) {
		time_to_lines_with_limit(language, hours, minutes, seconds, high_resolution_limits[i],
			candidate, candidate_format);
		FontChoice candidate_font = choose_render_font(candidate, candidate_format);
		int candidate_line_count = count_text_lines(candidate);

		if (!found || font_visual_rank(candidate_font) < font_visual_rank(best_font)
				|| (candidate_font == best_font && candidate_line_count > best_line_count)) {
			copy_text_lines(text, format, candidate, candidate_format);
			best_font = candidate_font;
			best_line_count = candidate_line_count;
			found = true;
		}

		if (candidate_font == FONT_CHOICE_LARGE && candidate_line_count >= 3) {
			break;
		}
	}

	render_font_choice = best_font;
}

static void choose_date_lines(Language language, int day, int date, int month,
		char text[NUM_LINES][BUFFER_SIZE], char format[])
{
	static const int high_resolution_limits[] = { 7, 8, 9, 10, FUZZY_TEXT_LINE_LENGTH };
	char candidate[NUM_LINES][BUFFER_SIZE];
	char candidate_format[NUM_LINES];
	FontChoice best_font = FONT_CHOICE_SMALL;
	int best_line_count = 0;
	bool found = false;

	if (!should_use_high_resolution_layout()) {
		date_to_lines(language, day, date, month, text, format);
		render_font_choice = choose_render_font(text, format);
		return;
	}

	for (int i = 0; i < (int)(sizeof(high_resolution_limits) / sizeof(high_resolution_limits[0])); i++) {
		date_to_lines_with_limit(language, day, date, month, high_resolution_limits[i],
			candidate, candidate_format);
		FontChoice candidate_font = choose_render_font(candidate, candidate_format);
		int candidate_line_count = count_text_lines(candidate);

		if (!found || font_visual_rank(candidate_font) < font_visual_rank(best_font)
				|| (candidate_font == best_font && candidate_line_count > best_line_count)) {
			copy_text_lines(text, format, candidate, candidate_format);
			best_font = candidate_font;
			best_line_count = candidate_line_count;
			found = true;
		}

		if (candidate_font == FONT_CHOICE_LARGE && candidate_line_count >= 3) {
			break;
		}
	}

	render_font_choice = best_font;
}

// Configure the layers for the given text
static int configureLayersForText(char text[NUM_LINES][BUFFER_SIZE], char format[])
{
	int numLines = 0;

	// Set bold layer.
	int i;
	for (i = 0; i < NUM_LINES; i++) {
		if (strlen(text[i]) > 0) {
			if (format[i] == 'b')
			{
				configureBoldLayer(lines[i].nextLayer);
			}
			else
			{
				configureLightLayer(lines[i].nextLayer);
			}
		}
		else
		{
			break;
		}
	}
	numLines = i;

	// Set y positions for the lines
	for (int i = 0; i < numLines; i++)
	{
		TextFrame frame = text_frame_for_line(screen_bounds.size.w, screen_bounds.size.h,
			PBL_IF_ROUND_ELSE(true, false), render_font_choice, numLines, i);
		layer_set_frame((Layer *)lines[i].nextLayer, GRect(screen_bounds.size.w, frame.y, frame.w, frame.h));
	}

	return numLines;
}

// Update screen based on new time
static void display_time(struct tm *t)
{
  // The current time text will be stored in the following strings
  char textLine[NUM_LINES][BUFFER_SIZE];
  char format[NUM_LINES];
  
  if (showTime || dateTimeout > 1) {
  	choose_time_lines(lang, t->tm_hour, t->tm_min, t->tm_sec, textLine, format);
    dateTimeout = 0;
    showTime = true;
  } else {
    choose_date_lines(lang, t->tm_wday, t->tm_mday, t->tm_mon, textLine, format);
  }
  
  int nextNLines = configureLayersForText(textLine, format);

  int delay = 0;
  for (int i = 0; i < NUM_LINES; i++) {
    if (forceDisplayUpdate || nextNLines != currentNLines || needToUpdateLine(&lines[i], textLine[i])) {
      updateLineTo(&lines[i], textLine[i], delay);
      delay += ANIMATION_STAGGER_TIME;
    }
  }

  currentNLines = nextNLines;
  forceDisplayUpdate = false;
}

static void tap_handler(AccelAxisType axis, int32_t direction)
{
  showTime = !showTime;
  display_time(t);
}

static void initLineForStart(Line* line)
{
	// Switch current and next layer
	TextLayer* tmp  = line->currentLayer;
	line->currentLayer = line->nextLayer;
	line->nextLayer = tmp;

	// Move current layer to screen;
	GRect rect = layer_get_frame((Layer *)line->currentLayer);
	rect.origin.x = (screen_bounds.size.w - rect.size.w) / 2;
	layer_set_frame((Layer *)line->currentLayer, rect);
}

// Update screen without animation first time we start the watchface
static void display_initial_time(struct tm *t)
{
	// The current time text will be stored in the following strings
	char textLine[NUM_LINES][BUFFER_SIZE];
	char format[NUM_LINES];

	choose_time_lines(lang, t->tm_hour, t->tm_min, t->tm_sec, textLine, format);

	// This configures the nextLayer for each line
	currentNLines = configureLayersForText(textLine, format);

	// Set the text and configure layers to the start position
	for (int i = 0; i < currentNLines; i++)
	{
		updateLayerText(lines[i].nextLayer, textLine[i]);
		// This call switches current- and nextLayer
		initLineForStart(&lines[i]);
	}	
}

// Time handler called every minute by the system
static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
	if (use_test_time) {
		return;
	}
	t = tick_time;
  
  if (!showTime) {
    dateTimeout++;
  }
  
	display_time(tick_time);
}

/**
 * Debug methods. For quickly debugging enable debug macro on top to transform the watchface into
 * a standard app and you will be able to change the time with the up and down buttons
 */
#if DEBUG

static void up_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
	(void)recognizer;
	(void)window;
	
	t->tm_min += 5;
	if (t->tm_min >= 60) {
		t->tm_min = 0;
		t->tm_hour += 1;
		
		if (t->tm_hour >= 24) {
			t->tm_hour = 0;
		}
	}
	display_time(t);
}


static void down_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
	(void)recognizer;
	(void)window;
	
	t->tm_min -= 5;
	if (t->tm_min < 0) {
		t->tm_min = 55;
		t->tm_hour -= 1;
		
		if (t->tm_hour < 0) {
			t->tm_hour = 23;
		}
	}
	display_time(t);
}

static void click_config_provider(ClickConfig **config, Window *window) {
  (void)window;

  config[BUTTON_ID_UP]->click.handler = (ClickHandler) up_single_click_handler;
  config[BUTTON_ID_UP]->click.repeat_interval_ms = 100;

  config[BUTTON_ID_DOWN]->click.handler = (ClickHandler) down_single_click_handler;
  config[BUTTON_ID_DOWN]->click.repeat_interval_ms = 100;
}

#endif

static void apply_settings_tuple(const uint32_t key, const Tuple* new_tuple) {
	GTextAlignment alignment;
	switch (key) {
		case TEXT_ALIGN_KEY:
			text_align = valid_text_align(new_tuple->value->uint8);
			persist_write_int(TEXT_ALIGN_KEY, text_align);
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Set text alignment: %u", text_align);

			alignment = lookup_text_alignment(text_align);
			for (int i = 0; i < NUM_LINES; i++)
			{
				text_layer_set_text_alignment(lines[i].currentLayer, alignment);
				text_layer_set_text_alignment(lines[i].nextLayer, alignment);
				layer_mark_dirty(text_layer_get_layer(lines[i].currentLayer));
				layer_mark_dirty(text_layer_get_layer(lines[i].nextLayer));
			}
			break;
		case INVERT_KEY:
			if (new_tuple->value->uint8 == 1)
			{
				foreground_color = 0x000000;
				background_color = 0xFFFFFF;
			}
			else
			{
				foreground_color = 0xFFFFFF;
				background_color = 0x000000;
			}
			persist_write_int(FOREGROUND_COLOR_KEY, foreground_color);
			persist_write_int(BACKGROUND_COLOR_KEY, background_color);
			apply_window_colours();
			apply_layer_styles();
			break;
		case LANGUAGE_KEY:
			lang = valid_language((Language) new_tuple->value->uint8);
			persist_write_int(LANGUAGE_KEY, lang);
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Set language: %u", lang);
			break;
		case FONT_CHOICE_KEY:
			font_choice = (FontChoice) new_tuple->value->uint8;
			if (font_choice >= FONT_CHOICE_COUNT)
			{
				font_choice = FONT_CHOICE_CLASSIC;
			}
			persist_write_int(FONT_CHOICE_KEY, font_choice);
			apply_layer_styles();
			break;
		case FOREGROUND_COLOR_KEY:
			foreground_color = new_tuple->value->int32;
			persist_write_int(FOREGROUND_COLOR_KEY, foreground_color);
			apply_layer_styles();
			break;
		case BACKGROUND_COLOR_KEY:
			background_color = new_tuple->value->int32;
			persist_write_int(BACKGROUND_COLOR_KEY, background_color);
			apply_window_colours();
			break;
		case TEST_HOUR_KEY:
			requested_test_hour = new_tuple->value->uint8;
			pending_test_hour = true;
			break;
		case TEST_MINUTE_KEY:
			requested_test_minute = new_tuple->value->uint8;
			pending_test_minute = true;
			break;
	}
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
	Tuple *tuple = dict_read_first(iterator);

	while (tuple != NULL) {
		apply_settings_tuple(tuple->key, tuple);
		tuple = dict_read_next(iterator);
	}

	if (pending_test_hour && pending_test_minute) {
		time_t raw_time;
		time(&raw_time);
		test_time = *localtime(&raw_time);
		test_time.tm_hour = requested_test_hour;
		test_time.tm_min = requested_test_minute;
		test_time.tm_sec = 0;
		t = &test_time;
		use_test_time = true;
	}
	pending_test_hour = false;
	pending_test_minute = false;

	if (t) {
		display_time(t);
	}
}

static void inbox_dropped_callback(AppMessageResult reason, void *context)
{
	APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message dropped: %d", reason);
}

static void init_line(Line* line)
{
	// Create layers with dummy position to the right of the screen
	line->currentLayer = text_layer_create(GRect(screen_bounds.size.w, 0, screen_bounds.size.w, 50));
	line->nextLayer = text_layer_create(GRect(screen_bounds.size.w, 0, screen_bounds.size.w, 50));

	// Configure a style
	configureLightLayer(line->currentLayer);
	configureLightLayer(line->nextLayer);

	// Set the text buffers
	line->lineStr1[0] = '\0';
	line->lineStr2[0] = '\0';
	text_layer_set_text(line->currentLayer, line->lineStr1);
	text_layer_set_text(line->nextLayer, line->lineStr2);

	// Initially there are no animations
	line->animation1 = NULL;
	line->animation2 = NULL;
}

static void destroy_line(Line* line)
{
	destroy_property_animation(&line->animation1);
	destroy_property_animation(&line->animation2);

	// Free layers
	text_layer_destroy(line->currentLayer);
	text_layer_destroy(line->nextLayer);
}

static void window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	screen_bounds = layer_get_bounds(window_layer);
	if (screen_bounds.size.w >= 200 && screen_bounds.size.h >= 200) {
		load_custom_fonts();
	}
	apply_window_colours();

	// Init and load lines
	for (int i = 0; i < NUM_LINES; i++)
	{
		init_line(&lines[i]);
		layer_add_child(window_layer, (Layer *)lines[i].currentLayer);
		layer_add_child(window_layer, (Layer *)lines[i].nextLayer);
	}

	// Configure time on init
	time_t raw_time;

	time(&raw_time);
	t = localtime(&raw_time);
	display_initial_time(t);

}

static void window_unload(Window *window)
{
	for (int i = 0; i < NUM_LINES; i++)
	{
		destroy_line(&lines[i]);
	}
	unload_custom_fonts();
}

static void handle_init() {
	if (!persist_exists(SETTINGS_SCHEMA_KEY)
			|| persist_read_int(SETTINGS_SCHEMA_KEY) < SETTINGS_SCHEMA_VERSION)
	{
		text_align = TEXT_ALIGN_CENTER;
		foreground_color = 0xFFFFFF;
		background_color = 0x000000;
		font_choice = FONT_CHOICE_CLASSIC;
		lang = EN_GB;
		persist_write_int(TEXT_ALIGN_KEY, text_align);
		persist_write_int(FOREGROUND_COLOR_KEY, foreground_color);
		persist_write_int(BACKGROUND_COLOR_KEY, background_color);
		persist_write_int(FONT_CHOICE_KEY, font_choice);
		persist_write_int(LANGUAGE_KEY, lang);
		persist_write_int(SETTINGS_SCHEMA_KEY, SETTINGS_SCHEMA_VERSION);
	}

	// Load settings from persistent storage
	if (persist_exists(TEXT_ALIGN_KEY))
	{
		text_align = valid_text_align(persist_read_int(TEXT_ALIGN_KEY));
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read text alignment from store: %u", text_align);
	}
	if (persist_exists(LANGUAGE_KEY))
	{
		lang = valid_language((Language) persist_read_int(LANGUAGE_KEY));
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read language from store: %u", lang);
	}
	if (persist_exists(FONT_CHOICE_KEY))
	{
		font_choice = (FontChoice) persist_read_int(FONT_CHOICE_KEY);
		if (font_choice >= FONT_CHOICE_COUNT)
		{
			font_choice = FONT_CHOICE_CLASSIC;
		}
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read font choice from store: %u", font_choice);
	}
	if (persist_exists(FOREGROUND_COLOR_KEY))
	{
		foreground_color = persist_read_int(FOREGROUND_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read foreground colour from store: %u", foreground_color);
	}
	else if (persist_exists(INVERT_KEY) && persist_read_bool(INVERT_KEY))
	{
		foreground_color = 0x000000;
	}
	if (persist_exists(BACKGROUND_COLOR_KEY))
	{
		background_color = persist_read_int(BACKGROUND_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read background colour from store: %u", background_color);
	}
	else if (persist_exists(INVERT_KEY) && persist_read_bool(INVERT_KEY))
	{
		background_color = 0xFFFFFF;
	}

	window = window_create();
	window_set_background_color(window, colour_from_rgb(background_color));
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload
	});

	// Initialize message queue
	const int inbound_size = 128;
	const int outbound_size = 64;
	app_message_register_inbox_received(inbox_received_callback);
	app_message_register_inbox_dropped(inbox_dropped_callback);
	app_message_open(inbound_size, outbound_size);

	const bool animated = true;
	window_stack_push(window, animated);
  
  // Sample as little as often to save battery and no need for precision
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  accel_tap_service_subscribe(tap_handler);

	// Subscribe to minute ticks
	tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

#if DEBUG
	// Button functionality
	window_set_click_config_provider(window, (ClickConfigProvider) click_config_provider);
#endif
}

static void handle_deinit()
{
	// Free window
	window_destroy(window);
}

int main(void)
{
	handle_init();
	app_event_loop();
	handle_deinit();
}
