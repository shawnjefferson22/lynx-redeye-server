#ifndef GAME_H
#define GAME_H


#define BUF_SIZE	      	32				// packet buffer size
#define CLIENT_TIMEOUT  	5         // timeout client interval
#define NUM_GAMES 			  41			  // number of games in the game list


typedef struct CLIENT_T
{
    struct sockaddr_in client_addr;
    time_t last_heard;
    uint8_t last_msg_recv[BUF_SIZE];
    uint8_t last_msg_sent[BUF_SIZE];
} CLIENT_T;

typedef struct STATS_T
{
    uint64_t malformed;
    uint64_t bad_checksum;
    uint64_t good_checksum;
    uint64_t last_malformed;
    uint64_t last_bad_checksum;
} STATS_T;

typedef struct GAME_T
{
  uint16_t game_id;             // game id
  uint8_t logon;                // in logon phase?
  uint8_t num_players;          // number of players
  uint8_t max_players;			    // max players for this game
  char **name;					        // pointer to game name in games list
  CLIENT_T client[16];          // client list, up to 16
  struct GAME_T *next;         	// point to next game
} GAME_T;

typedef struct GAME_LIST_T
{
  uint16_t game_id;				      // game id
  uint8_t max_players;			    // maximum players for ths game
  char *name;					          // the game name
} GAME_LIST_T;


extern struct GAME_T *games;                  // games being played list
extern struct GAME_LIST_T game_list[];  		  // game list
extern struct STATS_T stats;                  // global packet statistics

GAME_T *find_game_by_client_address(const struct sockaddr_in* addr);
GAME_T *find_game_by_id(uint16_t id);
uint8_t find_client_in_game(GAME_T *game, struct sockaddr_in* addr);
GAME_T *create_new_game(uint16_t game_id, struct sockaddr_in* addr);
void join_game(GAME_T *game, struct sockaddr_in* addr);
uint8_t send_to_other_clients(struct GAME_T *game, uint8_t sender, uint8_t *packet, uint8_t psize);
void handle_client_timeout();
uint8_t find_game_in_game_list(uint16_t gid);


// defined in main.c
extern int sockfd;				// Socket File Descriptor
extern void util_dump_bytes(const uint8_t *buff, uint32_t buff_size);


#endif
