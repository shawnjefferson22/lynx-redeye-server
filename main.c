#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "game.h"


#define VERSION_STR 	"1.0"
#define STATS_PRINT_DUR		10


void util_dump_bytes(const uint8_t *buff, uint32_t buff_size);
uint8_t calc_checksum(uint8_t *buf);

int sockfd;				// Socket File Descriptor
STATS_T stats;


/* print bytes to screen in hex format
 */
void util_dump_bytes(const uint8_t *buff, uint32_t buff_size)
{
    int bytes_per_line = 16;
    for (int j=0; j < buff_size; j += bytes_per_line)
    {
        for (int k = 0; (k + j) < buff_size && k < bytes_per_line; k++)
            printf("%02X ", buff[k + j]);
    }
}


/* Calculate checksum of redeye packet, return true if good, false if not
 *
 * Checksum calculation is 255 - size, message
 */
uint8_t calc_checksum(uint8_t *buf)
{
	uint16_t ck;
	uint8_t i, sz;


	sz = buf[0];
	ck = 255;
	for(i=0; i<sz+1; i++)
	  ck -= buf[i];
    ck = (ck & 0xFF);			// only one byte

	if ((uint8_t) ck == buf[sz+1])
	  return 1;
	else {
	 //printf("packet failed checksum! %02X %d %02X PKT: ", (ck & 0xFF), sz, buf[sz+1]);
	 //util_dump_bytes(buf, sz+2);
	 //printf("\n");

	 return 0;
    }
}


//void print_stats(void)
//{
//	printf("*** Global Stats - malformed: %ld, bad checksum: %ld, good checksum: %ld\n", stats.malformed, stats.bad_checksum, stats.good_checksum);
//}


int main(int argc, char *argv[])
{
    
	uint8_t buf[BUF_SIZE];				// packet buffer
	struct GAME_T *g;
    uint8_t pnum;						// player number
    uint16_t gid;						// game id
	time_t start, now, dur;
	float mal_percent;
	float bad_percent;
	bool stats_printed = true;


    // Handle arguments
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <port> [-v/-vv]\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

	printf("Fujinet Lynx Redeye Server, v%s\n\n", VERSION_STR);

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        return 1;
    }

    // bind UDP listening port
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind failed");
        return 1;
    }
	printf("Listening for connections on port %d...\n\n", port);


	// Initialize some variables
    games = NULL;
	g = NULL;

	start = time(NULL);
	stats.malformed = stats.last_malformed = 0;
	stats.bad_checksum = stats.last_bad_checksum = 0;
	stats.good_checksum = 0;

	// Main game service loop
    while (1) {
        memset(buf, 0, BUF_SIZE);			// clear it

        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);

		// Get UDP packet
        int recvfrom_ret = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*)&cliaddr, &clilen);
        if (recvfrom_ret < 0)
        {
            perror("recvfrom failed");
            continue;
	} 
		// print stats
		now = time(NULL);
		dur = now - start;
		//printf("time %ld %ld %ld\n", start, now, dur);
		if ((dur % STATS_PRINT_DUR) == 0) {
			if (!stats_printed) {
				mal_percent = ((float) stats.malformed / (float) (stats.good_checksum+stats.malformed+stats.bad_checksum)) * 100;
				bad_percent = ((float)stats.bad_checksum / (float) (stats.good_checksum+stats.malformed+stats.bad_checksum)) * 100;
				printf("*** Global Stats - good: %ld, malformed: %ld %.2f%% ∆:%ld, bad checksum: %ld %.2f%% ∆:%ld\n", stats.good_checksum, stats.malformed, mal_percent,
						(stats.malformed - stats.last_malformed), stats.bad_checksum, bad_percent, (stats.bad_checksum - stats.last_bad_checksum));
				stats.last_malformed = stats.malformed;
			    stats.last_bad_checksum = stats.bad_checksum;
				stats_printed = true;
			}
		}
		else {
			stats_printed = false;
		}
		
		// prune timed out clients and dead games
		handle_client_timeout();

		// *** DEBUGGING ***
    #ifdef DEBUG
	    printf("Received from %s:%d ", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
        printf("PKT: ");
        util_dump_bytes(buf, recvfrom_ret);
        printf("\n");
	#endif

		// sanity check on the packet length
		if ((buf[0] > 16) || ((recvfrom_ret < 3) || (recvfrom_ret > 16))) {
			//printf("Discarding malformed packet, size %d\n", buf[0]);
			stats.malformed++;
			continue;
		}

		// Check the checksum of the packet
		if (!calc_checksum(buf)) {
		  	stats.bad_checksum++;
			continue;												// just discard this packet
		}
		else
			stats.good_checksum++;

		// Look for client in an existing game
		g = find_game_by_client_address(&cliaddr);
		if (!g) {												// no game found for this client
		   if (buf[0] == 5) {									// this is a logon packet
				gid = (uint16_t) (buf[4] + (buf[5] * 256));		// extract game id

		     	g = find_game_by_id(gid);						// find a game matching id that's in logon phase
		     	// join a game in progress if in logon mode and not at max players already
		     	if (g && (g->logon) && (g->num_players != g->max_players)) {		// found game in logon phase, with free slots for players
		       		join_game(g, &cliaddr);
		       		printf("Client %s:%d not found, joining game %04X %s\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), gid, *g->name);
	        	}
				// create a new game
				else {
		   			g = create_new_game(gid, &cliaddr);
		   			printf("Client %s:%d not found, creating game id: %04X %s, max players: %d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), gid, *g->name, g->max_players);
					continue;									// back to the beginning of loop, no other clients to send this to yet
	       		}
		   	}
			else {
				// client not found in any game, and not a logon packet, just discard it!
	      		printf("Client %s:%d not found in any game, and not a logon packet! PKT: \n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
	      		util_dump_bytes(buf, recvfrom_ret);
	 			printf("\n");
		    	continue;										// back to beginning of loop, discard the packet
			}
		}
		//printf("Client %s:%d found in game %d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), g->game_id);

		// update some client details
		pnum = find_client_in_game(g, &cliaddr);								// find the player number in this game of sender
		if (pnum == 255)														// client not found in game (something weird happened)
			continue;															// back to beginning of loop, discard this packet

		time_t t = time(NULL);
		g->client[pnum].last_heard = t;											// record last hard time
	    //memset(g->client[pnum].last_msg_recv, 0, BUF_SIZE);						// clear last msg recv
	    //memcpy(g->client[pnum].last_msg_recv, buf, recvfrom_ret);				// record last msg recv

		// Check if the logon phase is ending
		if (g->logon && (buf[0] == 5) && (buf[1] == 2)) {			// game is logon phase, packet is a logon packet (size 5), and message id is logon ending
		  	g->logon = 0;											// this game is ending for logon, block new players
			printf("Logon ending for game %04X %s, players: %d\n", g->game_id, *g->name, g->num_players);
		}

		// mirror this packet to other clients in this game
		if (g->num_players > 1)	{									// don't even bother if only one player
			//printf("Sending to other clients in game %d %s, for player %d\n", g->game_id, *g->name, pnum);
			//printf("%d ", pnum);
			send_to_other_clients(g, pnum, buf, recvfrom_ret);		// mirror this packet to other clients in game
		}
    }

    return 0;
}






