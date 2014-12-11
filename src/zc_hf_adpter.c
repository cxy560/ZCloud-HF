/**
******************************************************************************
* @file     zc_hf_adpter.c
* @authors  cxy
* @version  V1.0.0
* @date     10-Sep-2014
* @brief    Event
******************************************************************************
*/
#include <zc_protocol_controller.h>
#include <zc_timer.h>
#include <zc_module_interface.h>
#include <hsf.h>
#include <zc_hf_adpter.h>
#include <stdlib.h>

extern PTC_ProtocolCon  g_struProtocolController;
PTC_ModuleAdapter g_struHfAdapter;

MSG_Buffer g_struRecvBuffer;
MSG_Buffer g_struRetxBuffer;
MSG_Buffer g_struClientBuffer;


MSG_Queue  g_struRecvQueue;
MSG_Buffer g_struSendBuffer[MSG_BUFFER_SEND_MAX_NUM];
MSG_Queue  g_struSendQueue;

u8 g_u8MsgBuildBuffer[MSG_BULID_BUFFER_MAXLEN];
u8 g_u8CiperBuffer[MSG_CIPER_BUFFER_MAXLEN];
u8 g_u8ClientCiperBuffer[MSG_CIPER_BUFFER_MAXLEN];
u8 g_u8ClientSendLen = 0;


u16 g_u16TcpMss;
u16 g_u16LocalPort;


HF_StaInfo g_struHfStaInfo = {
    DEFAULT_IOT_CLOUD_KEY,
    DEFAULT_IOT_PRIVATE_KEY,
    DEFAULT_DEVICIID,
    "www.baidu.com"
};
u8 g_u8recvbuffer[HF_MAX_SOCKET_LEN];
ZC_UartBuffer g_struUartBuffer;
HF_TimerInfo g_struHfTimer[ZC_TIMER_MAX_NUM];
hfthread_mutex_t g_struTimermutex;
int g_Bcfd;
u8  g_u8BcSendBuffer[100];

extern u32 g_u32GloablIp;

/*************************************************
* Function: HF_ReadDataFormFlash
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_ReadDataFormFlash(void) 
{
    hfuflash_read(0, (char*)(&g_struHfStaInfo), sizeof(g_struHfStaInfo));
}

/*************************************************
* Function: HF_WriteDataToFlash
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_WriteDataToFlash()
{
    hfuflash_erase_page(0,1); 
    hfuflash_write(0, (char*)(&g_struHfStaInfo), sizeof(g_struHfStaInfo));
}

/*************************************************
* Function: HF_timer_callback
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void USER_FUNC HF_timer_callback(hftimer_handle_t htimer) 
{
    u8 u8TimeId;
    hfthread_mutext_lock(g_struTimermutex);
    u8TimeId = hftimer_get_timer_id(htimer);
    if (1 == g_struHfTimer[u8TimeId].u32FirstFlag)
    {
        g_struHfTimer[u8TimeId].u32FirstFlag = 0;
        hftimer_start(htimer);
    }
    else
    {
        TIMER_TimeoutAction(u8TimeId);
        TIMER_StopTimer(u8TimeId);
    }
    
    hfthread_mutext_unlock(g_struTimermutex);
}
/*************************************************
* Function: HF_StopTimer
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_StopTimer(u8 u8TimerIndex)
{
    hftimer_stop(g_struHfTimer[u8TimerIndex].struHandle);
    hftimer_delete(g_struHfTimer[u8TimerIndex].struHandle);
}

/*************************************************
* Function: HF_SetTimer
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_SetTimer(u8 u8Type, u32 u32Interval, u8 *pu8TimeIndex)
{
    u8 u8TimerIndex;
    u32 u32Retval;

    u32Retval = TIMER_FindIdleTimer(&u8TimerIndex);
    if (ZC_RET_OK == u32Retval)
    {
        TIMER_AllocateTimer(u8Type, u8TimerIndex, (u8*)&g_struHfTimer[u8TimerIndex]);
        g_struHfTimer[u8TimerIndex].struHandle = hftimer_create(NULL,u32Interval,false,u8TimerIndex,
             HF_timer_callback,0);
        g_struHfTimer[u8TimerIndex].u32FirstFlag = 1;
        hftimer_start(g_struHfTimer[u8TimerIndex].struHandle);  
        
        *pu8TimeIndex = u8TimerIndex;
    }
    
    return u32Retval;
}


/*************************************************
* Function: HF_SendDataToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendDataToCloud(PTC_Connection *pstruConnection)
{
    MSG_Buffer *pstruBuf = NULL;

    u16 u16DataLen; 
    pstruBuf = (MSG_Buffer *)MSG_PopMsg(&g_struSendQueue); 
    
    if (NULL == pstruBuf)
    {
        return;
    }
    
    u16DataLen = pstruBuf->u32Len; 
    send(pstruConnection->u32Socket, pstruBuf->u8MsgBuffer, u16DataLen, 0);
    ZC_Printf("send data len = %d\n", u16DataLen);
    pstruBuf->u8Status = MSG_BUFFER_IDLE;
    pstruBuf->u32Len = 0;
    return;
}
/*************************************************
* Function: HF_RecvDataFromCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_RecvDataFromCloud(u8 *pu8Data, u32 u32DataLen)
{
    u32 u32RetVal;
    u16 u16PlainLen;
    u32RetVal = MSG_RecvDataFromCloud(pu8Data, u32DataLen);

    if (ZC_RET_OK == u32RetVal)
    {
        if (MSG_BUFFER_FULL == g_struRecvBuffer.u8Status)
        {
            u32RetVal = SEC_Decrypt((ZC_SecHead*)g_u8CiperBuffer, 
                g_u8CiperBuffer + sizeof(ZC_SecHead), g_struRecvBuffer.u8MsgBuffer, &u16PlainLen);

            g_struRecvBuffer.u32Len = u16PlainLen;
            if (ZC_RET_OK == u32RetVal)
            {
                u32RetVal = MSG_PushMsg(&g_struRecvQueue, (u8*)&g_struRecvBuffer);
            }
        }
    }
    
    return;
}
/*************************************************
* Function: HF_FirmwareUpdateFinish
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_FirmwareUpdateFinish(u32 u32TotalLen)
{
    int retval;
    retval = hfupdate_complete(HFUPDATE_SW, u32TotalLen);
    if (HF_SUCCESS == retval)
    {
        return ZC_RET_OK;
    }
    else
    {
        return ZC_RET_ERROR;    
    }
}


/*************************************************
* Function: HF_FirmwareUpdate
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_FirmwareUpdate(u8 *pu8FileData, u32 u32Offset, u32 u32DataLen)
{
    int retval;
    if (0 == u32Offset)
    {
        hfupdate_start(HFUPDATE_SW);
    }
    
    retval = hfupdate_write_file(HFUPDATE_SW, u32Offset, (char *)pu8FileData, u32DataLen); 
    if (retval < 0)
    {
        return ZC_RET_ERROR;
    }
    
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_SendDataToMoudle
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_SendDataToMoudle(u8 *pu8Data, u16 u16DataLen)
{
    u8 u8MagicFlag[4] = {0x02,0x03,0x04,0x05};
    hfuart_send(HFUART0,(char*)u8MagicFlag,4,1000); 
    hfuart_send(HFUART0,(char*)pu8Data,u16DataLen,1000); 
    return ZC_RET_OK;
}


/*************************************************
* Function: HF_GetCloudKey
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_GetCloudKey(u8 **pu8Key)
{
    *pu8Key = g_struHfStaInfo.u8CloudKey;
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_GetPrivateKey
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_GetPrivateKey(u8 **pu8Key)
{
    *pu8Key = g_struHfStaInfo.u8PrivateKey;
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_GetVersion
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_GetVersion(u8 **pu8Version)
{
    *pu8Version = g_struHfStaInfo.u8EqVersion;
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_GetDeviceId
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_GetDeviceId(u8 **pu8DeviceId)
{
    *pu8DeviceId = g_struHfStaInfo.u8DeviciId;
    return ZC_RET_OK;
}

/*************************************************
* Function: HF_Rest
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Rest(void)
{

}
/*************************************************
* Function: HF_SendDataToNet
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendDataToNet(u32 u32Fd, u8 *pu8Data, u16 u16DataLen, ZC_SendParam *pstruParam)
{
    send(u32Fd, pu8Data, u16DataLen, 0);
}

/*************************************************
* Function: HF_StoreRegisterInfor
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_StoreRegisterInfor(u8 *pu8Data, u16 u16DataLen)
{
    ZC_RegisterReq *pstruRegister;

    pstruRegister = (ZC_RegisterReq *)(pu8Data);

    memcpy(g_struHfStaInfo.u8PrivateKey, pstruRegister->u8ModuleKey, ZC_MODULE_KEY_LEN);
    memcpy(g_struHfStaInfo.u8DeviciId, pstruRegister->u8DeviceId, ZC_HS_DEVICE_ID_LEN);
    memcpy(g_struHfStaInfo.u8DeviciId + ZC_HS_DEVICE_ID_LEN, pstruRegister->u8Domain, ZC_DOMAIN_LEN);
    memcpy(g_struHfStaInfo.u8EqVersion, pstruRegister->u8EqVersion, ZC_EQVERSION_LEN);
    
    return ZC_RET_OK;
}


/*************************************************
* Function: HF_CloudRecvfunc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
USER_FUNC static void HF_CloudRecvfunc(void* arg) 
{
    s32 s32RecvLen=0; 
    fd_set fdread;
    u32 u32Index;
    u32 u32Len=0; 
    u32 u32ActiveFlag = 0;
    struct sockaddr_in cliaddr;
    int connfd;
    extern u8 g_u8ClientStart;
    u32 u32MaxFd = 0;
    struct timeval timeout; 
    ZC_ClientQueryRsp struRsp;
    u16 u16Len;
    struct sockaddr_in addr;
    int tmp=1;    

    
    while(1) 
    {
        ZC_StartClientListen();

        u32ActiveFlag = 0;
        
        timeout.tv_sec= 0; 
        timeout.tv_usec= 1000; 
        
        FD_ZERO(&fdread);

        FD_SET(g_Bcfd, &fdread);
        u32MaxFd = u32MaxFd > g_Bcfd ? u32MaxFd : g_Bcfd;

        if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
        {
            FD_SET(g_struProtocolController.struClientConnection.u32Socket, &fdread);
            u32MaxFd = u32MaxFd > g_struProtocolController.struClientConnection.u32Socket ? u32MaxFd : g_struProtocolController.struClientConnection.u32Socket;
            u32ActiveFlag = 1;
        }
        
        if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
        && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
        {
            FD_SET(g_struProtocolController.struCloudConnection.u32Socket, &fdread);
            u32MaxFd = u32MaxFd > g_struProtocolController.struCloudConnection.u32Socket ? u32MaxFd : g_struProtocolController.struCloudConnection.u32Socket;
            u32ActiveFlag = 1;
        }


        for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
        {
            if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
            {
                FD_SET(g_struClientInfo.u32ClientFd[u32Index], &fdread);
                u32MaxFd = u32MaxFd > g_struClientInfo.u32ClientFd[u32Index] ? u32MaxFd : g_struClientInfo.u32ClientFd[u32Index];
                u32ActiveFlag = 1;            
            }
        }


        if (0 == u32ActiveFlag)
        {
            continue;
        }
        
        select(u32MaxFd + 1, &fdread, NULL, NULL, &timeout);
        
        if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
        && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
        {
            if (FD_ISSET(g_struProtocolController.struCloudConnection.u32Socket, &fdread))
            {
                s32RecvLen = recv(g_struProtocolController.struCloudConnection.u32Socket, g_u8recvbuffer, HF_MAX_SOCKET_LEN, 0); 
                
                if(s32RecvLen > 0) 
                {
                    ZC_Printf("recv data len = %d\n", s32RecvLen);
                    HF_RecvDataFromCloud(g_u8recvbuffer, s32RecvLen);
                }
                else
                {
                    ZC_Printf("recv error, len = %d\n",s32RecvLen);
                    PCT_DisConnectCloud(&g_struProtocolController);
                    
                    g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
                    g_struUartBuffer.u32RecvLen = 0;
                }
            }
            
        }

        
        for (u32Index = 0; u32Index < ZC_MAX_CLIENT_NUM; u32Index++)
        {
            if (0 == g_struClientInfo.u32ClientVaildFlag[u32Index])
            {
                if (FD_ISSET(g_struClientInfo.u32ClientFd[u32Index], &fdread))
                {
                    s32RecvLen = recv(g_struClientInfo.u32ClientFd[u32Index], g_u8recvbuffer, HF_MAX_SOCKET_LEN, 0); 
                    if (s32RecvLen > 0)
                    {
                        ZC_RecvDataFromClient(g_struClientInfo.u32ClientFd[u32Index], g_u8recvbuffer, s32RecvLen);
                    }
                    else
                    {   
                        ZC_ClientDisconnect(g_struClientInfo.u32ClientFd[u32Index]);
                        close(g_struClientInfo.u32ClientFd[u32Index]);
                    }
                    
                }
            }
            
        }

        if (PCT_INVAILD_SOCKET != g_struProtocolController.struClientConnection.u32Socket)
        {
            if (FD_ISSET(g_struProtocolController.struClientConnection.u32Socket, &fdread))
            {
                connfd = accept(g_struProtocolController.struClientConnection.u32Socket,(struct sockaddr *)&cliaddr,&u32Len);

                if (ZC_RET_ERROR == ZC_ClientConnect((u32)connfd))
                {
                    close(connfd);
                }
                else
                {
                    ZC_Printf("accept client = %d", connfd);
                }
            }
        }

        if (FD_ISSET(g_Bcfd, &fdread))
        {
            tmp = sizeof(addr); 
            s32RecvLen = recvfrom(g_Bcfd, g_u8BcSendBuffer, 100, 0, (struct sockaddr *)&addr, (socklen_t*)&tmp); 
            if(s32RecvLen > 0) 
            {
                
                memset((char*)&addr,0,sizeof(addr));
                addr.sin_family = AF_INET; 
                addr.sin_port = htons(ZC_MOUDLE_BROADCAST_PORT); 
                addr.sin_addr.s_addr=inet_addr("255.255.255.255"); 

                struRsp.addr[0] = g_u32GloablIp & 0xff;
                struRsp.addr[1] = (g_u32GloablIp >> 8) & 0xff;        
                struRsp.addr[2] = (g_u32GloablIp >> 16) & 0xff;
                struRsp.addr[3] = (g_u32GloablIp >> 24)  & 0xff;        
                EVENT_BuildMsg(ZC_CODE_CLIENT_QUERY_RSP, 0, g_u8MsgBuildBuffer, &u16Len, (u8*)&struRsp, sizeof(ZC_ClientQueryRsp));
                sendto(g_Bcfd,g_u8MsgBuildBuffer,u16Len,0,(struct sockaddr *)&addr,sizeof(addr));             
            } 
        }
        
    } 
}

/*************************************************
* Function: HF_ConnectToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_ConnectToCloud(PTC_Connection *pstruConnection)
{
    int fd; 
    struct sockaddr_in addr;
    struct ip_addr struIp;
    int retval;
    
    memset((char*)&addr,0,sizeof(addr));
    ZC_Printf("connect to cloud\n");
    retval = hfnet_gethostbyname((const char*)g_struHfStaInfo.u8CloudAddr, &struIp);
    if (HF_SUCCESS != retval)
    {
        return ZC_RET_ERROR;
    }
    
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ZC_CLOUD_PORT);
    addr.sin_addr.s_addr=inet_addr("42.62.41.75");
    fd = socket(AF_INET, SOCK_STREAM, 0);

    if(fd<0)
        return ZC_RET_ERROR;
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))< 0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }

    ZC_Printf("connect ok!\n");
    g_struProtocolController.struCloudConnection.u32Socket = fd;

    return ZC_RET_OK;
}
/*************************************************
* Function: HF_ConnectToCloud
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_ListenClient(PTC_Connection *pstruConnection)
{
    int fd; 
    struct sockaddr_in servaddr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0)
        return ZC_RET_ERROR;

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port = htons(pstruConnection->u16Port);
    if(bind(fd,(struct sockaddr *)&servaddr,sizeof(servaddr))<0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }
    
    if (listen(fd, TCP_DEFAULT_LISTEN_BACKLOG)< 0)
    {
        close(fd);
        return ZC_RET_ERROR;
    }

    ZC_Printf("Tcp Listen Port = %d\n", pstruConnection->u16Port);
    g_struProtocolController.struClientConnection.u32Socket = fd;

    return ZC_RET_OK;
}

/*************************************************
* Function: HF_BcInit
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_BcInit()
{
    int tmp=1;
    struct sockaddr_in addr; 

    addr.sin_family = AF_INET; 
    addr.sin_port = htons(ZC_MOUDLE_PORT); 
    addr.sin_addr.s_addr=htonl(INADDR_ANY);

    g_Bcfd = socket(AF_INET, SOCK_DGRAM, 0); 

    tmp=1; 
    setsockopt(g_Bcfd, SOL_SOCKET,SO_BROADCAST,&tmp,sizeof(tmp)); 

    hfnet_set_udp_broadcast_port_valid(ZC_MOUDLE_PORT, ZC_MOUDLE_PORT + 1);

    bind(g_Bcfd, (struct sockaddr*)&addr, sizeof(addr)); 

    return;
}
/*************************************************
* Function: HF_SendBc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_SendBc()
{
    struct sockaddr_in addr;
    u16 u16Len;
    static int sleepcount = 0;
    
    if (PCT_STATE_CONNECT_CLOUD != g_struProtocolController.u8MainState)
    {
        sleepcount = 0;
        return;
    }
    sleepcount++;
    if (sleepcount > 250000)
    {
        memset((char*)&addr,0,sizeof(addr));
        addr.sin_family = AF_INET; 
        addr.sin_port = htons(ZC_MOUDLE_BROADCAST_PORT); 
        addr.sin_addr.s_addr=inet_addr("255.255.255.255"); 
        

        EVENT_BuildBcMsg(g_u8MsgBuildBuffer, &u16Len);

        if (g_struProtocolController.u16SendBcNum < (PCT_SEND_BC_MAX_NUM / 2))
        {
           sendto(g_Bcfd,g_u8MsgBuildBuffer,u16Len,0,(struct sockaddr *)&addr,sizeof(addr)); 
           g_struProtocolController.u16SendBcNum++;
        }
        sleepcount = 0;
    }
    
}

/*************************************************
* Function: HF_Cloudfunc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
USER_FUNC static void HF_Cloudfunc(void* arg) 
{
    int fd;
    u32 u32Timer = 0;

    HF_BcInit();

    while(1) 
    {
        fd = g_struProtocolController.struCloudConnection.u32Socket;
        hfthread_mutext_lock(g_struTimermutex);
        PCT_Run();
        
        if (PCT_STATE_DISCONNECT_CLOUD == g_struProtocolController.u8MainState)
        {
            close(fd);
            u32Timer = rand();
            u32Timer = (PCT_TIMER_INTERVAL_RECONNECT) * (u32Timer % 10 + 1);
            PCT_ReconnectCloud(&g_struProtocolController, u32Timer);
            g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
            g_struUartBuffer.u32RecvLen = 0;
        }
        else
        {
            HF_SendDataToCloud(&g_struProtocolController.struCloudConnection);
        }
        HF_SendBc();
        hfthread_mutext_unlock(g_struTimermutex);
    } 
}

/*************************************************
* Function: HF_Init
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Init()
{
    ZC_Printf("MT Init\n");
    g_struHfAdapter.pfunConnectToCloud = HF_ConnectToCloud;
    g_struHfAdapter.pfunListenClient = HF_ListenClient;
    g_struHfAdapter.pfunSendToNet = HF_SendDataToNet;   
    g_struHfAdapter.pfunUpdate = HF_FirmwareUpdate;     
    g_struHfAdapter.pfunUpdateFinish = HF_FirmwareUpdateFinish;
    g_struHfAdapter.pfunSendToMoudle = HF_SendDataToMoudle;  
    g_struHfAdapter.pfunStoreInfo = HF_StoreRegisterInfor;
    g_struHfAdapter.pfunGetCloudKey = HF_GetCloudKey;   
    g_struHfAdapter.pfunGetPrivateKey = HF_GetPrivateKey; 
    g_struHfAdapter.pfunGetVersion = HF_GetVersion;    
    g_struHfAdapter.pfunGetDeviceId = HF_GetDeviceId;   
    g_struHfAdapter.pfunSetTimer = HF_SetTimer;   
    g_struHfAdapter.pfunStopTimer = HF_StopTimer;
    
    g_struHfAdapter.pfunRest = HF_Rest;
    g_u16TcpMss = 1000;
    PCT_Init(&g_struHfAdapter);

    g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
    g_struUartBuffer.u32RecvLen = 0;

    hfthread_create(HF_Cloudfunc,"HF_Cloudfunc",256,NULL,HFTHREAD_PRIORITIES_LOW,NULL,NULL); 
    hfthread_create(HF_CloudRecvfunc,"HF_CloudRecvfunc",256,NULL,HFTHREAD_PRIORITIES_LOW,NULL,NULL); 
    hfthread_mutext_new(&g_struTimermutex);
}

/*************************************************
* Function: HF_WakeUp
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_WakeUp()
{
    PCT_WakeUp();
}
/*************************************************
* Function: HF_Sleep
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Sleep()
{
    PCT_Sleep();
    
    g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
    g_struUartBuffer.u32RecvLen = 0;
}

/******************************* FILE END ***********************************/


