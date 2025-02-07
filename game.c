#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "game.h"


/* Game List */
GAME_LIST_T game_list[] = {
	{0x0000, 2, "Bill and Ted's Excellent Adventure"},				// standard redeye games
	{0x0001, 4, "Gauntlet: The Third Encounter"},
	{0x0002, 4, "Zalor Mercenary"},
	{0x0004, 4, "Xenophobe"},
	{0x0005, 8, "Todd's Adventure in Slime World"},
	{0x0006, 2, "Robosquash"},
	{0x0007, 4, "Warbirds"},
	{0x001E, 2, "Turbo Sub"},
	{0x0020, 2, "Basketbrawl"},
	{0x0028, 2, "World Class Soccer"},
	{0x0030, 2, "Hockey"},
	{0x0053, 2, "Shanghai"},
	{0x00C8, 6, "Checkered Flag"},
	{0x00D2, 2, "Rampart"},
	{0x00FF, 2, "Xybots"},
	{0x029A, 2, "Joust"},
	{0x0EFE, 2, "Road Riot 4WD"},
	{0x1313, 2, "Supersqweek"},
	{0x1355, 4, "Rampage"},
	{0x2050, 2, "Baseball Heroes"},
	{0x7000, 6, "Battle Wheels"},
	{0xB0B0, 2, "NFL Football"},
	{0xBABE, 2, "Raiden"},
	{0xDAD0, 4, "Tournament Cyberball"},
	{0xE001, 2, "Double Dragon"},									// remapped from 0xFFFF games
	{0xE002, 2, "European Soccer"},
	{0xE003, 2, "Lynx Casino"},
	{0xE004, 2, "Pit Fighter"},
	{0xE005, 2, "Relief Pitcher"},
	{0xE006, 4, "Super Off-Road"},
	{0xE101, 4, "Awesome Golf"},									// games that don't use redeye (shouldn't ever see these)
	{0xE102, 4, "Battlezone 2000"},									// including for completeness as these IDs are used in the
	{0xE103, 4, "California Games"},								// Fujinet firmware
	{0xE104, 6, "Championship Rally"},
	{0xE105, 2, "Fidelity Ulimate Chess Challenge"},
	{0xE106, 4, "Hyperdrome"},
	{0xE107, 4, "Jimmy Connor's Tennis"},
	{0xE108, 2, "Loopz"},
	{0xE109, 2, "Lynx Othello"},
	{0xE10A, 4, "Malibu Bikini Volleyball"},
	{0xE10B, 2, "Ponx"},
	{0xFFFF, 0, "Not Used"}
};

struct GAME_T *games;                   // games being played list



/* find_game_in_game_list
 *
 * Searches the game list for the game id and returns the index into
 * the list, so that we can lookup name and max players.  Returns 255
 * if the game was not found.
 */
uint8_t find_game_in_game_list(uint16_t gid)
{
  uint8_t i;


  i = 0;
  while(1) {
    if (game_list[i].game_id == gid)				// game found!
      return(i);

	if (game_list[i].game_id == 0xFFFF)			// game not found
	  return(255);

	i++;
  }
}



/* find_game_by_client_addresss
 *
 * Return pointer to game that contains the client address
 * otherwise return NULL. We assumed that a client can only
 * be in one game (which means we need to clean old games).
 */
GAME_T *find_game_by_client_address(const struct sockaddr_in* addr)
{
  uint8_t i;
  GAME_T *g;


  // Point to the head of our game list
  g = games;
  if (!g)
    return(NULL);

  while (g) {
	    // search all players in game
      for (i=0; i < g->num_players; i++) {
	      if (addr->sin_family == g->client[i].client_addr.sin_family &&
            addr->sin_port == g->client[i].client_addr.sin_port &&
            addr->sin_addr.s_addr == g->client[i].client_addr.sin_addr.s_addr) {
		        //printf("find_game_by_client, client found in game %d\n", g->game_id);
            return(g);
  	    }
	    }

	    g = g->next;				// try next game
  }

  // not found, probably need to start or join
  return(NULL);
}


/* find_game_by_game_id
 *
 * Return a pointer to the game for the game_id listed
 * otherwise return NULL.  We only return games that are in
 * the logon phase.
 */
GAME_T *find_game_by_id(uint16_t id)
{
  GAME_T *g;


  // Point to the head of our game list
  g = games;
  if (!g)
    return(NULL);

  while (g) {
	// match the game id and check that it's in logon phase
    if ((g->game_id == id) && (g->logon))
      return(g);

    // try next game
    g = g->next;
  }

  // not found
  return(NULL);
}


/* find_client_in_game
 *
 * Finds a client by address within a specific game.  Returns 255 if not found.
 */
uint8_t find_client_in_game(GAME_T *game, struct sockaddr_in* addr)
{
  uint8_t i;


  // Check valid game
  if (!game)
    return(255);

  // search all players in game
  for (i=0; i < game->num_players; i++) {
	if (addr->sin_family == game->client[i].client_addr.sin_family &&
        addr->sin_port == game->client[i].client_addr.sin_port &&
        addr->sin_addr.s_addr == game->client[i].client_addr.sin_addr.s_addr) {
      return(i);
  	}
  }

  // not found in this game
  return(255);
}


/* create_new_game
 *
 * Creates a new game and adds client to game (as client 0).
 */
GAME_T *create_new_game(uint16_t game_id, struct sockaddr_in* addr)
{
  struct GAME_T *newgame;
  struct GAME_T *g;
  uint8_t i;


  // Allocate a new game
  newgame = malloc(sizeof(GAME_T));                     // allocate new game node
  if (!newgame)
    return(NULL);

  // Find the last game in the game list
  g = games;
  if (!g) {
    games = newgame;			// this is new head of list
  }
  else {
    while (g->next)
      g = g->next;
    g->next = newgame;			// add game to end of game list
  }
  newgame->next = NULL;

  // setup new game
  newgame->game_id = game_id;			// just a dummy game ID, that I hope isn't used
  newgame->logon = 1;					// logon phase starts every game
  newgame->num_players = 1;				// we have one player, the player who starts the game

  i = find_game_in_game_list(game_id); 					// find name and max_players for this game
  if (i != 255) {
	  newgame->max_players = game_list[i].max_players;
	  newgame->name = &game_list[i].name;
  }

  newgame->client[0].last_heard = time(NULL);
  memset(newgame->client[0].last_msg_sent, 0, BUF_SIZE);
  memset(newgame->client[0].last_msg_recv, 0, BUF_SIZE);
  memcpy((void *) &(newgame->client[0].client_addr), (void *) addr, sizeof(struct sockaddr_in));

  /* DEBUGGING */
  //printf("new_game:%d %d %d\n", newgame->game_id, newgame->logon, newgame->num_players);
  //printf("client[0] %f %s:%d\n", newgame->client[0].last_heard, inet_ntoa(newgame->client[0].client_addr.sin_addr), ntohs(newgame->client[0].client_addr.sin_port));

  return(newgame);
}


/* join_game
 *
 * Adds player to a current game, it's expected that the game_id is correct
 * and that the game is in logon mode still.
 */
void join_game(GAME_T *game, struct sockaddr_in* addr)
{

  // Check valid game
  if (!game)
    return;

  // Don't allow a client to join a game in progress, must be in logon phase
  if (game->logon) {
    memcpy((void *) &(game->client[game->num_players].client_addr), (void *) addr, sizeof(struct sockaddr_in));
	  game->client[game->num_players].last_heard = time(NULL);
    memset(game->client[0].last_msg_sent, 0, BUF_SIZE);
    memset(game->client[0].last_msg_recv, 0, BUF_SIZE);

    game->num_players++;
  }
}


/* send_to_other_clients
 *
 * Iterate through the game list, sending this packet to the other clients in the game.
 */
uint8_t send_to_other_clients(struct GAME_T *game, uint8_t sender, uint8_t *packet, uint8_t psize)
{
  uint8_t i;
  uint8_t sendto_ret;
  socklen_t clilen;


  // Check valid game
  if (!game)
    return(0);

  // iterate through client list
  for(i=0; i<game->num_players; i++) {
    if (i != sender) {	 					// don't send to the original sender
      clilen = sizeof(game->client[i].client_addr);
	    sendto_ret = sendto(sockfd, packet, psize, 0, (struct sockaddr*) &(game->client[i].client_addr), clilen);

	    // *** DEBUGGING ***
      //printf("Sending to %s:%d ", inet_ntoa(game->client[i].client_addr.sin_addr), ntohs(game->client[i].client_addr.sin_port));
      //printf("PKT: ");
      //util_dump_bytes(packet, psize);
      //printf("\n");

      // update the last message sent to this client
	    memset(game->client[i].last_msg_sent, 0, BUF_SIZE);
	    memcpy(game->client[i].last_msg_sent, packet, psize);

	    if (sendto_ret < 0) {
        perror("sendto failed");
      }
    }
  }

  return(1);
}


/* handle_client_timeout
 *
 * Walks through each game, and increments last_hard time for
 * each client.  Also, check if the last_heard time is longer
 * then the client timeout value, and if so, remove that client.
 * Note that when a client is removed the game will probably freeze,
 * but if the client hasn't communicated in a while, the game is
 * probably already frozen or abadoned.
 *
 */
void handle_client_timeout() {
  GAME_T *g;
  GAME_T *lg;       // pointer to previous game
  uint8_t i,j;
  time_t t;       // time interval to now


  t = time(NULL);

  // Walk the games list, determine which clients to remove and prune empty games
  g = lg = games;
  while (g) {
    for(i=0; i<g->num_players; i++) {
      if ((t - g->client[i].last_heard) > CLIENT_TIMEOUT) {
        printf("handle_client_timeout, game:%04X client:%d lh:%ld\n", g->game_id, i, (t - g->client[i].last_heard));
        for (j=i; j<g->num_players; j++) {
          //printf("handle_client_timeout, pruning game:%d client:%d\n", g->game_id, j);
          memcpy(&g->client[j], &g->client[j+1], sizeof(CLIENT_T));
        }

        i--;
        g->num_players--;
      }
    }
       
    if (g->num_players == 0) {
        printf("handle_client_timeout, pruning game with id %04X\n", g->game_id);
        if (g == games) {               // is this the head of the list?
          games = g->next;              // point head to next node (which may be NULL)
          free(g);                      // free node
          g = lg = games;               // re-init pointers to new head
          continue;                     // head back up to the top of the while loop
        }
        else {
          lg->next = g->next;           // remove node from the linked list
          free(g);                      // free the current game list node
          g = lg;                       // point g to lg, so while loop works
        }
    }

    lg = g;           // save pointer to previous game
    g = g->next;      // go to next game in list
  }

  return;
}

