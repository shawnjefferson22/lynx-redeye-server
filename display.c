#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ncurses.h>
#include <locale.h>
#include "game.h"
#include "display.h"

WINDOW *win_games;
WINDOW *win_stats;
WINDOW *win_packets;
WINDOW *win_log;

#define TITLE_H		1
#define STATS_H		6
#define GAMES_H		10
#define LOG_LINES 	200

int log_h;
int half;

char log_buffer[LOG_LINES][128];
int log_index = 0;

STATS_T stats;
time_t start;
bool stats_printed = true;


void display_init()
{
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	setlocale(LC_ALL, "");

	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	log_h = rows - (TITLE_H + STATS_H + GAMES_H);
	half = cols / 2;

	// create windows
	win_stats = newwin(STATS_H, half, TITLE_H, 0);
	win_packets = newwin(STATS_H, cols - half, TITLE_H, half);
	win_games = newwin(GAMES_H, cols, TITLE_H + STATS_H, 0);
	win_log = newwin(log_h, cols, TITLE_H + STATS_H + GAMES_H, 0);

	// draw borders
	box(win_stats, 0, 0);
	box(win_packets, 0, 0);
	box(win_games, 0, 0);
	box(win_log, 0, 0);
}


void ui_refresh()
{
	static struct timespec last_ui = {0};

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	long ms = (now.tv_sec - last_ui.tv_sec) * 1000 + (now.tv_nsec - last_ui.tv_nsec) / 1000000;

	if (ms > 5) {
		draw_title();
		draw_game_stats();
		draw_packet_stats();
		draw_games();
		draw_log();

     	wrefresh(win_stats);
    	wrefresh(win_packets);
    	wrefresh(win_games);
    	wrefresh(win_log);

       	last_ui = now;
	}
}


void ui_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    vsnprintf(log_buffer[log_index], 128, fmt, args);
   	fputs(log_buffer[log_index], fp);

    log_index = (log_index + 1) % LOG_LINES;

    va_end(args);
}


void clear_log()
{
	memset(log_buffer, 0, sizeof(log_buffer));
	log_index = 0;
}


void draw_title()
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    const char *title = "Lynx RedEye Server v1.1";

    int x = (cols - strlen(title)) / 2;

    attron(A_REVERSE);
    mvhline(0, 0, ' ', cols);
    mvprintw(0, x, "%s", title);
    attroff(A_REVERSE);

    refresh();
}

void draw_log()
{
    werase(win_log);

    int row = 1;
    int idx = log_index;

    for(int i=0; i<log_h-2; i++)
    {
        idx--;
        if (idx < 0) idx = LOG_LINES - 1;

        mvwprintw(win_log, row++, 1, "%s", log_buffer[idx]);
    }

    box(win_log, 0, 0);
    mvwprintw(win_log, 0, 2, "LOG");
}


void draw_game_stats()
{
    int game_count = 0;
    int client_count = 0;
    int logon_count = 0;
    int running_count = 0;

    GAME_T *g = games;
    while(g)
    {
        game_count++;
        client_count += g->num_players;
        if (g->logon)
        	logon_count++;
        else
        	running_count++;
        g = g->next;
    }

    werase(win_stats);
    box(win_stats, 0, 0);
    mvwprintw(win_stats, 0, 2, "GAME STATS");

    mvwprintw(win_stats, 1, 2, "Total Games:   %d", game_count);
    mvwprintw(win_stats, 2, 2, "Logon state:   %d", logon_count);
    mvwprintw(win_stats, 3, 2, "Game state:    %d", running_count);
    mvwprintw(win_stats, 4, 2, "Total Clients: %d", client_count);
}


void draw_packet_stats()
{
    werase(win_packets);
    box(win_packets, 0, 0);
    mvwprintw(win_packets, 0, 2, "PACKET STATS");

	// cacluate percentages
    float mal_percent, bad_percent = 0;

	if (stats.malformed)
		mal_percent = ((float) stats.malformed / (float) (stats.good_checksum+stats.malformed+stats.bad_checksum)) * 100;
	if (stats.bad_checksum)
		bad_percent = ((float)stats.bad_checksum / (float) (stats.good_checksum+stats.malformed+stats.bad_checksum)) * 100;

    mvwprintw(win_packets, 1, 2, "Good Packets: %lu", stats.good_checksum);
    mvwprintw(win_packets, 2, 2, "Bad Checksum: %lu %.2f%% delta:%ld", stats.bad_checksum, bad_percent, (stats.bad_checksum - stats.last_bad_checksum));
    mvwprintw(win_packets, 3, 2, "Malformed:    %lu %.2f%% delta:%ld", stats.malformed, mal_percent, (stats.malformed - stats.last_malformed));

    stats.last_malformed = stats.malformed;
	stats.last_bad_checksum = stats.bad_checksum;
}


void draw_games()
{
    werase(win_games);
    box(win_games, 0, 0);
    mvwprintw(win_games, 0, 2, "RUNNING GAMES");

    wattron(win_games, A_BOLD);
							  //          1	        2         3         4         5         6         7
							  //01234567890123456789012345678901234567890123456789012345678901234567890123456789
							  //9999 NAME NAME NAME NAME NAME NAME NAME NAMEN PLRS LOGON SEQ 12345678
    mvwprintw(win_games, 1, 2, "ID  NAME                                     PLRS STATE PLR-DAT0 PLR-DAT1 SEQTIME ROUNDS AVGSEQTIME");
    wattroff(win_games, A_BOLD);

    // testing
    //mvwprintw(win_games, 2, 2, "0001 Gauntlet: The Third Encounter             2   LOGON  0  00000000 500 ms");

    int row = 2;
    GAME_T *g = games;

    while(g && row < GAMES_H - 1)
    {
        mvwprintw(win_games, row++, 1, "%04X %-40s  %-3d %-5s %1d%1d%1d%1d%1d%1d%1d%1d %1d%1d%1d%1d%1d%1d%1d%1d %4lu ms  %-6lu %4lu ms", g->game_id, *g->name,
        		  g->num_players, (g->logon ? "LOGON" : "GAME"),
        		  g->state.plr_data_recv[0][7], g->state.plr_data_recv[0][6], g->state.plr_data_recv[0][5], g->state.plr_data_recv[0][4],
        		  g->state.plr_data_recv[0][3], g->state.plr_data_recv[0][2], g->state.plr_data_recv[0][1], g->state.plr_data_recv[0][0],
        		  g->state.plr_data_recv[1][7], g->state.plr_data_recv[1][6], g->state.plr_data_recv[1][5], g->state.plr_data_recv[1][4],
        		  g->state.plr_data_recv[1][3], g->state.plr_data_recv[1][2], g->state.plr_data_recv[1][1], g->state.plr_data_recv[1][0],
        		  g->last_round_time, g->rounds, g->avg_round_time);
        g = g->next;
    }
}


void util_dump_bytes(const uint8_t *buff, uint32_t buff_size)
{
    int bytes_per_line = 16;
    char line[128];

    for (int j = 0; j < buff_size; j += bytes_per_line) {
    	int offset = 0;
        offset += snprintf(line + offset, sizeof(line) - offset, "DEBUG %04X: ", j);

        for (int k = 0; (k + j) < buff_size && k < bytes_per_line; k++) {
            offset += snprintf(line + offset, sizeof(line) - offset, "%02X ", buff[j + k]);
        }

        ui_log("%s\n", line);
    }
}


void print_game_packet(const uint8_t *buff, uint32_t buff_size)
{
    char line[256];
    int offset = 0;

    offset += snprintf(line + offset, sizeof(line) - offset, "DEBUG GAME PKT: ");
    // hex dump (single line)
    for (int j = 0; j < buff_size && offset < sizeof(line); j++) {
        offset += snprintf(line + offset, sizeof(line) - offset, "%02X ", buff[j]);
    }

    // decode fields
    uint8_t seq = (buff[1] & 0x80) ? 1 : 0;
    uint8_t plr = (buff[1] & 0x78) >> 3;
    uint8_t msg = (buff[1] & 0x07);

    offset += snprintf(line + offset, sizeof(line) - offset, "- Seq=%d Plr=%d Msg=%d\n", seq, plr, msg);

    ui_log("%s", line);
}


void print_logon_packet(const uint8_t *buff, uint32_t buff_size)
{
    char line[256];
    int offset = 0;

    offset += snprintf(line + offset, sizeof(line) - offset, "DEBUG LOGON PKT: ");
    // hex dump (single line)
    for (int j = 0; j < buff_size && offset < sizeof(line); j++) {
        offset += snprintf(line + offset, sizeof(line) - offset, "%02X ", buff[j]);
    }

    // decode fields
    uint8_t msg = buff[1];
    uint8_t countdown = buff[2];
    uint8_t plrs = buff[3] - 1;
    // buff[4] + [5] contain game id, already extracted


    offset += snprintf(line + offset, sizeof(line) - offset, "- Msg=%02X Plrs=%d countdown=%d\n", msg, plrs, countdown);
    ui_log("%s", line);
}


void print_stats()
{
	//time_t now, dur;
	float mal_percent;
	float bad_percent;

	mal_percent = ((float) stats.malformed / (float) (stats.good_checksum+stats.malformed+stats.bad_checksum)) * 100;
	bad_percent = ((float)stats.bad_checksum / (float) (stats.good_checksum+stats.malformed+stats.bad_checksum)) * 100;

	ui_log("STATS good: %ld, malformed: %ld %.2f%% DELTA:%ld, bad checksum: %ld %.2f%% DELTA:%ld\n", stats.good_checksum, stats.malformed, mal_percent,
		(stats.malformed - stats.last_malformed), stats.bad_checksum, bad_percent, (stats.bad_checksum - stats.last_bad_checksum));
}