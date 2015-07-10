#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdarg.h>
#include "srrp.h"

//#include <libexplain/pclose.h>	//not used anymore

#include "ini.h"
#include "ini.c"

FILE* logs;

/*
* Config struct to be loaded in 
*/
typedef struct{
    //server
    const char* server_addr;
    int server_port;

    //iperf
    int tcp_iperf_port;
    int udp_iperf_port;

    const char* nslookup_addr;

    //MAC address - unique ID
    const char* ether;
} configuration;
configuration config;

static int handler(void* user, const char* section, const char* name, const char* value){
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("server", "server_addr")) {
        pconfig->server_addr = strdup(value);
    } else if (MATCH("server", "server_port")) {
        pconfig->server_port = atoi(value);
    } else if (MATCH("iperf", "tcp_iperf_port")) {
        pconfig->tcp_iperf_port = atoi(value);
    } else if (MATCH("iperf", "udp_iperf_port")) {
        pconfig->udp_iperf_port = atoi(value);
    } else if (MATCH("nslookup", "nslookup_addr")) {
        pconfig->nslookup_addr = strdup(value);
    } else if (MATCH("client", "ether")) {
        pconfig->ether = strdup(value);
    }else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

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
	logs = fopen("/var/log/sensor/sensor.log", "a+");

	int clientSocket;
	char buffer[1024];
	char recv_buff[1024];
	char send_buff[1024];
	struct sockaddr_in serverAddr;
	socklen_t addr_size;

	//parse configurations from file
    if (ini_parse("/etc/sensor/config.ini", handler, &config) < 0) {
        client_log("Error", "Can't load 'config.ini'\n");
        return 1;
    }

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
	serverAddr.sin_port = htons(config.server_port);
	/* Set IP address to localhost */
	serverAddr.sin_addr.s_addr = inet_addr(config.server_addr);
	/* Set all bits of the padding field to 0 */
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);  

	/*---- Connect the socket to the server using the address struct ----*/
	addr_size = sizeof serverAddr;
	if(connect(clientSocket, (struct sockaddr *) &serverAddr, addr_size)){
		client_log("Error", "Connecting to server - %s", strerror(errno));
		return 0;
	}

	client_log("Info", "Connected to server");

	//get IP of interface - add interface to config file
	struct ifreq ifr;
	strncpy(ifr.ifr_name, "en0", IFNAMSIZ-1);
	ioctl(clientSocket, SIOCGIFADDR, &ifr);
	struct in_addr local_ip = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;

	printf("Local IP %s\n", inet_ntoa(local_ip));

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
			bytes = recv(clientSocket, recv_buff, sizeof(recv_buff),0);
			struct srrp_request * request = (struct srrp_request *) recv_buff;

			if(request->type == SRRP_HB){
				//heatbeat request
				printf("Received hb request - %d bytes\n", bytes);
				//build response
				response_init((struct srrp_response *) send_buff, request->type, SRRP_SCES, request->dst_id);

				send(clientSocket, send_buff, response_size((struct srrp_response *) send_buff), 0);
			}else if(request->type == SRRP_ETHER){
				//ethernet request
				printf("Received ether request - %d bytes\n", bytes);
				//build response
				response_init((struct srrp_response *) send_buff, request->type, SRRP_SCES, request->dst_id);

				//bit of a hack to break from srrp to send MAC address 
				memcpy(&((struct srrp_response *)send_buff)->results[0], config.ether, strlen(config.ether) + 1);
				//also for the ip
				memcpy(&((struct srrp_response *)send_buff)->results[0]+ 20, &local_ip, sizeof(local_ip));

				send(clientSocket, send_buff, response_size((struct srrp_response *) send_buff) /*header*/ + 21 /*mac*/ + sizeof(local_ip) /*ip*/, 0);
			}else if(request->type == SRRP_BW){				
				client_log("Info", "Recevived iperf request - %d bytes", bytes);

				if(fork() == 0){
					//listen for SIGCHLD so pclose resturns the status 
					signal(SIGCHLD, SIG_DFL);

					char * cmd_fmt = "iperf -c %s -p %d -t %d -y C";
					char cmd[100];

					//default params
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
					sprintf(cmd, cmd_fmt, config.server_addr, config.tcp_iperf_port, dur);

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

					int exit_status = pclose(fp);
					if(exit_status != 0){
						client_log("Error", "iperf failed exit status %d", exit_status);

						//should send resonpse with failed success code

						_exit(1);
					}else{

						//build response
						if(parse_iperf(request->type, request->dst_id, (struct srrp_response *) send_buff, result)){
							client_log("Error", "Failed to parse iperf response");
							_exit(0);
						}

						client_log("Info", "Sending iperf results");

						send(clientSocket, send_buff, response_size((struct srrp_response *) send_buff), 0);
					}

					_exit(0);
				}

			}else if(request->type == SRRP_RTT){
				client_log("Info", "Received ping request - %d bytes", bytes);

				if(fork() == 0){
					//listen for SIGCHLD so pclose resturns the status 
					signal(SIGCHLD, SIG_DFL);
					
					char * command_fmt = "ping -c %d %s";
					char command[100];

					//default params
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
					sprintf(command, command_fmt, itterations, config.server_addr);

					printf("%s\n", command);

					fp = popen(command , "r");
					if(fp == NULL){
						client_log("Error", "Failed to run command %s", command);
						_exit(1);
					}

					//get otuput	-	single line because of -y C flage
					char result[100];
					while(fgets(result, sizeof(result)-1, fp) != NULL){}

					int exit_status = pclose(fp);
					if(exit_status != 0){
						client_log("Error", "command failed exit status %d", exit_status);

						//should respond

					}else{

						if(parse_ping(request->type, request->dst_id, (struct srrp_response *) send_buff, result)){
							client_log("Error", "Failed to parse ping response");
							_exit(0);
						}

						send(clientSocket, send_buff, response_size((struct srrp_response *) send_buff), 0);

						_exit(0);
					}

				}	
			}else if(request->type == SRRP_UDP){
				client_log("Info", "Received UDP iperf request - %d bytes", bytes);

				if(fork() == 0){
					//listen for SIGCHLD so pclose resturns the status 
					signal(SIGCHLD, SIG_DFL);

					char * cmd_fmt = "iperf -c %s -u -p %d -b %dK -l %d -t %d -S %d -y C";
					char cmd[100];

					//default params
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

					sprintf(cmd, cmd_fmt, config.server_addr, config.udp_iperf_port, speed, size, duration, dscp);

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

					int exit_status = pclose(fp);
					if(exit_status != 0){
						client_log("Error", "udp iperf failed exit status %d", exit_status);

						//should send resonpse with failed success code
						//parse_failure()

						_exit(1);
					}else{

						if(parse_udp(request->type, request->dst_id, (struct srrp_response *) send_buff, result, speed, dscp)){
							client_log("Error", "Failed to parse udp response");
							_exit(0);
						}

						send(clientSocket, send_buff, response_size((struct srrp_response *) send_buff), 0);

						client_log("Info", "Sending udp iperf results");

						_exit(0);
					}
				}

			}else if(request->type == SRRP_DNS){
				client_log("Info", "Received dns request");

				if(fork() == 0){
					//listen for SIGCHLD so pclose resturns the status 
					signal(SIGCHLD, SIG_DFL);

					char * cmd_fmt = "nslookup %s";
					char cmd[100];

					sprintf(cmd, cmd_fmt, config.nslookup_addr);

					struct timeval start, end;
					float mtime, secs, usecs;

					gettimeofday(&start, NULL);

					fp = popen(cmd , "r");
					if(fp == NULL){
						client_log("Error", "Failed to run command %s", cmd);
						_exit(1);
					}

					//get otuput
					char result[200];
					while(fgets(result, sizeof(result)-1, fp) != NULL){}

					//work out resolution time
					gettimeofday(&end, NULL);
					secs  = end.tv_sec  - start.tv_sec;
					usecs = end.tv_usec - start.tv_usec;
					mtime = ((secs) * 1000 + usecs/1000.0) + 0.5;

					int exit_status = pclose(fp);
					if(exit_status != 0){
						client_log("Info", "DNS status failure - exit status %d", exit_status);

						parse_failure(request->type, request->dst_id, (struct srrp_response *) send_buff);

					}else{
						client_log("Info", "DNS status sucess - exit status %d", exit_status);	

						parse_dns(request->type, request->dst_id, (struct srrp_response *) send_buff, mtime);
					}					

					send(clientSocket, send_buff, response_size((struct srrp_response *) send_buff), 0);

					client_log("Info", "Sending dns response");				

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
			break;
		}
	}

	client_log("Info", "Recevied empty data, closing connection");

	fclose(logs);
	close(clientSocket);  

	return 0;
}
