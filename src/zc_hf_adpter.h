/**
******************************************************************************
* @file     zc_hf_adpter.h
* @authors  cxy
* @version  V1.0.0
* @date     10-Sep-2014
* @brief    HANDSHAKE
******************************************************************************
*/

#ifndef  __ZC_HF_ADPTER_H__ 
#define  __ZC_HF_ADPTER_H__

#include <zc_common.h>
#include <zc_protocol_controller.h>
#include <zc_module_interface.h>

typedef struct 
{
    u32 u32FirstFlag;
    hftimer_handle_t struHandle;
}HF_TimerInfo;


#define HF_MAX_SOCKET_LEN    (1000)





#ifdef __cplusplus
extern "C" {
#endif
void HF_Init(void);
void HF_WakeUp(void);
void HF_Sleep(void);
void HF_ReadDataFormFlash(void);
void HF_WriteDataToFlash(u8 *pu8Data, u16 u16Len);
#ifdef __cplusplus
}
#endif
#endif
/******************************* FILE END ***********************************/

