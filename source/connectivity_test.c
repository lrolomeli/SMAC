/*!
* The Clear BSD License
* Copyright (c) 2015, Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
* All rights reserved.
* 
* \file
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
* 
* * Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
* 
* * Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
* 
* * Neither the name of the copyright holder nor the names of its
*   contributors may be used to endorse or promote products derived from
*   this software without specific prior written permission.
* 
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/************************************************************************************
*************************************************************************************
* Include
*************************************************************************************
************************************************************************************/

#include "connectivity_test.h"
#include "connectivity_test_platform.h"
#include "fsl_debug_console.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_common.h"
#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_port.h"
#include "pin_mux.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BOARD_LED_GPIO BOARD_LED_RED_GPIO
#define BOARD_LED_GPIO_PIN BOARD_LED_RED_GPIO_PIN

#define BOARD_SW_GPIO BOARD_SW3_GPIO
#define BOARD_SW_PORT BOARD_SW3_PORT
#define BOARD_SW_GPIO_PIN BOARD_SW3_GPIO_PIN
#define BOARD_SW_IRQ BOARD_SW3_IRQ
#define BOARD_SW_IRQ_HANDLER BOARD_SW3_IRQ_HANDLER
#define BOARD_SW_NAME BOARD_SW3_NAME
/************************************************************************************
*************************************************************************************
* Private type definitions
*************************************************************************************
************************************************************************************/
#if CT_Feature_RSSI_Has_Sign
typedef int8_t energy8_t;
typedef int32_t energy32_t;
#else
typedef uint8_t energy8_t;
typedef uint32_t energy32_t;
#endif
/************************************************************************************
*************************************************************************************
* Macros
*************************************************************************************
************************************************************************************/
#define gPrbs9BufferLength_c	 ( 65 )
#define gContTxModSelectPN9_c    ( 2 )
#define gContTxModSelectOnes_c   ( 1 )
#define gContTxModSelectZeros_c  ( 0 )
#define SelfNotificationEvent()  ((void)OSA_EventSet(gTaskEvent, gCTSelf_EVENT_c))

#define gUART_RX_EVENT_c         (1<<0)
#define gMcps_Cnf_EVENT_c        (1<<1)
#define gMcps_Ind_EVENT_c        (1<<2)
#define gMlme_EdCnf_EVENT_c      (1<<3)
#define gMlme_CcaCnf_EVENT_c     (1<<4)
#define gMlme_TimeoutInd_EVENT_c (1<<5)
#define gRangeTest_EVENT_c       (1<<6)
#define gCTSelf_EVENT_c          (1<<7)
#define gTimePassed_EVENT_c      (1<<8)
//#define gButtonPressed_Event_c	 (1<<9)

#define gEventsAll_c             (gUART_RX_EVENT_c | gMcps_Ind_EVENT_c | gMcps_Cnf_EVENT_c | \
gMlme_TimeoutInd_EVENT_c | gMlme_EdCnf_EVENT_c | gMlme_CcaCnf_EVENT_c | \
    gRangeTest_EVENT_c | gCTSelf_EVENT_c | gTimePassed_EVENT_c /*| gButtonPressed_Event_c*/)

#define Delay_ms(a)        
#define FlaggedDelay_ms(a)       TMR_StartSingleShotTimer(AppDelayTmr, a, DelayTimeElapsed, NULL)

#ifdef gPHY_802_15_4g_d
#define GetTimestampUS() PhyTime_GetTimestampUs()
#define GetTransmissionTime(payload, bitrate) ((((gPhyFSKPreambleLength_c + \
gPhyMRFSKPHRLength_c + gPhyMRFSKSFDLength_c + \
    sizeof(smacHeader_t) + payload +  gPhyFCSSize_c )*8000 )/ bitrate))
#else
#define GetTimestampUS() (16*PhyTime_GetTimestamp())
#define GetTransmissionTime(payload, bitrate) (((6 + sizeof(smacHeader_t) + payload + 2)*32))
//bitrate is fixed for 2.4 GHz
#define crtBitrate      (0)
#endif

#if gMpmMaxPANs_c == 2
#define gNumPans_c   2
#else
#define gNumPans_c   1
#endif
/************************************************************************************
*************************************************************************************
* Public memory declarations
*************************************************************************************
************************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Whether the SW button is pressed */
volatile bool g_ButtonPress = false;

/*osa variables*/
osaEventId_t          gTaskEvent;
osaEventFlags_t       gTaskEventFlags;

/*smac related variables*/
bool_t bTxDone;
bool_t bRxDone;
bool_t bScanDone;
bool_t gCCaGotResult;
bool_t gIsChannelIdle;
bool_t bEdDone;
bool_t failedPRBS9;
uint8_t u8LastRxRssiValue;
bool_t evTestParameters;
uint8_t au8ScanResults[129];
#if gMpmMaxPANs_c == 2
bool_t bDataInd[2];
uint8_t u8PanRSSI[2];
#endif

/*serial manager related variables*/
uint8_t gu8UartData;
bool_t evDataFromUART;
uint8_t mAppSer;

/*connectivity test state machine variables*/
operationModes_t testOpMode;                                                    
operationModes_t prevOpMode; 

channels_t       testChannel;
uint8_t          testPower;
uint8_t          testPayloadLen;
uint8_t          contTxModBitValue;
uint8_t          ccaThresh;
uint8_t ChannelToScan;
bool_t shortCutsEnabled;
ConnectivityStates_t       connState;
ContinuousTxRxTestStates_t cTxRxState;
PerTxStates_t              perTxState;
PerRxStates_t              perRxState;
RangeTxStates_t            rangeTxState;
RangeRxStates_t            rangeRxState;
SendStates_t		sendState;
ReceiveStates_t		ReceiveState;
EditRegsStates_t    eRState; 
oRStates_t          oRState;
rRStates_t          rRState;
dRStates_t          dRState;
CSenseTCtrlStates_t   cstcState; 
smacTestMode_t contTestRunning;

#if CT_Feature_Xtal_Trim
uint8_t          xtalTrimValue;
#endif

/*asp related variables*/
AppToAspMessage_t aspTestRequestMsg;

extern uint8_t u8Prbs9Buffer[gPrbs9BufferLength_c];
/************************************************************************************
*************************************************************************************
* Private memory declarations
*************************************************************************************
************************************************************************************/
static uint8_t gau8RxDataBuffer[gMaxSmacSDULength_c  + sizeof(rxPacket_t)];   
#if gMpmMaxPANs_c == 2
static uint8_t gau8RxDataBufferAlt[gMaxSmacSDULength_c + sizeof(rxPacket_t)];
#endif
static uint8_t gau8TxDataBuffer[gMaxSmacSDULength_c  + sizeof(txPacket_t)];                        

static txPacket_t * gAppTxPacket;
static rxPacket_t * gAppRxPacket;

static uint8_t timePassed;
static tmrTimerID_t RangeTestTmr;                                                     
static tmrTimerID_t AppDelayTmr;
/************************************************************************************
*************************************************************************************
* Private prototypes
*************************************************************************************
************************************************************************************/
#if CT_Feature_Calibration
extern void StoreTrimValueToFlash (uint32_t trimValue, CalibrationOptionSelect_t option);
#endif

/*platform independent functions*/
static void SerialUIStateMachine(void);
static bool_t SerialContinuousTxRxTest(void);
static bool_t PacketErrorRateTx(void);
static bool_t PacketErrorRateRx(void);
static bool_t SendReceivePackets(operationModes_t mode);
static void SetRadioRxOnNoTimeOut(void);
static void HandleEvents(int32_t evSignals);

static void PrintPerRxFinalLine(uint16_t u16Received, uint16_t u16Total);
static bool_t stringComp(uint8_t * au8leftString, uint8_t * au8RightString, uint8_t bytesToCompare);

#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
static uint32_t HexString2Dec(uint8_t* hexString);
#endif
/********************************/

static void RangeTest_Timer_CallBack ();
static bool_t RangeTx(void);
static bool_t RangeRx(void);

static bool_t EditRegisters(void);
bool_t OverrideRegisters(void);
bool_t ReadRegisters(void);
bool_t DumpRegisters(void);
bool_t bIsRegisterDirect = TRUE;

static bool_t CSenseAndTCtrl(void);
static void TransmissionControlHandler(void);
static void CarrierSenseHandler(void);
static smacErrors_t TestMode ( smacTestMode_t  mode);
static void PacketHandler_Prbs9(void);
static void DelayTimeElapsed(void*);
static void IncrementChannelOnEdEvent();
#if gMpmMaxPANs_c == 2
static bool_t ConfigureAlternatePan(void);
#endif

extern void ReadRFRegs(registerAddressSize_t, registerAddressSize_t);
extern void PrintTestParameters(bool_t bEraseLine);

/*************************************/
/************************************************************************************
*************************************************************************************
* Public functions
*************************************************************************************
************************************************************************************/
void InitProject(void);
void InitSmac(void);
void main_task(uint32_t param);
extern void ResetMCU(void);
void UartRxCallBack(void * param);

/************************************************************************************
*
* InitProject
*
************************************************************************************/
void InitProject(void)
{   
    /*Global Data init*/
    testPayloadLen = gMaxSmacSDULength_c;
    
    testOpMode       = gDefaultOperationMode_c;
    testChannel      = gDefaultChannelNumber_c;
    testPower        = gDefaultOutputPower_c;
    testPayloadLen   = gDefaultPayload_c;
    contTestRunning  = gTestModeForceIdle_c;
    shortCutsEnabled = FALSE; 
    connState        = gConnInitState_c;
    cTxRxState       = gCTxRxStateInit_c;
    perTxState       = gPerTxStateInit_c;
    perRxState       = gPerRxStateInit_c;
    rangeTxState     = gRangeTxStateInit_c;
    rangeRxState     = gRangeRxStateInit_c;
    prevOpMode       = gDefaultOperationMode_c;
    oRState          = gORStateInit_c;
    rRState          = gRRStateInit_c;
    dRState          = gDRStateInit_c;
    ccaThresh        = gDefaultCCAThreshold_c;
    bEdDone          = FALSE;
    evDataFromUART = FALSE; 
#if gMpmMaxPANs_c == 2
    bDataInd[0]      = FALSE;
    bDataInd[1]      = FALSE;
#endif
    
    InitProject_custom();
}

/************************************************************************************
*************************************************************************************
* SAP functions
*************************************************************************************
************************************************************************************/

/*(Management) Sap handler for managing timeout indication and ED confirm */
smacErrors_t smacToAppMlmeSap(smacToAppMlmeMessage_t* pMsg, instanceId_t instance)
{
    switch(pMsg->msgType)
    {
    case gMlmeEdCnf_c:
        au8ScanResults[pMsg->msgData.edCnf.scannedChannel] = pMsg->msgData.edCnf.energyLeveldB;
        (void)OSA_EventSet(gTaskEvent, gMlme_EdCnf_EVENT_c);
        break;
    case gMlmeCcaCnf_c:
        (void)OSA_EventSet(gTaskEvent, gMlme_CcaCnf_EVENT_c);
        if(pMsg->msgData.ccaCnf.status == gErrorNoError_c)
            gIsChannelIdle = TRUE;
        else
            gIsChannelIdle = FALSE;
        break;
    case gMlmeTimeoutInd_c:
        (void)OSA_EventSet(gTaskEvent, gMlme_TimeoutInd_EVENT_c);
	break;
    default:
        break;
    }
    MEM_BufferFree(pMsg);
    return gErrorNoError_c;
}
/*(Data) Sap handler for managing data confirm and data indication */
smacErrors_t smacToAppMcpsSap(smacToAppDataMessage_t* pMsg, instanceId_t instance)
{
    switch(pMsg->msgType)
    {
    case gMcpsDataInd_c:
        if(pMsg->msgData.dataInd.pRxPacket->rxStatus == rxSuccessStatus_c)
        {
            u8LastRxRssiValue = pMsg->msgData.dataInd.u8LastRxRssi;
#if gMpmMaxPANs_c == 2
            bDataInd[instance] = TRUE;
            u8PanRSSI[instance] = pMsg->msgData.dataInd.u8LastRxRssi;
#endif
            (void)OSA_EventSet(gTaskEvent, gMcps_Ind_EVENT_c);
        }
        break;
    case gMcpsDataCnf_c:
        if(pMsg->msgData.dataCnf.status == gErrorNoError_c)
        {
            (void)OSA_EventSet(gTaskEvent, gMcps_Cnf_EVENT_c);
        }
        break;
    default:
        break;
    }
    
    MEM_BufferFree(pMsg);
    return gErrorNoError_c;
}

static void HandleEvents(int32_t evSignals)
{
    uint16_t u16SerBytesCount = 0;
    
    if(evSignals & gUART_RX_EVENT_c)
    {
        if(gSerial_Success_c == Serial_GetByteFromRxBuffer(mAppSer, &gu8UartData, &u16SerBytesCount))
        {
            if(shortCutsEnabled)
            {
                ShortCutsParser(gu8UartData);  
            }
            else
            {
                evDataFromUART = TRUE;
            }
            Serial_RxBufferByteCount(mAppSer, &u16SerBytesCount);
            if(u16SerBytesCount)
            {
                (void)OSA_EventSet(gTaskEvent, gUART_RX_EVENT_c);
            }
        }
    }
    if(evSignals & gMcps_Cnf_EVENT_c)
    {
        bTxDone = TRUE;
    }
    if(evSignals & gMcps_Ind_EVENT_c)
    {
        bRxDone = TRUE;
    }
    if(evSignals & gMlme_TimeoutInd_EVENT_c)
    {
    }
    if(evSignals & gRangeTest_EVENT_c)
    {
        bRxDone=TRUE; 
    }
    if(evSignals & gMlme_EdCnf_EVENT_c)
    {
        if (cTxRxState == gCTxRxStateRunnigScanTest_c)
        {
            IncrementChannelOnEdEvent();
        }
        if (cTxRxState == gCTxRxStateRunnigEdTest_c)
        {
            cTxRxState = gCTxRxStateRunningEdTestGotResult_c;
        }
        if (connState == gConnCSenseAndTCtrl_c)
        {
            bScanDone = TRUE;
        }
        bEdDone = TRUE;
    }
    if(evSignals & gMlme_CcaCnf_EVENT_c)
    {
        gCCaGotResult = TRUE;
        Serial_Print(mAppSer, "Channel ", gAllowToBlock_d);
        Serial_PrintDec(mAppSer, (uint32_t)testChannel);
        Serial_Print(mAppSer, " is ", gAllowToBlock_d);
        if(gIsChannelIdle)
            Serial_Print(mAppSer,"Idle\r\n", gAllowToBlock_d);
        else
            Serial_Print(mAppSer,"Busy\r\n", gAllowToBlock_d);
    }
    if(evSignals & gCTSelf_EVENT_c)
    {
    }
}

/*!
 * @brief Interrupt service fuction of switch.
 *
 * This function toggles the LED
 */
void BOARD_SW_IRQ_HANDLER(void)
{
    /* Clear external interrupt flag. */
    GPIO_ClearPinsInterruptFlags(BOARD_SW_GPIO, 1U << BOARD_SW_GPIO_PIN);
    /* Change state of button. */
    g_ButtonPress = true;
    SelfNotificationEvent();//OSA_EventSet(gTaskEvent, gButtonPressed_Event_c);
}

void button_Init(void){

	/* Define the init structure for the input switch pin */
	    gpio_pin_config_t sw_config = {
	        kGPIO_DigitalInput, 0,
	    };

    CLOCK_EnableClock(kCLOCK_PortC);                           /* Port C Clock Gate Control: Clock enabled */

      const port_pin_config_t portc4_pin40_config = {
        kPORT_PullUp,                                            /* Internal pull-up resistor is enabled */
        kPORT_SlowSlewRate,                                      /* Slow slew rate is configured */
        kPORT_PassiveFilterDisable,                              /* Passive filter is disabled */
        kPORT_LowDriveStrength,                                  /* Low drive strength is configured */
        kPORT_MuxAsGpio,                                         /* Pin is configured as PTC4 */
      };

      PORT_SetPinConfig(PORTC, 4, &portc4_pin40_config);  /* PORTC4 (pin 40) is configured as PTC4 */

      /* Init input switch GPIO. */
          PORT_SetPinInterruptConfig(BOARD_SW_PORT, BOARD_SW_GPIO_PIN, kPORT_InterruptFallingEdge);
          EnableIRQ(BOARD_SW_IRQ);
          GPIO_PinInit(BOARD_SW_GPIO, BOARD_SW_GPIO_PIN, &sw_config);

}
/*************************************************************************/
/*Main Task: Application entry point*/
/*************************************************************************/
void main_task(uint32_t param)
{
    static bool_t bIsInitialized = FALSE;
    static bool_t bUserInteraction = FALSE;
    //Initialize Memory Manager, Timer Manager and LEDs.
    if( !bIsInitialized )
    {
        hardware_init();
        
        MEM_Init();
        TMR_Init();
        LED_Init();
        button_Init();
        //initialize PHY
        Phy_Init();
        //initialize Serial Manager
        SerialManager_Init();
        
        LED_StartSerialFlash(LED1);
        
        gTaskEvent = OSA_EventCreate(TRUE);
        InitApp();
        
        /*Prints the Welcome screens in the terminal*/  
        PrintMenu(cu8Logo, mAppSer);
        
        connState = gConnIdleState_c; 
        bIsInitialized = TRUE;
    }
    if(!bUserInteraction)
    {
        while(1)
        {
            (void)OSA_EventWait(gTaskEvent, gEventsAll_c, FALSE, osaWaitForever_c ,&gTaskEventFlags);
            HandleEvents(gTaskEventFlags);
            if(evDataFromUART)
            {
                evDataFromUART = FALSE;
                if(gu8UartData == '\r')
                {
                    LED_StopFlashingAllLeds();
                    SelfNotificationEvent();
                    bUserInteraction = TRUE;
                    break;
                }
                else
                {
                    PrintMenu(cu8Logo, mAppSer);
                }
            }
            if(gUseRtos_c == 0)
            {
                break;
            }
        }
    }
    if(bUserInteraction)
    {
        while(1)
        {
            (void)OSA_EventWait(gTaskEvent, gEventsAll_c, FALSE, osaWaitForever_c ,&gTaskEventFlags);
            HandleEvents(gTaskEventFlags);
            SerialUIStateMachine();  
            if (gUseRtos_c == 0)
            {
                break;
            } 
        }
    } 
}

/*************************************************************************/
/*InitApp: Initializes application modules and data*/
/*************************************************************************/
void InitApp()
{
    RangeTestTmr = TMR_AllocateTimer();             //Allocate a timer for Range Test option
    AppDelayTmr  = TMR_AllocateTimer();             //Allocate a timer for inserting delays whenever needed
    
    gAppTxPacket = (txPacket_t*)gau8TxDataBuffer;   //Map TX packet to buffer
    gAppRxPacket = (rxPacket_t*)gau8RxDataBuffer;   //Map Rx packet to buffer     
    gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
    
    Serial_InitInterface( &mAppSer, APP_SERIAL_INTERFACE_TYPE, APP_SERIAL_INTERFACE_INSTANCE ); //Get handle of uart interface
    Serial_SetBaudRate (mAppSer, gUARTBaudRate115200_c);   //Set 115200 as default baud
    Serial_SetRxCallBack(mAppSer, UartRxCallBack, NULL);   //Set Receive callback for uart 
    
    //Initialize SMAC
    InitSmac();
    //Tell SMAC who to call when it needs to pass a message to the application thread.
    Smac_RegisterSapHandlers((SMAC_APP_MCPS_SapHandler_t)smacToAppMcpsSap,(SMAC_APP_MLME_SapHandler_t)smacToAppMlmeSap,0);
#if gMpmMaxPANs_c == 2
    Smac_RegisterSapHandlers((SMAC_APP_MCPS_SapHandler_t)smacToAppMcpsSap,(SMAC_APP_MLME_SapHandler_t)smacToAppMlmeSap,1);
#endif
    
    InitProject();
    
    InitApp_custom();
    
    ASP_Init(0);
    
    SMACFillHeader(&(gAppTxPacket->smacHeader), gBroadcastAddress_c);                   //@CMA, Conn Test. Start with broadcast address default
    (void)MLMEPAOutputAdjust(testPower);
    (void)MLMESetChannelRequest(testChannel);                                     //@CMA, Conn Test. Start Foperation at default channel
}

void button_pressed(void)
{
	if (g_ButtonPress)
	{
		g_ButtonPress = false;
		LED_ToggleLed(LED_ALL);
		Serial_Print(mAppSer, "\n\r -Enviando\n\r\n",gAllowToBlock_d);
		gAppTxPacket->u8DataLength = 13;
	    FLib_MemCpy(&(gAppTxPacket->smacPdu.smacPdu[0]), "Equipo 2, Sw3\n",13);
	    gAppTxPacket->smacPdu.smacPdu[0+13] = '\0';
	    Serial_Print(mAppSer, (char * )&(gAppTxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);
	    (void)MCPSDataRequest(gAppTxPacket);

	}
}

/************************************************************************************
*
* Connectivity Test State Machine
*
************************************************************************************/
void SerialUIStateMachine(void)
{
    if((gConnSelectTest_c == connState) && evTestParameters)
    {
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);   
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;

    }

    button_pressed();

    switch(connState)
    {
    case gConnIdleState_c:
        PrintMenu(cu8MainMenu, mAppSer);
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;
        connState = gConnSelectTest_c;
        break;
    case gConnSelectTest_c:
        if(evDataFromUART){
            if('1' == gu8UartData)
            {
                cTxRxState = gCTxRxStateInit_c;
                connState = gConnContinuousTxRxState_c;
            }
            else if('2' == gu8UartData)
            {
                sendState = gPerTxStateInit_c;
                perRxState = gPerRxStateInit_c;
                connState = gConnPerState_c;
            }
            else if('3' == gu8UartData)
            {
                rangeTxState = gRangeTxStateInit_c;
                rangeRxState = gRangeRxStateInit_c;
                connState = gConnRangeState_c;
            }
            else if('4' == gu8UartData)
            {
                cstcState = gCsTcStateInit_c;
                connState = gConnCSenseAndTCtrl_c;
            }

            else if('7' == gu8UartData)
            {
                connState = gConnSendReceiveState_c;
                sendState = gSendStateInit_c;
            }

            else if('S' == gu8UartData)
            {
                if(mTxOperation_c == testOpMode)
                {
                	LED_ToggleLed(LED_ALL);
                	Serial_Print(mAppSer, "\n\r -Enviando\n\r\n",gAllowToBlock_d);
                	gAppTxPacket->u8DataLength = 11;
                    FLib_MemCpy(&(gAppTxPacket->smacPdu.smacPdu[0]), "Equipo 2, S",11);
                    //char * pelon = (char *)&(gAppTxPacket->smacPdu.smacPdu[0]);
                    gAppTxPacket->smacPdu.smacPdu[0+11] = '\0';
                    Serial_Print(mAppSer, (char * )&(gAppTxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);
                    (void)MCPSDataRequest(gAppTxPacket);
                }
                else
                {
                	Serial_Print(mAppSer, "\n\r -Recibiendo\n\r\n",gAllowToBlock_d);
//                	(void)MLMERXEnableRequest(gAppRxPacket, 0);
//                	if (gAppRxPacket->rxStatus == rxSuccessStatus_c)
//                	{
//                        Serial_Print(mAppSer, (char * )&(u16PacketsIndex[gAppRxPacket->smacPdu.smacPdu[0]]),gAllowToBlock_d);
//                	}
                }

            }
#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
            else if('5' == gu8UartData)
            {
                eRState = gERStateInit_c;
                connState = gConnRegEditState_c;
            }
#endif
#if CT_Feature_Bitrate_Select
            else if('6' == gu8UartData)
            {
                bsState = gBSStateInit_c;
                connState = gConnBitrateSelectState_c;
            }
#endif
#if CT_Feature_Calibration
            else if('7' == gu8UartData)
            {
                connState = gConnEDMeasCalib_c;
                edCalState= gEdCalStateInit_c;
            }
#endif
            else if('!' == gu8UartData)
            {
                ResetMCU();
            }
            evDataFromUART = FALSE;
            SelfNotificationEvent();
        }
        break;
    case gConnContinuousTxRxState_c:
        if(SerialContinuousTxRxTest()) 
        {
            connState = gConnIdleState_c;
            SelfNotificationEvent();
        }
        break;
    case gConnPerState_c:
        if(mTxOperation_c == testOpMode)
        {
            if(PacketErrorRateTx())
            {
                connState = gConnIdleState_c;
                SelfNotificationEvent();
            }
        }
        else
        {
            if(PacketErrorRateRx())
            {
                connState = gConnIdleState_c;
                SelfNotificationEvent();
            }
        }
        break;
    case gConnRangeState_c:
        if(mTxOperation_c == testOpMode)
        {
            if(RangeTx())
            {
                connState = gConnIdleState_c;
                SelfNotificationEvent();
            }
        }
        else
        {
            if(RangeRx())
            {
                connState = gConnIdleState_c;
                SelfNotificationEvent();
            }
        }
        break;
    case gConnRegEditState_c:
        if(EditRegisters()) 
        {
            connState = gConnIdleState_c;
            SelfNotificationEvent();
        }
        break;
    case gConnSendReceiveState_c:
        if(mTxOperation_c == testOpMode)
        {
            if(SendReceivePackets(mTxOperation_c))
            {
                connState = gConnIdleState_c;
                SelfNotificationEvent();
            }
        }
        else
        {
            if(SendReceivePackets(mRxOperation_c))
            {
                connState = gConnIdleState_c;
                SelfNotificationEvent();
            }
        }
        break;

#if CT_Feature_Bitrate_Select
    case gConnBitrateSelectState_c:
        if(Bitrate_Select()) 
        {
            connState = gConnIdleState_c;
        }
        break;
#endif
    case gConnCSenseAndTCtrl_c:
        if(CSenseAndTCtrl()) 
        {
            connState = gConnIdleState_c;
            SelfNotificationEvent();
        }
        break;
#if CT_Feature_Calibration
    case gConnEDMeasCalib_c:
        if(EDCalibrationMeasurement())
        {
            connState = gConnIdleState_c;
            SelfNotificationEvent();
        }
        break;
#endif
    default:
        break;
        
    }
    if(prevOpMode != testOpMode)
    {
        perTxState = gPerTxStateInit_c;
        perRxState = gPerRxStateInit_c;
        rangeTxState = gRangeTxStateInit_c;
        rangeRxState = gRangeRxStateInit_c;
        prevOpMode = testOpMode;
        SelfNotificationEvent();
    }
}




bool_t SendReceivePackets(operationModes_t mode)
{
	/*********************
	 * Send Variables *
	 ********************/
	static uint8_t menuSelection;
    static uint8_t payload = 0;
    bool_t bBackFlag = FALSE;

    /*********************
     * Receive Variables *
     ********************/
    static energy32_t e32RssiSum[gNumPans_c];
    static uint16_t u16ReceivedPackets[gNumPans_c];
    static uint16_t u16PacketsIndex[gNumPans_c];
    static uint16_t u16TotalPackets[gNumPans_c];
    energy8_t e8TempRssivalue;

    if(mTxOperation_c == mode && evTestParameters)
    {
    	(void)MLMERXDisableRequest();
    }
    else if(evTestParameters)
    {
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }

    if(mTxOperation_c == mode)
    {

        switch(sendState)
        {
        case gSendStateInit_c:
            PrintMenu(cu8ShortCutsBar, mAppSer);
            PrintMenu(cu8SendTestMenu, mAppSer);
            PrintTestParameters(FALSE);
            shortCutsEnabled = TRUE;
            sendState = gSendStateSelectPacketNum_c;
            (void)MLMERXDisableRequest();
            break;
        case gSendStateSelectPacketNum_c:
            if(evDataFromUART)
            {
            	switch(gu8UartData)
            	{

            	case '1':
            		menuSelection = 1;
            		sendState = gSendStateStartTest_c;
            		SelfNotificationEvent();
            		break;
            	case '2':
            		menuSelection = 2;
            		sendState = gSendStateStartTest_c;
            		SelfNotificationEvent();
            		break;
            	case '3':
            		menuSelection = 3;
            		sendState = gSendStateStartTest_c;
            		SelfNotificationEvent();
            		break;
            	case '4':
            		menuSelection = 4;
            		sendState = gSendStateStartTest_c;
            		SelfNotificationEvent();
            		break;

            	case 'p':
            		bBackFlag = TRUE;
            		SelfNotificationEvent();
            		break;
            	}
                evDataFromUART = FALSE;
            }
            break;

        case gSendStateStartTest_c:

			switch(menuSelection)
			{
			case 1:
				gAppTxPacket->u8DataLength = menuSelection;
				gAppTxPacket->smacPdu.smacPdu[0] = 1;
	            Serial_Print(mAppSer, "\f\r\n Running Sending test (1)", gAllowToBlock_d);
	            bTxDone = FALSE;
	            (void)MCPSDataRequest(gAppTxPacket);
	            sendState = gSendStateIdle_c;
				break;
			case 2:
				Serial_Print(mAppSer, "\f\r\n Running Sending test (2)", gAllowToBlock_d);
				shortCutsEnabled = FALSE;
				if(evDataFromUART)
					{
						if((gu8UartData >= '0') && (gu8UartData <= '9'))
						{
							gAppTxPacket->u8DataLength = gu8UartData - '0';

							for(uint8_t payload_count = 0; payload_count < gAppTxPacket->u8DataLength; payload_count++)
							{
								gAppTxPacket->smacPdu.smacPdu[payload_count] = payload_count + 1 + '0';
							}
				            bTxDone = FALSE;
				            (void)MCPSDataRequest(gAppTxPacket);
	            			gAppTxPacket->smacPdu.smacPdu[gAppTxPacket->u8DataLength] = '\0';
	            		    Serial_Print(mAppSer, (char * )&(gAppTxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);
				            Serial_Print(mAppSer, "\f\r\n Message Sent ", gAllowToBlock_d);
							sendState = gSendStateIdle_c;
						}

						else if('p' == gu8UartData)
						{
							bBackFlag = TRUE;
							SelfNotificationEvent();
						}
					}

				break;
			case 3:
	            //Serial_Print(mAppSer, "\n\r\n Running Sending test (3)", gAllowToBlock_d);
	            shortCutsEnabled = FALSE;
	            if('\e' != gu8UartData)
	            {

	            	if(evDataFromUART)
	            	{

	            		if('\r' == gu8UartData)
	            		{
	            			gAppTxPacket->u8DataLength = payload;
	            			(void)MCPSDataRequest(gAppTxPacket);
	            			gAppTxPacket->smacPdu.smacPdu[payload] = '\0';
	            		    Serial_Print(mAppSer, (char * )&(gAppTxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);
	            			payload = 0;
	            			evDataFromUART = FALSE;
	            		}
	            		else
	            		{
		            		evDataFromUART = FALSE;
		            		gAppTxPacket->smacPdu.smacPdu[payload] = gu8UartData;
		            		payload++;
	            		}
	            	}
	            }
	            else{
	            	Serial_Print(mAppSer, "\n\r\n test (3) Finished press [enter] to continue", gAllowToBlock_d);
	            	sendState = gSendStateIdle_c;
	            }
				break;
			}

            break;
        case gSendStateIdle_c:
            if((evDataFromUART) && ('\r' == gu8UartData))
            {
            	sendState = gSendStateInit_c;
                evDataFromUART = FALSE;
                SelfNotificationEvent();
            }
            break;
        default:
            break;
        }

    	return bBackFlag;

    }


    else
    {

        switch(ReceiveState)
        {
        case gReceiveStateInit_c:
            shortCutsEnabled = TRUE;
            PrintMenu(cu8ShortCutsBar, mAppSer);
            PrintMenu(cu8PerRxTestMenu, mAppSer);
            PrintTestParameters(FALSE);
            u16TotalPackets[0] = 0;
            u16ReceivedPackets[0] = 0;
            u16PacketsIndex[0] = 0;
            e32RssiSum[0] = 0;
            ReceiveState = gReceiveWaitStartTest_c;
            break;
        case gReceiveWaitStartTest_c:
            if(evDataFromUART)
            {
                if(' ' == gu8UartData)
                {
                    Serial_Print(mAppSer, "\f\n\rPER Test Rx Running\r\n\r\n", gAllowToBlock_d);
                    bRxDone = FALSE;
                    gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
                    MLMESetActivePan(gSmacPan0_c);
                    (void)MLMERXEnableRequest(gAppRxPacket, 0);
                    shortCutsEnabled = FALSE;
                    ReceiveState = gReceiveStateStartTest_c;
                }
                else if('p' == gu8UartData)
                {
                    bBackFlag = TRUE;
                    ReceiveState = gReceiveStateInit_c;
                    SelfNotificationEvent();
                }
                evDataFromUART = FALSE;
            }
            break;
        case gReceiveStateStartTest_c:
            if(bRxDone)
            {
                if (gAppRxPacket->rxStatus == rxSuccessStatus_c)
                {

                	LED_ToggleLed(LED_ALL);

                    if(stringComp((uint8_t*)"ledoff",&gAppRxPacket->smacPdu.smacPdu[0],6))
                    {
                    	LED_StopFlashingAllLeds();
                    }

                    if(stringComp((uint8_t*)"ledflash",&gAppRxPacket->smacPdu.smacPdu[0],8))
                    {
                    	LED_StartSerialFlash(LED1);
                    }

					u16ReceivedPackets[gAppRxPacket->instanceId]++;
					e32RssiSum[gAppRxPacket->instanceId] += (energy8_t) u8LastRxRssiValue;
					Serial_Print(mAppSer, "Packet ", gAllowToBlock_d);
					Serial_PrintDec(mAppSer, (uint32_t) u16ReceivedPackets[gAppRxPacket->instanceId]);
					Serial_Print(mAppSer, ". Packet index: ", gAllowToBlock_d);

					/** Packet Pay load */
	                gAppRxPacket->smacPdu.smacPdu[gAppRxPacket->u8DataLength] = '\0';
	                Serial_Print(mAppSer, (char * )&(gAppRxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);

					Serial_Print(mAppSer, ". Rssi during RX: ", gAllowToBlock_d);
					e8TempRssivalue = (energy8_t) u8LastRxRssiValue;

					if (e8TempRssivalue < 0)
					{
						e8TempRssivalue *= -1;
						Serial_Print(mAppSer, "-", gAllowToBlock_d);
					}
					Serial_PrintDec(mAppSer, (uint32_t) e8TempRssivalue);
					Serial_Print(mAppSer, "\r\n", gAllowToBlock_d);

               }
               SelfNotificationEvent();
               bRxDone = FALSE;
               if(u16PacketsIndex[gAppRxPacket->instanceId] < u16TotalPackets[gAppRxPacket->instanceId])
               {
                   /*set active pan and enter rx after receiving packet*/
                   MLMESetActivePan(gAppRxPacket->instanceId);
                   gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
                   MLMERXEnableRequest(gAppRxPacket, 0);
               }
           }
           if(evDataFromUART)
           {
               if(' ' == gu8UartData)
               {
            	   Serial_Print(mAppSer, "\n\rPress [Enter] to return to menu\r\n\r\n", gAllowToBlock_d);
                   ReceiveState = gReceiveStateIdle_c;
               }
               else if('p' == gu8UartData)
               {
                   bBackFlag = TRUE;
               }
               evDataFromUART = FALSE;
           }
           else{
               gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
               MLMESetActivePan(gSmacPan0_c);
               (void)MLMERXEnableRequest(gAppRxPacket, 0);
               shortCutsEnabled = FALSE;
               ReceiveState = gReceiveStateStartTest_c;
           }
                break;
           case gReceiveStateIdle_c:
               if((evDataFromUART) && ('\r' == gu8UartData))
               {
                   MLMESetActivePan(gSmacPan0_c);
                   gAppRxPacket = (rxPacket_t*)gau8RxDataBuffer;
                   ReceiveState = gReceiveStateInit_c;
                   SelfNotificationEvent();
               }
               evDataFromUART = FALSE;
               break;
           default:
               break;
         }

    	return bBackFlag;

    }


    return bBackFlag;
}

/************************************************************************************
*
* Continuous Tests State Machine
*
************************************************************************************/
bool_t SerialContinuousTxRxTest(void)
{
    bool_t bBackFlag = FALSE;
    uint8_t u8Index;
    energy8_t e8TempEnergyValue;
    if(evTestParameters)
    {
        (void)TestMode(gTestModeForceIdle_c);
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);                                 
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif 
        if(gTestModePRBS9_c == contTestRunning)
        {
            cTxRxState = gCTxRxStateRunningPRBS9Test_c;
        } 
        (void)TestMode(contTestRunning);
        
        if(gCTxRxStateSelectTest_c == cTxRxState)
        {
            PrintTestParameters(TRUE);
        }
        else
        {
            PrintTestParameters(FALSE);
            Serial_Print(mAppSer, "\r\n", gAllowToBlock_d);     
        }
        
        if(gCTxRxStateRunnigRxTest_c == cTxRxState)
        {
            bRxDone = FALSE;
            gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
            (void)MLMERXEnableRequest(gAppRxPacket, 0);
        }
        evTestParameters = FALSE;
    }
    
    switch(cTxRxState)
    {
    case gCTxRxStateIdle_c:
        if((evDataFromUART) && ('\r' == gu8UartData))
        {
            cTxRxState = gCTxRxStateInit_c;
            evDataFromUART = FALSE;  
            SelfNotificationEvent();
        }
        break;
    case gCTxRxStateInit_c:
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8ContinuousTestMenu, mAppSer); 
        //Phy in StandBy, smacstate in Idle. 
        (void)TestMode(gTestModeForceIdle_c);  
        while(MLMESetChannelRequest(testChannel));                                   
        Serial_Print(mAppSer, cu8ContinuousTestTags[contTestRunning], gAllowToBlock_d);
        if(contTestRunning == gTestModeContinuousTxModulated_c)
        {
            Serial_Print(mAppSer, cu8TxModTestTags[contTxModBitValue],gAllowToBlock_d);
        }
        (void)TestMode(contTestRunning);
        Serial_Print(mAppSer, "\r\n\r\n", gAllowToBlock_d);       
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;           
        cTxRxState = gCTxRxStateSelectTest_c; 
        break;
    case gCTxRxStateSelectTest_c:
        if(evDataFromUART)
        {           
            if('1' == gu8UartData)
            {
                contTestRunning = gTestModeForceIdle_c;              
                cTxRxState = gCTxRxStateInit_c;
                SelfNotificationEvent();
            }
            else if('2' == gu8UartData)
            {
                shortCutsEnabled = FALSE;
                (void)TestMode(gTestModeForceIdle_c);
                contTestRunning = gTestModePRBS9_c;  
                MLMESetChannelRequest(testChannel);      
                Serial_Print(mAppSer, "\f\r\nPress [p] to stop the Continuous PRBS9 test\r\n", gAllowToBlock_d);
                (void)TestMode(contTestRunning);
                cTxRxState = gCTxRxStateRunningPRBS9Test_c;
            }
            else if('3' == gu8UartData)
            {
                contTestRunning = gTestModeContinuousTxModulated_c;               
                cTxRxState = gCTxRxStateRunningTXModSelectOpt;
                //        Serial_Print(mAppSer, "\f\r\n To use this mode shunt pins 3-4 on J18", gAllowToBlock_d);
                Serial_Print(mAppSer, "\f\r\nPress 2 for PN9, 1 to modulate values of 1 and 0 to modulate values of 0", gAllowToBlock_d);
                
            }
            else if('4' == gu8UartData)
            {
                if(gTestModeContinuousTxUnmodulated_c != contTestRunning) 
                { 
                    contTestRunning = gTestModeContinuousTxUnmodulated_c;               
                    cTxRxState = gCTxRxStateInit_c;
                    SelfNotificationEvent();
                }
            }
            else if('5' == gu8UartData)
            {
                shortCutsEnabled = FALSE;
                (void)TestMode(gTestModeForceIdle_c);  
                MLMESetChannelRequest(testChannel);
                contTestRunning = gTestModeForceIdle_c;
                Serial_Print(mAppSer, "\f\r\nPress [p] to stop receiving broadcast packets \r\n", gAllowToBlock_d);                                               
                bRxDone = FALSE;
                gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
                (void)MLMERXEnableRequest(gAppRxPacket, 0);
                cTxRxState = gCTxRxStateRunnigRxTest_c;
            }
            else if('6' == gu8UartData)
            {
                (void)TestMode(gTestModeForceIdle_c);
                contTestRunning = gTestModeForceIdle_c;
                Serial_Print(mAppSer, "\f\r\nPress [p] to stop the Continuous ED test\r\n", gAllowToBlock_d);               
                cTxRxState = gCTxRxStateRunnigEdTest_c;
                FlaggedDelay_ms(200);
            }
            else if('7' == gu8UartData)
            {
                (void)TestMode(gTestModeForceIdle_c);
                contTestRunning = gTestModeForceIdle_c;
                ChannelToScan= gDefaultChannelNumber_c;                            
                Serial_Print(mAppSer, "\f\r\nPress [p] to stop the Continuous SCAN test\r\n", gAllowToBlock_d);
                bScanDone = FALSE;
                cTxRxState = gCTxRxStateRunnigScanTest_c;
                SelfNotificationEvent();
            }
            else if('8' == gu8UartData)
            {
                (void)TestMode(gTestModeForceIdle_c);                        
                Serial_Print(mAppSer, "\f\r\nPress [p] to stop the Continuous CCA test\r\n", gAllowToBlock_d);
                contTestRunning = gTestModeForceIdle_c;                
                cTxRxState = gCTxRxStateRunnigCcaTest_c;
                FlaggedDelay_ms(100);
                MLMECcaRequest();
            }
#if CT_Feature_BER_Test
            else if ('9' == gu8UartData)
            {
                Serial_Print(mAppSer, "\f\r\nPress [p] to stop the Continuous BER test\r\n", gAllowToBlock_d);
                contTestRunning = gTestModeContinuousRxBER_c;               
                cTxRxState = gCTxRxStateInit_c;
                SelfNotificationEvent();
            }
#endif
            else if('p' == gu8UartData)
            { 
                (void)TestMode(gTestModeForceIdle_c);
                (void)MLMESetChannelRequest(testChannel); 
                TMR_StopTimer(AppDelayTmr);
                timePassed = FALSE;
                bBackFlag = TRUE;
            }
            evDataFromUART = FALSE;
        }
        break;
    case gCTxRxStateRunningTXModSelectOpt:
        if(evDataFromUART)
        {
            if(gu8UartData == '2')
                contTxModBitValue = gContTxModSelectPN9_c;
            else if(gu8UartData == '1')
                contTxModBitValue = gContTxModSelectOnes_c;
            else if(gu8UartData == '0')
                contTxModBitValue = gContTxModSelectZeros_c;

            evDataFromUART = FALSE;
            cTxRxState = gCTxRxStateInit_c;
            SelfNotificationEvent();
        }
        break;
    case gCTxRxStateRunningPRBS9Test_c:
        if(bTxDone || failedPRBS9)
        {
            failedPRBS9 = FALSE;
            bTxDone     = FALSE;
            PacketHandler_Prbs9();
        }
        if(evDataFromUART && 'p' == gu8UartData)
        {
            contTestRunning = gTestModeForceIdle_c;
            (void)TestMode(gTestModeForceIdle_c);
            (void)MLMESetChannelRequest(testChannel); 
            TMR_StopTimer(AppDelayTmr);
            timePassed = FALSE;
            Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Continuous test menu ", gAllowToBlock_d);
            cTxRxState = gCTxRxStateIdle_c;
            evDataFromUART = FALSE;
            shortCutsEnabled = TRUE;
        }
        break;
    case gCTxRxStateRunnigRxTest_c:
        if(bRxDone)
        {
            if (gAppRxPacket->rxStatus == rxSuccessStatus_c)
            {
                Serial_Print(mAppSer, "New Packet: ", gAllowToBlock_d);
                Serial_Print(mAppSer, (char * )&(gAppTxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);
                for(u8Index = 0; u8Index < (gAppRxPacket->u8DataLength); u8Index++){
                    Serial_PrintHex(mAppSer, &(gAppRxPacket->smacPdu.smacPdu[u8Index]), 1, 0);
                    //Serial_PrintDec(mAppSer, gAppRxPacket->smacPdu.smacPdu[u8Index]);
                }
                gAppRxPacket->smacPdu.smacPdu[gAppRxPacket->u8DataLength] = '\0';
                Serial_Print(mAppSer, (char * )&(gAppRxPacket->smacPdu.smacPdu[0]),gAllowToBlock_d);
                Serial_Print(mAppSer, " \r\n", gAllowToBlock_d);
            }
            bRxDone = FALSE;
            gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
            (void)MLMERXEnableRequest(gAppRxPacket, 0);
        }
        if((evDataFromUART) && ('p' == gu8UartData))
        {
            (void)MLMERXDisableRequest();
            (void)TestMode(gTestModeForceIdle_c);
            Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Continuous test menu ", gAllowToBlock_d);
            cTxRxState = gCTxRxStateIdle_c;
            evDataFromUART = FALSE;
        }
        break;
    case gCTxRxStateRunnigEdTest_c:
        if(timePassed)
        {
            timePassed = FALSE;
            FlaggedDelay_ms(100);
            MLMEScanRequest(testChannel);
        }
        if((evDataFromUART) && ('p' == gu8UartData))
        {
            Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Continuous test menu ", gAllowToBlock_d);
            cTxRxState = gCTxRxStateIdle_c;
            evDataFromUART = FALSE;
            timePassed = FALSE;
            TMR_StopTimer(AppDelayTmr);
        }
        
        break;
    case gCTxRxStateRunningEdTestGotResult_c:
        Serial_Print(mAppSer, "Energy on the Channel ", gAllowToBlock_d);
        Serial_PrintDec(mAppSer, (uint32_t)testChannel);
        Serial_Print(mAppSer, " : ", gAllowToBlock_d);
        e8TempEnergyValue = (energy8_t)au8ScanResults[testChannel];
#if CT_Feature_RSSI_Has_Sign
        if(e8TempEnergyValue < 0)
        {
            e8TempEnergyValue *= -1;
#else 
            if(e8TempEnergyValue !=0)
            {
#endif
                Serial_Print(mAppSer, "-", gAllowToBlock_d);
            }
            Serial_PrintDec(mAppSer, (uint32_t)e8TempEnergyValue);
            Serial_Print(mAppSer, "dBm\r\n", gAllowToBlock_d);
            cTxRxState = gCTxRxStateRunnigEdTest_c;
            break; 
        case gCTxRxStateRunnigCcaTest_c:
            if(timePassed && gCCaGotResult)
            {
                gCCaGotResult = FALSE;
                timePassed = FALSE;
                MLMECcaRequest();
                FlaggedDelay_ms(100);
            }
            if((evDataFromUART) && ('p' == gu8UartData))
            {
                Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Continuous test menu ", gAllowToBlock_d);
                cTxRxState = gCTxRxStateIdle_c;
                evDataFromUART = FALSE;
                timePassed = FALSE;
                TMR_StopTimer(AppDelayTmr);
            }
            break;
        case gCTxRxStateRunnigScanTest_c:
            if(bScanDone && timePassed)
            {                                              
                //Enters here until all channels have been scanned. Then starts to print.
                Serial_Print(mAppSer, "Results : ", gAllowToBlock_d);
                for(u8Index = gMinChannel_c; u8Index <= gMaxChannel_c ; u8Index++)
                {                                                         
                    e8TempEnergyValue = (energy8_t)au8ScanResults[u8Index];
#if CT_Feature_RSSI_Has_Sign
                    if(e8TempEnergyValue < 0)
                    {
                        e8TempEnergyValue *= -1;
#else
                    if(e8TempEnergyValue != 0)
                    {
#endif
                        Serial_Print(mAppSer, "-", gAllowToBlock_d);
                    }
                    Serial_PrintDec(mAppSer, (uint32_t) e8TempEnergyValue);
                    Serial_Print(mAppSer, ",", gAllowToBlock_d);   
                }
                Serial_Print(mAppSer, "\b \r\n", gAllowToBlock_d);
                bScanDone = FALSE;                                                   
                ChannelToScan = gDefaultChannelNumber_c;                             // Restart channel count
                timePassed = FALSE;
            }                                                                         
                
            if((evDataFromUART) && ('p' == gu8UartData))
            {
                Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Continuous test menu ", gAllowToBlock_d);
                cTxRxState = gCTxRxStateIdle_c;
                evDataFromUART = FALSE;
            }
            else
            {
                if(ChannelToScan == gDefaultChannelNumber_c)
                {
                    smacErrors_t err = MLMEScanRequest((channels_t)ChannelToScan);                                            
                    if(err == gErrorNoError_c)
                        ChannelToScan++;
                }
                //Each of the other channels is scanned after SMAC notifies us that 
                //it has obtained the energy value on the currently scanned channel 
                //(channel scanning is performed asynchronously). See IncrementChannelOnEdEvent().
            }
            break;
        default:
            break;
    }
    return bBackFlag;
}
        
/************************************************************************************
*
* PER Handler for board that is performing TX
*
************************************************************************************/
bool_t PacketErrorRateTx(void)
{
    const uint16_t u16TotalPacketsOptions[] = {1,25,100,500,1000,2000,5000,10000,65535};
    static uint16_t u16TotalPackets;
    static uint16_t u16SentPackets;
    static uint32_t miliSecDelay;
    static uint32_t u32MinDelay = 4;
    uint8_t u8Index;
    bool_t bBackFlag = FALSE;
    
    if(evTestParameters)
    {
        (void)MLMERXDisableRequest();
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);                                 
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }
    
    switch(perTxState)
    {
    case gPerTxStateInit_c:
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8PerTxTestMenu, mAppSer);
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;           
        perTxState = gPerTxStateSelectPacketNum_c;
        miliSecDelay = 0;
        u32MinDelay = 4;
        (void)MLMERXDisableRequest();
        break;
    case gPerTxStateSelectPacketNum_c:
        if(evDataFromUART)
        {
            if((gu8UartData >= '0') && (gu8UartData <= '8'))
            {
                u16TotalPackets = u16TotalPacketsOptions[gu8UartData - '0'];
                shortCutsEnabled = FALSE;  
                u32MinDelay += (GetTransmissionTime(testPayloadLen, crtBitrate) / 1000);
                Serial_Print(mAppSer,"\r\n\r\n Please type TX interval in miliseconds ( > ",gAllowToBlock_d);
                Serial_PrintDec(mAppSer, u32MinDelay);
                Serial_Print(mAppSer,"ms ) and press [ENTER]\r\n", gAllowToBlock_d);
                perTxState = gPerTxStateInputPacketDelay_c;
            }
            else if('p' == gu8UartData)
            { 
                bBackFlag = TRUE;
            }
            evDataFromUART = FALSE;
        }
        break;
    case gPerTxStateInputPacketDelay_c:
        if(evDataFromUART)
        {
            if(gu8UartData == '\r')
            {
                if(miliSecDelay < u32MinDelay)
                {
                    Serial_Print(mAppSer,"\r\n\tError: TX Interval too small\r\n",gAllowToBlock_d);
                    perTxState = gPerTxStateInit_c;
                    SelfNotificationEvent();
                }
                else
                {
                    perTxState = gPerTxStateStartTest_c;
                    SelfNotificationEvent();
                }
            }
            else if((gu8UartData >= '0') && (gu8UartData <='9'))
            {
                miliSecDelay = miliSecDelay*10 + (gu8UartData - '0');
                Serial_PrintDec(mAppSer, (uint32_t)(gu8UartData - '0'));
            }
            else if('p' == gu8UartData)
            { 
                perTxState = gPerTxStateInit_c;
                SelfNotificationEvent();
            }
            evDataFromUART = FALSE;
        }
        break;
    case gPerTxStateStartTest_c:
        gAppTxPacket->u8DataLength = testPayloadLen;
        u16SentPackets = 0;
        
        gAppTxPacket->smacPdu.smacPdu[0] = (u16TotalPackets >> 8);
        gAppTxPacket->smacPdu.smacPdu[1] = (uint8_t)u16TotalPackets;
        gAppTxPacket->smacPdu.smacPdu[2] = ((u16SentPackets+1) >> 8);
        gAppTxPacket->smacPdu.smacPdu[3] = (uint8_t)(u16SentPackets+1);
        FLib_MemCpy(&(gAppTxPacket->smacPdu.smacPdu[4]), "SMAC PER Demo",13);

        if(17 < testPayloadLen)
        {
            for(u8Index=17;u8Index<testPayloadLen;u8Index++)
            {     
                gAppTxPacket->smacPdu.smacPdu[u8Index] = (u8Index%10)+'0';            
            }
        }
        bTxDone = FALSE;
        (void)MCPSDataRequest(gAppTxPacket);
        u16SentPackets++;
        Serial_Print(mAppSer, "\f\r\n Running PER Tx, Sending ", gAllowToBlock_d);
        Serial_PrintDec(mAppSer, (uint32_t)u16TotalPackets);
        Serial_Print(mAppSer, " Packets", gAllowToBlock_d);
        
        perTxState = gPerTxStateRunningTest_c;
        FlaggedDelay_ms(miliSecDelay);
        break;
    case gPerTxStateRunningTest_c:
        if(bTxDone && timePassed)
        {
            bTxDone = FALSE;
            timePassed = FALSE;
            
            Serial_Print(mAppSer,"\r\n Packet ",gAllowToBlock_d);
            Serial_PrintDec(mAppSer,(uint32_t)u16SentPackets);
            if(u16SentPackets == u16TotalPackets)
            {    
                Serial_Print(mAppSer, "\r\n PER Tx DONE \r\n", gAllowToBlock_d);
                Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the PER Tx test menu ", gAllowToBlock_d);
                perTxState = gPerTxStateIdle_c;
            }
            else
            {
                gAppTxPacket->smacPdu.smacPdu[2] = ((u16SentPackets+1) >> 8);
                gAppTxPacket->smacPdu.smacPdu[3] = (uint8_t)(u16SentPackets+1);
                gAppTxPacket->u8DataLength = testPayloadLen;
                (void)MCPSDataRequest(gAppTxPacket);
                u16SentPackets++;
                FlaggedDelay_ms(miliSecDelay);
            }
        }
        if(evDataFromUART && gu8UartData == ' ')
        {
            Serial_Print(mAppSer,"\r\n\r\n-Test interrupted by user. Press [ENTER] to continue\r\n\r\n",gAllowToBlock_d);
            
            TMR_StopTimer(AppDelayTmr);
            timePassed = FALSE;
            
            MLMETXDisableRequest();
            bTxDone = FALSE;
            
            perTxState = gPerTxStateIdle_c;
        }
        break;	
    case gPerTxStateIdle_c:
        if((evDataFromUART) && ('\r' == gu8UartData))
        {
            perTxState = gPerTxStateInit_c;
            evDataFromUART = FALSE;
            SelfNotificationEvent();
        }
        break;
    default:
        break;
    }
    
    return bBackFlag;
}
        
/************************************************************************************
*
* PER Handler for board that is performing RX
*
************************************************************************************/
bool_t PacketErrorRateRx(void)
{
    static energy32_t e32RssiSum[gNumPans_c];
    static uint16_t u16ReceivedPackets[gNumPans_c];
    static uint16_t u16PacketsIndex[gNumPans_c];  
    static uint16_t u16TotalPackets[gNumPans_c];
    static energy8_t  e8AverageRssi[gNumPans_c];
    static bool_t bPrintStatistics = FALSE;
    energy8_t e8TempRssivalue;
    uint8_t u8PanCount = 0;
    
    bool_t bBackFlag = FALSE;
    if(evTestParameters)
    {
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);                                 
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }
    switch(perRxState)
    {
    case gPerRxStateInit_c:
        shortCutsEnabled = TRUE;
        bPrintStatistics = FALSE;
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8PerRxTestMenu, mAppSer);
        PrintTestParameters(FALSE);
        u16TotalPackets[0] = 0;
        u16ReceivedPackets[0] = 0;
        u16PacketsIndex[0] = 0;
        e32RssiSum[0] = 0;
#if gMpmMaxPANs_c == 2
        u16TotalPackets[1] = 0;
        u16ReceivedPackets[1] = 0;
        u16PacketsIndex[1] = 0;
        e32RssiSum[1] = 0;
        bDataInd[0] = FALSE;
        bDataInd[1] = FALSE;
        perRxState = gPerRxConfigureAlternatePan_c;
        SelfNotificationEvent();
#else
        perRxState = gPerRxWaitStartTest_c;
#endif
        break;
#if gMpmMaxPANs_c == 2
    case gPerRxConfigureAlternatePan_c:
        if(evDataFromUART && gu8UartData == 'p')
        {
            evDataFromUART = FALSE;
            perRxState = gPerRxStateInit_c;
            bBackFlag = TRUE;
            break;
        }
        if(ConfigureAlternatePan() == TRUE)
        {
            perRxState = gPerRxWaitStartTest_c;
        }
        break;
#endif
    case gPerRxWaitStartTest_c:
        if(evDataFromUART)
        {
            if(' ' == gu8UartData)
            {
                Serial_Print(mAppSer, "\f\n\rPER Test Rx Running\r\n\r\n", gAllowToBlock_d);
                bRxDone = FALSE;
                gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
                MLMESetActivePan(gSmacPan0_c);
                (void)MLMERXEnableRequest(gAppRxPacket, 0);
#if gMpmMaxPANs_c == 2
                MLMESetActivePan(gSmacPan1_c);
                gAppRxPacket = (rxPacket_t*)gau8RxDataBufferAlt;
                gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
                (void)MLMERXEnableRequest(gAppRxPacket, 0);
#endif
                shortCutsEnabled = FALSE;  
                perRxState = gPerRxStateStartTest_c;
            }
            else if('p' == gu8UartData)
            { 
                bBackFlag = TRUE;
            }
            evDataFromUART = FALSE;
        }
        break;
    case gPerRxStateStartTest_c:
        if(bRxDone)
        {
#if gMpmMaxPANs_c == 2
            if(bDataInd[gSmacPan0_c] == TRUE)
            {
                gAppRxPacket = (rxPacket_t*)(gau8RxDataBuffer);
                bDataInd[gSmacPan0_c] = FALSE;
                if(bDataInd[gSmacPan1_c] == TRUE)
                {
                    OSA_EventSet(gTaskEvent, gMcps_Ind_EVENT_c);
                }
            }
            else if(bDataInd[gSmacPan1_c] == TRUE)
            {
                gAppRxPacket = (rxPacket_t*)(gau8RxDataBufferAlt);
                bDataInd[gSmacPan1_c] = FALSE;
                if(bDataInd[gSmacPan0_c] == TRUE)
                {
                    OSA_EventSet(gTaskEvent, gMcps_Ind_EVENT_c);
                }
            }
#endif
            if (gAppRxPacket->rxStatus == rxSuccessStatus_c)
            {
                if(stringComp((uint8_t*)"SMAC PER Demo",&gAppRxPacket->smacPdu.smacPdu[4],13))
                {
                    u16TotalPackets[gAppRxPacket->instanceId] = 
                        ((uint16_t)gAppRxPacket->smacPdu.smacPdu[0] <<8) + gAppRxPacket->smacPdu.smacPdu[1];
                    u16PacketsIndex[gAppRxPacket->instanceId] = 
                        ((uint16_t)gAppRxPacket->smacPdu.smacPdu[2] <<8) + gAppRxPacket->smacPdu.smacPdu[3];
                    u16ReceivedPackets[gAppRxPacket->instanceId]++;
#if gMpmMaxPANs_c == 2
                    e32RssiSum[gAppRxPacket->instanceId] += 
                        (energy8_t)u8PanRSSI[gAppRxPacket->instanceId];
#else
                    e32RssiSum[gAppRxPacket->instanceId] += (energy8_t)u8LastRxRssiValue;
#endif
                    e8AverageRssi[gAppRxPacket->instanceId] = 
                        (energy8_t)(e32RssiSum[gAppRxPacket->instanceId]/u16ReceivedPackets[gAppRxPacket->instanceId]);
#if gMpmMaxPANs_c == 2
                    Serial_Print(mAppSer, "Pan: ", gAllowToBlock_d);
                    Serial_PrintDec(mAppSer, (uint32_t)gAppRxPacket->instanceId);
                    Serial_Print(mAppSer,". ", gAllowToBlock_d);
#endif
                    Serial_Print(mAppSer, "Packet ", gAllowToBlock_d);
                    Serial_PrintDec(mAppSer,(uint32_t)u16ReceivedPackets[gAppRxPacket->instanceId]);
                    Serial_Print(mAppSer, ". Packet index: ",gAllowToBlock_d);
                    Serial_PrintDec(mAppSer, (uint32_t)u16PacketsIndex[gAppRxPacket->instanceId]);
                    Serial_Print(mAppSer, ". Rssi during RX: ", gAllowToBlock_d);
#if gMpmMaxPANs_c == 2
                    e8TempRssivalue = (energy8_t)u8PanRSSI[gAppRxPacket->instanceId];
#else
                    e8TempRssivalue = (energy8_t)u8LastRxRssiValue;
#endif
#if CT_Feature_RSSI_Has_Sign
                    if(e8TempRssivalue < 0)
                    {
                        e8TempRssivalue *= -1;
#else
                    if(e8TempRssivalue != 0)
                    {
#endif
                        Serial_Print(mAppSer, "-", gAllowToBlock_d);
                    }
                    Serial_PrintDec(mAppSer, (uint32_t)e8TempRssivalue);
                    Serial_Print(mAppSer, "\r\n", gAllowToBlock_d);
                    if(u16PacketsIndex[gAppRxPacket->instanceId] == 
                       u16TotalPackets[gAppRxPacket->instanceId])
                    { 
#if gMpmMaxPANs_c != 2
                        bPrintStatistics = TRUE;
                        SelfNotificationEvent();
                        perRxState = gPerRxStateIdle_c;
#else        
                        if(u16PacketsIndex[1-gAppRxPacket->instanceId] == 
                           u16TotalPackets[1-gAppRxPacket->instanceId] && 
                               u16TotalPackets[1-gAppRxPacket->instanceId] != 0)
                        {
                            bPrintStatistics = TRUE;
                            SelfNotificationEvent();
                            perRxState = gPerRxStateIdle_c;
                        }
#endif
                    }
                }
           } 
           bRxDone = FALSE;
           if(u16PacketsIndex[gAppRxPacket->instanceId] < u16TotalPackets[gAppRxPacket->instanceId])
           {
               /*set active pan and enter rx after receiving packet*/
               MLMESetActivePan(gAppRxPacket->instanceId);
               gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
               MLMERXEnableRequest(gAppRxPacket, 0);
           } 
       }
       if(evDataFromUART)
       {
           if(' ' == gu8UartData)
           {
               u8PanCount = 0;
               do
               {  
                   Serial_Print(mAppSer,"\r\n Statistics on PAN", gAllowToBlock_d);
                   Serial_PrintDec(mAppSer, (uint32_t)u8PanCount);
                   Serial_Print(mAppSer, "\r\n", gAllowToBlock_d);
                   MLMESetActivePan((smacMultiPanInstances_t)u8PanCount);
                   (void)MLMERXDisableRequest();
                   Serial_Print(mAppSer,"\r\nAverage Rssi during PER: ",gAllowToBlock_d);
#if CT_Feature_RSSI_Has_Sign
                   if(e8AverageRssi[u8PanCount] < 0)
                   {
                       e8AverageRssi[u8PanCount] *= -1;
#else
                   if(e8AverageRssi[u8PanCount] != 0)
                   {
#endif
                       Serial_Print(mAppSer, "-",gAllowToBlock_d);    
                   }
                   Serial_PrintDec(mAppSer, (uint32_t)e8AverageRssi[u8PanCount]);
                   Serial_Print(mAppSer," dBm\r\n",gAllowToBlock_d);
                   Serial_Print(mAppSer, "\n\rPER Test Rx Stopped\r\n\r\n", gAllowToBlock_d);
                   PrintPerRxFinalLine(u16ReceivedPackets[u8PanCount],u16TotalPackets[u8PanCount]);
                }
                while(++u8PanCount < gNumPans_c);
                perRxState = gPerRxStateIdle_c;
           } 
           evDataFromUART = FALSE;
       }         
            break;
       case gPerRxStateIdle_c:
           if(bPrintStatistics == TRUE)
           {
               bPrintStatistics = FALSE;
               u8PanCount = 0;
               do
               {
                   Serial_Print(mAppSer,"\r\nAverage Rssi during PER: ", gAllowToBlock_d);
#if CT_Feature_RSSI_Has_Sign
                   if(e8AverageRssi[u8PanCount] < 0)
                   {
                       e8AverageRssi[u8PanCount] *= -1;
#else
                   if(e8AverageRssi[u8PanCount] != 0)
                   {
#endif
                       Serial_Print(mAppSer, "-", gAllowToBlock_d);
                   }
                   Serial_PrintDec(mAppSer, (uint32_t)e8AverageRssi[u8PanCount]);
                   Serial_Print(mAppSer," dBm\r\n",gAllowToBlock_d);
#if gMpmMaxPANs_c == 2
                   Serial_Print(mAppSer, "\n\rPER Test Finished on Pan ", gAllowToBlock_d);
                   Serial_PrintDec(mAppSer, u8PanCount);
                   Serial_Print(mAppSer, "\r\n\r\n", gAllowToBlock_d);
#else
                   Serial_Print(mAppSer, "\n\rPER Test Finished\r\n\r\n", gAllowToBlock_d);
#endif
                   PrintPerRxFinalLine(u16ReceivedPackets[u8PanCount],u16TotalPackets[u8PanCount]);  
                }
                while(++u8PanCount < gNumPans_c);
           }
           if((evDataFromUART) && ('\r' == gu8UartData))
           {
               MLMESetActivePan(gSmacPan0_c);
               gAppRxPacket = (rxPacket_t*)gau8RxDataBuffer;
               perRxState = gPerRxStateInit_c;
               SelfNotificationEvent();
           }
           evDataFromUART = FALSE;
           break;
       default:
           break;
     }
     return bBackFlag;
}
            
/************************************************************************************
*
* Range Test Handler for board that is performing TX
*
************************************************************************************/
bool_t RangeTx(void)
{
    bool_t bBackFlag = FALSE;
    static energy32_t e32RSSISum;
    static uint16_t u16ReceivedPackets;
    static uint16_t u16PacketsDropped;
    energy8_t  e8AverageRSSI;
    energy8_t  e8CurrentRSSI;
    
    if(evTestParameters)
    {
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);                               
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }
    
    switch(rangeTxState)
    {
    case gRangeTxStateInit_c:
        e32RSSISum = 0;
        u16ReceivedPackets = 0;
        u16PacketsDropped = 0;
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8RangeTxTestMenu, mAppSer);
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;           
        rangeTxState = gRangeTxWaitStartTest_c;
        break;
    case gRangeTxWaitStartTest_c:
        if(evDataFromUART)
        {
            if(' ' == gu8UartData)
            {
                shortCutsEnabled = FALSE; 
                Serial_Print(mAppSer, "\f\r\nRange Test Tx Running\r\n", gAllowToBlock_d);
                rangeTxState = gRangeTxStateStartTest_c;
                FlaggedDelay_ms(200);
            }
            else if('p' == gu8UartData)
            { 
                bBackFlag = TRUE;
            }
            evDataFromUART = FALSE;
        }
        break;
    case gRangeTxStateStartTest_c:
        if(!timePassed) //waiting 200 ms
            break;
        timePassed = FALSE;
        bTxDone = FALSE;
        gAppTxPacket->u8DataLength = 16;
        gAppTxPacket->smacPdu.smacPdu[0]  = 0;
        FLib_MemCpy(&(gAppTxPacket->smacPdu.smacPdu[1]), "SMAC Range Demo",15);
        MLMERXDisableRequest();                                                
        TMR_StopTimer(RangeTestTmr);                                       
        (void)MCPSDataRequest(gAppTxPacket);
        rangeTxState = gRangeTxStateRunningTest_c;
        break;
    case gRangeTxStateRunningTest_c:
        if(bTxDone)
        {                                         
            TMR_StartSingleShotTimer (RangeTestTmr, 80, RangeTest_Timer_CallBack, NULL);
            SetRadioRxOnNoTimeOut();
            rangeTxState = gRangeTxStatePrintTestResults_c;
        }
        break;
    case gRangeTxStatePrintTestResults_c:
        if(bRxDone)
        {                                                       
            if(gAppRxPacket->rxStatus == rxSuccessStatus_c)
            { 
                if(stringComp((uint8_t*)"SMAC Range Demo",&gAppRxPacket->smacPdu.smacPdu[1],15))
                {
                    e8CurrentRSSI = (energy8_t)(gAppRxPacket->smacPdu.smacPdu[0]); 
                    e32RSSISum += e8CurrentRSSI;  
                    
                    u16ReceivedPackets++;
                    e8AverageRSSI = (energy8_t)(e32RSSISum/u16ReceivedPackets);
                    Serial_Print(mAppSer, "\r\n RSSI = ", gAllowToBlock_d);
#if CT_Feature_RSSI_Has_Sign
                    if(e8CurrentRSSI < 0)
                    {
                        e8CurrentRSSI *= -1;
#else
                    if(e8CurrentRSSI !=0)
                    {
#endif
                        Serial_Print(mAppSer, "-", gAllowToBlock_d);
                    }
                    Serial_PrintDec(mAppSer, (uint32_t)e8CurrentRSSI);
                    Serial_Print(mAppSer," dBm", gAllowToBlock_d);
                }
                else
                {                                   
                    TMR_StartSingleShotTimer (RangeTestTmr, 80, RangeTest_Timer_CallBack, NULL); 
                    SetRadioRxOnNoTimeOut();                                             
                }
            }
            else
            {
                u16PacketsDropped++;
                Serial_Print(mAppSer, "\r\nPacket Dropped", gAllowToBlock_d);
                bRxDone= FALSE;                                                 
            }
            if(evDataFromUART && (' ' == gu8UartData))
            {
                Serial_Print(mAppSer, "\n\r\n\rRange Test Tx Stopped\r\n\r\n", gAllowToBlock_d);
                e8AverageRSSI = (energy8_t)(e32RSSISum/u16ReceivedPackets);
                Serial_Print(mAppSer, "Average RSSI     ", gAllowToBlock_d);
#if CT_Feature_RSSI_Has_Sign
                if(e8AverageRSSI < 0)
                {
                    e8AverageRSSI *= -1;
#else
                if(e8AverageRSSI != 0)
                {
#endif
                    Serial_Print(mAppSer, "-", gAllowToBlock_d);
                            
                }
                Serial_PrintDec(mAppSer, (uint32_t) e8AverageRSSI);
                
                Serial_Print(mAppSer," dBm", gAllowToBlock_d);
                Serial_Print(mAppSer, "\r\nPackets dropped ", gAllowToBlock_d);
                Serial_PrintDec(mAppSer, (uint32_t)u16PacketsDropped);  
                Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Range Tx test menu", gAllowToBlock_d);
                rangeTxState = gRangeTxStateIdle_c;
                (void)MLMERXDisableRequest();
                TMR_StopTimer(AppDelayTmr);
                timePassed = FALSE;
            }
            else
            {
                rangeTxState = gRangeTxStateStartTest_c;
                FlaggedDelay_ms(200);
            }
            evDataFromUART = FALSE;
       }         
       break;
     case gRangeTxStateIdle_c:
         if((evDataFromUART) && ('\r' == gu8UartData))
         {
             rangeTxState = gRangeTxStateInit_c;
             SelfNotificationEvent();
         }
         evDataFromUART = FALSE;
         break;
     default:
         break;
    }
    return bBackFlag;
}
        
/************************************************************************************
*
* Range Test Handler for board that is performing RX
*
************************************************************************************/
bool_t RangeRx(void)
{
    bool_t bBackFlag = FALSE;
    static energy32_t e32RSSISum;
    static uint16_t u16ReceivedPackets;
    energy8_t  e8AverageRSSI;
    energy8_t  e8CurrentRSSI;
    
    if(evTestParameters)
    {
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);                                
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }  
    switch(rangeRxState)
    {
    case gRangeRxStateInit_c:
        e32RSSISum = 0;
        u16ReceivedPackets = 0;
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8RangeRxTestMenu, mAppSer);
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;           
        rangeRxState = gRangeRxWaitStartTest_c;
        break;
    case gRangeRxWaitStartTest_c:
        if(evDataFromUART)
        {
            if(' ' == gu8UartData)
            {
                shortCutsEnabled = FALSE; 
                Serial_Print(mAppSer, "\f\r\nRange Test Rx Running\r\n", gAllowToBlock_d);
                rangeRxState = gRangeRxStateStartTest_c;
            }
            else if('p' == gu8UartData)
            { 
                bBackFlag = TRUE;
            }
            evDataFromUART = FALSE;
            SelfNotificationEvent();
        }
        break;
    case gRangeRxStateStartTest_c:
        SetRadioRxOnNoTimeOut();
        rangeRxState = gRangeRxStateRunningTest_c;
        break;
    case gRangeRxStateRunningTest_c:
        if(evDataFromUART && (' ' == gu8UartData))
        {             
            (void)MLMERXDisableRequest();
            Serial_Print(mAppSer, "\n\r\n\rRange Test Rx Stopped\r\n\r\n", gAllowToBlock_d);
            e8AverageRSSI = (energy8_t)(e32RSSISum/u16ReceivedPackets);
            Serial_Print(mAppSer, "Average RSSI     ", gAllowToBlock_d);
#if CT_Feature_RSSI_Has_Sign
            if(e8AverageRSSI < 0)
            {
                e8AverageRSSI *= -1;
#else 
            if(e8AverageRSSI != 0)
            {
#endif
                Serial_Print(mAppSer, "-", gAllowToBlock_d);  
            }
            Serial_PrintDec(mAppSer, (uint32_t) e8AverageRSSI);
            Serial_Print(mAppSer," dBm", gAllowToBlock_d);
            Serial_Print(mAppSer, "\r\n\r\n Press [enter] to go back to the Range Rx test menu", gAllowToBlock_d);
            rangeRxState = gRangeRxStateIdle_c;
        }
        evDataFromUART = FALSE;
        if(bRxDone)
        {
            if(gAppRxPacket->rxStatus == rxSuccessStatus_c)
            { 
                if(stringComp((uint8_t*)"SMAC Range Demo",&gAppRxPacket->smacPdu.smacPdu[1],15))
                {
                    bRxDone = FALSE;
                    FlaggedDelay_ms(4);
                }
                else
                {
                    SetRadioRxOnNoTimeOut();
                }
            }
            else
            {
                SetRadioRxOnNoTimeOut();
            }
        }
        if(timePassed)
        {
            timePassed = FALSE;
            bTxDone = FALSE;                                   
            gAppTxPacket->smacPdu.smacPdu[0] = u8LastRxRssiValue;
            FLib_MemCpy(&(gAppTxPacket->smacPdu.smacPdu[1]), "SMAC Range Demo",15);
            gAppTxPacket->u8DataLength = 16;
            (void)MCPSDataRequest(gAppTxPacket);
            rangeRxState = gRangeRxStatePrintTestResults_c;
        }
        break;
    case gRangeRxStatePrintTestResults_c:
        if(bTxDone)
        {       
            e8CurrentRSSI= (energy8_t)u8LastRxRssiValue;                                   
            e32RSSISum += e8CurrentRSSI;
            u16ReceivedPackets++;
            e8AverageRSSI = (uint8_t)(e32RSSISum/u16ReceivedPackets);
            Serial_Print(mAppSer, "\r\n RSSI = ", gAllowToBlock_d);
#if CT_Feature_RSSI_Has_Sign
            if(e8CurrentRSSI < 0)
            {
                e8CurrentRSSI *= -1;
#else
            if(e8CurrentRSSI != 0)
            {
#endif
                Serial_Print(mAppSer, "-" , gAllowToBlock_d);
            }
            Serial_PrintDec(mAppSer, (uint32_t) e8CurrentRSSI);
            Serial_Print(mAppSer," dBm", gAllowToBlock_d);
            rangeRxState = gRangeRxStateStartTest_c;
            SelfNotificationEvent();
        }
        break;
    case gRangeRxStateIdle_c:
        if((evDataFromUART) && ('\r' == gu8UartData))
        {
            rangeRxState = gRangeRxStateInit_c;
            SelfNotificationEvent();
        }
        evDataFromUART = FALSE;
        break;
    default:
        break;
    }
    return bBackFlag;
}
                
/************************************************************************************
*
* Handler for viewing/modifying XCVR registers
*
************************************************************************************/
bool_t EditRegisters(void)
{
    bool_t bBackFlag = FALSE;
    if(evTestParameters)
    {
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMESetChannelRequest(testChannel);                                
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }
    
    switch(eRState)
    {
    case gERStateInit_c:
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8RadioRegistersEditMenu, mAppSer);
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;           
        eRState = gERWaitSelection_c;
        break;
    case gERWaitSelection_c:
        if(evDataFromUART)
        {
#if CT_Feature_Direct_Registers
            if('1' == gu8UartData)
            {
                bIsRegisterDirect = TRUE;
                oRState = gORStateInit_c;
                eRState = gERStateOverrideReg_c;
                SelfNotificationEvent();
            }
            else if('2' == gu8UartData)
            {
                bIsRegisterDirect = TRUE;
                rRState = gRRStateInit_c;
                eRState = gERStateReadReg_c;
                SelfNotificationEvent();
            }
#if CT_Feature_Indirect_Registers
            else if('3' == gu8UartData)
            {
                bIsRegisterDirect = FALSE;
                oRState = gORStateInit_c;
                eRState = gERStateOverrideReg_c;
                SelfNotificationEvent();
            }
            else if('4' == gu8UartData)
            {
                bIsRegisterDirect = FALSE;
                rRState = gRRStateInit_c;
                eRState = gERStateReadReg_c;
                SelfNotificationEvent();
            }
            else if('5' == gu8UartData)
            {
                dRState = gDRStateInit_c;
                eRState  = gERStateDumpAllRegs_c;
                SelfNotificationEvent();
            }
#else
            else if('3' == gu8UartData)
            {
                dRState = gDRStateInit_c;
                eRState  = gERStateDumpAllRegs_c;
                SelfNotificationEvent();
            }
#endif
            else
#endif
                if('p' == gu8UartData)
                { 
                    bBackFlag = TRUE;
                }
            evDataFromUART = FALSE;
        }
        break;
    case gERStateOverrideReg_c:
        if(OverrideRegisters()) 
        {
            eRState = gERStateInit_c;
            SelfNotificationEvent();
        }    
        break;
    case gERStateReadReg_c:
        if(ReadRegisters()) 
        {
            eRState = gERStateInit_c;
            SelfNotificationEvent();
        }    
        break;
    case gERStateDumpAllRegs_c:
        if(DumpRegisters()) {
            eRState = gERStateInit_c;
            SelfNotificationEvent();
        }
        break;
    default:
        break;
    }
    return bBackFlag;
}

/************************************************************************************
*
* Dump registers
*
************************************************************************************/
bool_t DumpRegisters(void)
{
#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
    bool_t bBackFlag = FALSE;
    
    switch(dRState)
    {
    case gDRStateInit_c:
        Serial_Print(mAppSer, "\f\r\rDump Registers\r\n", gAllowToBlock_d);   
        Serial_Print(mAppSer, "\r\n-Press [space] to dump registers\r\n", gAllowToBlock_d);
        Serial_Print(mAppSer, "\r\n-Press [p] Previous Menu\r\n", gAllowToBlock_d);
        shortCutsEnabled = FALSE;   
        dRState = gDRStateDumpRegs_c;
        SelfNotificationEvent();
        break;
    case gDRStateDumpRegs_c:
        if(evDataFromUART){
            if(gu8UartData == 'p') 
            {
                bBackFlag = TRUE;
            }
            else if (gu8UartData == ' ') 
            {
                Serial_Print(mAppSer, "\r\n -Dumping registers... \r\n", gAllowToBlock_d);
                const registerLimits_t* interval = registerIntervals;
                
                while(!((*interval).regStart == 0 && (*interval).regEnd == 0))
                {
                    Serial_Print(mAppSer, "\r\n -Access type: ", gAllowToBlock_d);
                    if( (*interval).bIsRegisterDirect )
                    {
                        Serial_Print(mAppSer,"direct\r\n", gAllowToBlock_d);
                    }
                    else
                    {
                        Serial_Print(mAppSer,"indirect\r\n", gAllowToBlock_d);
                    }
                    bIsRegisterDirect = (*interval).bIsRegisterDirect;
                    ReadRFRegs((*interval).regStart, (*interval).regEnd);
                    interval++;
                }
                dRState = gDRStateInit_c;
                SelfNotificationEvent();
            }
        }
        evDataFromUART = FALSE;
        break;
    default:
        break;
    }
    return bBackFlag; 
#else
    return TRUE;
#endif
}

/************************************************************************************
*
* Read and print register values with addresses from u8RegStartAddress 
* to u8RegStopAddress
*
************************************************************************************/
void ReadRFRegs(registerAddressSize_t rasRegStartAddress, registerAddressSize_t rasRegStopAddress)
{ 
#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
    static uint16_t rasRegAddress; 
    registerSize_t rsRegValue; 
    Serial_Print(mAppSer, " ---------------------------------------  ", gAllowToBlock_d); 
    for(rasRegAddress = rasRegStartAddress; 
        rasRegAddress <= rasRegStopAddress; 
        rasRegAddress += (gRegisterSize_c) )
    { 
        Serial_Print(mAppSer, "\r\n|    Address : 0x", gAllowToBlock_d); 
        Serial_PrintHex(mAppSer, (uint8_t*)&rasRegAddress,gRegisterSize_c,0); 
        
        aspTestRequestMsg.msgType = aspMsgTypeXcvrReadReq_c;
        aspTestRequestMsg.msgData.aspXcvrData.addr = (uint16_t)rasRegAddress;
        aspTestRequestMsg.msgData.aspXcvrData.len  = gRegisterSize_c;
        aspTestRequestMsg.msgData.aspXcvrData.mode = !bIsRegisterDirect;
        
        APP_ASP_SapHandler(&aspTestRequestMsg, 0);
        rsRegValue = *((registerSize_t*)aspTestRequestMsg.msgData.aspXcvrData.data);              
        
        Serial_Print(mAppSer, " Data value : 0x", gAllowToBlock_d);                
        Serial_PrintHex(mAppSer, (uint8_t*)&rsRegValue, gRegisterSize_c, 0);  
        Serial_Print(mAppSer, "   |", gAllowToBlock_d);
    }    
    Serial_Print(mAppSer, "\r\n ---------------------------------------  \r\n", gAllowToBlock_d); 
#endif
}

/************************************************************************************
*
* Read register
*
************************************************************************************/
bool_t ReadRegisters(void)
{
#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
    bool_t bBackFlag = FALSE;
    static uint8_t au8RxString[5];
    static uint8_t u8Index;
    static registerAddressSize_t rasRegAddress;
    static registerSize_t rsRegValue;
    static char    auxToPrint[2];
    
    switch(rRState)
    {
    case gRRStateInit_c:
        Serial_Print(mAppSer, "\f\r\rRead Registers\r\n", gAllowToBlock_d);           
        Serial_Print(mAppSer, "\r\n-Press [p] Previous Menu\r\n", gAllowToBlock_d);
        shortCutsEnabled = FALSE;   
        rRState = gRRStateStart_c;
        SelfNotificationEvent();
        break;
    case gRRStateStart_c:
        Serial_Print(mAppSer, "\r\n -write the Register address in Hex and [enter]: 0x", gAllowToBlock_d);
        u8Index = 0;
        rRState = gRRWaitForTheAddress_c; 
        break;
    case gRRWaitForTheAddress_c:
        if(evDataFromUART)
        {
            if((!isAsciiHex(gu8UartData)) && ('\r' != gu8UartData))
            {
                if('p' == gu8UartData)
                { 
                    bBackFlag = TRUE;
                }
                else
                {
                    Serial_Print(mAppSer, "\r\n -Invalid Character!! ", gAllowToBlock_d);
                    rRState = gRRStateStart_c; 
                    SelfNotificationEvent();
                }
            }
            else if((gRegisterAddressASCII_c == u8Index) && ('\r' != gu8UartData))
            { 
                Serial_Print(mAppSer, "\r\n -Value out of Range!! ", gAllowToBlock_d);
                rRState = gRRStateStart_c;
                SelfNotificationEvent();
            }
            else if(isAsciiHex(gu8UartData))
            {
                au8RxString[u8Index++] = gu8UartData;
                auxToPrint[0] = gu8UartData;
                auxToPrint[1] = '\0';
                Serial_Print(mAppSer, auxToPrint, gAllowToBlock_d);
            }
            else
            {
                au8RxString[u8Index] = 0;
                rasRegAddress = (registerAddressSize_t)HexString2Dec(au8RxString);
                aspTestRequestMsg.msgType = aspMsgTypeXcvrReadReq_c;
                aspTestRequestMsg.msgData.aspXcvrData.addr = (uint16_t)rasRegAddress;
                aspTestRequestMsg.msgData.aspXcvrData.len  = gRegisterSize_c;
                aspTestRequestMsg.msgData.aspXcvrData.mode = !bIsRegisterDirect;
                APP_ASP_SapHandler(&aspTestRequestMsg, 0);
                rsRegValue = *((registerSize_t*)aspTestRequestMsg.msgData.aspXcvrData.data);
                
                Serial_Print(mAppSer, "\r\n -Register value : 0x", gAllowToBlock_d);
                Serial_PrintHex(mAppSer, (uint8_t*)&rsRegValue,gRegisterSize_c,0);
                Serial_Print(mAppSer, "\r\n", gAllowToBlock_d);
                
                rRState = gRRStateStart_c; 
                SelfNotificationEvent();
            }
            evDataFromUART = FALSE;
        }
        break;
    default:
        break;
    }
    return bBackFlag;  
#else
    return TRUE;
#endif
}

/************************************************************************************
*
* Override Register
*
************************************************************************************/
bool_t OverrideRegisters(void)
{
#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
    bool_t bBackFlag = FALSE;
    static uint8_t au8RxString[5];
    static uint8_t u8Index;
    static registerAddressSize_t rasRegAddress;
    static registerSize_t rsRegValue;
    static char auxToPrint[2];
    
    switch(oRState)
    {
    case gORStateInit_c:
        Serial_Print(mAppSer, "\f\r\nWrite Registers\r\n", gAllowToBlock_d);          
        Serial_Print(mAppSer, "\r\n-Press [p] Previous Menu\r\n", gAllowToBlock_d);
        shortCutsEnabled = FALSE;   
        oRState = gORStateStart_c;
        SelfNotificationEvent();
        break;
    case gORStateStart_c:
        Serial_Print(mAppSer, "\r\n -write the Register address in Hex and [enter]: 0x", gAllowToBlock_d);
        u8Index = 0;
        oRState = gORWaitForTheAddress_c; 
        break;
    case gORWaitForTheAddress_c:
        if(evDataFromUART){
            if((!isAsciiHex(gu8UartData)) && ('\r' != gu8UartData))
            {
                if('p' == gu8UartData)
                { 
                    bBackFlag = TRUE;
                }
                else
                {
                    Serial_Print(mAppSer, "\r\n -Invalid Character!! ", gAllowToBlock_d);
                    oRState = gORStateStart_c;  
                    SelfNotificationEvent();
                }
            }
            else if((gRegisterAddressASCII_c == u8Index) && ('\r' != gu8UartData))
            { 
                Serial_Print(mAppSer, "\r\n -Value out of Range!! ", gAllowToBlock_d);
                oRState = gORStateStart_c;
                SelfNotificationEvent();
            }
            else if(isAsciiHex(gu8UartData))
            {
                au8RxString[u8Index++] = gu8UartData;
                auxToPrint[0] = gu8UartData;
                auxToPrint[1] = '\0';
                Serial_Print(mAppSer, auxToPrint, gAllowToBlock_d);
            }
            else
            {
                au8RxString[u8Index] = 0;
                rasRegAddress = (registerAddressSize_t)HexString2Dec(au8RxString);
                Serial_Print(mAppSer, "\r\n -write the Register value to override in Hex and [enter]: 0x", gAllowToBlock_d);
                u8Index = 0;
                oRState = gORWaitForTheValue_c; 
            }
            evDataFromUART = FALSE;
        }
        break;
    case gORWaitForTheValue_c:
        if(evDataFromUART)
        {
            if((!isAsciiHex(gu8UartData)) && ('\r' != gu8UartData))
            {
                if('p' == gu8UartData)
                { 
                    bBackFlag = TRUE;
                }
                else
                {
                    Serial_Print(mAppSer, "\r\n -Invalid Character!! ", gAllowToBlock_d);
                    oRState = gORStateStart_c;  
                    SelfNotificationEvent();
                }
            }
            else if((2 == u8Index) && ('\r' != gu8UartData))
            { 
                Serial_Print(mAppSer, "\r\n -Value out of Range!! ", gAllowToBlock_d);
                oRState = gORStateStart_c;  
                SelfNotificationEvent();
            }
            else if(isAsciiHex(gu8UartData))
            {
                au8RxString[u8Index++] = gu8UartData;
                auxToPrint[0] = gu8UartData;
                auxToPrint[1] = '\0';
                Serial_Print(mAppSer, auxToPrint, gAllowToBlock_d);
            }
            else
            {
                au8RxString[u8Index] = 0;
                rsRegValue = (registerSize_t)HexString2Dec(au8RxString);
                aspTestRequestMsg.msgType = aspMsgTypeXcvrWriteReq_c;
                aspTestRequestMsg.msgData.aspXcvrData.addr = (uint16_t)rasRegAddress;
                aspTestRequestMsg.msgData.aspXcvrData.len  = gRegisterAddress_c;
                aspTestRequestMsg.msgData.aspXcvrData.mode = !bIsRegisterDirect;
                FLib_MemCpy(aspTestRequestMsg.msgData.aspXcvrData.data, &rsRegValue, gRegisterSize_c);
                APP_ASP_SapHandler(&aspTestRequestMsg, 0);
                
                Serial_Print(mAppSer, "\r\n Register overridden \r\n", gAllowToBlock_d);
                u8Index = 0;
                oRState = gORStateStart_c; 
                SelfNotificationEvent();
            }
            evDataFromUART = FALSE;
        }
        break;
    default:
        break;
    }
    return bBackFlag; 
#else
    return TRUE;
#endif
}

/************************************************************************************
*
* Handler for Carrier Sense Test and Transmission Control Test
*
************************************************************************************/
bool_t CSenseAndTCtrl(void)
{
    bool_t bBackFlag = FALSE;
    static uint8_t testSelector = 0;
    
    if(evTestParameters)
    {
        (void)MLMESetChannelRequest(testChannel);   
#if CT_Feature_Calibration
        (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
        (void)MLMEPAOutputAdjust(testPower);
#if CT_Feature_Xtal_Trim
        aspTestRequestMsg.msgType = aspMsgTypeSetXtalTrimReq_c;
        aspTestRequestMsg.msgData.aspXtalTrim.trim = xtalTrimValue;
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
#endif
        PrintTestParameters(TRUE);
        evTestParameters = FALSE;
    }
    
    switch(cstcState)
    {
    case gCsTcStateInit_c:
        TestMode(gTestModeForceIdle_c);
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8RadioCSTCSelectMenu, mAppSer);
        PrintTestParameters(FALSE);
        shortCutsEnabled = TRUE;
        bTxDone = FALSE;
        bScanDone = FALSE;
        timePassed = FALSE;
        
        cstcState = gCsTcStateSelectTest_c;
        break;
    case gCsTcStateSelectTest_c:
        if(evDataFromUART)
        {
            if('1' == gu8UartData)
            {
                cstcState = gCsTcStateCarrierSenseStart_c;
                testSelector = 1;
                SelfNotificationEvent();
            }
            else if ('2' == gu8UartData)
            {
                cstcState = gCsTcStateTransmissionControlStart_c;
                testSelector = 2;
                SelfNotificationEvent();
            }
            else if( 'p' == gu8UartData)
            {
                cstcState = gCsTcStateInit_c;
                testSelector = 0;
                bBackFlag = TRUE;
            }
            evDataFromUART = FALSE;
        }
        break;
    default:
        if(testSelector == 1)
            CarrierSenseHandler();
        else if(testSelector == 2)
            TransmissionControlHandler();
        break;
    }
    return bBackFlag;  
}

/************************************************************************************
*
* Handler for Transmission Control Test called by above function
*
************************************************************************************/
void TransmissionControlHandler(void)
{
    uint32_t totalTimeMs;
    const uint16_t u16TotalPacketsOptions[] = {1,25,100,500,1000,2000,5000,10000,65535};
    static uint16_t u16TotalPackets;
    static uint16_t u16PacketCounter = 0;
    static uint16_t miliSecDelay = 0;
    static phyTime_t startTime;
    int8_t fillIndex = 0;
    uint8_t* smacPduPtr;
    energy8_t e8TempRssivalue;
    
    switch(cstcState)
    {
    case gCsTcStateTransmissionControlStart_c:
        PrintMenu(cu8ShortCutsBar, mAppSer);
        PrintMenu(cu8CsTcTestMenu, mAppSer);
        PrintTestParameters(FALSE);
        miliSecDelay = 0;
        u16TotalPackets = 0;
        u16PacketCounter = 0;
        fillIndex = testPayloadLen / gPrbs9BufferLength_c;
        
        while(fillIndex > 0)
        {
            fillIndex--;
            smacPduPtr = gAppTxPacket->smacPdu.smacPdu + fillIndex * gPrbs9BufferLength_c;
            FLib_MemCpy(smacPduPtr, u8Prbs9Buffer, gPrbs9BufferLength_c);
        }
        smacPduPtr = gAppTxPacket->smacPdu.smacPdu + ((testPayloadLen / gPrbs9BufferLength_c)*gPrbs9BufferLength_c);
        FLib_MemCpy(smacPduPtr, u8Prbs9Buffer, (testPayloadLen % gPrbs9BufferLength_c));
        
        gAppTxPacket->u8DataLength = testPayloadLen;
        
        cstcState = gCsTcStateTransmissionControlSelectNumOfPackets_c;
        break;
    case gCsTcStateTransmissionControlSelectNumOfPackets_c:
        if(evDataFromUART)
        {
            if((gu8UartData >= '0') && (gu8UartData <= '8'))
            {
                u16TotalPackets = u16TotalPacketsOptions[gu8UartData - '0'];  
                cstcState = gCsTcStateTransmissionControlSelectInterpacketDelay_c;
                Serial_Print(mAppSer,"\r\n\r\n Please type InterPacket delay in miliseconds and press [ENTER]",gAllowToBlock_d);
                Serial_Print(mAppSer,"\r\n(During test, exit by pressing [SPACE])\r\n\r\n",gAllowToBlock_d);
                SelfNotificationEvent();
            }
            else if('p' == gu8UartData)
            { 
                cstcState = gCsTcStateInit_c;
                SelfNotificationEvent();
            }
            evDataFromUART = FALSE;
        }
        break;
    case gCsTcStateTransmissionControlSelectInterpacketDelay_c:
        if(evDataFromUART)
        {
            if(gu8UartData == '\r' && miliSecDelay != 0)
            {
                cstcState = gCsTcStateTransmissionControlPerformingTest_c;
                startTime = GetTimestampUS();
                (void)MLMEScanRequest(testChannel);
            }
            else if((gu8UartData >= '0') && (gu8UartData <='9'))
            {
                miliSecDelay = miliSecDelay*10 + (gu8UartData - '0');
                Serial_PrintDec(mAppSer, (uint32_t)(gu8UartData - '0'));
            }
            else if('p' == gu8UartData)
            { 
                cstcState = gCsTcStateInit_c;
                SelfNotificationEvent();
            }
            evDataFromUART = FALSE;
        }
        break;
    case gCsTcStateTransmissionControlPerformingTest_c:
        if(bScanDone)
        {
            bScanDone = FALSE;
            (void)MCPSDataRequest(gAppTxPacket);
        }
        if(bTxDone)
        {
            bTxDone = FALSE;                                                  
            u16PacketCounter++;
            Serial_Print(mAppSer,"\r\n\tPacket number: ",gAllowToBlock_d);
            Serial_PrintDec(mAppSer, (uint32_t)(u16PacketCounter));
            Serial_Print(mAppSer, "; RSSI value: ", gAllowToBlock_d);
            e8TempRssivalue = (energy8_t) au8ScanResults[testChannel];
#if CT_Feature_RSSI_Has_Sign
            if(e8TempRssivalue < 0)
            {
                e8TempRssivalue *= -1;
#else 
            if(e8TempRssivalue != 0)
            {
#endif
                Serial_Print(mAppSer, "-", gAllowToBlock_d);
            }
            Serial_PrintDec(mAppSer, (uint32_t)e8TempRssivalue);
            Serial_Print(mAppSer," dBm\r\n",gAllowToBlock_d);
            if(u16PacketCounter < u16TotalPackets)
            {
                totalTimeMs  = (uint32_t)(GetTimestampUS() - startTime);
                totalTimeMs -= GetTransmissionTime(testPayloadLen, crtBitrate);
                totalTimeMs = (totalTimeMs % 1000 < 500) ? totalTimeMs/1000 : (totalTimeMs/1000)+1;
                if(totalTimeMs > miliSecDelay)
                {
                    Serial_Print(mAppSer, " Overhead + Transmission + ED = ~",gAllowToBlock_d);
                    Serial_PrintDec(mAppSer, totalTimeMs);
                    Serial_Print(mAppSer,"ms\r\n Interpacket delay too small (Press [ENTER] to continue)\r\n",gAllowToBlock_d);
                    cstcState = gCsTcStateTransmissionControlEndTest_c;
                    SelfNotificationEvent();
                    break;
                }
                FlaggedDelay_ms(miliSecDelay - totalTimeMs);
            }
            else
            {        
                Serial_Print(mAppSer,"\r\n\r\nFinished transmitting ",gAllowToBlock_d);
                Serial_PrintDec(mAppSer, (uint32_t)u16TotalPackets);
                Serial_Print(mAppSer," packets!\r\n\r\n",gAllowToBlock_d);
                Serial_Print(mAppSer,"\r\n -Press [ENTER] to end Transmission Control Test", gAllowToBlock_d);
                cstcState = gCsTcStateTransmissionControlEndTest_c;
            }
        }
        if(timePassed)
        {
            timePassed = FALSE;
            startTime = GetTimestampUS();
            (void)MLMEScanRequest(testChannel);
        }
        if(evDataFromUART && gu8UartData == ' ')
        {
            Serial_Print(mAppSer,"\r\n\r\n-Test interrupted by user. Press [ENTER] to continue\r\n\r\n",gAllowToBlock_d);
            cstcState = gCsTcStateTransmissionControlEndTest_c;
        }
        break;
    case gCsTcStateTransmissionControlEndTest_c:    
        if(evDataFromUART && gu8UartData == '\r')
        {
            cstcState = gCsTcStateInit_c;
            SelfNotificationEvent();
        }
        evDataFromUART = FALSE;
        break;
    default:
        break;
    }
}
/************************************************************************************
*
* Handler for Carrier Sense Test
*
************************************************************************************/
void CarrierSenseHandler(void)
{
    int8_t fillIndex = 0;
    uint8_t* smacPduPtr;
    energy8_t e8TempRssivalue;
    
    switch(cstcState)
    {
    case gCsTcStateCarrierSenseStart_c:
#if CT_Feature_Calibration
        if( gMode1Bitrate_c == crtBitrate )
        {
            (void)MLMESetAdditionalRFOffset(gOffsetIncrement + 30);
        }
        else
        {
            (void)MLMESetAdditionalRFOffset(gOffsetIncrement + 60);
        }
#endif
        (void)MLMESetChannelRequest(testChannel);
        
        Serial_Print(mAppSer, "\r\n\r\n Press [SPACE] to begin/interrupt test",gAllowToBlock_d);
        Serial_Print(mAppSer,  "\r\n Press [p] to return to previous menu", gAllowToBlock_d);
        shortCutsEnabled = FALSE;
        Serial_Print(mAppSer,"\r\n",gAllowToBlock_d);
        
        fillIndex = testPayloadLen / gPrbs9BufferLength_c;
        while(fillIndex > 0)
        {
            fillIndex--;
            smacPduPtr = gAppTxPacket->smacPdu.smacPdu + fillIndex * gPrbs9BufferLength_c;
            FLib_MemCpy(smacPduPtr, u8Prbs9Buffer, gPrbs9BufferLength_c);
        }
        smacPduPtr = gAppTxPacket->smacPdu.smacPdu + ((testPayloadLen / gPrbs9BufferLength_c)*gPrbs9BufferLength_c);
        FLib_MemCpy(smacPduPtr, u8Prbs9Buffer, (testPayloadLen % gPrbs9BufferLength_c));
        
        gAppTxPacket->u8DataLength = testPayloadLen;
        
        cstcState = gCsTcStateCarrierSenseSelectType_c;
        break;
    case gCsTcStateCarrierSenseSelectType_c:
        if(evDataFromUART)
        {
            if(' ' == gu8UartData)
            {
                cstcState = gCsTcStateCarrierSensePerformingTest_c;
                (void)MLMEScanRequest(testChannel);
            }
            else if ('p' == gu8UartData)
            {
#if CT_Feature_Calibration
                (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
                (void)MLMESetChannelRequest(testChannel);
                cstcState = gCsTcStateInit_c;
                SelfNotificationEvent();
            }
            evDataFromUART = FALSE;
        }
        break;
    case gCsTcStateCarrierSensePerformingTest_c:
        if(bScanDone)
        {
            bScanDone = FALSE;
            Serial_Print(mAppSer, "\r\n\tSampling done. RSSI value: ", gAllowToBlock_d);
            e8TempRssivalue = (energy8_t)au8ScanResults[testChannel];
#if CT_Feature_RSSI_Has_Sign
            if(e8TempRssivalue < 0)
            {
                e8TempRssivalue *= -1;
#else
            if(e8TempRssivalue != 0)
            {
#endif
                Serial_Print(mAppSer, "-", gAllowToBlock_d);
            }
            Serial_PrintDec(mAppSer, (uint32_t)e8TempRssivalue);
            Serial_Print(mAppSer, "dBm", gAllowToBlock_d);
            if(e8TempRssivalue > ccaThresh)
            {
                (void)MCPSDataRequest(gAppTxPacket);
            }
            else
            {
                (void)MLMEScanRequest(testChannel);
            }
        }
        if(bTxDone)
        {
            bTxDone = FALSE;
            
            Serial_Print(mAppSer,"\r\n Transmission Performed\r\n", gAllowToBlock_d);
            Serial_Print(mAppSer,"\r\n -Press [ENTER] to end Carrier Sense Test", gAllowToBlock_d);
            cstcState = gCsTcStateCarrierSenseEndTest_c;
        }
        if(evDataFromUART && gu8UartData == ' ')
        {
            Serial_Print(mAppSer,"\r\n\r\n-Test interrupted by user. Press [ENTER] to continue\r\n\r\n",gAllowToBlock_d);
            cstcState = gCsTcStateCarrierSenseEndTest_c;
        }
        break;
    case gCsTcStateCarrierSenseEndTest_c:
        if(evDataFromUART && gu8UartData == '\r')
        {
#if CT_Feature_Calibration
            (void)MLMESetAdditionalRFOffset(gOffsetIncrement);
#endif
            (void)MLMESetChannelRequest(testChannel);
            cstcState = gCsTcStateInit_c;
            SelfNotificationEvent();
        }
        evDataFromUART = FALSE;
        break;
    default:
        break;
    }
}
    
/************************************************************************************
*
* Auxiliary Functions
*
************************************************************************************/

/**************************************************************************************/
void SetRadioRxOnNoTimeOut(void)
{
    bRxDone = FALSE;
    gAppRxPacket->u8MaxDataLength = gMaxSmacSDULength_c;
    (void)MLMERXEnableRequest(gAppRxPacket, 0);
}

/**************************************************************************************/
void PrintPerRxFinalLine(uint16_t u16Received, uint16_t u16Total)
{
    Serial_Print(mAppSer,"Received ", gAllowToBlock_d);
    Serial_PrintDec(mAppSer,(uint32_t)u16Received);
    Serial_Print(mAppSer," of ", gAllowToBlock_d);
    Serial_PrintDec(mAppSer,(uint32_t)u16Total);
    Serial_Print(mAppSer," packets transmitted \r\n", gAllowToBlock_d);
    Serial_Print(mAppSer,"\r\n Press [enter] to go back to the Per Rx test menu", gAllowToBlock_d);
}

/************************************************************************************
* 
* 
* By employing this function, users can execute a test of the radio. Test mode 
* implements the following:
*   -PRBS9 Mode, 
*   -Force_idle, 
*   -Continuos TX without modulation, 
*   -Continuos TX with modulation.(0's,1's and PN patterns)
*
************************************************************************************/
smacErrors_t TestMode
(
smacTestMode_t  mode  /*IN: The test mode to start.*/
)
{
    aspTestRequestMsg.msgType = aspMsgTypeTelecTest_c;
    
#if(TRUE == smacParametersValidation_d)
    if(gMaxTestMode_c <= mode)
    {
        return gErrorOutOfRange_c;
    }
#endif
    
    if(gTestModeForceIdle_c == mode)
    {
        aspTestRequestMsg.msgData.aspTelecTest.mode = gTestForceIdle_c;
    }                                                                             
    else if(gTestModeContinuousTxModulated_c == mode)
    {
        if(contTxModBitValue==gContTxModSelectOnes_c)
        {
            aspTestRequestMsg.msgData.aspTelecTest.mode = gTestContinuousTxModOne_c;
        }
        else if(contTxModBitValue == gContTxModSelectZeros_c)
        {
            aspTestRequestMsg.msgData.aspTelecTest.mode = gTestContinuousTxModZero_c;
        }
        else if(contTxModBitValue == gContTxModSelectPN9_c)
        {
#ifdef gPHY_802_15_4g_d
            aspTestRequestMsg.msgData.aspTelecTest.mode = gTestContinuousTxContPN9_c;
#else
            aspTestRequestMsg.msgData.aspTelecTest.mode = gTestPulseTxPrbs9_c;
#endif
        }
    } 
    else if(gTestModeContinuousTxUnmodulated_c == mode)
    { 
        aspTestRequestMsg.msgData.aspTelecTest.mode = gTestContinuousTxNoMod_c;
    } 
    else if(gTestModeContinuousRxBER_c == mode)
    {
        aspTestRequestMsg.msgData.aspTelecTest.mode = gTestContinuousRx_c;
    }
    else if(gTestModePRBS9_c == mode)
    {   
        /*Set Data Mode*/
        gAppTxPacket->u8DataLength = gPrbs9BufferLength_c;
        FLib_MemCpy(gAppTxPacket->smacPdu.smacPdu, u8Prbs9Buffer, gPrbs9BufferLength_c);
        PacketHandler_Prbs9(); 
    }
    if(gTestModePRBS9_c != mode)
        (void)APP_ASP_SapHandler(&aspTestRequestMsg, 0);
    
    return gErrorNoError_c;
}

/************************************************************************************
* PacketHandler_Prbs9
* 
* This function sends OTA the content of a PRBS9 polynomial of 65 bytes of payload.
*
*
************************************************************************************/
void PacketHandler_Prbs9(void)                                                  
{
    smacErrors_t err;
    /*@CMA, Need to set Smac to Idle in order to get PRBS9 to work after a second try on the Conn Test menu*/
    (void)MLMERXDisableRequest();                                               
    (void)MLMETXDisableRequest();
    err = MCPSDataRequest(gAppTxPacket);  
    if(err != gErrorNoError_c)
    {
        failedPRBS9 = TRUE;
        SelfNotificationEvent(); //in case data isn't sent, no confirm event will fire. 
        //this way we need to make sure the application will not freeze.
    }
}

/*****************************************************************************
* UartRxCallBack function
*
* Interface assumptions:
* This callback is triggered when a new byte is received over the UART
*
* Return Value:
* None
*****************************************************************************/
void UartRxCallBack(void * param) 
{
    (void)OSA_EventSet(gTaskEvent, gUART_RX_EVENT_c);
}

/*@CMA, Conn Test. Range Test CallBack*/
void RangeTest_Timer_CallBack ()
{
    (void)OSA_EventSet(gTaskEvent, gRangeTest_EVENT_c);
}


/************************************************************************************
*
* Increments channel on the ED Confirm event and fires a new ED measurement request
*
************************************************************************************/
static void IncrementChannelOnEdEvent()
{
    bScanDone = FALSE;
    smacErrors_t err;
    if (ChannelToScan <= gMaxChannel_c)                              
    {                                                                  
        err = MLMEScanRequest((channels_t)ChannelToScan);                                          
        if(err == gErrorNoError_c)
            ChannelToScan++;                                                //Increment channel to scan
    }
    else 
    {
        bScanDone = TRUE;                                               //PRINT ALL CHANNEL RESULTS
        FlaggedDelay_ms(300);                                           //Add delay between channel scanning series.
    } 
}

/************************************************************************************
*
* Configures channel for second pan and sets dwell time
*
************************************************************************************/
#if gMpmMaxPANs_c == 2
static bool_t ConfigureAlternatePan(void)
{
    bool_t bBackFlag = FALSE;
    static uint8_t u8Channel = 0;
    static uint8_t u8PS = 0;
    static uint8_t u8Range = 0;
    static MpmPerConfigStates_t mMpmPerState = gMpmStateInit_c;
    
    switch(mMpmPerState)
    {
    case gMpmStateInit_c:
        if(evDataFromUART)
        {
            evDataFromUART = FALSE;
            if(gu8UartData == ' ')
            {
                shortCutsEnabled = FALSE;
                u8Channel    = 0;
                u8PS         = 0;
                u8Range      = 0;
                Serial_Print(mAppSer, "\r\n\r\nDual Pan RX\r\nType channel number between ", gAllowToBlock_d);
                Serial_PrintDec(mAppSer, (uint32_t)gMinChannel_c);
                Serial_Print(mAppSer, " and ", gAllowToBlock_d);
                Serial_PrintDec(mAppSer, (uint32_t)gMaxChannel_c);
                Serial_Print(mAppSer, " and press [ENTER]. \r\nMake sure input channel differs from channel"
                             " selected using shortcut keys\r\n", gAllowToBlock_d);
                mMpmPerState = gMpmStateConfigureChannel_c;
            }
        }
        break;
    case gMpmStateConfigureChannel_c:
        if(evDataFromUART)
        {
            evDataFromUART = FALSE;
            if(gu8UartData == '\r')
            {
                mMpmPerState = gMpmStateConfirmChannel_c;
                SelfNotificationEvent();
            }
            else if (gu8UartData >= '0' && gu8UartData <= '9')
            {
                if( (uint16_t)(u8Channel*10 + (gu8UartData-'0')) <= 0xFF)
                {
                    u8Channel = u8Channel*10 + (gu8UartData - '0');
                    Serial_PrintDec(mAppSer, (uint32_t)(gu8UartData-'0'));
                }
            }
        }
        break;
    case gMpmStateConfirmChannel_c:
        if(u8Channel < gMinChannel_c || u8Channel > gMaxChannel_c)
        {
            Serial_Print(mAppSer, "\r\n\t Error: Invalid channel. Input valid channel\r\n",gAllowToBlock_d);
            u8Channel = 0;
            mMpmPerState = gMpmStateConfigureChannel_c;
        }else if(u8Channel == testChannel)
        {
            Serial_Print(mAppSer, "\r\n\t Error: Same channel for both PANs. Input valid channel\r\n", gAllowToBlock_d);
            u8Channel = 0;
            mMpmPerState = gMpmStateConfigureChannel_c;
        }else
        {
            (void)MLMESetActivePan(gSmacPan1_c);
            (void)MLMESetChannelRequest((channels_t)u8Channel);
            (void)MLMESetActivePan(gSmacPan0_c);
            Serial_Print(mAppSer,"\r\nConfigure Dwell Time (PS*(RANGE + 1) ms): \r\n", gAllowToBlock_d);
            PrintMenu(cu8MpmMenuPs, mAppSer);
            mMpmPerState = gMpmStateInputDwellPS_c;
        }
        break;
    case gMpmStateInputDwellPS_c:
        if(evDataFromUART)
        {
            evDataFromUART = FALSE;
            if(gu8UartData >='0' && gu8UartData <='3')
            {
                u8PS = gu8UartData - '0';
                Serial_Print(mAppSer, "\r\nInput RANGE parameter between 0 and 63"
                             " and press [ENTER]\r\n",gAllowToBlock_d);
                mMpmPerState = gMpmStateInputDwellRange_c;
            }
        }
        break;
    case gMpmStateInputDwellRange_c:
        if(evDataFromUART)
        {
            evDataFromUART = FALSE;
            if(gu8UartData >='0' && gu8UartData <='9')
            {
                u8Range = u8Range*10 + (gu8UartData - '0');
                if(u8Range >= ((mDualPanDwellTimeMask_c >> mDualPanDwellTimeShift_c) + 1))
                {
                    Serial_Print(mAppSer,"\r\n\tError: Invalid RANGE. Input new value.\r\n",gAllowToBlock_d);
                    u8Range = 0;
                }
                else
                {
                    Serial_PrintDec(mAppSer, (uint32_t)(gu8UartData - '0'));
                }
            }
            else if(gu8UartData == '\r')
            {
                (void)MLMEConfigureDualPanSettings(TRUE, TRUE, u8PS, u8Range);
                Serial_Print(mAppSer,"\r\n\t Done! Press [SPACE] to start test\r\n", gAllowToBlock_d);
                mMpmPerState = gMpmStateExit_c;
                SelfNotificationEvent();
            }
        }
        break;
    case gMpmStateExit_c:
        bBackFlag = TRUE;
        mMpmPerState = gMpmStateInit_c;
        break;
    }
    
    return bBackFlag;
}
#endif
/***********************************************************************
*********************Utilities Software********************************
************************************************************************/

static bool_t stringComp(uint8_t * au8leftString, uint8_t * au8RightString, uint8_t bytesToCompare)
{
    do
    {
    }while((*au8leftString++ == *au8RightString++) && --bytesToCompare);
    return(0 == bytesToCompare);
}

static void DelayTimeElapsed(void* param)
{
    (void)param;
    timePassed = TRUE;
    (void)OSA_EventSet(gTaskEvent, gTimePassed_EVENT_c);
}

#if CT_Feature_Direct_Registers || CT_Feature_Indirect_Registers
static uint32_t HexString2Dec(uint8_t* hexString)
{
    uint32_t decNumber = 0;
    uint8_t  idx = 0;
    while(hexString[idx] && idx < 8)
    {
        if(hexString[idx] >= 'a' && hexString[idx] <= 'f')
        {
            decNumber = (decNumber << 4) + hexString[idx] - 'a' + 10;
        }
        else if(hexString[idx] >= 'A' && hexString[idx] <= 'F')
        {
            decNumber = (decNumber << 4) + hexString[idx] - 'A' + 10;
        }
        else if(hexString[idx] >= '0' && hexString[idx] <= '9')
        {
            decNumber = (decNumber << 4) + hexString[idx] - '0';
        }
        else
        {
            break;
        }
        ++idx;
    }
    return decNumber;
}
#endif

/***********************************************************************
************************************************************************/
