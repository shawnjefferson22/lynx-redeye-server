#ifndef GAME_H
#define GAME_H

#include <sys/socket.h>


#define BUF_SIZE	      	  32				// packet buffer size
#define MAX_PKT_SIZE		    16				// max packet size we're handling (may need to go higher?)
#define CLIENT_TIMEOUT  	  30*1000    // client timeout client interval (seconds)
#define NUM_GAMES 			    42			  // number of games in the game list
#define MAX_PLAYERS			    16				// maximum players allowed in game
#define LOGON_SUPPRESS      10         // number of logon messages to suppress
#define REQ_BACKOFF_TIME    90        // time to suppress repeated data requests (msg 4)
#define LOGON_BACKOFF_TIME  500       // time to suppress repeated logon packets if my player number changed
#define LOGON_DELAY         150       // logon countdown timer (for real mode)
#define LOGON_RESTART_BACKOFF 2000    // in game logon restart backoff timer (2 seconds)


typedef struct STATS_T
{
    uint64_t malformed;
    uint64_t bad_checksum;
    uint64_t good_checksum;
    uint64_t last_malformed;
    uint64_t last_bad_checksum;
} STATS_T;

typedef struct CLIENT_T
{
    struct sockaddr_in client_addr;
    int64_t last_heard;
    uint8_t player_num;
    int64_t player_num_change_timer;
    bool player_num_changed;
} CLIENT_T;

typedef struct GAME_STATE_T
{
	bool logon;                                         	// in logon phase?
	int64_t logon_timer;                               	  // end logon state countdown timer
	int8_t plr_data_recv[2][MAX_PLAYERS];              		// plr_data_recv for seq/player
	uint8_t seq_plr_data[2][MAX_PLAYERS][BUF_SIZE];     	// seq/player data cache
	uint64_t last_req_time[2][MAX_PLAYERS][MAX_PLAYERS];	// seq/player request from each player, for each player
} GAME_STATE_T;

typedef struct GAME_T
{
  uint32_t instance;			      // game instance (to differentiate duplicate games)
  uint16_t game_id;             // game id
  char **name;					        // pointer to game name in games list
  uint8_t max_players;			    // max players for this game
  uint8_t num_players;          // number of players
  CLIENT_T client[MAX_PLAYERS]; // client list, up to 16
  GAME_STATE_T state;			      // game state
  uint64_t game_start;          // time of game start
  uint64_t rounds;              // number of rounds (sequences)
  uint64_t round_start;			    // round start time (sequence)
  uint64_t last_round_time;		  // last round time (ms)
  uint64_t avg_round_time;      // average round time (ms)
  struct GAME_T *next;         	// pointer to next game
} GAME_T;

typedef struct GAME_LIST_T
{
  uint16_t game_id;				      // game id
  uint8_t max_players;			    // maximum players for ths game
  char *name;					          // pointer to the game name
} GAME_LIST_T;

extern struct GAME_T *games;              // games being played
extern struct GAME_LIST_T game_list[];  	// game list for name, max players
extern struct STATS_T stats;  	          // global packet statistics

// helpers
int popcount(uint8_t bits);
uint64_t get_time_ms();

// game list searching
GAME_T *find_game_by_client_address(struct sockaddr_in* addr);
GAME_T *find_game_by_id(uint16_t id);
uint8_t find_client_in_game(GAME_T *game, struct sockaddr_in* addr);
uint8_t find_game_in_game_list(uint16_t gid);
uint8_t find_client_by_player_num(GAME_T* game, uint8_t player_num);

// join/create/remove/status of games
GAME_T *create_new_game(uint16_t game_id, struct sockaddr_in* addr);
void join_game(GAME_T *game, struct sockaddr_in* addr);
void handle_client_timeout();

// game state
void reset_game_state(GAME_T *game);
void reset_seq_state(GAME_T *game);
bool check_logon_state(struct GAME_T *game);
bool check_req_sent(struct GAME_T *game, uint8_t seq, uint8_t from_plr, uint8_t for_plr);
bool check_logon_sent(struct GAME_T *game, uint8_t plr);

// process game packets
void process_logon_packet(struct GAME_T *game, uint8_t client_num, uint8_t *buf, uint32_t buff_size);
void process_game_packet(struct GAME_T *game, uint8_t client_num, const uint8_t *buf, uint32_t buff_size);
bool is_duplicate_data(const uint8_t *buf, uint8_t *cache);
uint8_t calc_checksum(uint8_t *buf);
void recalculate_checksum(uint8_t *buf);

// mirror data to other players
bool send_to_other_clients(struct GAME_T *game, uint8_t sender, const uint8_t *packet, uint8_t psize);
bool send_data_to_client(struct GAME_T *game, uint8_t req_player, const uint8_t *packet, uint8_t psize);
bool valid_sequence_data(GAME_T *game, uint8_t seq, uint8_t player_mask);
void send_countdown_packets(GAME_T *game, uint8_t pnum);

// defined in main.c
extern int sockfd;				// Socket File Descriptor
extern bool monitor_mode;
extern bool verbose_log;
extern bool packet_log;
extern FILE *fp;
GAME_T *client_lookup(struct sockaddr_in *cliaddr, uint8_t *buf, uint8_t *pnum);


#endif
