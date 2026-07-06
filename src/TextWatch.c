#include <pebble.h>

#include "num2words.h"
#include "spy_face.h"
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
#define DATE_POSITION_KEY 6
#define DATE_FORMAT_KEY 7
#define ROW_ONE_COLOR_KEY 8
#define ROW_TWO_COLOR_KEY 9
#define ROW_THREE_COLOR_KEY 10
#define DATE_PART_ONE_COLOR_KEY 11
#define DATE_PART_TWO_COLOR_KEY 12
#define DATE_PART_THREE_COLOR_KEY 13
#define ANALOGUE_SECONDS_KEY 14
#define DISPLAY_MODE_KEY 15
#define SETTINGS_SCHEMA_KEY 97
#define TEST_HOUR_KEY 98
#define TEST_MINUTE_KEY 99

#define SETTINGS_SCHEMA_VERSION 6

#define TEXT_ALIGN_CENTER 0
#define TEXT_ALIGN_LEFT 1
#define TEXT_ALIGN_RIGHT 2

#define DATE_POSITION_OFF 0
#define DATE_POSITION_TOP 1
#define DATE_POSITION_BOTTOM 2

#define DATE_FORMAT_DD_MM_YY 0
#define DATE_FORMAT_MM_DD_YYYY 1
#define DATE_FORMAT_MON_D_AUG 2
#define DATE_FORMAT_DD_SLASH_MM 3
#define DATE_FORMAT_MM_SLASH_DD 4

#define DISPLAY_MODE_DIGITAL 0
#define DISPLAY_MODE_ANALOGUE 1

#define TEXT_COLOUR_MATCH_FOREGROUND -1
#define CUSTOM_ROW_COUNT 3
#define DATE_PART_COUNT 3
#define DATE_LAYER_HEIGHT 22
#define DATE_LAYER_VERTICAL_MARGIN 2
#define QUIET_TIME_ICON_SIZE 18
#define QUIET_TIME_ICON_SPACING 3

// The time it takes for a layer to slide in or out.
#define ANIMATION_DURATION 400
// Delay between the layers animations, from top to bottom
#define ANIMATION_STAGGER_TIME 150
// Delay from the start of the current layer going out until the next layer slides in
#define ANIMATION_OUT_IN_DELAY 100

static int text_align = TEXT_ALIGN_CENTER;
static int foreground_color = 0xFFFFFF;
static int background_color = 0x000000;
static int row_colors[CUSTOM_ROW_COUNT] = {
	TEXT_COLOUR_MATCH_FOREGROUND,
	TEXT_COLOUR_MATCH_FOREGROUND,
	TEXT_COLOUR_MATCH_FOREGROUND,
};
static int date_part_colors[DATE_PART_COUNT] = {
	TEXT_COLOUR_MATCH_FOREGROUND,
	TEXT_COLOUR_MATCH_FOREGROUND,
	TEXT_COLOUR_MATCH_FOREGROUND,
};
static int date_position = DATE_POSITION_OFF;
static int date_format = DATE_FORMAT_DD_MM_YY;
static bool analogue_seconds_enabled = true;
static int display_mode = DISPLAY_MODE_DIGITAL;
static FontChoice font_choice = FONT_CHOICE_CLASSIC;
static FontChoice render_font_choice = FONT_CHOICE_CLASSIC;
static Language lang = EN_GB;
static GFont custom_font_large_light;
static GFont custom_font_large_bold;
static GFont custom_font_medium_light;
static GFont custom_font_medium_bold;

static Window *window;
static GRect screen_bounds;
static bool window_loaded = false;
static TextLayer *date_layers[DATE_PART_COUNT];
static char date_text[DATE_PART_COUNT][8];
static Layer *quiet_time_icon_layer;
static Layer *spy_face_layer;
static bool quiet_time_active = false;

typedef struct {
	TextLayer *currentLayer;
	TextLayer *nextLayer;
	char lineStr1[BUFFER_SIZE];
	char lineStr2[BUFFER_SIZE];
	char currentFormat;
	char nextFormat;
	int currentRow;
	int nextRow;
	PropertyAnimation *animation1;
	PropertyAnimation *animation2;
} Line;

static Line lines[NUM_LINES];

static struct tm *t;
static struct tm current_time;
static struct tm test_time;
static bool use_test_time = false;
static bool pending_test_hour = false;
static bool pending_test_minute = false;
static int requested_test_hour = 0;
static int requested_test_minute = 0;
static bool rendered_backlight_on = false;
static bool subscribed_to_seconds = false;

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed);

static void update_tick_subscription(bool use_second_ticks)
{
	if (subscribed_to_seconds == use_second_ticks) {
		return;
	}

	tick_timer_service_subscribe(use_second_ticks ? SECOND_UNIT : MINUTE_UNIT, handle_minute_tick);
	subscribed_to_seconds = use_second_ticks;
}

static struct tm *remember_time(struct tm *date_time)
{
	if (date_time == NULL) {
		return NULL;
	}

	current_time = *date_time;
	t = &current_time;
	return t;
}

static void refresh_spy_face(struct tm *date_time)
{
	if (spy_face_layer == NULL || date_time == NULL) {
		return;
	}

	BatteryChargeState battery_state = battery_state_service_peek();
	bool backlight_on = light_is_on();
	SpyFaceState state = {
		.hour = date_time->tm_hour,
		.minute = date_time->tm_min,
		.second = date_time->tm_sec,
		.day = date_time->tm_mday,
		.month = date_time->tm_mon,
		.year = date_time->tm_year + 1900,
		.weekday = date_time->tm_wday,
		.date_format = date_format,
		.display_mode = display_mode,
		.battery_percent = battery_state.charge_percent,
		.bluetooth_connected = bluetooth_connection_service_peek(),
		.backlight_on = backlight_on,
		.quiet_time_active = quiet_time_is_active(),
		.analogue_seconds_enabled = analogue_seconds_enabled,
		.twenty_four_hour_style = clock_is_24h_style(),
	};

	rendered_backlight_on = backlight_on;
	spy_face_layer_set_state(spy_face_layer, &state);
	if (backlight_on && spy_face_layer_wants_second_ticks(spy_face_layer)) {
		update_tick_subscription(true);
	} else if (subscribed_to_seconds) {
		update_tick_subscription(false);
	}
}

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

static void destroy_finished_property_animation(PropertyAnimation **animation)
{
	if (*animation == NULL) {
		return;
	}

	property_animation_destroy(*animation);
	*animation = NULL;
}

static void animationStoppedHandler(struct Animation *animation, bool finished, void *context)
{
	(void)animation;
	(void)finished;

	Line *line = (Line *)context;
	if (line == NULL) {
		return;
	}

	if (line->nextLayer != NULL) {
		GRect rect = layer_get_frame((Layer *)line->nextLayer);
		rect.origin.x = screen_bounds.size.w;
		layer_set_frame((Layer *)line->nextLayer, rect);
	}

	destroy_finished_property_animation(&line->animation1);
	destroy_finished_property_animation(&line->animation2);
}

// Animate line
static void makeAnimationsForLayer(Line *line, int delay)
{
	TextLayer *current = line->currentLayer;
	TextLayer *next = line->nextLayer;
	if (current == NULL || next == NULL) {
		return;
	}

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
	}, line);

	// Start the animations
	animation_schedule(animation1);
	animation_schedule(animation2);
}

static void updateLayerText(TextLayer* layer, char* text)
{
	if (layer == NULL) {
		return;
	}
	const char* layerText = text_layer_get_text(layer);
	if (layerText == NULL) {
		return;
	}
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
	char tmp_format = line->nextFormat;
	line->nextFormat = line->currentFormat;
	line->currentFormat = tmp_format;
	int tmp_row = line->nextRow;
	line->nextRow = line->currentRow;
	line->currentRow = tmp_row;
}

// Check to see if the current line needs to be updated
static bool needToUpdateLine(Line *line, char *nextValue)
{
	if (line->currentLayer == NULL) {
		return true;
	}
	const char *currentStr = text_layer_get_text(line->currentLayer);
	if (currentStr == NULL) {
		return true;
	}

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

static int valid_date_position(int position)
{
	switch (position) {
		case DATE_POSITION_OFF:
		case DATE_POSITION_TOP:
		case DATE_POSITION_BOTTOM:
			return position;
		default:
			return DATE_POSITION_OFF;
	}
}

static int valid_date_format(int format)
{
	switch (format) {
		case DATE_FORMAT_DD_MM_YY:
		case DATE_FORMAT_MM_DD_YYYY:
		case DATE_FORMAT_MON_D_AUG:
		case DATE_FORMAT_DD_SLASH_MM:
		case DATE_FORMAT_MM_SLASH_DD:
			return format;
		default:
			return DATE_FORMAT_DD_MM_YY;
	}
}

static int valid_display_mode(int mode)
{
	switch (mode) {
		case DISPLAY_MODE_DIGITAL:
		case DISPLAY_MODE_ANALOGUE:
			return mode;
		default:
			return DISPLAY_MODE_DIGITAL;
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

static void format_corner_date(char buffer[], size_t buffer_size, const struct tm *date_time)
{
	static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	if (buffer_size == 0) {
		return;
	}

	int day = date_time->tm_mday;
	if (day < 1) {
		day = 1;
	} else if (day > 31) {
		day = 31;
	}
	int month = date_time->tm_mon;
	if (month < 0) {
		month = 0;
	} else if (month > 11) {
		month = 11;
	}
	int weekday = date_time->tm_wday;
	if (weekday < 0) {
		weekday = 0;
	} else if (weekday > 6) {
		weekday = 6;
	}
	int year = date_time->tm_year + 1900;
	if (year < 0) {
		year = 0;
	} else if (year > 9999) {
		year = 9999;
	}

	switch (date_format) {
		case DATE_FORMAT_MM_DD_YYYY:
			snprintf(buffer, buffer_size, "%02d-%02d-%04d", month + 1, day, year);
			break;
		case DATE_FORMAT_MON_D_AUG:
			snprintf(buffer, buffer_size, "%s %d %s", days[weekday], day, months[month]);
			break;
		case DATE_FORMAT_DD_SLASH_MM:
			snprintf(buffer, buffer_size, "%02d/%02d", day, month + 1);
			break;
		case DATE_FORMAT_MM_SLASH_DD:
			snprintf(buffer, buffer_size, "%02d/%02d", month + 1, day);
			break;
		default:
			snprintf(buffer, buffer_size, "%02d-%02d-%02d", day, month + 1, year % 100);
			break;
	}
	buffer[buffer_size - 1] = '\0';
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

static GColor configured_text_colour(int configured_colour)
{
	if (configured_colour == TEXT_COLOUR_MATCH_FOREGROUND) {
		return foreground_colour();
	}

	GColor colour = colour_from_rgb(configured_colour);
#ifndef PBL_COLOR
	GColor background = background_colour();
	if (gcolor_equal(colour, background)) {
		return gcolor_equal(background, GColorBlack) ? GColorWhite : GColorBlack;
	}
#endif
	return colour;
}

static GColor row_colour_for_index(int row)
{
	if (row < 0) {
		row = 0;
	} else if (row >= CUSTOM_ROW_COUNT) {
		row = CUSTOM_ROW_COUNT - 1;
	}

	return configured_text_colour(row_colors[row]);
}

static GColor date_part_colour_for_index(int part)
{
	if (part < 0) {
		part = 0;
	} else if (part >= DATE_PART_COUNT) {
		part = DATE_PART_COUNT - 1;
	}

	return configured_text_colour(date_part_colors[part]);
}

static GFont font_for_choice(FontChoice choice, bool bold)
{
	switch (choice)
	{
		case FONT_CHOICE_LARGE:
			if (bold) {
				if (custom_font_large_bold == NULL) {
					custom_font_large_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_54));
				}
				return custom_font_large_bold ? custom_font_large_bold : fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
			}
			if (custom_font_large_light == NULL) {
				custom_font_large_light = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_54));
			}
			return custom_font_large_light ? custom_font_large_light : fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
		case FONT_CHOICE_MEDIUM:
			if (bold) {
				if (custom_font_medium_bold == NULL) {
					custom_font_medium_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_48));
				}
				return custom_font_medium_bold ? custom_font_medium_bold : fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
			}
			if (custom_font_medium_light == NULL) {
				custom_font_medium_light = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_48));
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
	if (textlayer == NULL) {
		return;
	}
	text_layer_set_font(textlayer, font_for_choice(render_font_choice, true));
	text_layer_set_text_color(textlayer, row_colour_for_index(2));
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
	text_layer_set_overflow_mode(textlayer, GTextOverflowModeTrailingEllipsis);
}

// Configure light line of text
static void configureLightLayer(TextLayer *textlayer, int row)
{
	if (textlayer == NULL) {
		return;
	}
	text_layer_set_font(textlayer, font_for_choice(render_font_choice, false));
	text_layer_set_text_color(textlayer, row_colour_for_index(row));
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
	text_layer_set_overflow_mode(textlayer, GTextOverflowModeTrailingEllipsis);
}

static void configureLayerForFormat(TextLayer *textlayer, char format, int row)
{
	if (format == 'b') {
		configureBoldLayer(textlayer);
	} else {
		configureLightLayer(textlayer, row);
	}
}

static GRect date_layer_frame_for_position(int position)
{
	const int horizontal_margin = PBL_IF_ROUND_ELSE(20, 4);
	int y = DATE_LAYER_VERTICAL_MARGIN;

	if (position == DATE_POSITION_BOTTOM) {
		y = screen_bounds.size.h - DATE_LAYER_HEIGHT - DATE_LAYER_VERTICAL_MARGIN;
	}

	return GRect(horizontal_margin, y, screen_bounds.size.w - (horizontal_margin * 2), DATE_LAYER_HEIGHT);
}

static GRect date_layer_frame(void)
{
	return date_layer_frame_for_position(date_position);
}

static GFont date_layer_font(void)
{
	return fonts_get_system_font(FONT_KEY_GOTHIC_18);
}

static void quiet_time_icon_layer_update_proc(Layer *layer, GContext *ctx)
{
	if (!quiet_time_active) {
		return;
	}

	GRect bounds = layer_get_bounds(layer);
	int diameter = bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h;
	int radius = diameter / 2;
	GPoint centre = GPoint(bounds.size.w / 2, bounds.size.h / 2);

	graphics_context_set_fill_color(ctx, date_part_colour_for_index(0));
	graphics_fill_circle(ctx, centre, radius);
	graphics_context_set_fill_color(ctx, background_colour());
	graphics_fill_circle(ctx, GPoint(centre.x + (radius / 2), centre.y - (radius / 4)), radius);
}

static int measured_date_part_width(int part, GFont font, int max_width)
{
	if (part < 0 || part >= DATE_PART_COUNT || date_text[part][0] == '\0') {
		return 0;
	}

	GSize size = graphics_text_layout_get_content_size(
		date_text[part],
		font,
		GRect(0, 0, max_width, DATE_LAYER_HEIGHT),
		GTextOverflowModeTrailingEllipsis,
		GTextAlignmentLeft
	);
	int width = size.w + 1;
	if (width < 1) {
		return 1;
	}
	if (width > max_width) {
		return max_width;
	}
	return width;
}

static int aligned_date_content_x(GRect frame, int content_width)
{
	int width = content_width;
	if (width > frame.size.w) {
		width = frame.size.w;
	}

	int x = frame.origin.x;
	if (text_align == TEXT_ALIGN_CENTER) {
		x += (frame.size.w - width) / 2;
	} else if (text_align == TEXT_ALIGN_RIGHT) {
		x += frame.size.w - width;
	}
	return x;
}

static void layoutDateLayers(void)
{
	GRect frame = date_layer_frame();
	GRect top_frame = date_layer_frame_for_position(DATE_POSITION_TOP);
	GFont font = date_layer_font();
	int widths[DATE_PART_COUNT];
	int total_width = 0;
	bool date_is_top = date_position == DATE_POSITION_TOP;
	int quiet_width = quiet_time_active ? QUIET_TIME_ICON_SIZE : 0;
	int quiet_spacing = quiet_time_active && date_is_top ? QUIET_TIME_ICON_SPACING : 0;
	int measured_width = frame.size.w - quiet_width - quiet_spacing;
	if (measured_width < 1) {
		measured_width = 1;
	}

	for (int i = 0; i < DATE_PART_COUNT; i++) {
		widths[i] = measured_date_part_width(i, font, date_is_top ? measured_width : frame.size.w);
		total_width += widths[i];
	}

	if (total_width > frame.size.w) {
		total_width = frame.size.w;
	}

	int group_width = total_width;
	if (date_is_top && quiet_time_active) {
		group_width += QUIET_TIME_ICON_SIZE + QUIET_TIME_ICON_SPACING;
	}

	int x = aligned_date_content_x(date_is_top ? top_frame : frame, group_width);
	if (date_is_top && quiet_time_active) {
		x += QUIET_TIME_ICON_SIZE + QUIET_TIME_ICON_SPACING;
	}

	for (int i = 0; i < DATE_PART_COUNT; i++) {
		if (date_layers[i] == NULL) {
			continue;
		}
		int width = widths[i] > 0 ? widths[i] : 1;
		layer_set_frame(text_layer_get_layer(date_layers[i]), GRect(x, frame.origin.y, width, frame.size.h));
		layer_set_hidden(text_layer_get_layer(date_layers[i]), date_position == DATE_POSITION_OFF || widths[i] == 0);
		x += widths[i];
	}

	if (quiet_time_icon_layer == NULL) {
		return;
	}

	if (!quiet_time_active) {
		layer_set_hidden(quiet_time_icon_layer, true);
		return;
	}

	int quiet_group_width = QUIET_TIME_ICON_SIZE;
	if (date_is_top && total_width > 0) {
		quiet_group_width += QUIET_TIME_ICON_SPACING + total_width;
	}
	int quiet_x = aligned_date_content_x(top_frame, quiet_group_width);
	int quiet_y = top_frame.origin.y + ((top_frame.size.h - QUIET_TIME_ICON_SIZE) / 2);
	layer_set_frame(quiet_time_icon_layer, GRect(quiet_x, quiet_y, QUIET_TIME_ICON_SIZE, QUIET_TIME_ICON_SIZE));
	layer_set_hidden(quiet_time_icon_layer, false);
	layer_mark_dirty(quiet_time_icon_layer);
}

static void configureDateLayer(TextLayer *textlayer, int part)
{
	if (textlayer == NULL) {
		return;
	}
	text_layer_set_font(textlayer, date_layer_font());
	text_layer_set_text_color(textlayer, date_part_colour_for_index(part));
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, GTextAlignmentLeft);
	text_layer_set_overflow_mode(textlayer, GTextOverflowModeTrailingEllipsis);
}

static void configureDateLayers(void)
{
	for (int i = 0; i < DATE_PART_COUNT; i++) {
		configureDateLayer(date_layers[i], i);
	}
	layoutDateLayers();
}

static bool is_date_separator(char value)
{
	return value == '-' || value == '/' || value == ' ';
}

static void split_corner_date(char source[])
{
	for (int i = 0; i < DATE_PART_COUNT; i++) {
		date_text[i][0] = '\0';
	}

	int part = 0;
	int offset = 0;
	for (int i = 0; source[i] != '\0' && part < DATE_PART_COUNT; i++) {
		if (offset >= (int)sizeof(date_text[part]) - 1) {
			continue;
		}

		date_text[part][offset++] = source[i];
		date_text[part][offset] = '\0';

		if (is_date_separator(source[i]) && source[i + 1] != '\0' && part < DATE_PART_COUNT - 1) {
			part++;
			offset = 0;
		}
	}
}

static void update_corner_date(struct tm *date_time)
{
	quiet_time_active = quiet_time_is_active();

	if (date_time == NULL) {
		configureDateLayers();
		return;
	}

	if (date_position == DATE_POSITION_OFF) {
		for (int i = 0; i < DATE_PART_COUNT; i++) {
			date_text[i][0] = '\0';
			if (date_layers[i] != NULL) {
				text_layer_set_text(date_layers[i], date_text[i]);
				layer_set_hidden(text_layer_get_layer(date_layers[i]), true);
			}
		}
		configureDateLayers();
		return;
	}

	char formatted_date[16];
	format_corner_date(formatted_date, sizeof(formatted_date), date_time);
	split_corner_date(formatted_date);
	for (int i = 0; i < DATE_PART_COUNT; i++) {
		if (date_layers[i] != NULL) {
			text_layer_set_text(date_layers[i], date_text[i]);
		}
	}
	configureDateLayers();
}

static void apply_window_colours(void)
{
	if (!window) {
		return;
	}
	window_set_background_color(window, background_colour());
	if (quiet_time_icon_layer != NULL) {
		layer_mark_dirty(quiet_time_icon_layer);
	}
}

static void apply_layer_styles(void)
{
	if (!window_loaded) {
		forceDisplayUpdate = true;
		return;
	}

	for (int i = 0; i < NUM_LINES; i++)
	{
		configureLayerForFormat(lines[i].currentLayer, lines[i].currentFormat, lines[i].currentRow);
		configureLayerForFormat(lines[i].nextLayer, lines[i].nextFormat, lines[i].nextRow);
		if (lines[i].currentLayer != NULL) {
			layer_mark_dirty(text_layer_get_layer(lines[i].currentLayer));
		}
		if (lines[i].nextLayer != NULL) {
			layer_mark_dirty(text_layer_get_layer(lines[i].nextLayer));
		}
	}
	configureDateLayers();
	forceDisplayUpdate = true;
}

static bool should_use_high_resolution_layout(void);

static FontChoice choose_render_font(char text[NUM_LINES][BUFFER_SIZE])
{
	return text_font_choice_that_fits(screen_bounds.size.w, screen_bounds.size.h,
		PBL_IF_ROUND_ELSE(true, false), font_choice, text);
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
		render_font_choice = choose_render_font(text);
		return;
	}

	for (int i = 0; i < (int)(sizeof(high_resolution_limits) / sizeof(high_resolution_limits[0])); i++) {
		time_to_lines_with_limit(language, hours, minutes, seconds, high_resolution_limits[i],
			candidate, candidate_format);
		FontChoice candidate_font = choose_render_font(candidate);
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
		render_font_choice = choose_render_font(text);
		return;
	}

	for (int i = 0; i < (int)(sizeof(high_resolution_limits) / sizeof(high_resolution_limits[0])); i++) {
		date_to_lines_with_limit(language, day, date, month, high_resolution_limits[i],
			candidate, candidate_format);
		FontChoice candidate_font = choose_render_font(candidate);
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
			lines[i].nextFormat = format[i];
			lines[i].nextRow = format[i] == 'b' ? 2 : i;
			configureLayerForFormat(lines[i].nextLayer, format[i], lines[i].nextRow);
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
		if (lines[i].nextLayer != NULL) {
			layer_set_frame((Layer *)lines[i].nextLayer, GRect(screen_bounds.size.w, frame.y, frame.w, frame.h));
		}
	}

	return numLines;
}

// Update screen based on new time
static void display_time(struct tm *t)
{
  refresh_spy_face(t);

  // The current time text will be stored in the following strings
  char textLine[NUM_LINES][BUFFER_SIZE];
  char format[NUM_LINES];
  update_corner_date(t);
  
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

static void initLineForStart(Line* line)
{
	// Switch current and next layer
	TextLayer* tmp  = line->currentLayer;
	line->currentLayer = line->nextLayer;
	line->nextLayer = tmp;
	char tmp_format = line->currentFormat;
	line->currentFormat = line->nextFormat;
	line->nextFormat = tmp_format;
	int tmp_row = line->currentRow;
	line->currentRow = line->nextRow;
	line->nextRow = tmp_row;
	if (line->currentLayer == NULL) {
		return;
	}

	// Move current layer to screen;
	GRect rect = layer_get_frame((Layer *)line->currentLayer);
	rect.origin.x = (screen_bounds.size.w - rect.size.w) / 2;
	layer_set_frame((Layer *)line->currentLayer, rect);
}

// Update screen without animation first time we start the watchface
static void display_initial_time(struct tm *t)
{
	refresh_spy_face(t);

	// The current time text will be stored in the following strings
	char textLine[NUM_LINES][BUFFER_SIZE];
	char format[NUM_LINES];
	update_corner_date(t);

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
	t = remember_time(tick_time);

	if ((units_changed & MINUTE_UNIT) == 0) {
		bool backlight_on = light_is_on();
		if ((backlight_on || rendered_backlight_on)
				&& spy_face_layer_wants_second_ticks(spy_face_layer)) {
			refresh_spy_face(t);
		} else {
			update_tick_subscription(false);
		}
		return;
	}

	refresh_spy_face(t);

	if (!showTime) {
		dateTimeout++;
	}

	display_time(t);
}

static void handle_battery_state(BatteryChargeState charge_state)
{
	(void)charge_state;
	refresh_spy_face(t);
}

static void handle_bluetooth_connection(bool connected)
{
	(void)connected;
	refresh_spy_face(t);
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
	switch (key) {
		case TEXT_ALIGN_KEY:
			text_align = valid_text_align(new_tuple->value->uint8);
			persist_write_int(TEXT_ALIGN_KEY, text_align);
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Set text alignment: %u", text_align);
			apply_layer_styles();
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
		case DATE_POSITION_KEY:
			date_position = valid_date_position(new_tuple->value->uint8);
			persist_write_int(DATE_POSITION_KEY, date_position);
			update_corner_date(t);
			break;
		case DATE_FORMAT_KEY:
			date_format = valid_date_format(new_tuple->value->uint8);
			persist_write_int(DATE_FORMAT_KEY, date_format);
			update_corner_date(t);
			refresh_spy_face(t);
			break;
		case ANALOGUE_SECONDS_KEY:
			analogue_seconds_enabled = new_tuple->value->uint8 != 0;
			persist_write_bool(ANALOGUE_SECONDS_KEY, analogue_seconds_enabled);
			refresh_spy_face(t);
			break;
		case DISPLAY_MODE_KEY:
			display_mode = valid_display_mode(new_tuple->value->uint8);
			persist_write_int(DISPLAY_MODE_KEY, display_mode);
			refresh_spy_face(t);
			break;
		case ROW_ONE_COLOR_KEY:
			row_colors[0] = new_tuple->value->int32;
			persist_write_int(ROW_ONE_COLOR_KEY, row_colors[0]);
			apply_layer_styles();
			break;
		case ROW_TWO_COLOR_KEY:
			row_colors[1] = new_tuple->value->int32;
			persist_write_int(ROW_TWO_COLOR_KEY, row_colors[1]);
			apply_layer_styles();
			break;
		case ROW_THREE_COLOR_KEY:
			row_colors[2] = new_tuple->value->int32;
			persist_write_int(ROW_THREE_COLOR_KEY, row_colors[2]);
			apply_layer_styles();
			break;
		case DATE_PART_ONE_COLOR_KEY:
			date_part_colors[0] = new_tuple->value->int32;
			persist_write_int(DATE_PART_ONE_COLOR_KEY, date_part_colors[0]);
			configureDateLayers();
			break;
		case DATE_PART_TWO_COLOR_KEY:
			date_part_colors[1] = new_tuple->value->int32;
			persist_write_int(DATE_PART_TWO_COLOR_KEY, date_part_colors[1]);
			configureDateLayers();
			break;
		case DATE_PART_THREE_COLOR_KEY:
			date_part_colors[2] = new_tuple->value->int32;
			persist_write_int(DATE_PART_THREE_COLOR_KEY, date_part_colors[2]);
			configureDateLayers();
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

	if (t && window_loaded) {
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
	if (line->currentLayer == NULL || line->nextLayer == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create text layers");
		return;
	}

	// Set the text buffers
	line->lineStr1[0] = '\0';
	line->lineStr2[0] = '\0';
	line->currentFormat = ' ';
	line->nextFormat = ' ';
	line->currentRow = 0;
	line->nextRow = 0;
	// Configure a style
	configureLightLayer(line->currentLayer, line->currentRow);
	configureLightLayer(line->nextLayer, line->nextRow);
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
	if (line->currentLayer != NULL) {
		text_layer_destroy(line->currentLayer);
		line->currentLayer = NULL;
	}
	if (line->nextLayer != NULL) {
		text_layer_destroy(line->nextLayer);
		line->nextLayer = NULL;
	}
}

static void window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	screen_bounds = layer_get_bounds(window_layer);
	apply_window_colours();

	// Init and load lines
	for (int i = 0; i < NUM_LINES; i++)
	{
		init_line(&lines[i]);
		if (lines[i].currentLayer != NULL) {
			layer_add_child(window_layer, (Layer *)lines[i].currentLayer);
		}
		if (lines[i].nextLayer != NULL) {
			layer_add_child(window_layer, (Layer *)lines[i].nextLayer);
		}
	}

	for (int i = 0; i < DATE_PART_COUNT; i++) {
		date_layers[i] = text_layer_create(date_layer_frame());
		if (date_layers[i] == NULL) {
			APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create date layer part %d", i);
			continue;
		}
		date_text[i][0] = '\0';
		text_layer_set_text(date_layers[i], date_text[i]);
		configureDateLayer(date_layers[i], i);
		layer_add_child(window_layer, text_layer_get_layer(date_layers[i]));
	}

	quiet_time_icon_layer = layer_create(GRect(0, 0, QUIET_TIME_ICON_SIZE, QUIET_TIME_ICON_SIZE));
	if (quiet_time_icon_layer == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create quiet time icon layer");
	} else {
		layer_set_update_proc(quiet_time_icon_layer, quiet_time_icon_layer_update_proc);
		layer_set_hidden(quiet_time_icon_layer, true);
		layer_add_child(window_layer, quiet_time_icon_layer);
	}

	spy_face_layer = spy_face_layer_create(screen_bounds);
	if (spy_face_layer == NULL) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create spy face layer");
	} else {
		layer_add_child(window_layer, spy_face_layer);
	}
	window_loaded = true;

	// Configure time on init
	time_t raw_time;

	time(&raw_time);
	t = remember_time(localtime(&raw_time));
	display_initial_time(t);

}

static void window_unload(Window *window)
{
	window_loaded = false;

	for (int i = 0; i < NUM_LINES; i++)
	{
		destroy_line(&lines[i]);
	}
	for (int i = 0; i < DATE_PART_COUNT; i++) {
		if (date_layers[i] != NULL) {
			text_layer_destroy(date_layers[i]);
			date_layers[i] = NULL;
		}
	}
	if (quiet_time_icon_layer != NULL) {
		layer_destroy(quiet_time_icon_layer);
		quiet_time_icon_layer = NULL;
	}
	if (spy_face_layer != NULL) {
		spy_face_layer_destroy(spy_face_layer);
		spy_face_layer = NULL;
	}
	unload_custom_fonts();
}

static void handle_init() {
	if (!persist_exists(SETTINGS_SCHEMA_KEY)
			|| persist_read_int(SETTINGS_SCHEMA_KEY) < SETTINGS_SCHEMA_VERSION)
	{
		if (!persist_exists(TEXT_ALIGN_KEY)) {
			persist_write_int(TEXT_ALIGN_KEY, text_align);
		}
		if (!persist_exists(FOREGROUND_COLOR_KEY)) {
			persist_write_int(FOREGROUND_COLOR_KEY, foreground_color);
		}
		if (!persist_exists(BACKGROUND_COLOR_KEY)) {
			persist_write_int(BACKGROUND_COLOR_KEY, background_color);
		}
		if (!persist_exists(FONT_CHOICE_KEY)) {
			persist_write_int(FONT_CHOICE_KEY, font_choice);
		}
		if (!persist_exists(LANGUAGE_KEY)) {
			persist_write_int(LANGUAGE_KEY, lang);
		}
		if (!persist_exists(DATE_POSITION_KEY)) {
			persist_write_int(DATE_POSITION_KEY, date_position);
		}
		if (!persist_exists(DATE_FORMAT_KEY)) {
			persist_write_int(DATE_FORMAT_KEY, date_format);
		}
		if (!persist_exists(ROW_ONE_COLOR_KEY)) {
			persist_write_int(ROW_ONE_COLOR_KEY, row_colors[0]);
		}
		if (!persist_exists(ROW_TWO_COLOR_KEY)) {
			persist_write_int(ROW_TWO_COLOR_KEY, row_colors[1]);
		}
		if (!persist_exists(ROW_THREE_COLOR_KEY)) {
			persist_write_int(ROW_THREE_COLOR_KEY, row_colors[2]);
		}
		if (!persist_exists(DATE_PART_ONE_COLOR_KEY)) {
			persist_write_int(DATE_PART_ONE_COLOR_KEY, date_part_colors[0]);
		}
		if (!persist_exists(DATE_PART_TWO_COLOR_KEY)) {
			persist_write_int(DATE_PART_TWO_COLOR_KEY, date_part_colors[1]);
		}
		if (!persist_exists(DATE_PART_THREE_COLOR_KEY)) {
			persist_write_int(DATE_PART_THREE_COLOR_KEY, date_part_colors[2]);
		}
		if (!persist_exists(ANALOGUE_SECONDS_KEY)) {
			persist_write_bool(ANALOGUE_SECONDS_KEY, analogue_seconds_enabled);
		}
		if (!persist_exists(DISPLAY_MODE_KEY)) {
			persist_write_int(DISPLAY_MODE_KEY, display_mode);
		}
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
	if (persist_exists(DATE_POSITION_KEY))
	{
		date_position = valid_date_position(persist_read_int(DATE_POSITION_KEY));
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read date position from store: %u", date_position);
	}
	if (persist_exists(DATE_FORMAT_KEY))
	{
		date_format = valid_date_format(persist_read_int(DATE_FORMAT_KEY));
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read date format from store: %u", date_format);
	}
	if (persist_exists(ROW_ONE_COLOR_KEY))
	{
		row_colors[0] = persist_read_int(ROW_ONE_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read row one colour from store: %d", row_colors[0]);
	}
	if (persist_exists(ROW_TWO_COLOR_KEY))
	{
		row_colors[1] = persist_read_int(ROW_TWO_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read row two colour from store: %d", row_colors[1]);
	}
	if (persist_exists(ROW_THREE_COLOR_KEY))
	{
		row_colors[2] = persist_read_int(ROW_THREE_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read row three colour from store: %d", row_colors[2]);
	}
	if (persist_exists(DATE_PART_ONE_COLOR_KEY))
	{
		date_part_colors[0] = persist_read_int(DATE_PART_ONE_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read date part one colour from store: %d", date_part_colors[0]);
	}
	if (persist_exists(DATE_PART_TWO_COLOR_KEY))
	{
		date_part_colors[1] = persist_read_int(DATE_PART_TWO_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read date part two colour from store: %d", date_part_colors[1]);
	}
	if (persist_exists(DATE_PART_THREE_COLOR_KEY))
	{
		date_part_colors[2] = persist_read_int(DATE_PART_THREE_COLOR_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read date part three colour from store: %d", date_part_colors[2]);
	}
	if (persist_exists(ANALOGUE_SECONDS_KEY))
	{
		analogue_seconds_enabled = persist_read_bool(ANALOGUE_SECONDS_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read analogue seconds setting from store: %u", analogue_seconds_enabled);
	}
	if (persist_exists(DISPLAY_MODE_KEY))
	{
		display_mode = valid_display_mode(persist_read_int(DISPLAY_MODE_KEY));
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read display mode from store: %u", display_mode);
	}

	window = window_create();
	window_set_background_color(window, colour_from_rgb(background_color));
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload
	});

	const bool animated = true;
	window_stack_push(window, animated);

	// Initialize message queue after window load so startup settings cannot race layer creation.
	const int inbound_size = 256;
	const int outbound_size = 64;
	app_message_register_inbox_received(inbox_received_callback);
	app_message_register_inbox_dropped(inbox_dropped_callback);
	app_message_open(inbound_size, outbound_size);
  
	// Subscribe to ticks and status services for the HUD face.
	tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
	battery_state_service_subscribe(handle_battery_state);
	bluetooth_connection_service_subscribe(handle_bluetooth_connection);

#if DEBUG
	// Button functionality
	window_set_click_config_provider(window, (ClickConfigProvider) click_config_provider);
#endif
}

static void handle_deinit()
{
	tick_timer_service_unsubscribe();
	battery_state_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();
	app_message_deregister_callbacks();

	// Free window
	window_destroy(window);
}

int main(void)
{
	handle_init();
	app_event_loop();
	handle_deinit();
}
