#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/ip.h>
#include "socket.h"
#include <linux/if.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netdb.h>
#include <net/route.h>
#include <netinet/in.h>
#include <signal.h>

struct src_dst_ip *ip = NULL;

struct session* path_head;
struct session* resv_head;
db_node *path_tree;
db_node *resv_tree;

int sock = 0;

volatile sig_atomic_t sigint_received = 0;

void handle_sigint(int sig) {
    printf("\nCaught SIGINT (Ctrl+C) during execution!\n");
    sigint_received = 1;
}

int main() {

    char srcip[16];
    char dstip[16];
    char nhip[16];
    uint16_t tunnel_id;
    int explicit = 0;
    char buffer[512];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    //struct sockaddr_in dest_addr;
    struct sockaddr_in addr;

    char sender_ip[16];
    char receiver_ip[16];
    struct in_addr send_ip, rece_ip;

    sock = socket(AF_INET, SOCK_RAW, RSVP_PROTOCOL);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("binding failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // only in PE1 or PE2 where we configure the tunnel for RSVP.
    // ------------------------------------------------------

    for (int i  = 0; i < 2; i++){
        printf("Enter tunnel_id: \n");
        scanf("%hd",&tunnel_id);
        getchar();

        printf("Enter src ip : \n");
        fgets(srcip, 16, stdin);

        printf("Enter dst ip: \n");
        fgets(dstip, 16, stdin);

        //printf("Is Explicit enable 1-yes 0-NO\n");
        //scanf("%d ", &explicit);

        int len = strlen(srcip);
        if(srcip[len-1] == '\n') 
            srcip[len-1] = '\0';

        len = strlen(dstip);
        if(dstip[len-1] == '\n')
            dstip[len-1] = '\0';

        path_msg *path = malloc(sizeof(path_msg));

        //get and assign nexthop
        if(get_nexthop(dstip, nhip)) {
            if(strcmp(nhip, " ") == 0) {
                inet_pton(AF_INET, "0.0.0.0", &path->nexthop_ip);
                printf("dont have route to the destination ip %s\n",inet_ntoa(path->dest_ip));
                continue;
            } else {
                inet_pton(AF_INET, nhip, &path->nexthop_ip);
	    }
 	} else {
		printf("no route to destination\n");
		continue;
	}	

        //path_msg path;
        path->tunnel_id = tunnel_id;
        inet_pton(AF_INET, srcip, &path->src_ip);
        inet_pton(AF_INET, dstip, &path->dest_ip);


        path->interval = 30;
        path->setup_priority = 7;
        path->hold_priority = 7;
        path->flags = 0;
        path->lsp_id = 1;
        path->IFH = 123;
        strncpy(path->name, "Path1", sizeof(path->name) - 1);
        path->name[sizeof(path->name) - 1] = '\0';

        path_tree = insert_node(path_tree, (void*)path, compare_path_insert); 
        display_tree(path_tree, 1);

        inet_pton(AF_INET, srcip, &send_ip);
        inet_pton(AF_INET, dstip, &rece_ip);

        if(resv_head == NULL) {
            resv_head = insert_session(resv_head, tunnel_id, srcip, dstip, 1);
        } else {
            insert_session(resv_head, tunnel_id, srcip, dstip, 1);
        }

        // Send RSVP-TE PATH Message
        send_path_message(sock, path->tunnel_id);
    }
    signal(SIGINT, handle_sigint);  // Register Ctrl+C handler

    //---------------------------------------------------------
    path_event_handler(); //send path msg
    int reached = 0;

    while(1) {
	    if (sigint_received == 1) {
		  //  path->tunnel_id = 1;
	       send_pathtear_message(sock, 1);
               path_tree = delete_node(path_tree, 1, compare_path_del, 1);     
	    }
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recvfrom(sock, buffer, sizeof(buffer), 0,
                (struct sockaddr*)&sender_addr, &addr_len);
        if (bytes_received < 0) {
            perror("Receive failed");
            continue;
        }

        struct rsvp_header *rsvp = (struct rsvp_header*)(buffer+20);

        switch(rsvp->msg_type) {

            case PATH_MSG_TYPE:

                //Receive PATH Message
                resv_event_handler();
                // get ip from the received path packet
                printf(" in path msg type\n");
                get_ip(buffer, sender_ip, receiver_ip, &tunnel_id);
                reached = dst_reached(sender_ip);

                printf("insert_path_session\n");
                if(path_head == NULL) {
                    path_head = insert_session(path_head, tunnel_id, sender_ip, receiver_ip,reached);
                } else {
                    insert_session(path_head, tunnel_id, sender_ip, receiver_ip, reached);
                }

                receive_path_message(sock,buffer,sender_addr);

                break;

            case RESV_MSG_TYPE:

                // Receive RSVP-TE RESV Message	
                path_event_handler();

                //get ip from the received resv msg
                printf(" in resv msg type\n");
                get_ip(buffer, sender_ip, receiver_ip, &tunnel_id);
                reached = dst_reached(sender_ip);

                printf("insert_resv_session\n");
                if(resv_head == NULL) {
                    resv_head = insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
                } else {
                    insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
                }

                receive_resv_message(sock,buffer,sender_addr);
                break;

	    case PATHTEAR_MSG_TYPE:
		//get ip from the received path tear msg
         //       get_ip(buffer, sender_ip, receiver_ip, &tunnel_id);
           //     reached = dst_reached(sender_ip);
 
/*                printf("insert_path_session\n");
                if(path_head == NULL) {
                    path_head = insert_session(path_head, tunnel_id, sender_ip, receiver_ip,reached);
                } else {
                    insert_session(path_head, tunnel_id, sender_ip, receiver_ip, reached);
                }*/

		receive_pathtear_message(sock,buffer,sender_addr);
		break;

	    case RESVTEAR_MSG_TYPE:
		
                //get ip from the received resvtear msg
             //   printf(" in resvtear msg type\n");
             //   get_ip(buffer, sender_ip, receiver_ip, &tunnel_id);
             //   reached = dst_reached(sender_ip);

/*                printf("insert_resv_session\n");
                if(resv_head == NULL) {
                    resv_head = insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
                } else {
                    insert_session(resv_head, tunnel_id, sender_ip, receiver_ip, reached);
                }*/

		receive_resvtear_message(sock,buffer,sender_addr);
		break;
        }
    }

    close(sock);
    return 0;
}

