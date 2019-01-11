#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/if_ether.h>
#include <net/if.h>

#include <linux/if_ether.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "list.h"


#include "rtspserver.h"
#include "rtsp_cmd.h"
#include "sample_comm.h"


#define MAX_RTSP_CHAN  2
#define RTP_H264               96
#define RTP_G726               97
#define MAX_RTSP_CLIENT       10
#define MAX_RTP_PKT_LENGTH   1400
#define RTSP_SERVER_PORT      554
#define RTSP_RECV_SIZE        1024
#define RTSP_MAX_VID          (1024*1024)
#define RTSP_MAX_AUD          (32*1024)
#define DEST_IP                "192.168.45.250"
#define DEST_PORT              5000

#define PARAM_STRING_MAX        100


RTP_FIXED_HEADER  *rtp_hdr;
NALU_HEADER		  *nalu_hdr;
FU_INDICATOR	  *fu_ind;
FU_HEADER		  *fu_hdr;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  1
#endif


typedef enum
{
	RTSP_IDLE = 0,
	RTSP_CONNECTED = 1,
	RTSP_SENDING = 2,
}RTSP_STATUS;

typedef struct
{
    int  nVidLen;
    int  nAudLen;
    int bIsIFrm;
    int bWaitIFrm;
    int bIsFree;
    char vidBuf[RTSP_MAX_VID];
    char audBuf[RTSP_MAX_AUD];
}RTSP_PACK;


typedef struct
{
	int index;
	int socket;
	int reqchn;
	int seqnum;
	unsigned int tsvid;
	unsigned int tsaud;
	int status;
	int sessionid;
	int rtpport[2];
	int rtcpport;
	char IP[20];
	char urlPre[PARAM_STRING_MAX];
}RTSP_CLIENT;

typedef struct
{
	int  vidLen;
	int  audLen;
	int  nFrameID;
	char vidBuf[RTSP_MAX_VID];
	char audBuf[RTSP_MAX_AUD];
}FRAME_PACK;

typedef struct _rtpbuf
{
	struct list_head list;
	HI_S32 len;
	char * buf;
}RTPbuf_s;


RTSP_PACK g_rtpPack[MAX_RTSP_CHAN];
RTSP_CLIENT g_rtspClients[MAX_RTSP_CLIENT];

int g_nSendDataChn = -1;
pthread_mutex_t g_mutex;
pthread_cond_t  g_cond;
pthread_mutex_t g_sendmutex;

pthread_t g_SendDataThreadId = 0;

int udpfd;

struct list_head RTPbuf_head = LIST_HEAD_INIT(RTPbuf_head);

void* RtspClientMsg(void *pParam)
{
    pthread_detach(pthread_self());
    int nRes;
    char pRecvBuf[RTSP_RECV_SIZE];
    RTSP_CLIENT *pClient = (RTSP_CLIENT *)pParam;
    memset(pRecvBuf, 0, sizeof(pRecvBuf));
    printf("RTSP:-----Create Client %s\n", pClient->IP);

		udpfd = socket(AF_INET,SOCK_DGRAM,0);//UDP
		struct sockaddr_in server;
		server.sin_family=AF_INET;
	   	server.sin_port=htons(g_rtspClients[0].rtpport[0]);          
	   	server.sin_addr.s_addr=inet_addr(g_rtspClients[0].IP);
		connect(udpfd,(struct sockaddr *)&server,sizeof(server));
		printf("udp up\n");
			
	while (pClient->status != RTSP_IDLE)
    {
        nRes = recv(pClient->socket, pRecvBuf, RTSP_RECV_SIZE, 0);
        //printf("-------------------%d\n",nRes);
        if (nRes < 1)
        {
            //usleep(1000);
            printf("RTSP:Recv Error--- %d\n", nRes);
            g_rtspClients[pClient->index].status = RTSP_IDLE;
            g_rtspClients[pClient->index].seqnum = 0;
            g_rtspClients[pClient->index].tsvid = 0;
            g_rtspClients[pClient->index].tsaud = 0;
            close(pClient->socket);
            break;
        }
        char cmdName[PARAM_STRING_MAX];
        char urlPreSuffix[PARAM_STRING_MAX];
        char urlSuffix[PARAM_STRING_MAX];
        char cseq[PARAM_STRING_MAX];

        ParseRequestString(pRecvBuf, nRes, cmdName, sizeof(cmdName), urlPreSuffix, sizeof(urlPreSuffix),
                           urlSuffix, sizeof(urlSuffix), cseq, sizeof(cseq));

        char *p = pRecvBuf;

        printf("<<<<<%s\n", p);

        //printf("\--------------------------\n");
        //printf("%s %s\n",urlPreSuffix,urlSuffix);

        if (strstr(cmdName, "OPTIONS"))
        {
            OptionAnswer(cseq, pClient->socket);
        } else if (strstr(cmdName, "DESCRIBE"))
        {
            DescribeAnswer(cseq, pClient->socket, urlSuffix, p);
            //printf("-----------------------------DescribeAnswer %s %s\n",
            //	urlPreSuffix,urlSuffix);
        } else if (strstr(cmdName, "SETUP"))
        {
            int rtpport, rtcpport;
            int trackID = 0;
            SetupAnswer(cseq, pClient->socket, pClient->sessionid, urlPreSuffix, p, &rtpport, &rtcpport);

            sscanf(urlSuffix, "trackID=%u", &trackID);
            //printf("----------------------------------------------TrackId %d\n",trackID);
            if (trackID < 0 || trackID >= 2) trackID = 0;
            g_rtspClients[pClient->index].rtpport[trackID] = rtpport;
            g_rtspClients[pClient->index].rtcpport = rtcpport;
            g_rtspClients[pClient->index].reqchn = atoi(urlPreSuffix);
            if (strlen(urlPreSuffix) < 100) strcpy(g_rtspClients[pClient->index].urlPre, urlPreSuffix);
            //printf("-----------------------------SetupAnswer %s-%d-%d\n",
            //	urlPreSuffix,g_rtspClients[pClient->index].reqchn,rtpport);
        } else if (strstr(cmdName, "PLAY"))
        {
            PlayAnswer(cseq, pClient->socket, pClient->sessionid, g_rtspClients[pClient->index].urlPre, p);
            g_rtspClients[pClient->index].status = RTSP_SENDING;
            printf("Start Play %d\n", pClient->index);
            //printf("-----------------------------PlayAnswer %d %d\n",pClient->index);
            //usleep(100);
        } else if (strstr(cmdName, "PAUSE"))
        {
            PauseAnswer(cseq, pClient->socket, p);
        } else if (strstr(cmdName, "TEARDOWN"))
        {
            TeardownAnswer(cseq, pClient->socket, pClient->sessionid, p);
            g_rtspClients[pClient->index].status = RTSP_IDLE;
            g_rtspClients[pClient->index].seqnum = 0;
            g_rtspClients[pClient->index].tsvid = 0;
            g_rtspClients[pClient->index].tsaud = 0;
            close(pClient->socket);
        }
    }
    printf("RTSP:-----Exit Client %s\n", pClient->IP);
    return NULL;
}

void* RtspServerListen(void *pParam)
{
	int s32Socket;
    struct sockaddr_in servaddr;
    int s32CSocket;
    int s32Rtn;
    int s32Socket_opt_value = 1;
    int nAddrLen;
    struct sockaddr_in addrAccept;
    int bResult;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(RTSP_SERVER_PORT);

    s32Socket = socket(AF_INET, SOCK_STREAM, 0);
	//SOL_SOCKET:通用套接字选项.
	//SO_REUSERADDR　　允许重用本地地址和端口　
	
    if (setsockopt(s32Socket, SOL_SOCKET, SO_REUSEADDR, &s32Socket_opt_value, sizeof(int)) == -1)
    {
        return (void *)(-1);
    }
    s32Rtn = bind(s32Socket, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in));
    if (s32Rtn < 0)
    {
        return (void *)(-2);
    }

    s32Rtn = listen(s32Socket, 50);   /*50,最大连接数*/
    if (s32Rtn < 0)
    {

        return (void *)(-2);
    }

	
    nAddrLen = sizeof(struct sockaddr_in);
    int nSessionId = 1000;
    while ((s32CSocket = accept(s32Socket, (struct sockaddr *)&addrAccept, &nAddrLen)) >= 0)
    {
        printf("<<<<RTSP Client %s Connected...\n", inet_ntoa(addrAccept.sin_addr));

        int nMaxBuf = 10 * 1024;  // 系统将会分配 2 x nMaxBuf 的缓冲大小
        if (setsockopt(s32CSocket, SOL_SOCKET, SO_SNDBUF, (char *)&nMaxBuf, sizeof(nMaxBuf)) == -1) printf("RTSP:!!!!!! Enalarge socket sending buffer error !!!!!!\n");
        int i;
        int bAdd = FALSE;
        for (i = 0; i < MAX_RTSP_CLIENT; i++)
        {
            if (g_rtspClients[i].status == RTSP_IDLE)
            {
                memset(&g_rtspClients[i], 0, sizeof(RTSP_CLIENT));
                g_rtspClients[i].index = i;
                g_rtspClients[i].socket = s32CSocket;
                g_rtspClients[i].status = RTSP_CONNECTED; //RTSP_SENDING;
                g_rtspClients[i].sessionid = nSessionId++;
                strcpy(g_rtspClients[i].IP, inet_ntoa(addrAccept.sin_addr));
                pthread_t threadIdlsn = 0;

                struct sched_param sched;
                sched.sched_priority = 1;
                //to return ACKecho
                pthread_create(&threadIdlsn, NULL, RtspClientMsg, &g_rtspClients[i]);
                pthread_setschedparam(threadIdlsn, SCHED_RR, &sched);

                bAdd = TRUE;
                break;
            }
        }
        if (bAdd == FALSE)
        {
            memset(&g_rtspClients[0], 0, sizeof(RTSP_CLIENT));
            g_rtspClients[0].index = 0;
            g_rtspClients[0].socket = s32CSocket;
            g_rtspClients[0].status = RTSP_CONNECTED; //RTSP_SENDING;
            g_rtspClients[0].sessionid = nSessionId++;
            strcpy(g_rtspClients[0].IP, inet_ntoa(addrAccept.sin_addr));
            pthread_t threadIdlsn = 0;
            struct sched_param sched;
            sched.sched_priority = 1;
            //to return ACKecho
            pthread_create(&threadIdlsn, NULL, RtspClientMsg, &g_rtspClients[0]);
            pthread_setschedparam(threadIdlsn, SCHED_RR, &sched);
            bAdd = TRUE;
        }
    }
    if (s32CSocket < 0)
    {
        // HI_OUT_Printf(0, "RTSP listening on port %d,accept err, %d\n", RTSP_SERVER_PORT, s32CSocket);
    }

    printf("----- INIT_RTSP_Listen() Exit !! \n");

    return NULL;
}

void SendRtpData(void )
{
		int i=0;
		int nChanNum=0;
		for(i=0; i<MAX_RTSP_CLIENT;i++)   //
		{
			if(g_rtspClients[i].status!=RTSP_SENDING)
			{
				continue;
			}
			int heart = g_rtspClients[i].seqnum %100;
			
			struct sockaddr_in server;
			int len = sizeof(server);
			server.sin_family=AF_INET;
			server.sin_port=htons(g_rtspClients[i].rtpport[0]);
			server.sin_addr.s_addr=inet_addr(g_rtspClients[i].IP); //DEST_IP
			int bytes = 0;
		//	float frametate =25;
			unsigned int timestamp_increse=0;
			timestamp_increse=3600;//(unsigned int)(90000.0 / frametate); 

			char* nalu_payload;
			int nAvFrmLen = 0;
			int nIsIFrm = 0;
			int nNaluType = 0;
			char sendbuf[1024*1024];
			int nReg;
			unsigned char u8NaluBytes;    //nalu首byte
			nIsIFrm = g_rtpPack[nChanNum].bIsIFrm;
			nAvFrmLen = g_rtpPack[nChanNum].nVidLen;  //发送的视频数据长度


			u8NaluBytes = g_rtpPack[nChanNum].vidBuf[4];
			printf("naulu:%x \n",u8NaluBytes);
			/*******************************/
			/******视频发送*****************/
			/*******************************/
			//rtp固定包头，为12字节,该句将sendbuf[0]的地址赋给rtp_hdr，
			//以后对rtp_hdr的写入操作将直接写入sendbuf。
			rtp_hdr =(RTP_FIXED_HEADER*)&sendbuf[0]; 
			//设置RTP HEADER，
			rtp_hdr->payload     = RTP_H264;       //负载类型号，
			rtp_hdr->version     = 2;          //版本号，此版本固定为2
			rtp_hdr->marker    = 0;            //标志位，由具体协议规定其值。
			rtp_hdr->ssrc      = htonl(10);   //随机指定为10，并且在本RTP会话中全局唯一
			if(nAvFrmLen<=1400)
			{
				//设置rtp M 位；
				rtp_hdr->marker  = 1;
				rtp_hdr->seq_no  = htons(g_rtspClients[i].seqnum++); //序列号，每发送一个RTP包增1
				nalu_hdr         = (NALU_HEADER*)&sendbuf[12]; 
				nalu_hdr->F      = (u8NaluBytes & 0x80) >> 7; 
				nalu_hdr->NRI    = (u8NaluBytes & 0x60) >> 5; 
				nalu_hdr->TYPE   = u8NaluBytes & 0x1f;
				
				nalu_payload=&sendbuf[13];//同理将sendbuf[13]赋给nalu_payload
				memcpy(nalu_payload,g_rtpPack[nChanNum].vidBuf,g_rtpPack[nChanNum].nVidLen);
				g_rtspClients[i].tsvid=g_rtspClients[i].tsvid+timestamp_increse;
				
				rtp_hdr->timestamp=htonl(g_rtspClients[i].tsvid);
				bytes=g_rtpPack[nChanNum].nVidLen + 13 ;				
				sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
			}
			else if(nAvFrmLen>1400)
			{
				//得到该nalu需要用多少长度为1400字节的RTP包来发送
				int k=0,l=0;
				k=nAvFrmLen/1400;//需要k个1400字节的RTP包
				l=nAvFrmLen%1400;//最后一个RTP包的需要装载的字节数
				int t=0;         //用于指示当前发送的是第几个分片RTP包
				g_rtspClients[i].tsvid=g_rtspClients[i].tsvid+timestamp_increse;
				rtp_hdr->timestamp=htonl(g_rtspClients[i].tsvid);
				while(t<=k)
				{
					rtp_hdr->seq_no = htons(g_rtspClients[i].seqnum++); //序列号，每发送一个RTP包增1
					if(t==0)
					{
						//设置rtp M 位；
						rtp_hdr->marker = 0;
						fu_ind         = (FU_INDICATOR*)&sendbuf[12];
						fu_ind->F      = (u8NaluBytes & 0x80) >> 7;   //n->forbidden_bit;
						fu_ind->NRI    = (u8NaluBytes & 0x60) >> 5; //n->nal_reference_idc>>5;
						fu_ind->TYPE   = 28;
						
						//设置FU HEADER,并将这个HEADER填入sendbuf[13]
						fu_hdr         =(FU_HEADER*)&sendbuf[13];
						fu_hdr->E      =0;
						fu_hdr->R      =0;
						fu_hdr->S      =1;
						fu_hdr->TYPE   =u8NaluBytes & 0x1f;  //n->nal_unit_type;
						
						nalu_payload=&sendbuf[14];//同理将sendbuf[14]赋给nalu_payload
						memcpy(nalu_payload,g_rtpPack[nChanNum].vidBuf,1400);//去掉NALU头
						
						bytes=1400+14;						
						sendto( udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
						t++;
						
					}
					else if(k==t)
					{
						
						//设置rtp M 位；当前传输的是最后一个分片时该位置1
						rtp_hdr->marker=1;
						fu_ind =(FU_INDICATOR*)&sendbuf[12]; 
						fu_ind->F= 0 ;   //n->forbidden_bit;
						fu_ind->NRI= nIsIFrm ;  //n->nal_reference_idc>>5;
						fu_ind->TYPE=28;
						
						//设置FU HEADER,并将这个HEADER填入sendbuf[13]
						fu_hdr =(FU_HEADER*)&sendbuf[13];
						fu_hdr->R=0;
						fu_hdr->S=0;
						fu_hdr->TYPE= u8NaluBytes & 0x1f;
						fu_hdr->E=1;
						
						nalu_payload=&sendbuf[14];
						memcpy(nalu_payload,g_rtpPack[nChanNum].vidBuf+t*1400,l);
						bytes=l+14;		
						sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
						t++;
					}
					else if(t<k && t!=0)
					{
						//设置rtp M 位；
						rtp_hdr->marker=0;
						//设置FU INDICATOR,并将这个HEADER填入sendbuf[12]
						fu_ind =(FU_INDICATOR*)&sendbuf[12]; 
						fu_ind->F=0;  //n->forbidden_bit;
						fu_ind->NRI=nIsIFrm;//n->nal_reference_idc>>5;
						fu_ind->TYPE=28;
						
						//设置FU HEADER,并将这个HEADER填入sendbuf[13]
						fu_hdr =(FU_HEADER*)&sendbuf[13];
						//fu_hdr->E=0;
						fu_hdr->R=0;
						fu_hdr->S=0;
						fu_hdr->E=0;
						fu_hdr->TYPE=nNaluType;
						
						nalu_payload=&sendbuf[14];
						memcpy(nalu_payload,g_rtpPack[nChanNum].vidBuf+t*1400,1400);
						bytes=1400+14;						
						sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
						t++;
					}
				}
			} 
#if 0
			/*******************************/
			/******音频发送*****************/
			/*******************************/
			//timestamp_increse=(unsigned int)(8000.0 / framerate);
			timestamp_increse = 8000;
			memset(sendbuf,0,sizeof(sendbuf));
			nAvFrmLen = g_rtpPack[nChanNum].nAudLen;
			
			rtp_hdr =(RTP_FIXED_HEADER*)&sendbuf[0]; 
			//设置RTP HEADER，
			rtp_hdr->payload     = RTP_G726;  
			rtp_hdr->version     = 2;        
			rtp_hdr->marker    = 0;          
			rtp_hdr->ssrc      = htonl(10);  

			//printf("-------------------------------%d\n",nAvFrmLen);
			if(nAvFrmLen<=1400)
			{
				//设置rtp M 位；
				rtp_hdr->marker=0;
				rtp_hdr->seq_no     = htons(g_rtspClients[i].seqnum++);
				nalu_hdr =(NALU_HEADER*)&sendbuf[12]; 
				nalu_hdr->F=0; 
				nalu_hdr->NRI=  nIsIFrm; 
				nalu_hdr->TYPE=  nNaluType;
				
				nalu_payload=&sendbuf[13];
				memcpy(nalu_payload,g_rtpPack[nChanNum].audBuf,g_rtpPack[nChanNum].nAudLen);
				g_rtspClients[i].tsaud=g_rtspClients[i].tsaud+timestamp_increse;
				
				rtp_hdr->timestamp=htonl(g_rtspClients[i].tsaud);
				bytes=g_rtpPack[nChanNum].nAudLen + 13 ;				
				sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
			}
			else if(nAvFrmLen>1400)
			{
				printf("-------->1400-----------------------%d\n",nAvFrmLen);
				int k=0,l=0;
				k=nAvFrmLen/1400;//需要k个1400字节的RTP包
				l=nAvFrmLen%1400;//最后一个RTP包的需要装载的字节数
				int t=0;         //用于指示当前发送的是第几个分片RTP包
				g_rtspClients[i].tsaud=g_rtspClients[i].tsaud+timestamp_increse;
				rtp_hdr->timestamp=htonl(g_rtspClients[i].tsaud);
				while(t<=k)
				{
					rtp_hdr->seq_no = htons(g_rtspClients[i].seqnum++); 
					if(t==0)
					{
						//设置rtp M 位；
						rtp_hdr->marker=0;
						fu_ind =(FU_INDICATOR*)&sendbuf[12];
						fu_ind->F= 0;   
						fu_ind->NRI= nIsIFrm; 
						fu_ind->TYPE=28;
						
						//设置FU HEADER,并将这个HEADER填入sendbuf[13]
						fu_hdr =(FU_HEADER*)&sendbuf[13];
						fu_hdr->E=0;
						fu_hdr->R=0;
						fu_hdr->S=1;
						fu_hdr->TYPE=nNaluType;
						
						nalu_payload=&sendbuf[14];
						memcpy(nalu_payload,g_rtpPack[nChanNum].audBuf,1400);
						
						bytes=1400+14;						
						sendto( udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
						t++;
						
					}
					else if(k==t)
					{
						
						//设置rtp M 位；当前传输的是最后一个分片时该位置1
						rtp_hdr->marker=1;
						fu_ind =(FU_INDICATOR*)&sendbuf[12]; 
						fu_ind->F= 0 ;   
						fu_ind->NRI= nIsIFrm ;  
						fu_ind->TYPE=28;
						
						//设置FU HEADER,并将这个HEADER填入sendbuf[13]
						fu_hdr =(FU_HEADER*)&sendbuf[13];
						fu_hdr->R=0;
						fu_hdr->S=0;
						fu_hdr->TYPE= nNaluType;
						fu_hdr->E=1;
						
						nalu_payload=&sendbuf[14];
						memcpy(nalu_payload,g_rtpPack[nChanNum].audBuf+t*1400,l);
						bytes=l+14;		
						sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
						t++;
					}
					else if(t<k && t!=0)
					{
						//设置rtp M 位；
						rtp_hdr->marker=0;
						//设置FU INDICATOR,并将这个HEADER填入sendbuf[12]
						fu_ind =(FU_INDICATOR*)&sendbuf[12]; 
						fu_ind->F=0;  
						fu_ind->NRI=nIsIFrm;
						fu_ind->TYPE=28;
						
						//设置FU HEADER,并将这个HEADER填入sendbuf[13]
						fu_hdr =(FU_HEADER*)&sendbuf[13];
						//fu_hdr->E=0;
						fu_hdr->R=0;
						fu_hdr->S=0;
						fu_hdr->E=0;
						fu_hdr->TYPE=nNaluType;
						
						nalu_payload=&sendbuf[14];
						memcpy(nalu_payload,g_rtpPack[nChanNum].audBuf+t*1400,1400);
						bytes=1400+14;						
						sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
						t++;
					}
				}
			} 
#endif			
		}
		//pthread_mutex_unlock(&g_rtpPack.sendmutex);
		//printf("RTSP:-----Send Frame len %d seq %d\n",nAvFrmLen,g_rtspClients[i].seqnum);
		//THREAD_SLEEP(1000/25);
}


void AddFrameToRtpListBUf(int nChanNum, unsigned char bIFrm,VENC_STREAM_S *pstStream)
{
	//2222222222222222
	int i,j,lens;
	g_nSendDataChn = nChanNum;
	g_rtpPack[nChanNum].nVidLen =0;
	g_rtpPack[nChanNum].bIsIFrm =bIFrm;

	for(j=0; j<MAX_RTSP_CLIENT; j++)
	{
		if(g_rtspClients[j].status == RTSP_SENDING)
		{
			for(i=0; i<pstStream->u32PackCount; i++)
			{
				RTPbuf_s *p = (RTPbuf_s *)malloc(sizeof(RTPbuf_s));
				INIT_LIST_HEAD(&(p->list));

				lens = pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset;
				p->buf = (char *)malloc(lens);
				p->len = lens;
				memcpy(p->buf,pstStream->pstPack[i].pu8Addr+pstStream->pstPack[i].u32Offset,lens);

				list_add_tail(&(p->list),&RTPbuf_head);
			}
		}
	}
}
void * SendRtpDataThread(void *p)
{
	while(1)
	{
		if(!list_empty(&RTPbuf_head))
		{
			
			RTPbuf_s *p = get_first_item(&RTPbuf_head,RTPbuf_s,list);
			g_rtpPack[0].nVidLen = p->len;
		    memcpy(g_rtpPack[0].vidBuf,p->buf,p->len);
			SendRtpData();
			list_del(&(p->list));
			free(p->buf);
			free(p);
			p = NULL;
			//count--;
			//printf("count = %d\n",count);
		
		}
		usleep(4000);
	}
}
void InitRtspServer()
{
	int i;
	pthread_t threadId = 0;
	for(i=0;i<MAX_RTSP_CHAN;i++)
	{
		memset(&g_rtpPack[i],0,sizeof(RTSP_PACK));
		g_rtpPack[i].bIsFree = TRUE;
		//g_rtpPack.bWaitIFrm = TRUE;
	}
	pthread_mutex_init(&g_sendmutex,NULL);
	pthread_mutex_init(&g_mutex,NULL);
	pthread_cond_init(&g_cond,NULL);
	memset(g_rtspClients,0,sizeof(RTSP_CLIENT)*MAX_RTSP_CLIENT);
	
	pthread_create(&g_SendDataThreadId, NULL, SendRtpDataThread, NULL);
	pthread_create(&threadId, NULL, RtspServerListen, NULL);
	printf("RTSP:-----Init Rtsp server\n");
}

int AddFrameToRtspBuf(int nChanNum, unsigned char bIFrm,VENC_STREAM_S *pstStream)
{

	  int i;
	  int lens = 0;
	  g_nSendDataChn = nChanNum;
	  g_rtpPack[nChanNum].nVidLen =0;
	  g_rtpPack[nChanNum].bIsIFrm =bIFrm;
	  for (i = 0; i < pstStream->u32PackCount; i++)
	  {
	  	  lens = pstStream->pstPack[i].u32Len-pstStream->pstPack[i].u32Offset;
		  memcpy(g_rtpPack[nChanNum].vidBuf,pstStream->pstPack[i].pu8Addr+pstStream->pstPack[i].u32Offset,lens);
		  g_rtpPack[nChanNum].nVidLen = lens;
		 // printf("lens = %fkB\n",lens/1024.0);
		  SendRtpData();
		  lens =0;
	  }
//		pthread_mutex_lock(&g_mutex);
//		pthread_cond_signal(&g_cond);
//		pthread_mutex_unlock(&g_mutex);

#if 0
	//if(eType == MBT_VIDEO)//视频
    {
		if(g_FrmPack[nChanNum].nFrameID!= nVidFrmNum && nVidFrmNum != 0)//新一帧
		{
			//pthread_mutex_lock(&g_rtpPack.sendmutex);
			memcpy(g_rtpPack[nChanNum].vidBuf,g_FrmPack[nChanNum].vidBuf,g_FrmPack[nChanNum].vidLen);
			memcpy(g_rtpPack[nChanNum].audBuf,g_FrmPack[nChanNum].audBuf,g_FrmPack[nChanNum].audLen);
			g_rtpPack[nChanNum].nVidLen = g_FrmPack[nChanNum].vidLen;
			g_rtpPack[nChanNum].nAudLen = g_FrmPack[nChanNum].audLen;
			g_rtpPack[nChanNum].bIsIFrm = bIFrm;
			g_rtpPack[nChanNum].bIsFree = FALSE;
			//pthread_mutex_unlock(&g_rtpPack.sendmutex);				
			g_nSendDataChn = nChanNum;
			pthread_mutex_lock(&g_mutex);
			pthread_cond_signal(&g_cond);
			pthread_mutex_unlock(&g_mutex);

			//printf("RTSP:----Chn %d Video %d Audio %d\n",nChanNum,
			//	g_rtpPack[nChanNum].nVidLen,g_rtpPack[nChanNum].nAudLen);
			if(nSize < RTSP_MAX_VID)
			{
				 memcpy(g_FrmPack[nChanNum].vidBuf,pData,nSize);
				 g_FrmPack[nChanNum].vidLen = nSize;   
				 g_FrmPack[nChanNum].nFrameID= nVidFrmNum;   
				 g_FrmPack[nChanNum].audLen = 0;
			}
		}
		else//同一帧
		{
			//printf("RTSP:-----Add Frame\n");
			if(g_FrmPack[nChanNum].vidLen+nSize < RTSP_MAX_VID)
			{
				memcpy(g_FrmPack[nChanNum].vidBuf+g_FrmPack[nChanNum].vidLen,pData,nSize);
				g_FrmPack[nChanNum].vidLen += nSize;
			}
			else
			{
				printf("rtsp max vid frame !!!\n");
			}
		}
    }
/****************************************
	else //音频
	{
	    if(g_FrmPack[nChanNum].audLen+nSize < RTSP_MAX_AUD)
	    {
		    memcpy(g_FrmPack[nChanNum].audBuf+g_FrmPack[nChanNum].audLen,pData,nSize);
		    g_FrmPack[nChanNum].audLen += nSize;
	    }
		else
		{
		   	g_FrmPack[nChanNum].audLen = 0;
		}
	}
********************************************/	

#endif
	return 0;
}


