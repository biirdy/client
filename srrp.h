struct srrp_param{
	uint32_t	param;
	uint32_t	value;
};

struct srrp_request{
	uint16_t			id;
	uint16_t			type;
	uint16_t			length;
	struct srrp_param	params[ ];
};

struct srrp_result{
	uint32_t	result;
	uint32_t	value;
};

struct srrp_response{
	uint16_t			id;
	uint16_t			length;
	struct srrp_result	results[ ];
};

