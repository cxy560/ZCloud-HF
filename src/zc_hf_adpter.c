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

extern PTC_ProtocolCon  g_struProtocolController;
PTC_ModuleAdapter g_struHfAdapter;

MSG_Buffer g_struRecvBuffer;
MSG_Buffer g_struRetxBuffer;

MSG_Queue  g_struRecvQueue;
MSG_Buffer g_struSendBuffer[MSG_BUFFER_SEND_MAX_NUM];
MSG_Queue  g_struSendQueue;

u8 g_u8MsgBuildBuffer[MSG_BULID_BUFFER_MAXLEN];
u8 g_u8CiperBuffer[MSG_CIPER_BUFFER_MAXLEN];


u16 g_u16TcpMss;
u16 g_u16LocalPort;


HF_StaInfo g_struHfStaInfo = {
    DEFAULT_IOT_CLOUD_KEY,
    DEFAULT_IOT_PRIVATE_KEY,
    DEFAULT_DEVICIID
};
u8 g_u8recvbuffer[HF_MAX_SOCKET_LEN];
HF_UartBuffer g_struUartBuffer;
HF_TimerInfo g_struHfTimer[ZC_TIMER_MAX_NUM];
hfthread_mutex_t g_struTimermutex;

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
* Function: HF_FirmwareUpdate
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_FirmwareUpdate(u8 *pu8FileData, u32 u32Offset, u32 u32DataLen)
{
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
    if (PCT_EQ_STATUS_ON == g_struProtocolController.u8EqStart)
    {
        ZC_Printf("send to moudle\n");
        hfuart_send(HFUART0,(char*)u8MagicFlag,4,1000); 
        hfuart_send(HFUART0,(char*)pu8Data,u16DataLen,1000); 
    }
    return ZC_RET_OK;
}
/*************************************************
* Function: HF_RecvDataFromMoudle
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_RecvDataFromMoudle(u8 *pu8Data, u16 u16DataLen)
{
    ZC_MessageHead *pstrMsg;
    ZC_RegisterReq *pstruRegister;

    ZC_TraceData(pu8Data, u16DataLen);

    if (0 == u16DataLen)
    {
        return ZC_RET_ERROR;
    }
    
    pstrMsg = (ZC_MessageHead *)pu8Data;
    switch(pstrMsg->MsgCode)
    {
        case ZC_CODE_DESCRIBE:
        {
            ZC_Printf("recv Moudle ZC_CODE_DESCRIBE data\n");
            pstruRegister = (ZC_RegisterReq *)(pstrMsg + 1);
            memcpy(g_struHfStaInfo.u8PrivateKey, pstruRegister->u8ModuleKey, ZC_MODULE_KEY_LEN);
            memcpy(g_struHfStaInfo.u8DeviciId, (u8*)(pstruRegister+1)+sizeof(ZC_MessageOptHead), ZC_HS_DEVICE_ID_LEN);
            memcpy(g_struHfStaInfo.u8DeviciId + ZC_HS_DEVICE_ID_LEN, pstruRegister->u8Domain, ZC_DOMAIN_LEN);
            g_struProtocolController.u8MainState = PCT_STATE_ACCESS_NET; 
            if (PCT_TIMER_INVAILD != g_struProtocolController.u8RegisterTimer)
            {
                TIMER_StopTimer(g_struProtocolController.u8RegisterTimer);
                g_struProtocolController.u8RegisterTimer = PCT_TIMER_INVAILD;
            }
            break;
        }
        default:
            PCT_HandleMoudleEvent(pu8Data, u16DataLen);
            break;
    }
    
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
    
    while(1) 
    {
        if ((g_struProtocolController.u8MainState >= PCT_STATE_WAIT_ACCESSRSP) 
        && (g_struProtocolController.u8MainState < PCT_STATE_DISCONNECT_CLOUD))
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
    
    memset((char*)&addr,0,sizeof(addr));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8384);
    addr.sin_addr.s_addr=inet_addr("192.168.1.111");
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
    while(1) 
    {
        fd = g_struProtocolController.struCloudConnection.u32Socket;
        hfthread_mutext_lock(g_struTimermutex);
        PCT_Run();
        HF_SendDataToCloud(&g_struProtocolController.struCloudConnection);
        if (PCT_STATE_DISCONNECT_CLOUD == g_struProtocolController.u8MainState)
        {
            close(fd);
            PCT_ReconnectCloud(&g_struProtocolController);
            g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
            g_struUartBuffer.u32RecvLen = 0;
        }
        hfthread_mutext_unlock(g_struTimermutex);
    } 
}

/*************************************************
* Function: HF_AssemblePkt
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
u32 HF_AssemblePkt(u8 *pu8Data, u32 u32DataLen) 
{
    ZC_MessageHead *pstruMsg;
    u8 u8MagicHead[4] = {0x02,0x03,0x04,0x05};
    u32 u32HeadLen;
    u32 u32MsgLen;

    u32HeadLen = sizeof(RCTRL_STRU_MSGHEAD) + sizeof(ZC_MessageHead);
    if (MSG_BUFFER_FULL == g_struUartBuffer.u32Status)
    {
        return ZC_RET_ERROR;
    }
    
    if (MSG_BUFFER_IDLE == g_struUartBuffer.u32Status)
    {

        if (u32DataLen < u32HeadLen)
        {
            memcpy(g_struUartBuffer.u8UartBuffer, pu8Data, u32DataLen);
            g_struUartBuffer.u32Status = MSG_BUFFER_SEGMENT_NOHEAD;
            g_struUartBuffer.u32RecvLen = u32DataLen;
        }
        else
        {
            if (0 != memcmp(pu8Data, u8MagicHead, 4))
            {
                g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
                g_struUartBuffer.u32RecvLen = 0;
                return ZC_RET_ERROR;
            }
            
            pstruMsg = (ZC_MessageHead *)(pu8Data + sizeof(RCTRL_STRU_MSGHEAD));
            u32MsgLen =  ZC_HTONS(pstruMsg->Payloadlen) + sizeof(ZC_MessageHead) + sizeof(RCTRL_STRU_MSGHEAD);

            if (u32MsgLen > HF_MAX_UARTBUF_LEN)
            {
                g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
                g_struUartBuffer.u32RecvLen = 0;
                return ZC_RET_ERROR;
            }

            if (u32MsgLen <= u32DataLen)
            {
                memcpy(g_struUartBuffer.u8UartBuffer, pu8Data, u32MsgLen);
                g_struUartBuffer.u32Status = MSG_BUFFER_FULL;
                g_struUartBuffer.u32RecvLen = u32MsgLen;
            }
            else
            {
                memcpy(g_struUartBuffer.u8UartBuffer, pu8Data, u32DataLen);
                g_struUartBuffer.u32Status = MSG_BUFFER_SEGMENT_HEAD;
                g_struUartBuffer.u32RecvLen = u32DataLen;
            }

        }

        return ZC_RET_OK;

    }

    if (MSG_BUFFER_SEGMENT_HEAD == g_struUartBuffer.u32Status)
    {
        pstruMsg = (ZC_MessageHead *)(g_struUartBuffer.u8UartBuffer + sizeof(RCTRL_STRU_MSGHEAD));
        u32MsgLen = ZC_HTONS(pstruMsg->Payloadlen) + sizeof(ZC_MessageHead) + sizeof(RCTRL_STRU_MSGHEAD);

        if (u32MsgLen <= u32DataLen + g_struUartBuffer.u32RecvLen)
        {
            memcpy((g_struUartBuffer.u8UartBuffer + g_struUartBuffer.u32RecvLen), 
                pu8Data, 
                (u32MsgLen - g_struUartBuffer.u32RecvLen));

            g_struUartBuffer.u32Status = MSG_BUFFER_FULL;
            g_struUartBuffer.u32RecvLen = u32MsgLen;
        }
        else
        {
            memcpy((g_struUartBuffer.u8UartBuffer + g_struUartBuffer.u32RecvLen), 
                pu8Data, 
                u32DataLen);
            g_struUartBuffer.u32RecvLen += u32DataLen;
            g_struUartBuffer.u32Status = MSG_BUFFER_SEGMENT_HEAD;
        }

        return ZC_RET_OK;
    }

    if (MSG_BUFFER_SEGMENT_NOHEAD == g_struUartBuffer.u32Status)
    {
        if ((g_struUartBuffer.u32RecvLen + u32DataLen) < u32HeadLen)
        {
            memcpy((g_struUartBuffer.u8UartBuffer + g_struUartBuffer.u32RecvLen), 
                pu8Data,
                u32DataLen);
            g_struUartBuffer.u32RecvLen += u32DataLen;
            g_struUartBuffer.u32Status = MSG_BUFFER_SEGMENT_NOHEAD;
        }
        else
        {
            memcpy((g_struUartBuffer.u8UartBuffer + g_struUartBuffer.u32RecvLen), 
                pu8Data,
                (u32HeadLen - g_struUartBuffer.u32RecvLen));

            if (0 != memcmp(g_struUartBuffer.u8UartBuffer, u8MagicHead, 4))
            {
                g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
                g_struUartBuffer.u32RecvLen = 0;
                return ZC_RET_ERROR;
            }

            pstruMsg = (ZC_MessageHead *)(g_struUartBuffer.u8UartBuffer + sizeof(RCTRL_STRU_MSGHEAD));
            u32MsgLen = ZC_HTONS(pstruMsg->Payloadlen) + sizeof(ZC_MessageHead) + sizeof(RCTRL_STRU_MSGHEAD);

            if (u32MsgLen > MSG_BUFFER_MAXLEN)
            {
                g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
                g_struUartBuffer.u32RecvLen = 0;                
                return ZC_RET_ERROR;
            }

            if (u32MsgLen <= u32DataLen + g_struUartBuffer.u32RecvLen)
            {
                memcpy((g_struUartBuffer.u8UartBuffer + g_struUartBuffer.u32RecvLen), 
                    pu8Data,
                    u32MsgLen - g_struUartBuffer.u32RecvLen);
                g_struUartBuffer.u32Status = MSG_BUFFER_FULL;
                g_struUartBuffer.u32RecvLen = u32MsgLen;

            }
            else
            {
                memcpy((g_struUartBuffer.u8UartBuffer + g_struUartBuffer.u32RecvLen), 
                    pu8Data,
                    u32DataLen);
                g_struUartBuffer.u32Status = MSG_BUFFER_SEGMENT_HEAD;
                g_struUartBuffer.u32RecvLen += u32DataLen;
            }

        }

        return ZC_RET_OK;

    }
    return ZC_RET_ERROR;
}

/*************************************************
* Function: HF_Moudlefunc
* Description: 
* Author: cxy 
* Returns: 
* Parameter: 
* History:
*************************************************/
void HF_Moudlefunc(u8 *pu8Data, u32 u32DataLen) 
{
    u32 u32RetVal;

    u32RetVal = HF_AssemblePkt(pu8Data, u32DataLen);

    if (ZC_RET_ERROR == u32RetVal)
    {
        return;
    }

    if (MSG_BUFFER_FULL == g_struUartBuffer.u32Status)
    {
        HF_RecvDataFromMoudle(g_struUartBuffer.u8UartBuffer + sizeof(RCTRL_STRU_MSGHEAD), 
            g_struUartBuffer.u32RecvLen - sizeof(RCTRL_STRU_MSGHEAD));
        g_struUartBuffer.u32Status = MSG_BUFFER_IDLE;
        g_struUartBuffer.u32RecvLen = 0;
    }

    return; 
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
    g_struHfAdapter.pfunSendToCloud = HF_SendDataToCloud;   
    g_struHfAdapter.pfunUpdate = HF_FirmwareUpdate;        
    g_struHfAdapter.pfunSendToMoudle = HF_SendDataToMoudle;  
    g_struHfAdapter.pfunRecvFormMoudle = HF_RecvDataFromMoudle;
    g_struHfAdapter.pfunGetCloudKey = HF_GetCloudKey;   
    g_struHfAdapter.pfunGetPrivateKey = HF_GetPrivateKey; 
    g_struHfAdapter.pfunGetVersion = HF_GetVersion;    
    g_struHfAdapter.pfunGetDeviceId = HF_GetDeviceId;   
    g_struHfAdapter.pfunSetTimer = HF_SetTimer;   
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


