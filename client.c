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
#include <signal.h>
#include <sys/wait.h>
#include <stdarg.h>
#include "srrp.h"

#include <libexplain/pclose.h>

FILE* logs;

void client_log(const char * type, const char * fmt, ...){
	//format message
	va_list args; 
	va_start(args, fmt);

	char msg[100];
	vsprintf(msg, fmt, args);

	va_end(args);

	//get timestamp
	time_t ltime;
	struct tm result;
	char stime[32];
	ltime = time(NULL);
	localtime_r(&ltime, &result);
	asctime_r(&result, stime);
	strtok(stime, "\n");			

	fprintf(logs, "%s - Client - [%s] - %s\n", stime, type, msg);		//write to log
	fflush(logs);

	printf("%s - %s\n", type, msg);
}

int main(int argc, char**argv){

	//log files
	logs = fopen("/var/log/tnp/client.log", "a+");

	int clientSocket;
	char buffer[1024];
	char recv_buff[1024];
	char send_buff[1024];
	struct sockaddr_in serverAddr;
	socklen_t addr_size;

	//casue zombies to be reaped automatically 
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
 		perror(0);
  		exit(1);
	}

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
		client_log("Error", "Connecting to server - %s", strerror(errno));
		return 0;
	}

	client_log("Info", "Connected to server");

	//timeout
	struct timeval tv;

	int bytes = 1;
	//receive loop
	while(bytes){

		FILE *fp;

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET (clientSocket, &rfds);

		tv.tv_sec = 60;
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
				client_log("Info", "Recevived iperf request - %d bytes", bytes);

				if(fork() == 0){

					char * cmd_fmt = "iperf -c %s -t %d -y C";
					char cmd[100];

					//default params
					char * dst_addr = "jbird.me";
					int dur = 10;

					//get parameters
					int i;
					for(i = 0; i < request->length; i++){
						if(request->params[i].param == SRRP_DUR){
							dur = request->params[i].value;
						}else{
							client_log("Error", "Invalid iperf parameter");
						}
					}

					//build command
					sprintf(cmd, cmd_fmt, dst_addr, dur);

					printf("%s\n", cmd);

					fp = popen(cmd, "r");
					if(fp == NULL){
						client_log("Error", "Failed to run command '%s'", cmd);
						_exit(1);
					}

					printf("Running iperf command\n");

					//get otuput	-	single line because of -y C flage
					char result[100];
					while(fgets(result, sizeof(result)-1, fp) != NULL){}

					int exit_status = WEXITSTATUS(pclose(fp));
					if(exit_status != 255){
						client_log("Error", "iperf failed exit status %d", exit_status);

						//should send resonpse with failed success code

						_exit(1);
					}else{
						
						//split up results
						char * tm, * src, * src_prt, * dst, * dst_port;
						int bps, data;
						double dur;
						tm = strtok(result, ",");
						src = strtok(NULL, ",");
						src_prt = strtok(NULL, ",");
						dst = strtok(NULL, ",");
						dst_port = strtok(NULL, ",");
						strtok(NULL, "-");
						dur = atof(strtok(NULL, ","));
						data = atoi(strtok(NULL, ","));
						bps = atoi(strtok(NULL, ","));

						//build response
						struct srrp_response * response;
						response = (struct srrp_response *) send_buff;
						response->id = request->id;
						response->length = 3;
						response->success = SRRP_SCES;

						//add results
						//bandwidth
						struct srrp_result bw;
						bw.result = SRRP_RES_BW;
						bw.value = bps;
						response->results[0] = bw;

						//duration
						struct srrp_result duration;
						duration.result = SRRP_RES_DUR;
						duration.value = dur;
						response->results[1] = duration;
	
						//bytes
						struct srrp_result size;
						size.result = SRRP_RES_SIZE;
						size.value = data;
						response->results[2] = size;

						client_log("Info", "Sending iperf results");

						send(clientSocket, send_buff, sizeof(send_buff), 0);
					}

					_exit(0);
				}

			}else if(request->type == SRRP_RTT){
				client_log("Info", "Received ping request - %d bytes", bytes);

				if(fork() == 0){
					
					char * command_fmt = "ping -c %d %s";
					char command[100];

					//default params
					char * dst_addr = "jbird.me";
					int itterations = 5;

					//load in params
					int i;
					for(i = 0; i < request->length; i++){
						if(request->params[i].param == SRRP_ITTR){
							itterations = request->params[i].value;
						}else{
							client_log("Error", "Invalid ping parameter");
						}
					}

					//build command
					sprintf(command, command_fmt, itterations, dst_addr);

					printf("%s\n", command);

					fp = popen(command , "r");
					if(fp == NULL){
						client_log("Error", "Failed to run command %s", command);

						_exit(1);
					}

					//get otuput	-	single line because of -y C flage
					char result[100];
					while(fgets(result, sizeof(result)-1, fp) != NULL){
						//printf("%s\n", result);
					}

					int exit_status = WEXITSTATUS(pclose(fp));
					if(exit_status != 255){
						client_log("Error", "command failed exit status %d", exit_status);

						//should respond

					}else{
						struct srrp_response * response = (struct srrp_response *) send_buff;

						if(parse_ping(request->id, response, result)){
							client_log("Error", "Failed to parse ping response");
							_exit(0);
						}

						send(clientSocket, send_buff, sizeof(send_buff), 0);

						_exit(0);
					}

				}	
			}else if(request->type == SRRP_UDP){
				client_log("Info", "Received UDP iperf request");

				if(fork() == 0){
					char * cmd_fmt = "iperf -c %s -u -p 5002 -b %dM -l %d -t %d -S %d -y C";
					char cmd[100];

					//default params
					char * dst_addr = "jbird.me";
					int speed = 1;
					int size = 1470;
					int duration = 10;
					int dscp = 0;

					//load in params
					int i;
					for(i = 0; i < request->length; i++){
						if(request->params[i].param == SRRP_SPEED){
							speed = request->params[i].value;
						}else if(request->params[i].param == SRRP_SIZE){
							size = request->params[i].value;
						}else if(request->params[i].param == SRRP_DUR){
							duration = request->params[i].value;
						}else if(request->params[i].param == SRRP_DSCP){
							dscp = request->params[i].value;
						}else{
							client_log("Error", "Invalid udp parameter");
						}
					}

					sprintf(cmd, cmd_fmt, dst_addr, speed, size, duration, dscp);

					printf("%s\n", cmd);

					fp = popen(cmd , "r");
					if(fp == NULL){
						client_log("Error", "Failed to run command %s", cmd);

						_exit(1);
					}

					//get otuput	-	single line because of -y C flage
					char result[200];
					while(fgets(result, sizeof(result)-1, fp) != NULL){
						printf("%s\n", result);
					}

					int exit_status = WEXITSTATUS(pclose(fp));
					if(exit_status != 255){
						client_log("Error", "udp iperf failed exit status %d", exit_status);

						//should send resonpse with failed success code

						_exit(1);
					}else{
						struct srrp_response * response = (struct srrp_response *) send_buff;

						printf("results=%s", result);

						if(parse_udp(request->id, response, result, speed, dscp)){
							client_log("Error", "Failed to parse udp response");
							_exit(0);
						}

						send(clientSocket, send_buff, sizeof(send_buff), 0);

						client_log("Info", "Sending udp iperf results");

						_exit(0);
					}
				}

			}else if(request->type == SRRP_DNS){
				client_log("Info", "Received dns request");

				if(fork() == 0){
					char * cmd = "nslookup google.co.uk";

					fp = popen(cmd , "r");
					if(fp == NULL){
						client_log("Error", "Failed to run command %s", cmd);
						_exit(1);
					}

					//get otuput
					char result[200];
					while(fgets(result, sizeof(result)-1, fp) != NULL){
						printf("%s\n", result);
					}

					int exit_status = pclose(fp);
					printf("%s\n", explain_pclose(fp));
					if(exit_status != 0){
						client_log("Info", "DNS status failure - exit status %d", exit_status);

						//create response
						struct srrp_response * response = (struct srrp_response *) send_buff;
						response->id == request->id;
						response->length = 0;
						response->success = SRRP_FAIL;

						send(clientSocket, send_buff, sizeof(send_buff), 0);
					}else{
						client_log("Info", "DNS status sucess - exit status %d", exit_status);	

						//create response
						struct srrp_response * response = (struct srrp_response *) send_buff;
						response->id == request->id;
						response->length = 0;
						response->success = SRRP_SCES;

						send(clientSocket, send_buff, sizeof(send_buff), 0);
					}

					_exit(0);

				}

			}else{
				//unrecognised data
				client_log("Error", "Recevied unrecognised data - %d - %d bytes", request->type, bytes);
			}
		}else{
			//timeout
			//dont care about timeout --- keep running
			client_log("Error", "Timeout");
		}
	}

	client_log("Info", "Recevied empty data, closing connection");

	fclose(logs);
	close(clientSocket);  

	return 0;
}