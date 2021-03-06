/******************************************************************************
  Some simple Hisilicon Hi35xx video encode functions.  Copyright (C), 2010-2018, Hisilicon Tech. Co., Ltd.
 ******************************************************************************
    Modification:  2017-2 Created******************************************************************************/
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include "sample_comm.h"
#include "hi_spi.h"


const HI_U8 g_SOI[2] = {0xFF, 0xD8};
const HI_U8 g_EOI[2] = {0xFF, 0xD9};
static pthread_t gs_VencPid;
static pthread_t gs_VpssCropPid;
static SAMPLE_VENC_GETSTREAM_PARA_S gs_stPara;
static SAMPLE_VPSS_CROP_INFO_S gs_stVpssCropInfo;
static HI_S32 gs_s32SnapCnt = 0;

//Hi3516CV300不支持SPI_LSB_FIRST模式设置，需自行对参数进行反转
static void reverse8(unsigned char *buf, unsigned int len)
{
	unsigned int i;
	for (i = 0; i < len; i++) 
	{
		buf[i] = (buf[i] & 0x55) << 1 | (buf[i] & 0xAA) >> 1;
		buf[i] = (buf[i] & 0x33) << 2 | (buf[i] & 0xCC) >> 2;
		buf[i] = (buf[i] & 0x0F) << 4 | (buf[i] & 0xF0) >> 4;
	}
}

/******************************************************************************
* function : Set venc memory location
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_MemConfig(HI_VOID)
{
    HI_S32 i = 0;
    HI_S32 s32Ret;
    HI_CHAR* pcMmzName;
    MPP_CHN_S stMppChnVENC;
    /* group, venc max chn is 64*/
    for (i = 0; i < 64; i++)
    {
        stMppChnVENC.enModId = HI_ID_VENC;
        stMppChnVENC.s32DevId = 0;
        stMppChnVENC.s32ChnId = i;
        pcMmzName = NULL;

        /*venc*/
        s32Ret = HI_MPI_SYS_SetMemConf(&stMppChnVENC, pcMmzName);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_SYS_SetMemConf with %#x!\n", s32Ret);
            return HI_FAILURE;
        }
    }
    return HI_SUCCESS;
}

/******************************************************************************
* function : venc bind vi
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_BindVi(VENC_CHN VeChn, VI_CHN ViChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VPSS;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = ViChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;
    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : venc unbind vi
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_UnBindVi(VENC_CHN VeChn, VI_CHN ViChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VIU;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = ViChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;
    s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : venc bind vpss
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_BindVpss(VENC_CHN VeChn, VPSS_GRP VpssGrp, VPSS_CHN VpssChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VPSS;
    stSrcChn.s32DevId = VpssGrp;
    stSrcChn.s32ChnId = VpssChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;
    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : venc unbind vpss
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_UnBindVpss(VENC_CHN VeChn, VPSS_GRP VpssGrp, VPSS_CHN VpssChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VPSS;
    stSrcChn.s32DevId = VpssGrp;
    stSrcChn.s32ChnId = VpssChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;
    s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : venc bind vo
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_BindVo(VO_DEV VoDev, VO_CHN VoChn, VENC_CHN VeChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VOU;
    stSrcChn.s32DevId = VoDev;
    stSrcChn.s32ChnId = VoChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;
    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : venc unbind vo
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_UnBindVo(VENC_CHN GrpChn, VO_DEV VoDev, VO_CHN VoChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VOU;
    stSrcChn.s32DevId = VoDev;
    stSrcChn.s32ChnId = VoChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = GrpChn;
    s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : vdec bind venc
******************************************************************************/
HI_S32 SAMPLE_COMM_VDEC_BindVenc(VDEC_CHN VdChn, VENC_CHN VeChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VDEC;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = VdChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;
    s32Ret = HI_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* function : venc unbind vo
******************************************************************************/
HI_S32 SAMPLE_COMM_VDEC_UnBindVenc(VDEC_CHN VdChn, VENC_CHN VeChn)
{
    HI_S32 s32Ret = HI_SUCCESS;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    stSrcChn.enModId = HI_ID_VDEC;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = VdChn;
    stDestChn.enModId = HI_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = VeChn;

    s32Ret = HI_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* funciton : get file postfix according palyload_type.
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_GetFilePostfix(PAYLOAD_TYPE_E enPayload, char* szFilePostfix)
{
    if (PT_H264 == enPayload)
    {
        strncpy(szFilePostfix, ".h264", 10);
    }
    else if (PT_H265 == enPayload)
    {
        strncpy(szFilePostfix, ".h265", 10);
    }
    else if (PT_JPEG == enPayload)
    {
        strncpy(szFilePostfix, ".jpg", 10);
    }
    else if (PT_MJPEG == enPayload)
    {
        strncpy(szFilePostfix, ".mjp", 10);
    }
    else if (PT_MP4VIDEO == enPayload)
    {
        strncpy(szFilePostfix, ".mp4", 10);
    }
    else
    {
        SAMPLE_PRT("payload type err!\n");
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

HI_S32 SAMPLE_COMM_VENC_GetGopAttr(VENC_GOP_MODE_E enGopMode,VENC_GOP_ATTR_S *pstGopAttr,VIDEO_NORM_E enNorm)
{
	switch(enGopMode)
	{
		case VENC_GOPMODE_NORMALP:
		pstGopAttr->enGopMode  = VENC_GOPMODE_NORMALP;
		pstGopAttr->stNormalP.s32IPQpDelta = 2;
		break;

		case VENC_GOPMODE_SMARTP:
		pstGopAttr->enGopMode  = VENC_GOPMODE_SMARTP;
		pstGopAttr->stSmartP.s32BgQpDelta = 4;
		pstGopAttr->stSmartP.s32ViQpDelta = 2;
		pstGopAttr->stSmartP.u32BgInterval = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 75 : 90;
		break;
		case VENC_GOPMODE_DUALP:
		pstGopAttr->enGopMode  = VENC_GOPMODE_DUALP;
		pstGopAttr->stDualP.s32IPQpDelta  = 4;
		pstGopAttr->stDualP.s32SPQpDelta  = 2;
		pstGopAttr->stDualP.u32SPInterval = 3;
		break;
		default:
		SAMPLE_PRT("not support the gop mode !\n");
		return HI_FAILURE;
		break;
	}
	return HI_SUCCESS;
}
/******************************************************************************
* funciton : save mjpeg stream.
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveMJpeg(FILE* fpMJpegFile, VENC_STREAM_S* pstStream)
{
    VENC_PACK_S*  pstData;
    HI_U32 i;
    //fwrite(g_SOI, 1, sizeof(g_SOI), fpJpegFile); //in Hi35xx, user needn't write SOI!
    for (i = 0; i < pstStream->u32PackCount; i++)
    {
        pstData = &pstStream->pstPack[i];
        fwrite(pstData->pu8Addr + pstData->u32Offset, pstData->u32Len - pstData->u32Offset, 1, fpMJpegFile);
        fflush(fpMJpegFile);
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : save jpeg stream.
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveJpeg(FILE* fpJpegFile, VENC_STREAM_S* pstStream)
{
    VENC_PACK_S*  pstData;
    HI_U32 i;
	
    for (i = 0; i < pstStream->u32PackCount; i++)
    {
        pstData = &pstStream->pstPack[i];
        fwrite(pstData->pu8Addr + pstData->u32Offset, pstData->u32Len - pstData->u32Offset, 1, fpJpegFile);
        fflush(fpJpegFile);
    }

    return HI_SUCCESS;
}

/******************************************************************************
* funciton : save H264 stream
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveH264(FILE* fpH264File, VENC_STREAM_S* pstStream)
{
    HI_S32 i;
	
    for (i = 0; i < pstStream->u32PackCount; i++)
    {
        fwrite(pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset,            
				pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset, 1, fpH264File);
        fflush(fpH264File);
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : save H265 stream
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveH265(FILE* fpH265File, VENC_STREAM_S* pstStream)
{
    HI_S32 i;
    for (i = 0; i < pstStream->u32PackCount; i++)
    {
        fwrite(pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset,
               pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset, 1, fpH265File);
        fflush(fpH265File);
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : save jpeg stream
******************************************************************************/
/*******************************************************************
*等待k210从机ready管脚在发送完命令后做好准备接收数据，这个代码能改进
*发送命令后要等待k210从机准备好,不然210会在下次接收数据会接收不到
********************************************************************/
//获取管脚的值
#define SPI_Ready_Num (5 * 8) + 3
HI_S32 SPI_GET_GPIO_Value()
{
	FILE *fp;
	unsigned char buf[10];
	
	//打开输出管脚文件
	fp = fopen("/sys/class/gpio/export", "w");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/export.\n");
		return HI_FAILURE;
	}
	fprintf(fp, "%d", SPI_Ready_Num);
	fclose(fp);
	
	//设置方向
	fp = fopen("/sys/class/gpio/gpio43/direction", "rb+");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/gpio43/direction.\n");
		return HI_FAILURE;
	}
	fprintf(fp, "in");
	fclose(fp);

	//获取管脚值
	fp = fopen("/sys/class/gpio/gpio43/value", "rb+");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/gpio43/value.\n");
		return HI_FAILURE;		
	}
	memset(buf, 0, 10);
	fread(buf, sizeof(char), sizeof(buf) - 1, fp);
	fclose(fp);

	fp = fopen("/sys/class/gpio/unexport", "w");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/unexport.\n");
		return HI_FAILURE;
	}
	fprintf(fp, "%d", SPI_Ready_Num);
	fclose(fp);
	
	return (int)(buf[0] - 48);
}

/******************************************************************************************
*SPI_Wait_Slave_Ready:这个函数表示在从机在接收到命令后，会拉低ready管脚，										  *
*					  表示从机做好了准备，主机可以发送数据等进一步操作了。										  *
*					  例如在发送一个命令读后，从机做相应的准备工作，让主机读										  *
*																						  *
*SPI_Wait_Slave_Idle：函数表示在发送命令前，从机是不是做好了准备(准备好就是ready是高电平).*
*					  例如：在读后，又要发送写指令，若是从机没做好准备，就会出错。			            			  *				
*注意:两个函数都是针对从机在接收命令的前后的状态																  *
*******************************************************************************************/
//等待从机的准备
HI_S32 SPI_Wait_Slave_Ready()
{
	int i = 0;

	for (i = 0; i < 0xffffff; i++)
	{
		if (0 == SPI_GET_GPIO_Value())
		{
			printf("Slave is Ready!!!\n");
			return HI_SUCCESS;
		}
		else
		{
			printf("SPI_Wait_Slave_Ready is not ready.\n");
		}
	}
	printf("SPI_Wait_Slave_Ready time out!!!\n");
	return HI_FAILURE;
}

//等待从机处于空闲状态
HI_S32 SPI_Wait_Slave_Idle()
{
	int i = 0;

	for (i = 0; i < 0xffffff; i++)
	{
		if (1 == SPI_GET_GPIO_Value())
		{
			printf("Slave is Idle, Ready to receive command!!!\n");
			return HI_SUCCESS;
		}
		else
		{
			printf("SPI_Wait_Slave_Idle is not Idle\n");
		}
	}
	printf("SPI_Wait_Slave_Idle time out!!!\n");
	return HI_FAILURE;
}

static int g_fd = -1;
static int i = 0;
//获取地址
HI_S32 SPI_Get_K210_DataAddr(unsigned char *addr)
{
		int i;
		HI_U32 value;
		int ret;
		char file_name[] = "/dev/spidev1.1";
//		struct spi_ioc_transfer mesg[1];
		unsigned char buf_tx[8];
		unsigned char buf_rx[8];
		memset(buf_tx, 0, sizeof(buf_tx));
		memset(buf_rx, 0, sizeof(buf_rx));

		//判断从机是不是做好接收命令的准备
		if (HI_SUCCESS != SPI_Wait_Slave_Idle())
		{
			return HI_FAILURE;
		}	
		
		//cmd = read_config
		buf_tx[0] = 0x01;
		//addr = 0x08
		buf_tx[1] = 0x08;
		buf_tx[2] = 0x08 >> 8;
		buf_tx[3] = 0x08 >> 16;
		buf_tx[4] = 0x08 >> 24;
		//len = 4,要获取地址的长度
		buf_tx[5] = 0x04;
		buf_tx[6] = 0x04 >> 8;
		buf_tx[7] = 0;
		for (i = 0; i < 7; i++)
		{
			buf_tx[7] += buf_tx[i];
			printf("buf_tx[%d] = %x\n", i, buf_tx[i]);
		}
		printf("buf_tx[7] = %x\n", buf_tx[7]);
		
		g_fd = open(file_name, O_RDWR);
		if (g_fd < 0)
		{
			printf("Open %s error!\n",file_name);
			return HI_FAILURE;
		}	
		
		value = SPI_MODE_0;
		ret = ioctl(g_fd, SPI_IOC_WR_MODE, &value);
		if (ret < 0)
		{
			printf("SPI_IOC_WR_MODE set error\n");
			close(g_fd);
			return HI_FAILURE;
		}

		value = 1000000;
		ret = ioctl(g_fd, SPI_IOC_WR_MAX_SPEED_HZ, &value);
		if (ret < 0)
		{
			printf("SPI_IOC_WR_MAX_SPEED_HZ set error\n");
			close(g_fd);
			return HI_FAILURE;
		}
	
		value = 8;
		ret = ioctl(g_fd, SPI_IOC_WR_BITS_PER_WORD, &value);
		if (ret < 0)
		{
			printf("SPI_IOC_WR_BITS_PER_WORD set error\n");
			close(g_fd);
			return HI_FAILURE;
		}

		//半双工读写
		write(g_fd, buf_tx, sizeof(buf_tx));

		//等待从机准备好
		SPI_Wait_Slave_Ready();
		
		read(g_fd, buf_rx, sizeof(buf_rx));
		
		printf("************************************************\n");
		for (i = 0; i < 4; i++)
		{
			printf("buf_rx[%d] = %x\n", i, buf_rx[i]);
			addr[i] = buf_rx[i];
		}
		printf("*************************************************\n");

		if ((0 == buf_rx[0]) && (0 == buf_rx[1]) && (0 == buf_rx[2]) && (0 == buf_rx[3]))
		{
			close(g_fd);
			return HI_FAILURE;
		}
		else if ((0xff == buf_rx[0]) && (0xff == buf_rx[1]) && (0xff == buf_rx[2]) && (0xff == buf_rx[3]))
		{
			close(g_fd);
			return HI_FAILURE;
		}
	
		close(g_fd);
		return HI_SUCCESS;
}

//转换地址
HI_VOID SPI_Addr_Flip(unsigned char *p, unsigned int *addr)
{
	int i = 0;
	unsigned char temp;
	
	for (i = 0; i < 4; i++)
	{
		printf("orignal : *(p + %d) = %x\n", i, *(p + i));
	}
	
	temp = *p;
	*p = *(p + 3);
	*(p + 3) = temp;

	temp = *(p + 1);
	*(p + 1) = *(p + 2);
	*(p + 2) = temp;

	for (i = 0; i < 4; i++)
	{
		printf("Fliped : *(p + %d) = %02x\n", i, *(p + i));
		*addr |= p[i] << ((3 - i) * 8); 
	}
	
	printf("INT ADDR: *addr = %08x\n", *addr);
}

/**************************************************************
* cmd: 命令的类型(根据k210程序而来);
*		0：WRITE_CONFIG
*		1：READ_CONFIG
*		2：WRITE_DATA_BYTE
*		3: READ_DATA_BYTE
*		4: WRITE_DATA_BLOCK
*		5: READ_DATA_BLOCK	
* len：要R/W的数据的长度;
* addr:
*		if (read == cmd)
*			则表示从k210芯片获取能存图片的地址;
*		if (write == cmd)
*			则表示把数据写到210的这个地址中去;
* return:
*		HI_FAILURE:命令发送失败;
*		HI_SUCCESS:命令发送成功;
***************************************************************/
static int icount = 0;
HI_S32 SPI_Send_CmdToK210(int cmd, int len, unsigned char *addr)
{
	int i;
	HI_U32 value;
	int ret;
	char file_name[] = "/dev/spidev1.1";
	unsigned char buf_tx[8];
	memset(buf_tx, 0, sizeof(buf_tx));
#if 1
	//发送命令前做修改
	if (cmd > 3)
	{
		if (len > 0x100000)
			len = 0x100000;
		*addr &=  0xf0;
		len = len >> 4;
	}
#endif	
	//判断从机是否做好接收命令的准备
	printf("*****************************************************\n");
	if (HI_SUCCESS != SPI_Wait_Slave_Idle())
	{
		return HI_FAILURE;
	}
	//cmd
	buf_tx[0] = cmd;
	//addr
	buf_tx[1] = *addr;
	buf_tx[2] = *(addr + 1);
	buf_tx[3] = *(addr + 2);
	buf_tx[4] = *(addr + 3);
	//len 
	buf_tx[5] = len;
	buf_tx[6] = len >> 8;
	buf_tx[7] = 0;
	for (i = 0; i < 7; i++)
	{
		buf_tx[7] += buf_tx[i];
	}

	g_fd = open(file_name, O_RDWR);
	if (g_fd < 0)
	{
		printf("Open %s error!\n",file_name);
		return HI_FAILURE;
	}
	
	value = SPI_MODE_0;
	ret = ioctl(g_fd, SPI_IOC_WR_MODE, &value);			//SPI_IOC_WR_MODE
	if (ret < 0)
	{
		printf("SPI_IOC_WR_MODE set error\n");
		close(g_fd);
		return HI_FAILURE;
	}

	value = 1000000;
	ret = ioctl(g_fd, SPI_IOC_WR_MAX_SPEED_HZ, &value); //SPI_IOC_WR_MAX_SPEED_HZ
	if (ret < 0)
	{
		printf("SPI_IOC_WR_MAX_SPEED_HZ set error\n");
		close(g_fd);
		return HI_FAILURE;
	}
	
	value = 8;
	ret = ioctl(g_fd, SPI_IOC_WR_BITS_PER_WORD, &value); //SPI_IOC_WR_BITS_PER_WORD
	if (ret < 0)
	{
		printf("SPI_IOC_WR_BITS_PER_WORD set error\n");
		close(g_fd);
		return HI_FAILURE; 
	}
	
	for (i = 0; i < 8; i++)
	{
		printf("buf_tx[%d] = %x\n", i, buf_tx[i]);
	}	
	
	write(g_fd, buf_tx, sizeof(buf_tx));

	icount++;
	printf("icount = %d\n", icount);
	
	SPI_Wait_Slave_Ready();
	printf("*****************************************************\n");
	
	close(g_fd);
	return HI_SUCCESS;
}	

/*****************************************************
* data: 要发送的数据;
* len：要发送数据的长度;
* return:
*		HI_FAILURE:命令发送失败;
*		HI_SUCCESS:命令发送成功;
*****************************************************/
HI_S32 SPI_Send_DataToK210(unsigned char *data, int len)
{
	int i;
	int ret;
	HI_U32 value;
	unsigned char *p = NULL;
	char file_name[] = "/dev/spidev1.1";
	p = malloc(len * sizeof(char));
	memset(p, 0, len);

	for (i = 0; i < len; i++)
	{
		*(p + i) = data[i];
		//printf(">>>>>>>>>>*(p + %d) = %x<<<<<<<<\n", i, *(p + i));
	}
	g_fd = open(file_name, O_RDWR);
	if (g_fd < 0)
	{
		printf("Open %s error!\n",file_name);
		free(p);
		return HI_FAILURE;
	}	

	value = SPI_MODE_0;
	ret = ioctl(g_fd, SPI_IOC_WR_MODE, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_WR_MODE set error\n");
		free(p);
		close(g_fd);
		return HI_FAILURE;
	}

	value = 1000000;
	ret = ioctl(g_fd, SPI_IOC_WR_MAX_SPEED_HZ, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_WR_MAX_SPEED_HZ set error\n");
		free(p);
		close(g_fd);
		return HI_FAILURE;
	}
	
	value = 8;
	ret = ioctl(g_fd, SPI_IOC_WR_BITS_PER_WORD, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_WR_BITS_PER_WORD set error\n");
		free(p);
		close(g_fd);
		return HI_FAILURE;
	}
	
	write(g_fd, p, len);
	free(p);
	close(g_fd);

	return HI_SUCCESS;
}

#if 0
HI_S32 SPI_Wait_Slave_Ready()
{

	int flag = 0;
	int i = 0;
	FILE *fp;
	unsigned char buf[10];
	
	//打开输出管脚文件
	fp = fopen("/sys/class/gpio/export", "w");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/export.\n");
		return HI_FAILURE;
	}
	fprintf(fp, "%d", SPI_Ready_Num);
	fclose(fp);
	
	//设置方向
	fp = fopen("/sys/class/gpio/gpio43/direction", "rb+");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/gpio43/direction.\n");
		return HI_FAILURE;
	}
	fprintf(fp, "in");
	fclose(fp);
	
	//获取管脚值
	fp = fopen("/sys/class/gpio/gpio43/value", "rb+");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/gpio43/value.\n");
		return HI_FAILURE;		
	}
	
	for (i = 0; i < 0xffffff; i++)
	{
		printf("i = %d\n", i);
		memset(buf, 0, 10);
		fread(buf, sizeof(char), sizeof(buf) - 1, fp);
		if (0 == (buf[0] - 48))
		{
			printf("low\n");
			flag = 1;
			break;
		}
		else
		{
			printf("high\n");
			flag = 0;
		}
	}
	fclose(fp);
	
	//关闭GPIO文件
	fp = fopen("/sys/class/gpio/unexport", "w");
	if (NULL == fp)
	{
		printf("cannot open /sys/class/gpio/unexport.\n");
		return HI_FAILURE;
	}
	fprintf(fp, "%d", SPI_Ready_Num);
	fclose(fp);

	if (flag)
	{
		printf("Slave is Ready!!!\n");
		return HI_SUCCESS;
	}
	printf(">>>>>>>Time out<<<<<<<!!!");	
	return HI_FAILURE;
}
#endif


/*************************************************************************
*发送视频流给从机k210,write一次只能传输4096字节,k210只能接收16倍数的字节数
**************************************************************************/
unsigned char u8Addr[4];  //k210里能存视频数据的地址
HI_S32 SPI_Send_StreamToK210(FILE* fpJpegFile, VENC_STREAM_S* pstStream)
{
	int i, j;
	int len = 0;
	int i16len_quotient = 0;
	int i16len_remainder = 0;
	int i16len_result = 0;
	VENC_PACK_S*  pstData;
	int quotient; /*商*/
	int remainder;/*余数*/
	unsigned char* Pvalidaddr = NULL;
	unsigned char buf[16] = {0};
	//发送写数据块命令
	for (i = 0; i < pstStream->u32PackCount; i++)
	{
		pstData = &pstStream->pstPack[i];
		Pvalidaddr = pstData->pu8Addr + pstData->u32Offset;
		len = (pstData->u32Len - pstData->u32Offset);		//获取数据的长度
		
		//发送写数据块命令
		if (len < 4096)
		{
			if(len < 16)
			{
				memset(buf, 0, sizeof(buf));
				memcpy(buf, Pvalidaddr, len);
				memset((buf + len), 0xff, (16 - len));
				SPI_Send_CmdToK210(4, 16, u8Addr);
				SPI_Send_DataToK210(buf, 16);    //SPI_Send_DataToK210(Pvalidaddr, 16);
			}
			else
			{
				i16len_quotient = len / 16;   //16的倍数
				i16len_remainder = len % 16;  //16的余数
				
				SPI_Send_CmdToK210(4, (i16len_quotient * 16), u8Addr);
				SPI_Send_DataToK210(Pvalidaddr, (i16len_quotient * 16));
				
				if (0 != i16len_remainder)
				{
					memset(buf, 0, sizeof(buf));
					memcpy(buf, (Pvalidaddr + (16 * i16len_quotient)), i16len_remainder);
					memset((buf + i16len_remainder), 0xff, (16 - i16len_remainder));
					SPI_Send_CmdToK210(4, 16, u8Addr);
					SPI_Send_DataToK210(buf, 16); 			//SPI_Send_DataToK210(Pvalidaddr, len);
				}
			}
		}	
#if 1
		else
		{
			quotient = (len / 4096);  //商
			remainder = (len % 4096); //余数
			
			//把商的倍数的数据传送完
			for (j = 0; j < quotient; j++)
			{
				SPI_Send_CmdToK210(4, 4096, u8Addr);
				SPI_Send_DataToK210(Pvalidaddr + (4096 * j), 4096);
			}
			//把余数的数据传送完
			if(0 != remainder)
			{
				i16len_result = len - (quotient * 4096);
				if (i16len_result < 16)
				{
					memset(buf, 0, sizeof(buf));
					memcpy(buf, (Pvalidaddr + (4096 * quotient)), i16len_result);
					memset((buf + i16len_result), 0xff, (16 - i16len_result));
					SPI_Send_CmdToK210(4, 16, u8Addr);
					SPI_Send_DataToK210(buf, 16); 
				}
				else
				{
					i16len_quotient = i16len_result / 16;
					i16len_remainder = i16len_result % 16;
					
					SPI_Send_CmdToK210(4, (i16len_quotient * 16), u8Addr);
					SPI_Send_DataToK210((Pvalidaddr + (4096 * quotient)), (i16len_quotient * 16));

					if (0 != i16len_remainder)
					{
						memset(buf, 0, sizeof(buf));
						memcpy(buf, ((Pvalidaddr + (4096 * quotient)) + (16 * i16len_quotient)), i16len_remainder);
						
						memset((buf + i16len_remainder), 0xff, (16 - i16len_remainder));
						
						SPI_Send_CmdToK210(4, 16, u8Addr);
						SPI_Send_DataToK210(buf, 16); 			//SPI_Send_DataToK210(Pvalidaddr, len);
					}
				}
			}
		}
#endif
	}
	
	return HI_SUCCESS;
}

#if 1
static int count = 0;
HI_S32 SAMPLE_COMM_VENC_SaveJPEG(FILE* fpJpegFile, VENC_STREAM_S* pstStream)
{
	int i = 0; 
	int len = 0;
	unsigned int  i32Addr = 0; //把字符地址转化为整形的地址
	VENC_PACK_S*  pstData;
	int quotient; /*商*/
	int remainder;/*余数*/
	unsigned char* Pvalidaddr = NULL;
	unsigned char test[16] = {
								0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
								0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0xef
							  };
	if (0 == count)
	{	
		//获取210可以提供的地址，只获取一次，再次进来就不再执行
		if (HI_FAILURE == SPI_Get_K210_DataAddr(u8Addr))
		{
			printf("Get Addr Error Try Again!!!\n");
			return HI_SUCCESS;
		}
		else
		{	
#if 0
			pstData = &pstStream->pstPack[0];
			Pvalidaddr = pstData->pu8Addr + pstData->u32Offset;
			len = (pstData->u32Len - pstData->u32Offset);

			//发送写块命令,在地址为u8Addr写一个字节数据
			SPI_Send_CmdToK210(4, 16, u8Addr);
			//发送数据给k210
			SPI_Send_DataToK210(test, 16);
			//SPI_Send_StreamToK210(fpJpegFile, pstStream);
#endif
			count++;
		}
	}
#if 1
	//发送数据
	SPI_Send_StreamToK210(fpJpegFile, pstStream);
	//发送写块命令,在地址为u8Addr写一个字节数据
	//SPI_Send_CmdToK210(4, 16, u8Addr);
	//发送数据给k210
	//SPI_Send_DataToK210(test, 16);
#endif	

	return HI_SUCCESS;
}
#endif

#if 0
HI_S32 SAMPLE_COMM_VENC_SaveJPEG(FILE* fpJpegFile, VENC_STREAM_S* pstStream)
{
    VENC_PACK_S*  pstData;
    int i, len;
	int j = 0;
	HI_U32 value;
	int ret;
	int quotient, remainder;/*商，余数*/
	__u64 *p = NULL;
	__u64 *p_read = NULL;
	char *point1 = NULL;
	char *point2 = NULL;
	unsigned char* Pvalidaddr = NULL;
	char file_name[] = "/dev/spidev1.1";
	struct spi_ioc_transfer mesg[1];
	unsigned char buf_tx[8];
	unsigned char buf_rx[8];
#if 0
	//mesg[0].tx_buf = (__u32)buff;
	mesg[0].rx_buf = NULL;
	//mesg[0].len = 1;
	mesg[0].speed_hz = 25000000;
	mesg[0].bits_per_word = 8;
	mesg[0].cs_change = 1;
#endif	

#if 1
	memset(buf_tx, 0, sizeof(buf_tx));
	memset(buf_rx, 0, sizeof(buf_rx));

	//cmd = read_config
	buf_tx[0] = 0x01;
	//addr = 0x08
	buf_tx[1] = 0x08;
	buf_tx[2] = 0x08 >> 8;
	buf_tx[3] = 0x08 >> 16;
	buf_tx[4] = 0x08 >> 24;
	//len = 4
	buf_tx[5] = 0x04;
	buf_tx[6] = 0x04 >> 8;
	buf_tx[7] = 0;
	for (i = 0; i < 7; i++)
	{
		buf_tx[7] += buf_tx[i];
		printf("buf_tx[%d] = %x\n", i, buf_tx[i]);
	}
	printf("buf_tx[7] = %x\n", buf_tx[7]);
	
	memset(mesg, 0, sizeof(mesg));
	//memcpy(p, Pvalidaddr, len);
	mesg[0].tx_buf = (__u64)buf_tx;
	mesg[0].rx_buf = (__u64)buf_rx; //设置为NULL
	mesg[0].len = 8;
	mesg[0].speed_hz = 1000000;
	mesg[0].bits_per_word = 8;
#endif

	g_fd = open(file_name, O_RDWR);
	if (g_fd < 0)
	{
		printf("Open %s error!\n",file_name);
		return HI_FAILURE;
	}	

	value = SPI_MODE_0;

	ret = ioctl(g_fd, SPI_IOC_WR_MODE, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_WR_MODE set error\n");
		close(g_fd);
		return HI_FAILURE;
	}
	//读模式设置
	ret = ioctl(g_fd, SPI_IOC_RD_MODE, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_RD_MODE set error\n");
		close(g_fd);
		return HI_FAILURE;	
	}

	value = 1000000;
	ret = ioctl(g_fd, SPI_IOC_WR_MAX_SPEED_HZ, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_WR_MAX_SPEED_HZ set error\n");
		close(g_fd);
		return HI_FAILURE;
	}

	//读模式设置
	ret = ioctl(g_fd, SPI_IOC_RD_MAX_SPEED_HZ, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_RD_MAX_SPEED_HZ set error\n");
		close(g_fd);
		return HI_FAILURE;
	}	
	
	value = 8;
	ret = ioctl(g_fd, SPI_IOC_WR_BITS_PER_WORD, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_WR_BITS_PER_WORD set error\n");
		close(g_fd);
		return HI_FAILURE;
	}

	//读模式设置/
	ret = ioctl(g_fd, SPI_IOC_RD_BITS_PER_WORD, &value);
	if (ret < 0)
	{
		printf("SPI_IOC_RD_BITS_PER_WORD set error\n");
		close(g_fd);
		return HI_FAILURE;
	}
	
	ret = ioctl(g_fd, SPI_IOC_MESSAGE(1), mesg);
	if (ret != mesg[0].len || ret < 0)
	{
		printf("SPI_IOC_MESSAGE(1) send error!!!\n");
		close(g_fd);
		return HI_FAILURE;
	}
	
	printf("************************************************\n");
	for (i = 0; i < 4; i++)
	{
		printf("buf_rx[%d] = %x\n", i, buf_rx[i]);
	}
	printf("*************************************************\n");
#if 0
    for (i = 0; i < pstStream->u32PackCount; i++)
    {
#if 0
        pstData = &pstStream->pstPack[i];
        fwrite(pstData->pu8Addr + pstData->u32Offset, pstData->u32Len - pstData->u32Offset, 1, fpJpegFile);
        fflush(fpJpegFile);
#endif

#if 0
		pstData = &pstStream->pstPack[i];		
		Pvalidaddr = pstData->pu8Addr + pstData->u32Offset; //获取数据的地址
		len = (pstData->u32Len - pstData->u32Offset);		//获取数据的长度
		printf("enter!!!!\n");
		if (len < 4096)
		{
/*
			unsigned char buf_tx[8];
			memset(buf_tx, 0, sizeof(buf_tx));
			//读命令
			buf_tx[0] = 0x01;
			//地址
			buf_tx[1] = 0x08;
			buf_tx[5] = 0x08;
			buf_tx[7] = (buf_tx[0] + buf_tx[1] + buf_tx[5]);

			ret = write(g_fd, buf_tx, sizeof(buf_tx));
			if (ret != sizeof(buf_tx))
			{
				printf("len < 4096 write error!!!\n");
			}
*/	
			ret = write(g_fd, Pvalidaddr, len);
			if (ret != len)
			{
				printf("len < 4096 write error!!!\n");
				return HI_FAILURE;
			}
		}
		else
		{
			quotient = (len / 4096); 		//商
			remainder = (len % 4096);		//余数
			//发送商的部分
			for (j = 0; j < quotient; j++)
			{
				ret = write(g_fd, Pvalidaddr + (4096 * quotient), 4096);
				if (ret != 4096)
				{
					printf("len > 4096 quotient write error!!!\n");
					return HI_FAILURE;
				}
			}
			//发送剩余的部分
			if (0 != remainder)
			{
				ret = write(g_fd, Pvalidaddr + (4096 * quotient), len - (quotient * 4096));
				if (ret != (len - (quotient * 4096)))
				{
					printf("len > 4096 remainder write error!!!\n");
					return HI_FAILURE;
				}
			}
		}
			
#endif 
//这里打开
#if 1
		/*向SPI写数据*/
		pstData = &pstStream->pstPack[i];
		Pvalidaddr = pstData->pu8Addr + pstData->u32Offset; //获取数据的地址
		
		len = (pstData->u32Len - pstData->u32Offset);		//获取数据的长度
		//spi一次传输数据不能大于4096字节(4kb)
		if (len < 4096)
		{
#if 0
			point2 = p = malloc(len * sizeof(char));
			//point1 = p_read = malloc(len * sizeof(char));
			memset(p, 0, len);
			//memset(p_read, 0, len);
#endif
//这里打开
#if 1
			ret = ioctl(g_fd, SPI_IOC_MESSAGE(1), mesg);
			if (ret != mesg[0].len || ret < 0)
			{
				printf("SPI_IOC_MESSAGE(1) send error!!!\n");
				close(g_fd);
				return HI_FAILURE;
			}
#endif		
			//free(p);
			//free(p_read);
		}
#if 0
		else
		{
			quotient = (len / 4096); //商
			remainder = (len % 4096);//余数
			//传输4096的整数倍的数据
			for (j = 0; j < quotient; j++)
			{
				point2 = p = malloc(4096 * sizeof(char));
				point1 = p_read = malloc(4096 * sizeof(char));
				memset(p, 0, 4096);
				memset(p_read, 0, 4096);
				
				memcpy(p, (Pvalidaddr + (4096 * j)), 4096);
				mesg[0].tx_buf = p;
				mesg[0].rx_buf = p_read;
				mesg[0].len = 4096;
				mesg[0].speed_hz = 25000000;
				mesg[0].bits_per_word = 8;
				mesg[0].cs_change = 1;
				//reverse8(p, mesg[0].len);
				ret = ioctl(g_fd, SPI_IOC_MESSAGE(1), mesg);
				if (ret != mesg[0].len || ret < 0)
				{
					printf("SPI_IOC_MESSAGE(1) send error!!!\n");
					close(g_fd);
					return HI_FAILURE;
				}

				free(p);
				free(p_read);
			}
			//传输余数剩余的部分
			if(0 != remainder)
			{
				point2 = p = malloc((len - (quotient * 4096)) * sizeof(char));
				point1 = p_read = malloc((len - (quotient * 4096)) * sizeof(char));
				memset(p, 0, (len - (quotient * 4096)));
				memset(p_read, 0, (len - (quotient * 4096)));
				
				memcpy(p, (Pvalidaddr + (4096 * quotient)), len - (quotient * 4096));
				mesg[0].tx_buf = p;
				mesg[0].rx_buf = p_read;
				mesg[0].len = len - (quotient * 4096);
				mesg[0].speed_hz = 25000000;
				mesg[0].bits_per_word = 8;
				mesg[0].cs_change = 1;
				//reverse8(p, mesg[0].len);
				ret = ioctl(g_fd, SPI_IOC_MESSAGE(1), mesg);
				if (ret != mesg[0].len || ret < 0)
				{
					printf("SPI_IOC_MESSAGE(1) send error!!!\n");
					close(g_fd);
					return HI_FAILURE;
				}
				free(p);
				free(p_read);
			}
		}
#endif

#if 0		
		for (j = 0; j < (pstData->u32Len - pstData->u32Offset); j++)
		{
			memcpy(buff, (Pvalidaddr+j), 1);
			reverse8(buff, mesg[0].len);
			//发送数据
			ret = ioctl(g_fd, SPI_IOC_MESSAGE(1), mesg);
			if (ret != mesg[0].len)
			{
				printf("SPI_IOC_MESSAGE(1) send error!!!\n");
				close(g_fd);
				return HI_FAILURE;
			}
		}
#endif

#endif
    }
#endif
	close(g_fd);
	
    return HI_SUCCESS;
}
#endif
/******************************************************************************
* funciton : the process of physical address retrace
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveFile(FILE* pFd, VENC_STREAM_BUF_INFO_S *pstStreamBuf, VENC_STREAM_S* pstStream)
{
    HI_U32 i;
    HI_U32 u32SrcPhyAddr;
    HI_U32 u32Left;
    HI_S32 s32Ret = 0;
	

	printf("This is running1\n");
    for(i=0; i<pstStream->u32PackCount; i++)
    {
    	//判断码流的大小是否大于buff的内存大小
        if (pstStream->pstPack[i].u32PhyAddr + pstStream->pstPack[i].u32Len >=
                pstStreamBuf->u32PhyAddr + pstStreamBuf->u32BufSize)
            {
                if (pstStream->pstPack[i].u32PhyAddr + pstStream->pstPack[i].u32Offset >=
                    pstStreamBuf->u32PhyAddr + pstStreamBuf->u32BufSize)
                {
                    /* physical address retrace in offset segment */
                    u32SrcPhyAddr = pstStreamBuf->u32PhyAddr +
                                    ((pstStream->pstPack[i].u32PhyAddr + pstStream->pstPack[i].u32Offset) - 
                                    (pstStreamBuf->u32PhyAddr + pstStreamBuf->u32BufSize));

                    s32Ret = fwrite ((void *)u32SrcPhyAddr, pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset, 1, pFd);
                    if(s32Ret<0)
                    {
                        SAMPLE_PRT("fwrite err %d\n", s32Ret);
                        return s32Ret;
                    }
                }
                else
                {
                    /* physical address retrace in data segment */
                    u32Left = (pstStreamBuf->u32PhyAddr + pstStreamBuf->u32BufSize) - pstStream->pstPack[i].u32PhyAddr;

                    s32Ret = fwrite((void *)(pstStream->pstPack[i].u32PhyAddr + pstStream->pstPack[i].u32Offset),
                                 u32Left - pstStream->pstPack[i].u32Offset, 1, pFd);
                    if(s32Ret<0)
                    {
                        SAMPLE_PRT("fwrite err %d\n", s32Ret);
                        return s32Ret;
                    }
                    
                    s32Ret = fwrite((void *)pstStreamBuf->u32PhyAddr, pstStream->pstPack[i].u32Len - u32Left, 1, pFd);
                    if(s32Ret<0)
                    {
                        SAMPLE_PRT("fwrite err %d\n", s32Ret);
                        return s32Ret;
                    }
                }
            }
            else
            {
                /* physical address retrace does not happen */
                s32Ret = fwrite ((void *)(pstStream->pstPack[i].u32PhyAddr + pstStream->pstPack[i].u32Offset),
                              pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset, 1, pFd);
                if(s32Ret<0)
                {
                    SAMPLE_PRT("fwrite err %d\n", s32Ret);
                    return s32Ret;
                }
            }
			//极有可能数据先写到缓冲区，然后再把数据写到文件
			fflush(pFd);
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : save snap stream
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveSnap(VENC_STREAM_BUF_INFO_S* pstStreamBufInfo,VENC_STREAM_S* pstStream)
{
    char acFile[FILE_NAME_LEN]  = {0};
    char acFile_dcf[FILE_NAME_LEN]  = {0};
    FILE* pFile;
    HI_S32 s32Ret;

    snprintf(acFile, FILE_NAME_LEN, "snap_%d.jpg", gs_s32SnapCnt);
    snprintf(acFile_dcf, FILE_NAME_LEN, "snap_%d.thm", gs_s32SnapCnt);
    pFile = fopen(acFile, "wb");
    if (pFile == NULL)
    {
        SAMPLE_PRT("open file err\n");
        return HI_FAILURE;
    }
    #ifndef __HuaweiLite__
    s32Ret = SAMPLE_COMM_VENC_SaveJPEG(pFile, pstStream);
    #else
	s32Ret = SAMPLE_COMM_VENC_SaveFile(pFile,pstStreamBufInfo,pstStream);
    #endif
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("save snap picture failed!\n");
        return HI_FAILURE;
    }
    fclose(pFile);
    gs_s32SnapCnt++;
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : save stream
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SaveStream(PAYLOAD_TYPE_E enType, FILE* pFd, VENC_STREAM_S* pstStream)
{
    HI_S32 s32Ret;
    if (PT_H264 == enType)
    {
        s32Ret = SAMPLE_COMM_VENC_SaveH264(pFd, pstStream);
    }
    else if (PT_MJPEG == enType)
    {
        s32Ret = SAMPLE_COMM_VENC_SaveMJpeg(pFd, pstStream);
    }
    else if (PT_H265 == enType)
    {
        s32Ret = SAMPLE_COMM_VENC_SaveH265(pFd, pstStream);
    }
	else if (PT_JPEG == enType)
    {
        s32Ret = SAMPLE_COMM_VENC_SaveJPEG(pFd, pstStream);
    }
    else
    {
        return HI_FAILURE;
    }
    return s32Ret;
}

/******************************************************************************
* funciton : Start venc stream mode (h264, mjpeg)
* note      : rate control parameter need adjust, according your case.
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_Start(VENC_CHN VencChn, PAYLOAD_TYPE_E enType, VIDEO_NORM_E enNorm, PIC_SIZE_E enSize, SAMPLE_RC_E enRcMode, HI_U32  u32Profile, ROTATE_E enRotate)
{
	/*
		VencChn = 0
		enType = PT_H264
		enNorm = VIDEO_ENCODING_MODE_PAL
		enSize = PIC_HD1080
		enRcMode = SAMPLE_RC_CBR
		u32Profile = 0
		enRotate = ROTATE_NONE
	*/
    HI_S32 s32Ret;
    VENC_CHN_ATTR_S stVencChnAttr;
    VENC_ATTR_H264_S stH264Attr;
    VENC_ATTR_H264_CBR_S    stH264Cbr;
    VENC_ATTR_H264_VBR_S    stH264Vbr;
	VENC_ATTR_H264_AVBR_S    stH264AVbr;
    VENC_ATTR_H264_FIXQP_S  stH264FixQp;
    VENC_ATTR_H265_S        stH265Attr;
    VENC_ATTR_H265_CBR_S    stH265Cbr;
    VENC_ATTR_H265_VBR_S    stH265Vbr;
    VENC_ATTR_H265_AVBR_S    stH265AVbr;
    VENC_ATTR_H265_FIXQP_S  stH265FixQp;
    VENC_ATTR_MJPEG_S stMjpegAttr;
    VENC_ATTR_MJPEG_FIXQP_S stMjpegeFixQp;
    VENC_ATTR_JPEG_S stJpegAttr;
    SIZE_S stPicSize;
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enNorm, enSize, &stPicSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Get picture size failed!\n");
        return HI_FAILURE;
    }

    if (ROTATE_90 == enRotate || ROTATE_270 == enRotate)
    {
        HI_U32 u32Temp;
        u32Temp = stPicSize.u32Height;
        stPicSize.u32Height = stPicSize.u32Width;
        stPicSize.u32Width = u32Temp;
    }
    /******************************************
     step 1:  Create Venc Channel    
     ******************************************/
    stVencChnAttr.stVeAttr.enType = enType;
    switch (enType)
    {
        case PT_H264:
        {
            stH264Attr.u32MaxPicWidth = stPicSize.u32Width; //1920
            stH264Attr.u32MaxPicHeight = stPicSize.u32Height; //1080
            stH264Attr.u32PicWidth = stPicSize.u32Width;/*the picture width*/
            stH264Attr.u32PicHeight = stPicSize.u32Height;/*the picture height*/            
            stH264Attr.u32BufSize  = stPicSize.u32Width * stPicSize.u32Height * 3/2;/*stream buffer size*/
            stH264Attr.u32Profile  = u32Profile;/*0: baseline; 1:MP; 2:HP;  3:svc_t */
            stH264Attr.bByFrame = HI_TRUE;/*get stream mode is slice mode or frame mode?*/
            //stH264Attr.u32BFrameNum = 0;/* 0: not support B frame; >=1: number of B frames */
            //stH264Attr.u32RefNum = 1;/* 0: default; number of refrence frame*/
            memcpy(&stVencChnAttr.stVeAttr.stAttrH264e, &stH264Attr, sizeof(VENC_ATTR_H264_S));
            if (SAMPLE_RC_CBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
                stH264Cbr.u32Gop            = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Cbr.u32StatTime       = 1; /* stream rate statics time(s) */
                stH264Cbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* input (vi) frame rate */
                stH264Cbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* target frame rate */
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH264Cbr.u32BitRate = 256; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH264Cbr.u32BitRate = 512;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH264Cbr.u32BitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH264Cbr.u32BitRate = 1024 * 2;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH264Cbr.u32BitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH264Cbr.u32BitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH264Cbr.u32BitRate = 1024 * 8;
                        break;
                    default :
                        stH264Cbr.u32BitRate = 1024 * 4;
                        break;
                }
                stH264Cbr.u32FluctuateLevel = 1; /* average bit rate */
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264Cbr, &stH264Cbr, sizeof(VENC_ATTR_H264_CBR_S));
            }
            else if (SAMPLE_RC_FIXQP == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;
                stH264FixQp.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264FixQp.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264FixQp.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264FixQp.u32IQp = 33;
                stH264FixQp.u32PQp = 33;
                stH264FixQp.u32BQp = 33;
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264FixQp, &stH264FixQp, sizeof(VENC_ATTR_H264_FIXQP_S));
            }
            else if (SAMPLE_RC_VBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
                stH264Vbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Vbr.u32StatTime = 1;
                stH264Vbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Vbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Vbr.u32MinQp = 10;
                stH264Vbr.u32MinIQp = 10;
                stH264Vbr.u32MaxQp = 40;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH264Vbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH264Vbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH264Vbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH264Vbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH264Vbr.u32MaxBitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH264Vbr.u32MaxBitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH264Vbr.u32MaxBitRate = 1024 * 8;	
                        break;
                    default :
                        stH264Vbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264Vbr, &stH264Vbr, sizeof(VENC_ATTR_H264_VBR_S));
            }
            else if (SAMPLE_RC_AVBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264AVBR;
                stH264AVbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264AVbr.u32StatTime = 1;
                stH264AVbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264AVbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH264AVbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH264AVbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH264AVbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH264AVbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH264AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH264AVbr.u32MaxBitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH264AVbr.u32MaxBitRate = 1024 * 8;	
                        break;
                    default :
                        stH264AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264AVbr, &stH264AVbr, sizeof(VENC_ATTR_H264_AVBR_S));
            }			
            else
            {
                return HI_FAILURE;
            }
        }
        break;
        case PT_MJPEG:
        {
            stMjpegAttr.u32MaxPicWidth = stPicSize.u32Width;
            stMjpegAttr.u32MaxPicHeight = stPicSize.u32Height;
            stMjpegAttr.u32PicWidth = stPicSize.u32Width;
            stMjpegAttr.u32PicHeight = stPicSize.u32Height;
            stMjpegAttr.u32BufSize = stPicSize.u32Width * stPicSize.u32Height * 3;
            stMjpegAttr.bByFrame = HI_TRUE;  /*get stream mode is field mode  or frame mode*/
            memcpy(&stVencChnAttr.stVeAttr.stAttrMjpege, &stMjpegAttr, sizeof(VENC_ATTR_MJPEG_S));
            if (SAMPLE_RC_FIXQP == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
                stMjpegeFixQp.u32Qfactor        = 50;
                stMjpegeFixQp.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stMjpegeFixQp.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                memcpy(&stVencChnAttr.stRcAttr.stAttrMjpegeFixQp, &stMjpegeFixQp,
                       sizeof(VENC_ATTR_MJPEG_FIXQP_S));
            }
            else if (SAMPLE_RC_CBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32StatTime       = 1;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32FluctuateLevel = 1;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 384 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 768 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 3 * 3;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 5 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 10 * 3;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 10 * 3;
                        break;
                    default :
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 10 * 3;
                        break;
                }
            }
            else if (SAMPLE_RC_VBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGVBR;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32StatTime = 1;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.fr32DstFrmRate = 5;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MinQfactor = 50;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxQfactor = 95;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 2 * 3;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 3 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 6 * 3;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 12 * 3;
                        break;
                    default :
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 4 * 3;
                        break;
                }
            }
            else
            {
                SAMPLE_PRT("cann't support other mode in this version!\n");
                return HI_FAILURE;
            }
        }
        break;
        case PT_JPEG:
            stJpegAttr.u32PicWidth  = stPicSize.u32Width;
            stJpegAttr.u32PicHeight = stPicSize.u32Height;
            stJpegAttr.u32MaxPicWidth  = stPicSize.u32Width;
            stJpegAttr.u32MaxPicHeight = stPicSize.u32Height;
            stJpegAttr.u32BufSize   = stPicSize.u32Width * stPicSize.u32Height * 3;
            stJpegAttr.bByFrame     = HI_TRUE;/*get stream mode is field mode  or frame mode*/
            stJpegAttr.bSupportDCF  = HI_FALSE;
            memcpy(&stVencChnAttr.stVeAttr.stAttrJpege, &stJpegAttr, sizeof(VENC_ATTR_JPEG_S));
            break;
        case PT_H265:
        {
            stH265Attr.u32MaxPicWidth = stPicSize.u32Width;
            stH265Attr.u32MaxPicHeight = stPicSize.u32Height;
            stH265Attr.u32PicWidth = stPicSize.u32Width;/*the picture width*/
            stH265Attr.u32PicHeight = stPicSize.u32Height;/*the picture height*/
            stH265Attr.u32BufSize  = stPicSize.u32Width * stPicSize.u32Height * 2;/*stream buffer size*/
            if (u32Profile >= 1)
            { 
			    stH265Attr.u32Profile = 0; 
			}/*0:MP; */
            else            
			{ 
			    stH265Attr.u32Profile  = u32Profile; 
			}/*0:MP*/
            stH265Attr.bByFrame = HI_TRUE;/*get stream mode is slice mode or frame mode?*/
            memcpy(&stVencChnAttr.stVeAttr.stAttrH265e, &stH265Attr, sizeof(VENC_ATTR_H265_S));
            if (SAMPLE_RC_CBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
                stH265Cbr.u32Gop            = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Cbr.u32StatTime       = 1; /* stream rate statics time(s) */
                stH265Cbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* input (vi) frame rate */
                stH265Cbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* target frame rate */
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH265Cbr.u32BitRate = 256; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH265Cbr.u32BitRate = 512;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH265Cbr.u32BitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH265Cbr.u32BitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH265Cbr.u32BitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH265Cbr.u32BitRate = 1024 * 8;
                        break;
                    default :
                        stH265Cbr.u32BitRate = 1024 * 4;
                        break;
                }
                stH265Cbr.u32FluctuateLevel = 1; /* average bit rate */
                memcpy(&stVencChnAttr.stRcAttr.stAttrH265Cbr, &stH265Cbr, sizeof(VENC_ATTR_H265_CBR_S));
            }
            else if (SAMPLE_RC_FIXQP == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265FIXQP;
                stH265FixQp.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265FixQp.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265FixQp.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265FixQp.u32IQp = 33;
                stH265FixQp.u32PQp = 33;
                stH265FixQp.u32BQp = 33;
                memcpy(&stVencChnAttr.stRcAttr.stAttrH265FixQp, &stH265FixQp, sizeof(VENC_ATTR_H265_FIXQP_S));
            }
            else if (SAMPLE_RC_VBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
                stH265Vbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Vbr.u32StatTime = 1;
                stH265Vbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Vbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Vbr.u32MinQp  = 10;
                stH265Vbr.u32MinIQp = 10;
                stH265Vbr.u32MaxQp  = 40;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH265Vbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH265Vbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH265Vbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH265Vbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH265Vbr.u32MaxBitRate = 1024 * 6;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH265Vbr.u32MaxBitRate = 1024 * 8;
                        break;
                    default :
                        stH265Vbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH265Vbr, &stH265Vbr, sizeof(VENC_ATTR_H265_VBR_S));
            }
			else if (SAMPLE_RC_AVBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265AVBR;
                stH265AVbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265AVbr.u32StatTime = 1;
                stH265AVbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265AVbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH265AVbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH265AVbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH265AVbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH265AVbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH265AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH265AVbr.u32MaxBitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH265AVbr.u32MaxBitRate = 1024 * 8;	
                        break;
                    default :
                        stH265AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264AVbr, &stH265AVbr, sizeof(VENC_ATTR_H265_AVBR_S));
            }	
            else
            {
                return HI_FAILURE;
            }
        }
        break;
        default:
            return HI_ERR_VENC_NOT_SUPPORT;
    }
    stVencChnAttr.stGopAttr.enGopMode  = VENC_GOPMODE_NORMALP;
    stVencChnAttr.stGopAttr.stNormalP.s32IPQpDelta = 0;
    s32Ret = HI_MPI_VENC_CreateChn(VencChn, &stVencChnAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_CreateChn [%d] faild with %#x! ===\n", \
                   VencChn, s32Ret);
        return s32Ret;
    }    
    s32Ret = HI_MPI_VENC_StartRecvPic(VencChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_StartRecvPic faild with%#x! \n", s32Ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}
/******************************************************************************
* funciton : Start venc stream mode (h264, mjpeg)
* note      : rate control parameter need adjust, according your case.
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_StartEx(VENC_CHN VencChn, PAYLOAD_TYPE_E enType, VIDEO_NORM_E enNorm, PIC_SIZE_E enSize, SAMPLE_RC_E enRcMode, HI_U32  u32Profile, VENC_GOP_ATTR_S *pstGopAttr)
{
    HI_S32 s32Ret;
    VENC_CHN_ATTR_S stVencChnAttr;
    VENC_ATTR_H264_S stH264Attr;
    VENC_ATTR_H264_CBR_S    stH264Cbr;
    VENC_ATTR_H264_VBR_S    stH264Vbr;
	VENC_ATTR_H264_AVBR_S    stH264AVbr;	
    VENC_ATTR_H264_FIXQP_S  stH264FixQp;
    VENC_ATTR_H265_S        stH265Attr;
    VENC_ATTR_H265_CBR_S    stH265Cbr;
    VENC_ATTR_H265_VBR_S    stH265Vbr;	
	VENC_ATTR_H265_AVBR_S    stH265AVbr;
    VENC_ATTR_H265_FIXQP_S  stH265FixQp;
    VENC_ATTR_MJPEG_S stMjpegAttr;
    VENC_ATTR_MJPEG_FIXQP_S stMjpegeFixQp;
    VENC_ATTR_JPEG_S stJpegAttr;
    SIZE_S stPicSize;
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enNorm, enSize, &stPicSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Get picture size failed!\n");
        return HI_FAILURE;
    }   
    /******************************************
     step 1:  Create Venc Channel
    ******************************************/
    stVencChnAttr.stVeAttr.enType = enType;
    switch (enType)
    {
        case PT_H264:
        {
            stH264Attr.u32MaxPicWidth = stPicSize.u32Width;
            stH264Attr.u32MaxPicHeight = stPicSize.u32Height;
            stH264Attr.u32PicWidth = stPicSize.u32Width;/*the picture width*/
            stH264Attr.u32PicHeight = stPicSize.u32Height;/*the picture height*/
            stH264Attr.u32BufSize  = stPicSize.u32Width * stPicSize.u32Height * 2;/*stream buffer size*/
            stH264Attr.u32Profile  = u32Profile;/*0: baseline; 1:MP; 2:HP;  3:svc_t */
            stH264Attr.bByFrame = HI_TRUE;/*get stream mode is slice mode or frame mode?*/
            //stH264Attr.u32BFrameNum = 0;/* 0: not support B frame; >=1: number of B frames */
            //stH264Attr.u32RefNum = 1;/* 0: default; number of refrence frame*/
            memcpy(&stVencChnAttr.stVeAttr.stAttrH264e, &stH264Attr, sizeof(VENC_ATTR_H264_S));
            if (SAMPLE_RC_CBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
                stH264Cbr.u32Gop            = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Cbr.u32StatTime       = 1; /* stream rate statics time(s) */
                stH264Cbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* input (vi) frame rate */
                stH264Cbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* target frame rate */
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH264Cbr.u32BitRate = 256; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH264Cbr.u32BitRate = 512;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH264Cbr.u32BitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH264Cbr.u32BitRate = 1024 * 2;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH264Cbr.u32BitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH264Cbr.u32BitRate = 1024 * 8;
                        break;
                    default :
                        stH264Cbr.u32BitRate = 1024 * 4;
                        break;
                }
                stH264Cbr.u32FluctuateLevel = 1; /* average bit rate */
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264Cbr, &stH264Cbr, sizeof(VENC_ATTR_H264_CBR_S));
            }
            else if (SAMPLE_RC_FIXQP == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;
                stH264FixQp.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264FixQp.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264FixQp.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264FixQp.u32IQp = 20;
                stH264FixQp.u32PQp = 23;
                stH264FixQp.u32BQp = 23;
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264FixQp, &stH264FixQp, sizeof(VENC_ATTR_H264_FIXQP_S));
            }
            else if (SAMPLE_RC_VBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
                stH264Vbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Vbr.u32StatTime = 1;
                stH264Vbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Vbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264Vbr.u32MinQp = 10;
                stH264Vbr.u32MinIQp = 10;
                stH264Vbr.u32MaxQp = 40;
				
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH264Vbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH264Vbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH264Vbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH264Vbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH264Vbr.u32MaxBitRate = 1024 * 6;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH264Vbr.u32MaxBitRate = 1024 * 8;
                        break;
                    default :
                        stH264Vbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264Vbr, &stH264Vbr, sizeof(VENC_ATTR_H264_VBR_S));
            }
            else if (SAMPLE_RC_AVBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264AVBR;
                stH264AVbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264AVbr.u32StatTime = 1;
                stH264AVbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH264AVbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH264AVbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH264AVbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH264AVbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH264AVbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH264AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH264AVbr.u32MaxBitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH264AVbr.u32MaxBitRate = 1024 * 8;	
                        break;
                    default :
                        stH264AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264AVbr, &stH264AVbr, sizeof(VENC_ATTR_H264_AVBR_S));
            }				
            else
            {
                return HI_FAILURE;
            }
        }
        break;
        case PT_MJPEG:
        {
            stMjpegAttr.u32MaxPicWidth = stPicSize.u32Width;
            stMjpegAttr.u32MaxPicHeight = stPicSize.u32Height;
            stMjpegAttr.u32PicWidth = stPicSize.u32Width;
            stMjpegAttr.u32PicHeight = stPicSize.u32Height;
            stMjpegAttr.u32BufSize = stPicSize.u32Width * stPicSize.u32Height * 3;
            stMjpegAttr.bByFrame = HI_TRUE;  /*get stream mode is field mode  or frame mode*/
            memcpy(&stVencChnAttr.stVeAttr.stAttrMjpege, &stMjpegAttr, sizeof(VENC_ATTR_MJPEG_S));
            if (SAMPLE_RC_FIXQP == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
                stMjpegeFixQp.u32Qfactor        = 90;
                stMjpegeFixQp.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stMjpegeFixQp.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                memcpy(&stVencChnAttr.stRcAttr.stAttrMjpegeFixQp, &stMjpegeFixQp,
                       sizeof(VENC_ATTR_MJPEG_FIXQP_S));
            }
            else if (SAMPLE_RC_CBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32StatTime       = 1;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32FluctuateLevel = 1;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 384 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 768 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 3 * 3;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 5 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 10 * 3;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 10 * 3;
                        break;
                    default :
                        stVencChnAttr.stRcAttr.stAttrMjpegeCbr.u32BitRate = 1024 * 10 * 3;
                        break;
                }
            }
            else if (SAMPLE_RC_VBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGVBR;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32StatTime = 1;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.fr32DstFrmRate = 5;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MinQfactor = 50;
                stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxQfactor = 95;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 2 * 3;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 3 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 6 * 3;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 12 * 3;
                        break;
					case PIC_UHD4K:
                        stH264Cbr.u32BitRate 								 = 1024 * 12 * 3;
                        break;
                    default :
                        stVencChnAttr.stRcAttr.stAttrMjpegeVbr.u32MaxBitRate = 1024 * 4 * 3;
                        break;
                }
            }
            else
            {
                SAMPLE_PRT("cann't support other mode in this version!\n");
                return HI_FAILURE;
            }
        }
        break;
        case PT_JPEG:
            stJpegAttr.u32PicWidth  = stPicSize.u32Width;
            stJpegAttr.u32PicHeight = stPicSize.u32Height;
            stJpegAttr.u32MaxPicWidth  = stPicSize.u32Width;
            stJpegAttr.u32MaxPicHeight = stPicSize.u32Height;
            stJpegAttr.u32BufSize   = stPicSize.u32Width * stPicSize.u32Height * 3;
            stJpegAttr.bByFrame     = HI_TRUE;/*get stream mode is field mode  or frame mode*/
            stJpegAttr.bSupportDCF  = HI_FALSE;
            memcpy(&stVencChnAttr.stVeAttr.stAttrJpege, &stJpegAttr, sizeof(VENC_ATTR_JPEG_S));
            break;
        case PT_H265:
        {
            stH265Attr.u32MaxPicWidth = stPicSize.u32Width;
            stH265Attr.u32MaxPicHeight = stPicSize.u32Height;
            stH265Attr.u32PicWidth = stPicSize.u32Width;/*the picture width*/
            stH265Attr.u32PicHeight = stPicSize.u32Height;/*the picture height*/
            stH265Attr.u32BufSize  = stPicSize.u32Width * stPicSize.u32Height * 2;/*stream buffer size*/
            if (u32Profile >= 1)
            { stH265Attr.u32Profile = 0; }/*0:MP; */
            else            
			{ stH265Attr.u32Profile  = u32Profile; }/*0:MP*/
            stH265Attr.bByFrame = HI_TRUE;/*get stream mode is slice mode or frame mode?*/
            //stH265Attr.u32BFrameNum = 0;/* 0: not support B frame; >=1: number of B frames */
            //stH265Attr.u32RefNum = 1;/* 0: default; number of refrence frame*/
            memcpy(&stVencChnAttr.stVeAttr.stAttrH265e, &stH265Attr, sizeof(VENC_ATTR_H265_S));
            if (SAMPLE_RC_CBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
                stH265Cbr.u32Gop            = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Cbr.u32StatTime       = 1; /* stream rate statics time(s) */
                stH265Cbr.u32SrcFrmRate      = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* input (vi) frame rate */
                stH265Cbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30; /* target frame rate */
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH265Cbr.u32BitRate = 256; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH265Cbr.u32BitRate = 512;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH265Cbr.u32BitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH265Cbr.u32BitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH265Cbr.u32BitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH265Cbr.u32BitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH265Cbr.u32BitRate = 1024 * 8;
                        break;
                    default :
                        stH265Cbr.u32BitRate = 1024 * 4;
                        break;
                }
                stH265Cbr.u32FluctuateLevel = 1; /* average bit rate */
                memcpy(&stVencChnAttr.stRcAttr.stAttrH265Cbr, &stH265Cbr, sizeof(VENC_ATTR_H265_CBR_S));
            }
            else if (SAMPLE_RC_FIXQP == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265FIXQP;
                stH265FixQp.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265FixQp.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265FixQp.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265FixQp.u32IQp = 20;
                stH265FixQp.u32PQp = 23;
                stH265FixQp.u32BQp = 25;
                memcpy(&stVencChnAttr.stRcAttr.stAttrH265FixQp, &stH265FixQp, sizeof(VENC_ATTR_H265_FIXQP_S));
            }
            else if (SAMPLE_RC_VBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
                stH265Vbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Vbr.u32StatTime = 1;
                stH265Vbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Vbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265Vbr.u32MinQp  = 10;
                stH265Vbr.u32MinIQp = 10;
                stH265Vbr.u32MaxQp  = 40;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH265Vbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH265Vbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH265Vbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH265Vbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH265Vbr.u32MaxBitRate = 1024 * 6;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH265Vbr.u32MaxBitRate = 1024 * 8;
                        break;
					case PIC_UHD4K:
                        stH265Vbr.u32MaxBitRate = 1024 * 8;
                        break;
                    default :
                        stH265Vbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH265Vbr, &stH265Vbr, sizeof(VENC_ATTR_H265_VBR_S));
            }
			else if (SAMPLE_RC_AVBR == enRcMode)
            {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265AVBR;
                stH265AVbr.u32Gop = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265AVbr.u32StatTime = 1;
                stH265AVbr.u32SrcFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                stH265AVbr.fr32DstFrmRate = (VIDEO_ENCODING_MODE_PAL == enNorm) ? 25 : 30;
                switch (enSize)
                {
                    case PIC_QCIF:
                        stH265AVbr.u32MaxBitRate = 256 * 3; /* average bit rate */
                        break;
                    case PIC_QVGA:    /* 320 * 240 */
                    case PIC_CIF:
                        stH265AVbr.u32MaxBitRate = 512 * 3;
                        break;
                    case PIC_D1:
                    case PIC_VGA:	   /* 640 * 480 */
                        stH265AVbr.u32MaxBitRate = 1024 * 2;
                        break;
                    case PIC_HD720:   /* 1280 * 720 */
                        stH265AVbr.u32MaxBitRate = 1024 * 3;
                        break;
                    case PIC_HD1080:  /* 1920 * 1080 */
                        stH265AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                    case PIC_5M:  /* 2592 * 1944 */
                        stH265AVbr.u32MaxBitRate = 1024 * 6;
                        break;
					case PIC_UHD4K:
                        stH265AVbr.u32MaxBitRate = 1024 * 8;	
                        break;
                    default :
                        stH265AVbr.u32MaxBitRate = 1024 * 4;
                        break;
                }
                memcpy(&stVencChnAttr.stRcAttr.stAttrH264AVbr, &stH265AVbr, sizeof(VENC_ATTR_H265_AVBR_S));
            }			
            else
            {
                return HI_FAILURE;
            }
        }
        break;
        default:
            return HI_ERR_VENC_NOT_SUPPORT;
    }

	if(PT_MJPEG == enType || PT_JPEG == enType )
    {
            stVencChnAttr.stGopAttr.enGopMode  = VENC_GOPMODE_NORMALP;
            stVencChnAttr.stGopAttr.stNormalP.s32IPQpDelta = 0;
            }
            else
            {
				memcpy(&stVencChnAttr.stGopAttr,pstGopAttr,sizeof(VENC_GOP_ATTR_S));		
            }

    s32Ret = HI_MPI_VENC_CreateChn(VencChn, &stVencChnAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_CreateChn [%d] faild with %#x! ===\n", \
                   VencChn, s32Ret);
        return s32Ret;
    }    /******************************************
     step 2:  Start Recv Venc Pictures
    ******************************************/
    s32Ret = HI_MPI_VENC_StartRecvPic(VencChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_StartRecvPic faild with%#x! \n", s32Ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : Stop venc ( stream mode -- H264, MJPEG )
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_Stop(VENC_CHN VencChn)
{
    HI_S32 s32Ret;
    /******************************************
     step 1:  Stop Recv Pictures
    ******************************************/
    s32Ret = HI_MPI_VENC_StopRecvPic(VencChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_StopRecvPic vechn[%d] failed with %#x!\n", \
                   VencChn, s32Ret);
        return HI_FAILURE;
    }    /******************************************
     step 2:  Distroy Venc Channel
    ******************************************/
    s32Ret = HI_MPI_VENC_DestroyChn(VencChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_DestroyChn vechn[%d] failed with %#x!\n", \
                   VencChn, s32Ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : Start snap
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SnapStart(VENC_CHN VencChn, SIZE_S* pstSize)
{
    HI_S32 s32Ret;
    VENC_CHN_ATTR_S stVencChnAttr;
    VENC_ATTR_JPEG_S stJpegAttr;
    /******************************************
     step 1:  Create Venc Channel
    ******************************************/
    stVencChnAttr.stVeAttr.enType = PT_JPEG;
    stJpegAttr.u32MaxPicWidth  = pstSize->u32Width;
    stJpegAttr.u32MaxPicHeight = pstSize->u32Height;
    stJpegAttr.u32PicWidth  = pstSize->u32Width;
    stJpegAttr.u32PicHeight = pstSize->u32Height;
    stJpegAttr.u32BufSize = pstSize->u32Width * pstSize->u32Height * 2;
    stJpegAttr.bByFrame = HI_TRUE;/*get stream mode is field mode  or frame mode*/
    stJpegAttr.bSupportDCF = HI_FALSE;
    memcpy(&stVencChnAttr.stVeAttr.stAttrJpege, &stJpegAttr, sizeof(VENC_ATTR_JPEG_S));
    s32Ret = HI_MPI_VENC_CreateChn(VencChn, &stVencChnAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_CreateChn [%d] faild with %#x!\n", \
                   VencChn, s32Ret);
        return s32Ret;
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : Stop snap
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SnapStop(VENC_CHN VencChn)
{
    HI_S32 s32Ret;
    s32Ret = HI_MPI_VENC_StopRecvPic(VencChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_StopRecvPic vechn[%d] failed with %#x!\n", VencChn, s32Ret);
        return HI_FAILURE;
    }
    s32Ret = HI_MPI_VENC_DestroyChn(VencChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_DestroyChn vechn[%d] failed with %#x!\n", VencChn, s32Ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : snap process
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_SnapProcess(VENC_CHN VencChn)
{
    struct timeval TimeoutVal;
    fd_set read_fds;
    HI_S32 s32VencFd;
    VENC_CHN_STAT_S stStat;
    VENC_STREAM_S stStream;
    HI_S32 s32Ret;
    VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_STREAM_BUF_INFO_S stStreamBufInfo;
		
    printf("press the Enter key to snap one pic\n");
    getchar();

    /******************************************
     step 2:  Start Recv Venc Pictures
    ******************************************/
    stRecvParam.s32RecvPicNum = 1;
    s32Ret = HI_MPI_VENC_StartRecvPicEx(VencChn, &stRecvParam);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VENC_StartRecvPic faild with%#x!\n", s32Ret);
        return HI_FAILURE;
    }    
    /******************************************
     step 3:  recv picture
    ******************************************/
    s32VencFd = HI_MPI_VENC_GetFd(VencChn);
    if (s32VencFd < 0)
    {
        SAMPLE_PRT("HI_MPI_VENC_GetFd faild with%#x!\n", s32VencFd);
        return HI_FAILURE;
    }
    FD_ZERO(&read_fds);
    FD_SET(s32VencFd, &read_fds);
    TimeoutVal.tv_sec  = 2;
    TimeoutVal.tv_usec = 0;
    s32Ret = select(s32VencFd + 1, &read_fds, NULL, NULL, &TimeoutVal);
    if (s32Ret < 0)
    {
        SAMPLE_PRT("snap select failed!\n");
        return HI_FAILURE;
    }
    else if (0 == s32Ret)
    {
        SAMPLE_PRT("snap time out!\n");
        return HI_FAILURE;
    }
    else
    {
        if (FD_ISSET(s32VencFd, &read_fds))
        {
            s32Ret = HI_MPI_VENC_Query(VencChn, &stStat);
            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VENC_Query failed with %#x!\n", s32Ret);
                return HI_FAILURE;
            }						
            /*******************************************************
			suggest to check both u32CurPacks and u32LeftStreamFrames at the same time,for example:
			 if(0 == stStat.u32CurPacks || 0 == stStat.u32LeftStreamFrames)
			 {				SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
				return HI_SUCCESS;
			 }			
			 *******************************************************/
            if (0 == stStat.u32CurPacks)
            {
                SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
                return HI_SUCCESS;
            }
            stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
            if (NULL == stStream.pstPack)
            {
                SAMPLE_PRT("malloc memory failed!\n");
                return HI_FAILURE;
            }
            stStream.u32PackCount = stStat.u32CurPacks;
            s32Ret = HI_MPI_VENC_GetStream(VencChn, &stStream, -1);
            if (HI_SUCCESS != s32Ret)
            {
                SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", s32Ret);
                free(stStream.pstPack);
                stStream.pstPack = NULL;
                return HI_FAILURE;
            }
	        s32Ret = HI_MPI_VENC_GetStreamBufInfo (VencChn, &stStreamBufInfo);
	        if (HI_SUCCESS != s32Ret)
	        {
	            SAMPLE_PRT("HI_MPI_VENC_GetStreamBufInfo failed with %#x!\n", s32Ret);
	            return HI_FAILURE;
	        }
            s32Ret = SAMPLE_COMM_VENC_SaveSnap(&stStreamBufInfo,&stStream);
            if (HI_SUCCESS != s32Ret)
            {
                SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", s32Ret);
                free(stStream.pstPack);
                stStream.pstPack = NULL;
                return HI_FAILURE;
            }
            s32Ret = HI_MPI_VENC_ReleaseStream(VencChn, &stStream);
            if (s32Ret)
            {
                SAMPLE_PRT("HI_MPI_VENC_ReleaseStream failed with %#x!\n", s32Ret);
                free(stStream.pstPack);
                stStream.pstPack = NULL;
                return HI_FAILURE;
            }
            free(stStream.pstPack);
            stStream.pstPack = NULL;
        }
    }    
    /******************************************
     step 4:  stop recv picture
    ******************************************/
    s32Ret = HI_MPI_VENC_StopRecvPic(VencChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VENC_StopRecvPic failed with %#x!\n",  s32Ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : get stream from each channels and save them
******************************************************************************/
HI_VOID* SAMPLE_COMM_VENC_GetVencStreamProc(HI_VOID* p)
{
    HI_S32 i;
    HI_S32 s32ChnTotal;
    VENC_CHN_ATTR_S stVencChnAttr;
    SAMPLE_VENC_GETSTREAM_PARA_S* pstPara;
    HI_S32 maxfd = 0;
    struct timeval TimeoutVal;
    fd_set read_fds;
    HI_S32 VencFd[VENC_MAX_CHN_NUM];
    HI_CHAR aszFileName[VENC_MAX_CHN_NUM][FILE_NAME_LEN]; //16, 128
    FILE* pFile[VENC_MAX_CHN_NUM];
    char szFilePostfix[10]; //文件尾名，也就是类似与.text
    VENC_CHN_STAT_S stStat;
    VENC_STREAM_S stStream;
    HI_S32 s32Ret;
    VENC_CHN VencChn;
    PAYLOAD_TYPE_E enPayLoadType[VENC_MAX_CHN_NUM];
    VENC_STREAM_BUF_INFO_S stStreamBufInfo[VENC_MAX_CHN_NUM];
	//改变该线程的名字
    prctl(PR_SET_NAME, "hi_getstream", 0, 0, 0);
	//线程的参数(bThreadStart = 1, s32Cnt = 1)
    pstPara = (SAMPLE_VENC_GETSTREAM_PARA_S*)p;
    s32ChnTotal = pstPara->s32Cnt;
    /******************************************
     step 1:  check & prepare save-file & venc-fd
    ******************************************/
    if (s32ChnTotal >= VENC_MAX_CHN_NUM)
    {
        SAMPLE_PRT("input count invaild\n");
        return NULL;
    }
    for (i = 0; i < s32ChnTotal; i++)
    {
        /* decide the stream file name, and open file to save stream */
        VencChn = i;
		//获取每个编码通道的属性
        s32Ret = HI_MPI_VENC_GetChnAttr(VencChn, &stVencChnAttr);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetChnAttr chn[%d] failed with %#x!\n", \
                       VencChn, s32Ret);
            return NULL;
        }
		//编码类型
        enPayLoadType[i] = stVencChnAttr.stVeAttr.enType;
        s32Ret = SAMPLE_COMM_VENC_GetFilePostfix(enPayLoadType[i], szFilePostfix);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("SAMPLE_COMM_VENC_GetFilePostfix [%d] failed with %#x!\n", \
                       stVencChnAttr.stVeAttr.enType, s32Ret);
            return NULL;
        }
		printf("aszFileName[%d] = %s\n", i, aszFileName[i]);
        snprintf(aszFileName[i], FILE_NAME_LEN, "stream_chn%d%s", i, szFilePostfix);
        pFile[i] = fopen(aszFileName[i], "wb");
        if (!pFile[i])
        {
            SAMPLE_PRT("open file[%s] failed!\n",
                       aszFileName[i]);
            return NULL;
        }        /* Set Venc Fd. */
        VencFd[i] = HI_MPI_VENC_GetFd(i); //获取编码通道对应的设备文件
        if (VencFd[i] < 0)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetFd failed with %#x!\n",
                       VencFd[i]);
            return NULL;
        }
        if (maxfd <= VencFd[i])
        {
            maxfd = VencFd[i];
        }
		//获取码流buffer的物理地址和大小
        s32Ret = HI_MPI_VENC_GetStreamBufInfo (i, &stStreamBufInfo[i]);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetStreamBufInfo failed with %#x!\n", s32Ret);
            return (void *)HI_FAILURE;
        }
    }

    /******************************************
     step 2:  Start to get streams of each channel.    
     ******************************************/
    while (HI_TRUE == pstPara->bThreadStart)
    {
        FD_ZERO(&read_fds);//将指定的文件描述符集清空
        for (i = 0; i < s32ChnTotal; i++)
        {
            FD_SET(VencFd[i], &read_fds); //在文件描述符集read_fds中增加新的文件描述符
        }
        TimeoutVal.tv_sec  = 2;
        TimeoutVal.tv_usec = 0;
        s32Ret = select(maxfd + 1, &read_fds, NULL, NULL, &TimeoutVal); //测试文件是否有输入
        if (s32Ret < 0)
        {
            SAMPLE_PRT("select failed!\n");
            break;
        }
        else if (s32Ret == 0)
        {
            SAMPLE_PRT("get venc stream time out, exit thread\n");
            continue;
        }
        else
        {
            for (i = 0; i < s32ChnTotal; i++)
            {
                if (FD_ISSET(VencFd[i], &read_fds))
                {
                    /*******************************************************
                    step 2.1 : query how many packs in one-frame stream.                    
                    *******************************************************/
                    memset(&stStream, 0, sizeof(stStream));
                    s32Ret = HI_MPI_VENC_Query(i, &stStat);
                    if (HI_SUCCESS != s32Ret)
                    {
                        SAMPLE_PRT("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", i, s32Ret);
                        break;
                    }										
                    /*******************************************************
					step 2.2 :suggest to check both u32CurPacks and u32LeftStreamFrames at the same time,for example:					 if(0 == stStat.u32CurPacks || 0 == stStat.u32LeftStreamFrames)
					 {						SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
						continue;
					 }					
					 *******************************************************/
                    if (0 == stStat.u32CurPacks)
                    {
                        SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
                        continue;
                    }                    
                    /*******************************************************
                     step 2.3 : malloc corresponding number of pack nodes.
                    *******************************************************/
                    stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
                    if (NULL == stStream.pstPack)
                    {
                        SAMPLE_PRT("malloc stream pack failed!\n");
                        break;
                    }                    
                    /*******************************************************
                     step 2.4 : call mpi to get one-frame stream
                    *******************************************************/
                    stStream.u32PackCount = stStat.u32CurPacks;
                    s32Ret = HI_MPI_VENC_GetStream(i, &stStream, HI_TRUE);
                    if (HI_SUCCESS != s32Ret)
                    {
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;
                        SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", \
                                   s32Ret);
                        break;
                    }                    
                    /*******************************************************
                     step 2.5 : save frame to file
                    *******************************************************/
                    #ifndef __HuaweiLite__
                    s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i], &stStream);
                    #else
                    s32Ret =SAMPLE_COMM_VENC_SaveFile(pFile[i], &stStreamBufInfo[i], &stStream);
                    #endif
                    if (HI_SUCCESS != s32Ret)
                    {
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;
                        SAMPLE_PRT("save stream failed!\n");
                        break;
                    }                    
                    /*******************************************************
                     step 2.6 : release stream                    
                     *******************************************************/
                    s32Ret = HI_MPI_VENC_ReleaseStream(i, &stStream);
                    if (HI_SUCCESS != s32Ret)
                    {
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;
                        break;
                    }                    
                    /*******************************************************
                     step 2.7 : free pack nodes
                    *******************************************************/
                    free(stStream.pstPack);
                    stStream.pstPack = NULL;
                }
            }
        }
    }    
    /*******************************************************
    * step 3 : close save-file
    *******************************************************/
    for (i = 0; i < s32ChnTotal; i++)
    {
        fclose(pFile[i]);
    }
    return NULL;
}

/******************************************************************************
* funciton : get svc_t stream from h264 channels and save them
******************************************************************************/
HI_VOID* SAMPLE_COMM_VENC_GetVencStreamProc_Svc_t(void* p)
{
    HI_S32 i = 0;
    HI_S32 s32Cnt = 0;
    HI_S32 s32ChnTotal;
    VENC_CHN_ATTR_S stVencChnAttr;
    SAMPLE_VENC_GETSTREAM_PARA_S* pstPara;
    HI_S32 maxfd = 0;
    struct timeval TimeoutVal;
    fd_set read_fds;
    HI_S32 VencFd[VENC_MAX_CHN_NUM];
    HI_CHAR aszFileName[VENC_MAX_CHN_NUM][64];
    FILE* pFile[VENC_MAX_CHN_NUM];
    char szFilePostfix[10];
    VENC_CHN_STAT_S stStat;
    VENC_STREAM_S stStream;
    HI_S32 s32Ret;
    VENC_CHN VencChn;
    PAYLOAD_TYPE_E enPayLoadType[VENC_MAX_CHN_NUM];
    VENC_STREAM_BUF_INFO_S stStreamBufInfo[VENC_MAX_CHN_NUM];

    prctl(PR_SET_NAME, "hi_getstream", 0, 0, 0);

    pstPara = (SAMPLE_VENC_GETSTREAM_PARA_S*)p;
    s32ChnTotal = pstPara->s32Cnt;

    /******************************************
     step 1:  check & prepare save-file & venc-fd
    ******************************************/
    if (s32ChnTotal >= VENC_MAX_CHN_NUM)
    {
        SAMPLE_PRT("input count invaild\n");
        return NULL;
    }
    for (i = 0; i < s32ChnTotal; i++)
    {
        /* decide the stream file name, and open file to save stream */
        VencChn = i;
        s32Ret = HI_MPI_VENC_GetChnAttr(VencChn, &stVencChnAttr);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetChnAttr chn[%d] failed with %#x!\n", \
                       VencChn, s32Ret);
            return NULL;
        }
        enPayLoadType[i] = stVencChnAttr.stVeAttr.enType;
        s32Ret = SAMPLE_COMM_VENC_GetFilePostfix(enPayLoadType[i], szFilePostfix);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_PRT("SAMPLE_COMM_VENC_GetFilePostfix [%d] failed with %#x!\n", \
                       stVencChnAttr.stVeAttr.enType, s32Ret);
            return NULL;
        }
        for (s32Cnt = 0; s32Cnt < 3; s32Cnt++)
        {
            snprintf(aszFileName[i + s32Cnt], FILE_NAME_LEN, "Tid%d%s", i + s32Cnt, szFilePostfix);
            pFile[i + s32Cnt] = fopen(aszFileName[i + s32Cnt], "wb");

            if (!pFile[i + s32Cnt])
            {
                SAMPLE_PRT("open file[%s] failed!\n",
                           aszFileName[i + s32Cnt]);
                return NULL;
            }
        }        /* Set Venc Fd. */
        VencFd[i] = HI_MPI_VENC_GetFd(i);
        if (VencFd[i] < 0)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetFd failed with %#x!\n",
                       VencFd[i]);
            return NULL;
        }
        if (maxfd <= VencFd[i])
        {
            maxfd = VencFd[i];
        }
		s32Ret = HI_MPI_VENC_GetStreamBufInfo (i, &stStreamBufInfo[i]);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetStreamBufInfo failed with %#x!\n", s32Ret);
            return NULL;
        }
    }

    /******************************************
     step 2:  Start to get streams of each channel.
    ******************************************/
    while (HI_TRUE == pstPara->bThreadStart)
    {
        FD_ZERO(&read_fds);
        for (i = 0; i < s32ChnTotal; i++)
        {
            FD_SET(VencFd[i], &read_fds);
        }
        TimeoutVal.tv_sec  = 2;
        TimeoutVal.tv_usec = 0;
        s32Ret = select(maxfd + 1, &read_fds, NULL, NULL, &TimeoutVal);
        if (s32Ret < 0)
        {
            SAMPLE_PRT("select failed!\n");
            break;
        }
        else if (s32Ret == 0)
        {
            SAMPLE_PRT("get venc stream time out, exit thread\n");
            continue;
        }
        else
        {
            for (i = 0; i < s32ChnTotal; i++)
            {
                if (FD_ISSET(VencFd[i], &read_fds))
                {
                    /*******************************************************
                    step 2.1 : query how many packs in one-frame stream.
                    *******************************************************/
                    memset(&stStream, 0, sizeof(stStream));
                    s32Ret = HI_MPI_VENC_Query(i, &stStat);
                    if (HI_SUCCESS != s32Ret)
                    {
                        SAMPLE_PRT("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", i, s32Ret);
                        break;
                    }										
                    /*******************************************************
					step 2.2 :suggest to check both u32CurPacks and u32LeftStreamFrames at the same time,for example:
					 if(0 == stStat.u32CurPacks || 0 == stStat.u32LeftStreamFrames)
					 {						SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
						continue;
					 }					
					 *******************************************************/
                    if (0 == stStat.u32CurPacks)
                    {
                        SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
                        continue;
                    }                    
                    /*******************************************************
                     step 2.3 : malloc corresponding number of pack nodes.
                    *******************************************************/
                    stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
                    if (NULL == stStream.pstPack)
                    {
                        SAMPLE_PRT("malloc stream pack failed!\n");
                        break;
                    }
                    /*******************************************************
                     step 2.4 : call mpi to get one-frame stream
                    *******************************************************/
                    stStream.u32PackCount = stStat.u32CurPacks;
                    s32Ret = HI_MPI_VENC_GetStream(i, &stStream, HI_TRUE);
                    if (HI_SUCCESS != s32Ret)
                    {
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;
                        SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", \
                                   s32Ret);
                        break;
                    }                    
                    /*******************************************************
                     step 2.5 : save frame to file
                    *******************************************************/
#if 1
                    for (s32Cnt = 0; s32Cnt < 3; s32Cnt++)
                    {
                        switch (s32Cnt)
                        {
                            case 0:
                                if (BASE_IDRSLICE == stStream.stH264Info.enRefType ||
                                    BASE_PSLICE_REFBYBASE == stStream.stH264Info.enRefType)
                                {  
                                    #ifndef __HuaweiLite__
                                    s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i+s32Cnt], &stStream);
                                    #else
									s32Ret = SAMPLE_COMM_VENC_SaveFile(pFile[i + s32Cnt], &stStreamBufInfo[i], &stStream);
                                    #endif
                                }
                                break;
                            case 1:
                                if (BASE_IDRSLICE == stStream.stH264Info.enRefType      ||
                                    BASE_PSLICE_REFBYBASE == stStream.stH264Info.enRefType ||
                                    BASE_PSLICE_REFBYENHANCE == stStream.stH264Info.enRefType)
                                {
                                    #ifndef __HuaweiLite__
                                    s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i+s32Cnt], &stStream);
                                    #else
									s32Ret = SAMPLE_COMM_VENC_SaveFile(pFile[i + s32Cnt], &stStreamBufInfo[i], &stStream);
                                    #endif
                                }
                                break;
                            case 2:
                                #ifndef __HuaweiLite__
                                s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i+s32Cnt], &stStream);
                                #else
								s32Ret = SAMPLE_COMM_VENC_SaveFile(pFile[i + s32Cnt], &stStreamBufInfo[i], &stStream);
                                #endif
                                break;
                        }
                        if (HI_SUCCESS != s32Ret)
                        {
                            free(stStream.pstPack);
                            stStream.pstPack = NULL;
                            SAMPLE_PRT("save stream failed!\n");
                            break;
                        }
                    }
#else
                    for (s32Cnt = 0; s32Cnt < 3; s32Cnt++)
                    {
                        if (s32Cnt == 0)
                        {
                            if (NULL != pFile[i + s32Cnt])
                            {
                                if (BASE_IDRSLICE == stStream.stH264Info.enRefType ||
                                    BASE_PSLICE_REFBYBASE == stStream.stH264Info.enRefType)
                                {
                                    s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i + s32Cnt], &stStream);
                                }
                            }
                        }
                        else if (s32Cnt == 1)
                        {
                            if (NULL != pFile[i + s32Cnt])
                            {
                                if (BASE_IDRSLICE == stStream.stH264Info.enRefType         ||
                                    BASE_PSLICE_REFBYBASE == stStream.stH264Info.enRefType ||
                                    BASE_PSLICE_REFBYENHANCE == stStream.stH264Info.enRefType)
                                {
                                    s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i + s32Cnt], &stStream);
                                }
                            }
                        }
                        else
                        {
                            if (NULL != pFile[i + s32Cnt])
                            {
                                s32Ret = SAMPLE_COMM_VENC_SaveStream(enPayLoadType[i], pFile[i + s32Cnt], &stStream);
                            }
                        }
                    }
                    if (HI_SUCCESS != s32Ret)
                    {
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;
                        SAMPLE_PRT("save stream failed!\n");
                        break;
                    }
#endif
                    /*******************************************************
                    step 2.6 : release stream
                    *******************************************************/
                    s32Ret = HI_MPI_VENC_ReleaseStream(i, &stStream);
                    if (HI_SUCCESS != s32Ret)
                    {
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;
                        break;
                    }                    
                    /*******************************************************
                     step 2.7 : free pack nodes
                    *******************************************************/
                    free(stStream.pstPack);
                    stStream.pstPack = NULL;
                }
            }
        }
    }    
    /*******************************************************
     step 3 : close save-file
    *******************************************************/
    for (i = 0; i < s32ChnTotal; i++)
    {
        for (s32Cnt = 0; s32Cnt < 3; s32Cnt++)
        {
            if (pFile[i + s32Cnt])
            {
                fclose(pFile[i + s32Cnt]);
            }
        }
    }
    return NULL;
}

/******************************************************************************
* funciton : SAMPLE_COMM_VPSS_SetChnCropProcess
******************************************************************************/
HI_VOID *SAMPLE_COMM_VPSS_SetChnCropProcess(HI_VOID *arg)
{
    HI_S32 s32Ret;
    SAMPLE_VPSS_CROP_INFO_S *pData = (SAMPLE_VPSS_CROP_INFO_S *)arg;
	VI_CHN VpssChn ;
	VPSS_GRP VpssGrp;
	VPSS_CROP_INFO_S stCropInfo;
	HI_U32 DetaW = 0;
	HI_U32 DetaH = 0;

    prctl(PR_SET_NAME, "hi_SetChnCrop", 0, 0, 0);

	VpssChn = pData->VpssChn;
	VpssGrp = pData->VpssGrp;
	DetaW = pData->stSrcSize.u32Width - pData->stDstSize.u32Width;
	DetaH = pData->stSrcSize.u32Height - pData->stDstSize.u32Height;

	memset(&stCropInfo, 0, sizeof(stCropInfo));
	while(pData->bThreadStart == HI_TRUE)
    {
		stCropInfo.bEnable = HI_TRUE;
		stCropInfo.enCropCoordinate = VPSS_CROP_ABS_COOR;
		stCropInfo.stCropRect.s32X = (stCropInfo.stCropRect.s32X + 2) % DetaW;
		stCropInfo.stCropRect.s32Y = (stCropInfo.stCropRect.s32Y + 2) % DetaH;
		stCropInfo.stCropRect.u32Width	= pData->stDstSize.u32Width;
		stCropInfo.stCropRect.u32Height = pData->stDstSize.u32Height;
#if 0		
		printf("crop:%d, %d, %d, %d\n", stCropInfo.stCropRect.s32X,
			stCropInfo.stCropRect.s32Y, stCropInfo.stCropRect.u32Width,
			stCropInfo.stCropRect.u32Height);
#endif
		s32Ret = HI_MPI_VPSS_SetChnCrop(VpssGrp, VpssChn, &stCropInfo);
		if (HI_SUCCESS != s32Ret)
		{
			printf("HI_MPI_VPSS_SetChnCrop err:0x%x\n", s32Ret);
			break;
		}

		usleep(500000);
	}
    printf("vpss channel crop Exit\n");
    return HI_NULL;
}

HI_S32 SAMPLE_COMM_VENC_StartGetStream(HI_S32 s32Cnt)
{
    gs_stPara.bThreadStart = HI_TRUE;
    gs_stPara.s32Cnt = s32Cnt;
	printf("come come come come come come come come come come\n");
    return pthread_create(&gs_VencPid, 0, SAMPLE_COMM_VENC_GetVencStreamProc, (HI_VOID*)&gs_stPara);
}
/******************************************************************************
* funciton : VpssChnCropProc
******************************************************************************/
HI_S32 SAMPLE_COMM_VPSS_SetChnCropProc(VPSS_GRP VpssGrp,VPSS_CHN VpssChn,HI_U32 u32SrcWidth,HI_U32 u32SrcHeigth,HI_U32 u32DstWidth,HI_U32 u32DstHeigth)
{
	gs_stVpssCropInfo.bThreadStart = HI_TRUE;
	gs_stVpssCropInfo.VpssGrp = VpssGrp;
	gs_stVpssCropInfo.VpssChn = VpssChn;
	gs_stVpssCropInfo.stSrcSize.u32Width = u32SrcWidth;
	gs_stVpssCropInfo.stSrcSize.u32Height = u32SrcHeigth;
	gs_stVpssCropInfo.stDstSize.u32Width = u32DstWidth;
	gs_stVpssCropInfo.stDstSize.u32Height = u32DstHeigth;
    return pthread_create(&gs_VpssCropPid, 0, SAMPLE_COMM_VPSS_SetChnCropProcess, (HI_VOID*)&gs_stVpssCropInfo);
}

/******************************************************************************
* funciton : start get venc svc-t stream process thread
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_StartGetStream_Svc_t(HI_S32 s32Cnt)
{
    gs_stPara.bThreadStart = HI_TRUE;
    gs_stPara.s32Cnt = s32Cnt;
    return pthread_create(&gs_VencPid, 0, SAMPLE_COMM_VENC_GetVencStreamProc_Svc_t, (HI_VOID*)&gs_stPara);
}

/******************************************************************************
* funciton : stop get venc stream process.
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_StopGetStream(void)
{
    if (HI_TRUE == gs_stPara.bThreadStart)
    {
        gs_stPara.bThreadStart = HI_FALSE;
        pthread_join(gs_VencPid, 0);
    }
    return HI_SUCCESS;
}

/******************************************************************************
* funciton : stop get venc stream process.
******************************************************************************/
HI_S32 SAMPLE_COMM_VPSS_StopChnCropProc(void)
{
    if (HI_TRUE == gs_stVpssCropInfo.bThreadStart)
    {
        gs_stVpssCropInfo.bThreadStart = HI_FALSE;
        pthread_join(gs_VpssCropPid, 0);
    }
    return HI_SUCCESS;
}

HI_VOID SAMPLE_COMM_VENC_ReadOneFrame( FILE* fp, HI_U8* pY, HI_U8* pU, HI_U8* pV,
                                       HI_U32 width, HI_U32 height, HI_U32 stride, HI_U32 stride2)
{
    HI_U8* pDst;
    HI_U32 u32Row;
    pDst = pY;
    for ( u32Row = 0; u32Row < height; u32Row++ )
    {
        if (fread( pDst, width, 1, fp ) != 1)
		{
        	printf("fread failed\n");
        	return;
    	}
        pDst += stride;
    }
    pDst = pU;
    for ( u32Row = 0; u32Row < height / 2; u32Row++ )
    {
        if (fread( pDst, width / 2, 1, fp ) != 1)
		{
        	printf("fread failed\n");
        	return;
    	}
        pDst += stride2;
    }
    pDst = pV;
    for ( u32Row = 0; u32Row < height / 2; u32Row++ )
    {
        if (fread( pDst, width / 2, 1, fp ) != 1)
		{
        	printf("fread failed\n");
        	return;
    	}
        pDst += stride2;
    }
}

HI_S32 SAMPLE_COMM_VENC_PlanToSemi(HI_U8* pY, HI_S32 yStride,
                                   HI_U8* pU, HI_S32 uStride,                                   HI_U8* pV, HI_S32 vStride,
                                   HI_S32 picWidth, HI_S32 picHeight)
{
    HI_S32 i;
    HI_U8* pTmpU, *ptu;
    HI_U8* pTmpV, *ptv;
    HI_S32 s32HafW = uStride >> 1 ;
    HI_S32 s32HafH = picHeight >> 1 ;
    HI_S32 s32Size = s32HafW * s32HafH;
    pTmpU = malloc( s32Size );
    ptu = pTmpU;
    pTmpV = malloc( s32Size );
    ptv = pTmpV;
    if ((pTmpU == HI_NULL) || (pTmpV == HI_NULL))
    {
        printf("malloc buf failed\n");
		if (pTmpU != HI_NULL)
        {
            free( pTmpU );
        }

		if (pTmpV != HI_NULL)
        {
            free( pTmpV );
        }
        return HI_FAILURE;
    }
    memcpy(pTmpU, pU, s32Size);
    memcpy(pTmpV, pV, s32Size);
    for (i = 0; i<s32Size >> 1; i++)
    {
        *pU++ = *pTmpV++;
        *pU++ = *pTmpU++;
    }
    for (i = 0; i<s32Size >> 1; i++)
    {
        *pV++ = *pTmpV++;
        *pV++ = *pTmpU++;
    }
    free( ptu );
    free( ptv );
    return HI_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

