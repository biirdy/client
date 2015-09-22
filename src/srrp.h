/*
* Sensor request response protocol
*/

//SRRP parameter types
#define SRRP_SIZE	1       // Size
#define SRRP_ITTR   2       // Itterations
#define SRRP_PRTO   3       // Protocol
#define SRRP_SPEED	4		// Speed
#define SRRP_DUR 	5		// Duration
#define SRRP_DSCP	6		// DSCP
#define SRRP_DN		7		// Domain name 
#define SRRP_SERVER	8		// server

#define PARAM_SIZE 4

//SRRP parameter
struct srrp_param{
	uint16_t	param;
	uint16_t	value;
} __attribute__((__packed__));

//SRRP request types 
#define SRRP_HB		1       // Heartbeat
#define SRRP_BW		2       // Bandwidth
#define SRRP_RTT	3       // Round trip time
#define SRRP_UDP	4       // UDP iperf - reports packet loss and jitter
#define SRRP_DNS	5       // DNS status 
#define SRRP_TRT	6       // Traceroute
#define SRRP_ETHER	10		// MAC address

#define REQUEST_HEADER_LENGTH 14

//SRRP request
struct srrp_request{
	uint16_t			type;
	uint16_t			length;
	uint16_t			m_id;
	struct in_addr		dst_ip;
	uint32_t			dst_id;
	struct srrp_param	params[ ];
} __attribute__((__packed__));

//SRRP result types 
#define SRRP_RES_RTTAVG 1       // RTT avgerage
#define SRRP_RES_RTTMAX 2       // RTT max
#define SRRP_RES_RTTMIN 3       // RTT min
#define SRRP_RES_RTTDEV 4       // RTT min
#define SRRP_RES_BW 	5       // Bandwidth
#define SRRP_RES_DUR	6		// Duration
#define SRRP_RES_SIZE	7		// Size
#define SRRP_RES_JTR	8       // Jitter
#define SRRP_RES_PKLS 	9      	// Packet loss
#define SRRP_RES_SPEED	10		// Send speed
#define SRRP_RES_DSCP	11		// DSCP flags 
#define SRRP_RES_HOP 	12      // Traceroute hop

#define RESULT_SIZE 8

//SRRP result 
struct srrp_result{
	uint32_t	result;
	uint32_t	value;
} __attribute__((__packed__));

//SRRP respone success codes
#define SRRP_SCES	1       // Successful
#define SRRP_PSCES  2       // Partly sucessful
#define SRRP_FAIL   3       // Failed

#define RESPONSE_HEADER_LENGTH 14

//SRRP response
struct srrp_response{
	uint32_t			type;
	uint32_t			dst_id;
	uint16_t			m_id;
	uint16_t			length;
	uint16_t			success;
	struct srrp_result	results[ ];
} __attribute__((__packed__));



/*
*
*/
int response_init(struct srrp_response * response, int type, int success, int id, int m_id){
	response->length 	= 0;
	response->type 		= type;
	response->dst_id 	= id;
	response->success 	= SRRP_SCES;
	response->m_id		= m_id;

	return 1;
}

/*
*
*/
int add_result(struct srrp_response * response, int result, float value){
	
	struct srrp_result res;
	res.result = result;
	memcpy(&res.value, &value, 4); 
	response->results[response->length] = res;
	response->length++;

	return(response->length);
}

/*
* Return the size of the response in bytes
*/
int response_size(struct srrp_response * response){
	return (response->length * RESULT_SIZE) + RESPONSE_HEADER_LENGTH;
}

int request_init(struct srrp_request * request, int type, int id, struct in_addr ip, int m_id){

	request->type 	= type;
	request->length = 0;
	request->dst_id = id;
	request->dst_ip = ip;
	request->m_id	= m_id;

	return 1;
}

int add_param(struct srrp_request * request, int type, int value){

	struct srrp_param param;
	param.param = type;
	param.value = value;
	request->params[request->length] = param;
	request->length++;

	return(request->length);
}

int add_param_string(struct srrp_request * request, int type, char * string){

	struct srrp_param param;
	param.param = type;					//type of sting
	param.value = ((strlen(string) + PARAM_SIZE) / PARAM_SIZE);	//length of params in value	

	request->params[request->length] = param;

	memcpy(&request->params[request->length + 1], string, strlen(string) + 1);
	request->length = request->length + ((strlen(string) + PARAM_SIZE) / PARAM_SIZE) + 1;	//ceiling division	 

	return(request->length);
}

int get_param_string(char ** dst, struct srrp_request * request, int index){
	*dst = malloc(request->params[index].value * PARAM_SIZE);
	strcpy(*dst, (char *)&request->params[index+1]);
	return index + request->params[index].value + 1;
}

/*
* Return the size of the request in bytes
*/
int request_size(struct srrp_request * request){
	return(request->length * PARAM_SIZE) + REQUEST_HEADER_LENGTH;
} 

/*
*
* Functions for parsing tools output into srrp_responses
*
*/

/*
* id 		- 
* response 	-
* output 	- the last line outputted by ping. String is destroyed.
*/
int parse_ping(int type, int dst_id, int m_id, struct srrp_response * response, char * output){

	if(response==NULL || output==NULL)
		return 1;

	strtok(output, "=");

	//header
	response_init(response, type, SRRP_SCES, dst_id, m_id);

	//resutls 
	add_result(response, SRRP_RES_RTTMIN, atof(strtok(NULL, "/")));
	add_result(response, SRRP_RES_RTTAVG, atof(strtok(NULL, "/")));
	add_result(response, SRRP_RES_RTTMAX, atof(strtok(NULL, "/")));
	add_result(response, SRRP_RES_RTTDEV, atof(strtok(NULL, "/")));

	return 0;
}



/*
* response 	-
* output 	- a comma seperated string produced by iperf using the '-y C' flag
*/
int parse_iperf(int type, int dst_id, int m_id, struct srrp_response * response, char * output){
	
	if(response==NULL || output==NULL)
		return 1;

	//header
	response_init(response, type, SRRP_SCES, dst_id, m_id);

	strtok(output, ",");	//time
	strtok(NULL, ",");		//src addr
	strtok(NULL, ",");		//src port
	strtok(NULL, ",");		//dst addr
	strtok(NULL, ",");		//dst port
	strtok(NULL, "-");

	//results
	add_result(response, SRRP_RES_DUR, atof(strtok(NULL, ",")));	//duration
	add_result(response, SRRP_RES_SIZE, atof(strtok(NULL, ",")));	//date
	add_result(response, SRRP_RES_BW, atof(strtok(NULL, ",")));		//bw				


	return 0;
}

/*
*
*/
int parse_failure(int type, int dst_id, int m_id, struct srrp_response * response){

	response_init(response, type, SRRP_FAIL, dst_id, m_id);

	return 1;
}

/*
*
*
*/
int parse_udp(int type, int dst_id, int m_id, struct srrp_response * response, char * output, int send_speed, int dscp_flag){

	if(response==NULL || output==NULL)
		return 1;

	strtok(output, ",");	//connection
	strtok(NULL, ",");		//ip
	strtok(NULL, ",");		//port
	strtok(NULL, ",");		//ip
	strtok(NULL, ",");		//port
	strtok(NULL, ",");		//id
	strtok(NULL, "-");		//time 

	//header
	response_init(response, type, SRRP_SCES, dst_id, m_id);

	//add results
	add_result(response, SRRP_RES_DUR, atof(strtok(NULL, ",")));
	add_result(response, SRRP_RES_SIZE, atof(strtok(NULL, ",")));
	add_result(response, SRRP_RES_BW, atof(strtok(NULL, ",")));
	add_result(response, SRRP_RES_JTR, atof(strtok(NULL, ",")));

	strtok(NULL, ",");		//recvd datagras
	strtok(NULL, ",");		//sent datagrams

	add_result(response, SRRP_RES_PKLS, atof(strtok(NULL, ",")));
	add_result(response, SRRP_RES_DSCP, (float) dscp_flag);
	add_result(response, SRRP_RES_SPEED, (float) send_speed);

	return 0;
}

int parse_dns(int type, int dst_id, int m_id, struct srrp_response * response, float result){

	if(response==NULL || !result)
		return 1;

	response_init(response, type, SRRP_SCES, dst_id, m_id);

	add_result(response, SRRP_RES_DUR, result);

	return 0;
}