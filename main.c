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
#include "game.h"
#include "display.h"


#define VERSION_STR 		"1.1"

int sockfd;				// Socket File Descriptor
bool monitor_mode = false;
FILE *fp;

uint8_t calc_checksum(uint8_t *buf);


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
		#ifdef DEBUG
	 	ui_log("DEBUG - packet failed checksum! %02X %d %02X", (ck & 0xFF), sz, buf[sz+1]);
	 	util_dump_bytes(buf, sz+2);
	 	#endif

	 return 0;
    }
}

/*
 * Ugly hack for case if UDP stream is concatenating packets
*/
bool check_buffer_for_more(uint8_t *buf, uint8_t bufsize)
{
	if (bufsize > buf[0]+2) {
		#ifdef DEBUG
		ui_log("DEBUG - found more buf[0]:%d buf[0]+2:%d\n", buf[0], buf[(buf[0]+2)]);
		#endif
		
		memmove(&buf[0], &buf[(buf[0]+2)], buf[buf[0]+1]);
		
		#ifdef DEBUG
		util_dump_bytes(buf, buf[0]+2);
		#endif

		if (buf[0] == 0)			// fix case of no more real data
			return(false);
		else
			return(true);
	}

	return(false);
}


int main(int argc, char *argv[])
{
	uint8_t buf[BUF_SIZE];				// packet buffer
	struct GAME_T *g;
    uint8_t pnum;						// player number
    uint16_t gid;						// game id


	//printf("Fujinet Lynx Redeye Server, v%s\n\n", VERSION_STR);

    // Handle arguments
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <port> [-monitor]\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    // monitor mode?
    if (argc == 3) {
		if (strcmp(argv[2], "-monitor")	== 0) {
			monitor_mode = true;
		}
	}

	// log file for monitor mode
	if (monitor_mode)
		fp = fopen("reserver-monitor.log", "w");
		
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

	// print startup status
	ui_log("Listening for connections on port %d.\n", port);
	if (monitor_mode)
		ui_log("Running in monitor mode.\n");

	display_init();
	ui_refresh();

	// Initialize some variables
    games = NULL;
	g = NULL;

	start = time(NULL);
	stats.malformed = stats.last_malformed = 0;
	stats.bad_checksum = stats.last_bad_checksum = 0;
	stats.good_checksum = 0;

	/* Main game service loop */
    while (1) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(sockfd, &readfds);

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 100000;

		int ret = select(sockfd+1, &readfds, NULL, NULL, &tv);
		if (ret < 0) {
			if (errno == EINTR)
				continue;

			perror("select failed");
			break;
		}

		if (FD_ISSET(sockfd, &readfds)) {
        	memset(buf, 0, BUF_SIZE);			// clear the buffer

        	struct sockaddr_in cliaddr;
        	socklen_t clilen = sizeof(cliaddr);

			int recvfrom_ret = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*)&cliaddr, &clilen);
			if (recvfrom_ret < 0) {
				perror("recvfrom failed");
				continue;
			}

			// *** DEBUGGING ***
    		#ifdef DEBUG
	    	ui_log("DEBUG Received packet from %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
        	util_dump_bytes(buf, recvfrom_ret);
			#endif
			
			// sanity check on the packet length (more than 16 bytes? or less than 3 bytes?)
			if ((buf[0] > MAX_PKT_SIZE) || ((recvfrom_ret < 3) || (recvfrom_ret > MAX_PKT_SIZE))) {
				#ifdef DEBUG
				ui_log("DEBUG Discarding malformed packet, size %d\n", buf[0]);
				#endif
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

			/***************/
			/* Game Lookup */
			/***************/
			// Look for client in an existing game
			g = find_game_by_client_address(&cliaddr);
			if (!g) {												// no game found for this client
		   		if (buf[0] == 5) {									// this is a logon packet
					gid = (uint16_t) (buf[4] + (buf[5] * 256));			// extract game id

		     		g = find_game_by_id(gid);						// find a game matching id that's in logon phase
		     		// join a game in progress if in logon mode and not at max players already
		     		if (g && (g->logon) && (g->num_players != g->max_players)) {		// found game in logon phase, with free slots for players
		       			join_game(g, &cliaddr);
		       			ui_log("Client %s:%d not found, joining game %04X %s\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), gid, *g->name);
	        		}
				// create a new game
				else {
		   			g = create_new_game(gid, &cliaddr);
		   			ui_log("Client %s:%d not found, creating game id: %04X %s, max players: %d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), gid, *g->name, g->max_players);
					continue;									// back to the beginning of loop, no other clients to send this to yet
	       		}
		   }
		   		else {
					// client not found in any game, and not a logon packet, just discard it!
	      			ui_log("Client %s:%d not found in any game, and not a logon packet!\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
	      			#ifdef DEBUG
	      			util_dump_bytes(buf, recvfrom_ret);
	      			#endif
		    		continue;										// back to beginning of loop, discard the packet
				}
			}
			#ifdef DEBUG
			ui_log("DEBUG Client %s:%d found in game %d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), g->game_id);
			#endif

			// update some client details
			if (!monitor_mode) {
			pnum = find_client_in_game(g, &cliaddr);	// find the player number in this game of sender
			if (pnum == 255)							// client not found in game (something weird happened)
				continue;								// back to beginning of loop, discard this packet

			time_t t = time(NULL);
			g->client[pnum].last_heard = t;				// record last hard time
		}
			else {
			time_t t = time(NULL);
			pnum = 0;
			g->client[pnum].last_heard = t;				// record last hard time			
		}

			/*****************/
			/* In Game State */
			/*****************/
			if (g->logon == 0) {
				#ifdef DEBUG
				print_game_packet(buf, buf[0]+2);
				#endif
				
				while(1) {
					process_game_packet(g, pnum, buf, buf[0]);
					if (!check_buffer_for_more(buf, recvfrom_ret))
						break;
					else
						recvfrom_ret -= buf[0]+2;
				}
			}
			else {
				/******************/
				/* In Logon State */
				/******************/
				#ifdef DEBUG
				print_game_packet(buf, buf[0]+2);
				#endif

				process_logon_packet(g, pnum, buf, buf[0]);

				/*******************/
				/* Send to Players */
				/*******************/
				if ((g->num_players > 1) && !monitor_mode)	{				// don't even bother if only one player, or in monitor mode
					send_to_other_clients(g, pnum, buf, recvfrom_ret);		// mirror this packet to other clients in game
			}
		}
		}

       	/*********************/
	   	/* Stats and Cleanup */
	   	/*********************/
	   	//handle_stats_print();				// print packet stats
	   	ui_refresh();
		if (!monitor_mode)
			handle_client_timeout();			// prune timed out clients and dead games
    
		
	}

	fclose(fp);
    return 0;
}






