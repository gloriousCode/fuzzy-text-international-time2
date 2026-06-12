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

#define TEXT_ALIGN_CENTER 0
#define TEXT_ALIGN_LEFT 1
#define TEXT_ALIGN_RIGHT 2

// The time it takes for a layer to slide in or out.
#define ANIMATION_DURATION 400
// Delay between the layers animations, from top to bottom
#define ANIMATION_STAGGER_TIME 150
// Delay from the start of the current layer going out until the next layer slides in
#define ANIMATION_OUT_IN_DELAY 100

static AppSync sync;
static uint8_t sync_buffer[160];

static int text_align = TEXT_ALIGN_CENTER;
static int foreground_color = 0xFFFFFF;
static int background_color = 0x000000;
static FontChoice font_choice = FONT_CHOICE_CLASSIC;
static FontChoice render_font_choice = FONT_CHOICE_CLASSIC;
static Language lang = EN_US;

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

static int currentNLines;

static bool showTime = true;
static int dateTimeout = 0;

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
	if (line->animation1 != NULL)
	{
		 property_animation_destroy(line->animation1);
	}
	if (line->animation2 != NULL)
	{
		 property_animation_destroy(line->animation2);
	}

	// Configure animation for current layer to move out
	GRect rect = layer_get_frame((Layer *)current);
	rect.origin.x =  -screen_bounds.size.w;
	line->animation1 = property_animation_create_layer_frame((Layer *)current, NULL, &rect);
	Animation *animation1 = property_animation_get_animation(line->animation1);
	animation_set_duration(animation1, ANIMATION_DURATION);
	animation_set_delay(animation1, delay);
	animation_set_curve(animation1, AnimationCurveEaseIn); // Accelerate

	// Configure animation for current layer to move in
	GRect rect2 = layer_get_frame((Layer *)next);
	rect2.origin.x = (screen_bounds.size.w - rect2.size.w) / 2;
	line->animation2 = property_animation_create_layer_frame((Layer *)next, NULL, &rect2);
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
	strcpy((char*)layerText, text);
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

static GColor colour_from_rgb(int rgb)
{
#ifdef PBL_COLOR
	return GColorFromRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
#else
	return rgb == 0x000000 ? GColorBlack : GColorWhite;
#endif
}

static const char *bold_font_key_for_choice(FontChoice choice)
{
	switch (choice)
	{
		case FONT_CHOICE_SHARP:
			return FONT_KEY_GOTHIC_28_BOLD;
		case FONT_CHOICE_COMPACT:
			return FONT_KEY_GOTHIC_24_BOLD;
		case FONT_CHOICE_TALL:
			return FONT_KEY_BITHAM_42_BOLD;
		default:
			return FONT_KEY_BITHAM_42_BOLD;
	}
}

static const char *light_font_key_for_choice(FontChoice choice)
{
	switch (choice)
	{
		case FONT_CHOICE_SHARP:
			return FONT_KEY_GOTHIC_28;
		case FONT_CHOICE_COMPACT:
			return FONT_KEY_GOTHIC_24;
		case FONT_CHOICE_TALL:
			return FONT_KEY_BITHAM_42_LIGHT;
		default:
			return FONT_KEY_BITHAM_42_LIGHT;
	}
}

// Configure bold line of text
static void configureBoldLayer(TextLayer *textlayer)
{
	text_layer_set_font(textlayer, fonts_get_system_font(bold_font_key_for_choice(render_font_choice)));
	text_layer_set_text_color(textlayer, colour_from_rgb(foreground_color));
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
}

// Configure light line of text
static void configureLightLayer(TextLayer *textlayer)
{
	text_layer_set_font(textlayer, fonts_get_system_font(light_font_key_for_choice(render_font_choice)));
	text_layer_set_text_color(textlayer, colour_from_rgb(foreground_color));
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
}

static void apply_window_colours(void)
{
	window_set_background_color(window, colour_from_rgb(background_color));
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
}

// Configure the layers for the given text
static int configureLayersForText(char text[NUM_LINES][BUFFER_SIZE], char format[])
{
	int numLines = 0;
	render_font_choice = text_font_choice_that_fits(screen_bounds.size.w, screen_bounds.size.h,
		PBL_IF_ROUND_ELSE(true, false), font_choice, text);

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
  	time_to_lines(lang, t->tm_hour, t->tm_min, t->tm_sec, textLine, format);
    dateTimeout = 0;
    showTime = true;
  } else {
    date_to_lines(lang, t->tm_wday, t->tm_mday, t->tm_mon, textLine, format);
  }
  
  int nextNLines = configureLayersForText(textLine, format);

  int delay = 0;
  for (int i = 0; i < NUM_LINES; i++) {
    if (nextNLines != currentNLines || needToUpdateLine(&lines[i], textLine[i])) {
      updateLineTo(&lines[i], textLine[i], delay);
      delay += ANIMATION_STAGGER_TIME;
    }
  }

  currentNLines = nextNLines;
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

	time_to_lines(lang, t->tm_hour, t->tm_min, t->tm_sec, textLine, format);

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

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context)
{
	APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d", app_message_error);
}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
	GTextAlignment alignment;
	switch (key) {
		case TEXT_ALIGN_KEY:
			text_align = new_tuple->value->uint8;
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
			lang = (Language) new_tuple->value->uint8;
			persist_write_int(LANGUAGE_KEY, lang);
			APP_LOG(APP_LOG_LEVEL_DEBUG, "Set language: %u", lang);

			if (t)
			{
				display_time(t);
			}
			break;
		case FONT_CHOICE_KEY:
			font_choice = (FontChoice) new_tuple->value->uint8;
			if (font_choice >= FONT_CHOICE_COUNT)
			{
				font_choice = FONT_CHOICE_CLASSIC;
			}
			persist_write_int(FONT_CHOICE_KEY, font_choice);
			apply_layer_styles();
			if (t)
			{
				display_time(t);
			}
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
	}
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
	// Free layers
	text_layer_destroy(line->currentLayer);
	text_layer_destroy(line->nextLayer);
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
		layer_add_child(window_layer, (Layer *)lines[i].currentLayer);
		layer_add_child(window_layer, (Layer *)lines[i].nextLayer);
	}

	// Configure time on init
	time_t raw_time;

	time(&raw_time);
	t = localtime(&raw_time);
	display_initial_time(t);

	Tuplet initial_values[] = {
		TupletInteger(TEXT_ALIGN_KEY, (uint8_t) text_align),
		TupletInteger(INVERT_KEY,     (uint8_t) 0),
		TupletInteger(LANGUAGE_KEY,   (uint8_t) lang),
		TupletInteger(FONT_CHOICE_KEY, (uint8_t) font_choice),
		TupletInteger(FOREGROUND_COLOR_KEY, foreground_color),
		TupletInteger(BACKGROUND_COLOR_KEY, background_color)
	};

	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values, ARRAY_LENGTH(initial_values),
			sync_tuple_changed_callback, sync_error_callback, NULL);
}

static void window_unload(Window *window)
{
	app_sync_deinit(&sync);

	for (int i = 0; i < NUM_LINES; i++)
	{
		destroy_line(&lines[i]);
	}
}

static void handle_init() {
	// Load settings from persistent storage
	if (persist_exists(TEXT_ALIGN_KEY))
	{
		text_align = persist_read_int(TEXT_ALIGN_KEY);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Read text alignment from store: %u", text_align);
	}
	if (persist_exists(LANGUAGE_KEY))
	{
		lang = (Language) persist_read_int(LANGUAGE_KEY);
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
	const int inbound_size = 64;
	const int outbound_size = 64;
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
