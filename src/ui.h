// ui.h - User Interface library
#ifndef UI_H
#define UI_H

#include "io.h"

// UI Colors
#define UI_COLOR_TITLE       COLOR_CYAN
#define UI_COLOR_PROMPT      COLOR_LIGHT_GREEN
#define UI_COLOR_TEXT        COLOR_LIGHT_GREY
#define UI_COLOR_SUCCESS     COLOR_GREEN
#define UI_COLOR_ERROR       COLOR_LIGHT_RED
#define UI_COLOR_FILE        COLOR_YELLOW
#define UI_COLOR_DIR         COLOR_LIGHT_BLUE
#define UI_COLOR_HIGHLIGHT   COLOR_WHITE

// UI Functions
void ui_print_banner(void);
void ui_print_divider(char ch);
void ui_print_header(const char *text);
void ui_print_footer(void);
void ui_print_info(const char *text, ...);
void ui_print_success(const char *text, ...);
void ui_print_error(const char *text, ...);
void ui_print_warning(const char *text, ...);
void ui_progress_bar(int percent, int width);
void ui_clear_line(void);

#endif