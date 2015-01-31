#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "srrp.h"

int main(int argc, char**argv){
	int clientSocket;
	char buffer[1024];
	char recv_buff[1024];
	char send_buff[1024];
	struct sockaddr_in serverAddr;
	socklen_t addr_size;

	/*---- Create the socket. The three arguments are: ----*/
	/* 1) Internet domain 2) Stream socket 3) Default protocol (TCP in this case) */
	clientSocket = socket(PF_INET, SOCK_STREAM, 0);

	/*---- Configure settings of the server address struct ----*/
	/* Address family = Internet */
	serverAddr.sin_family = AF_INET;
	/* Set port number, using htons function to use proper byte order */
	serverAddr.sin_port = htons(7891);
	/* Set IP address to localhost */
	serverAddr.sin_addr.s_addr = inet_addr(argv[1]);
	/* Set all bits of the padding field to 0 */
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);  

	/*---- Connect the socket to the server using the address struct ----*/
	addr_size = sizeof serverAddr;
	if(connect(clientSocket, (struct sockaddr *) &serverAddr, addr_size)){
		printf("Error connecting: %s\n", strerror(errno));
		return 0;
	}

	//timeout
	struct timeval tv;

	int bytes = 1;
	//receive loop
	while(bytes){
		//sleep(1);
		
		//struct srrp_response * response;
		//response = (struct srrp_response *) buffer;
		//response->id 		= 10;
		//response->length 	= 15;  

		/*---- Read the message from the server into the buffer ----*/
		//recv(clientSocket, buffer, 1024, 0);
		//strcpy(buffer, "Heartbeat");
		//send(clientSocket,buffer, 32,0);

		/*---- Print the received message ----*/
		//printf("Data received: %s",buffer);

		FILE *fp;

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET (clientSocket, &rfds);

		tv.tv_sec = 5;
		tv.tv_usec = 0;
				
		int ready = select(clientSocket + 1, &rfds, NULL, NULL, &tv);

		if(ready == -1 ){

		}else if(ready){
			bytes = recv(clientSocket,recv_buff, sizeof(recv_buff),0);
			struct srrp_request * request;
			request = (struct srrp_request *) recv_buff;

			if(request->type == SRRP_HB){
				//heatbeat request
				printf("Received hb request\n");
				//build response
				struct srrp_response * response;
				response = (struct srrp_response *) send_buff;
				response->id = 0;
				response->length = 0;
				send(clientSocket, send_buff, sizeof(send_buff), 0);
			}else if(request->type == SRRP_BW){
				printf("Received iperf request\n");

				if(fork() == 0){
					fp = popen("iperf -c jbird.me -y C", "r");
					if(fp == NULL){
						printf("Failed to run iperf command\n");
					}

					printf("Running iperf command\n");

					char result[100];
					while(fgets(result, sizeof(result)-1, fp) != NULL){
						printf("RESULT:%s\n", result);
					}

					if(WEXITSTATUS(pclose(fp)) > 0){
						printf("iperf failed\n");
					}else{
						printf("iperf successfull\n");
					}

					exit(0);
				}

				

				/*int i;
				for(i = 0; i < request->length; i++){
					printf("Param: %d\n", request->params[i].value);
				}*/
			}else{
				//unrecognised data
				//do nothing --- should log
			}
		}else{
			//timeout
			//dont care about timeout --- keep running
		}
	}

	close(clientSocket);  

	return 0;
}