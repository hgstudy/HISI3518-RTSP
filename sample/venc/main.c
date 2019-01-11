#include <stdio.h>
#include <stdlib.h>   //exit()
#include <signal.h>   //
#include <sys/time.h>
#include <time.h>
#include <unistd.h>  


#include "rtspserver.h"
#include "rtsputils.h"
#include "ringfifo.h"
#include "sample_comm.h"

extern int g_s32Quit ;


extern HI_S32 SAMPLE_VENC_1080P_CLASSIC(HI_VOID);
int main(int argc, char *argv[])
{   
    MPP_VERSION_S mppVersion;
    HI_MPI_SYS_GetVersion(&mppVersion);
    printf("MPP Ver %s\n", mppVersion.aVersion);

    HI_S32 s32Ret;

	InitRtspServer();

    /* H.264@1080p@30fps+H.265@1080p@30fps+H.264@D1@30fps */
    s32Ret = SAMPLE_VENC_1080P_CLASSIC();
    if (HI_SUCCESS == s32Ret)
   		printf("program exit normally!\n");
	else{
        printf("program exit abnormally!\n");
    	exit(s32Ret);
	}
}
