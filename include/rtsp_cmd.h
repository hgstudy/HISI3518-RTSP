#include <sys/types.h>

//typedef unsigned long u_int32_t;
typedef unsigned short u_int16_t;
typedef unsigned char u_int8_t;
typedef u_int16_t portNumBits;
typedef u_int32_t netAddressBits;
typedef long long _int64;
typedef enum
{
	FALSE=0,
	TRUE=1
}BOOL;

/*
typedef struct 
{
	char FileName[MAX_PATH];
	int startpos;
	int endpos;//-1表示到文件结尾处
}FILE_INFO;//查询出来的录象文件信息
*/

typedef struct 
{
	int startblock;//代表开始文件块号
	int endblock;//代表结束文件块号
	int BlockFileNum;//代表录像段数
	
}IDXFILEHEAD_INFO;//.IDX文件的头信息

typedef struct 
{
	_int64 starttime;//代表开始shijian
	_int64 endtime;//代表结束shijian 
	int startblock;//代表开始文件块号
	int endblock;//代表结束文件块号
	int stampnum;//代表时间戳数量
}IDXFILEBLOCK_INFO;//.IDX文件段信息

typedef struct 
{
	int blockindex;//代表所在文件块号
	int pos;//代表该时间戳在文件块的位置
	_int64 time;//代表时间戳时间戳的时间点
}IDXSTAMP_INFO;//.IDX文件的时间戳信息

typedef struct 
{
	char filename[150];//代表所在文件块号
	int pos;//代表该时间戳在文件块的位置
	_int64 time;//代表时间戳时间戳的时间点
}FILESTAMP_INFO;//.IDX文件的时间戳信息

typedef struct 
{
	char channelid[9];
	_int64 starttime;//代表开始shijian
	_int64 endtime;//代表结束shijian 
	_int64 session;
	int		type;	//类型
	int		encodetype;//编码格式;
}FIND_INFO;//.IDX文件的时间戳信息

typedef enum
{
	RTP_UDP,
	RTP_TCP,
	RAW_UDP
}StreamingMode;

BOOL OptionAnswer(char *cseq, int sock);
BOOL DescribeAnswer(char *cseq,int sock,char* urlSuffix,char* recvbuf);
BOOL SetupAnswer(char *cseq,int sock,int SessionId ,char * urlSuffix,char* recvbuf,int* rtpport, int* rtcpport);
BOOL PlayAnswer(char *cseq,int sock,int SessionId,char* urlPre,char * recvbuf);
BOOL PauseAnswer(char *cseq,int sock,char * recvbuf);
BOOL TeardownAnswer(char *cseq,int sock,int SessionId,char * recvbuf);

BOOL ParseRequestString(char const* reqStr,
		unsigned reqStrSize,
		char* resultCmdName,
		unsigned resultCmdNameMaxSize,
		char* resultURLPreSuffix,
		unsigned resultURLPreSuffixMaxSize,
		char* resultURLSuffix,
		unsigned resultURLSuffixMaxSize,
		char* resultCSeq,
		unsigned resultCSeqMaxSize);

void ParseTransportHeader(char const* buf,
		StreamingMode * streamingMode,			 
		char** streamingModeString,			  
		char** destinationAddressStr,			  
		u_int8_t * destinationTTL,			  
		portNumBits* clientRTPPortNum, // if UDP			  
		portNumBits* clientRTCPPortNum, // if UDP			  
		unsigned char* rtpChannelId, // if TCP		  
		unsigned char* rtcpChannelId // if TCP
		);



