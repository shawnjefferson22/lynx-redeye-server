#ifndef DISPLAY_H
#define DISPLAY_H

#define VERSION_STR 		"1.1"

#define STATS_PRINT_DUR		10
extern time_t start;

// screen display
extern void display_init();
extern void ui_refresh();
extern void ui_log(const char *fmt, ...);
extern void clear_log();
extern void draw_title();
extern void draw_log();
extern void draw_game_stats();
extern void draw_packet_stats();
extern void draw_games();

// logging functions
void util_dump_bytes(const uint8_t *buff, uint32_t buff_size);
void print_game_packet(const uint8_t *buff, uint32_t buff_size);
void print_logon_packet(const uint8_t *buff, uint32_t buff_size);
void print_stats();

#endif