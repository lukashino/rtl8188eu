/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8188E_PHYCFG_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_iol.h>
#include <rtl8188e_hal.h>

/*---------------------------Define Local Constant---------------------------*/
/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/

/*------------------------Define local variable------------------------------*/


/*--------------------Define export function prototype-----------------------*/
/*  Please refer to header file */
/*--------------------Define export function prototype-----------------------*/

/*----------------------------Function Body----------------------------------*/
/*  */
/*  1. BB register R/W API */
/*  */

/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u4Byte		BitMask,
*
* Output:	none
* Return:		u4Byte		Return the shift bit bit position of the mask
*/
static	u32
phy_CalculateBitShift(
	u32 BitMask
	)
{
	u32 i;

	for (i=0; i<=31; i++)
	{
		if ( ((BitMask>>i) &  0x1 ) == 1)
			break;
	}

	return (i);
}

#if (SIC_ENABLE == 1)
static bool
sic_IsSICReady(
		PADAPTER	Adapter
	)
{
	bool		bRet=false;
	u32		retryCnt=0;
	u8		sic_cmd=0xff;

	while (1)
	{
		if (retryCnt++ >= SIC_MAX_POLL_CNT)
		{
			return false;
		}

		sic_cmd = rtw_read8(Adapter, SIC_CMD_REG);
#if (SIC_HW_SUPPORT == 1)
		sic_cmd &= 0xf0;	/*  [7:4] */
#endif
		if (sic_cmd == SIC_CMD_READY)
			return true;
		else
		{
			rtw_msleep_os(1);
		}
	}

	return bRet;
}

static u32
sic_Read4Byte(
	void *		Adapter,
	u32		offset
	)
{
	u32	u4ret=0xffffffff;
#if RTL8188E_SUPPORT == 1
	u8	retry = 0;
#endif

	if (sic_IsSICReady(Adapter))
	{
#if (SIC_HW_SUPPORT == 1)
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_PREREAD);
#endif
		rtw_write8(Adapter, SIC_ADDR_REG, (u8)(offset&0xff));
		rtw_write8(Adapter, SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8));
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_READ);

#if RTL8188E_SUPPORT == 1
		retry = 4;
		while (retry--){
			rtw_udelay_os(50);
		}
#else
		rtw_udelay_os(200);
#endif

		if (sic_IsSICReady(Adapter))
			u4ret = rtw_read32(Adapter, SIC_DATA_REG);
	}

	return u4ret;
}

static void
sic_Write4Byte(
	void *		Adapter,
	u32		offset,
	u32		data
	)
{
#if RTL8188E_SUPPORT == 1
	u8	retry = 6;
#endif
	if (sic_IsSICReady(Adapter))
	{
#if (SIC_HW_SUPPORT == 1)
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_PREWRITE);
#endif
		rtw_write8(Adapter, SIC_ADDR_REG, (u8)(offset&0xff));
		rtw_write8(Adapter, SIC_ADDR_REG+1, (u8)((offset&0xff00)>>8));
		rtw_write32(Adapter, SIC_DATA_REG, (u32)data);
		rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_WRITE);
#if RTL8188E_SUPPORT == 1
		while (retry--){
			rtw_udelay_os(50);
		}
#else
		rtw_udelay_os(150);
#endif

	}
}
/*  */
/*  extern function */
/*  */
static void
SIC_SetBBReg(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask,
		u32		Data
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u32			OriginalValue, BitShift;
	u16			BBWaitCounter = 0;

	/*  */
	/*  Critical section start */
	/*  */

	if (BitMask!= bMaskDWord){/* if not "double word" write */
		OriginalValue = sic_Read4Byte(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = (((OriginalValue) & (~BitMask)) | (Data << BitShift));
	}

	sic_Write4Byte(Adapter, RegAddr, Data);
}

static u32
SIC_QueryBBReg(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u32			ReturnValue = 0, OriginalValue, BitShift;
	u16			BBWaitCounter = 0;

	OriginalValue = sic_Read4Byte(Adapter, RegAddr);
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	return (ReturnValue);
}

void
SIC_Init(
		PADAPTER	Adapter
	)
{
	/*  Here we need to write 0x1b8~0x1bf = 0 after fw is downloaded */
	/*  because for 8723E at beginning 0x1b8=0x1e, that will cause */
	/*  sic always not be ready */
#if (SIC_HW_SUPPORT == 1)
	rtw_write8(Adapter, SIC_INIT_REG, SIC_INIT_VAL);
	rtw_write8(Adapter, SIC_CMD_REG, SIC_CMD_INIT);
#else
	rtw_write32(Adapter, SIC_CMD_REG, 0);
	rtw_write32(Adapter, SIC_CMD_REG+4, 0);
#endif
}

static bool
SIC_LedOff(
		PADAPTER	Adapter
	)
{
	/*  When SIC is enabled, led pin will be used as debug pin, */
	/*  so don't execute led function when SIC is enabled. */
	return true;
}
#endif

/**
* Function:	PHY_QueryBBReg
*
* OverView:	Read "sepcific bits" from BB register
*
* Input:
*			PADAPTER		Adapter,
*			u4Byte			RegAddr,	The target address to be readback
*			u4Byte			BitMask		The target bit position in the target address
*								to be readback
* Output:	None
* Return:		u4Byte			Data		The readback register value
* Note:		This function is equal to "GetRegSetting" in PHY programming guide
*/
u32
rtl8188e_PHY_QueryBBReg(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask
	)
{
	u32	ReturnValue = 0, OriginalValue, BitShift;
	u16	BBWaitCounter = 0;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

#if (SIC_ENABLE == 1)
	return SIC_QueryBBReg(Adapter, RegAddr, BitMask);
#endif

	OriginalValue = rtw_read32(Adapter, RegAddr);
	BitShift = phy_CalculateBitShift(BitMask);
	ReturnValue = (OriginalValue & BitMask) >> BitShift;

	return (ReturnValue);
}


/**
* Function:	PHY_SetBBReg
*
* OverView:	Write "Specific bits" to BB register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			u4Byte			RegAddr,	The target address to be modified
*			u4Byte			BitMask		The target bit position in the target address
*									to be modified
*			u4Byte			Data		The new register value in the target bit position
*									of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRegSetting" in PHY programming guide
*/

void
rtl8188e_PHY_SetBBReg(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask,
		u32		Data
	)
{
	struct hal_data_8188e	*pHalData		= GET_HAL_DATA(Adapter);
	u32			OriginalValue, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

#if (SIC_ENABLE == 1)
	SIC_SetBBReg(Adapter, RegAddr, BitMask, Data);
	return;
#endif

	if (BitMask!= bMaskDWord){/* if not "double word" write */
		OriginalValue = rtw_read32(Adapter, RegAddr);
		BitShift = phy_CalculateBitShift(BitMask);
		Data = ((OriginalValue & (~BitMask)) | (Data << BitShift));
	}

	rtw_write32(Adapter, RegAddr, Data);
}


/*  */
/*  2. RF register R/W API */
/*  */
/**
* Function:	phy_RFSerialRead
*
* OverView:	Read regster from RF chips
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			Offset,		The target address to be read
*
* Output:	None
* Return:		u4Byte			reback value
* Note:		Threre are three types of serial operations:
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
*/
static	u32
phy_RFSerialRead(
		PADAPTER			Adapter,
		RF_RADIO_PATH_E	eRFPath,
		u32				Offset
	)
{
	u32						retValue = 0;
	struct hal_data_8188e				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;
	u32						tmplong,tmplong2;
	u8					RfPiEnable=0;
	/*  */
	/*  Make sure RF register offset is correct */
	/*  */
	Offset &= 0xff;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  For 92S LSSI Read RFLSSIRead */
	/*  For RF A/B write 0x824/82c(does not work in the future) */
	/*  We must use 0x824 for RF A and B to execute read trigger */
	tmplong = PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord);
	if (eRFPath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = PHY_QueryBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord);

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) | (NewOffset<<23) | bLSSIReadEdge;	/* T65 RF */

	PHY_SetBBReg(Adapter, rFPGA0_XA_HSSIParameter2, bMaskDWord, tmplong&(~bLSSIReadEdge));
	rtw_udelay_os(10);/*  PlatformStallExecution(10); */

	PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, bMaskDWord, tmplong2);
	rtw_udelay_os(100);/* PlatformStallExecution(100); */

	rtw_udelay_os(10);/* PlatformStallExecution(10); */

	if (eRFPath == RF_PATH_A)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XA_HSSIParameter1, BIT8);
	else if (eRFPath == RF_PATH_B)
		RfPiEnable = (u8)PHY_QueryBBReg(Adapter, rFPGA0_XB_HSSIParameter1, BIT8);

	if (RfPiEnable)
	{	/*  Read from BBreg8b8, 12 bits for 8190, 20bits for T65 RF */
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBackPi, bLSSIReadBackData);
	}
	else
	{	/* Read from BBreg8a0, 12 bits for 8190, 20 bits for T65 RF */
		retValue = PHY_QueryBBReg(Adapter, pPhyReg->rfLSSIReadBack, bLSSIReadBackData);
	}

	return retValue;
}

/**
* Function:	phy_RFSerialWrite
*
* OverView:	Write data to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			Offset,		The target address to be read
*			u4Byte			Data		The new register Data in the target bit position
*									of the target to be read
*
* Output:	None
* Return:		None
* Note:		Threre are three types of serial operations:
*			1. Software serial write
*			2. Hardware LSSI-Low Speed Serial Interface
*			3. Hardware HSSI-High speed
*			serial write. Driver need to implement (1) and (2).
*			This function is equal to the combination of RF_ReadReg() and  RFLSSIRead()
 *
 * Note:		  For RF8256 only
 *			 The total count of RTL8256(Zebra4) register is around 36 bit it only employs
 *			 4-bit RF address. RTL8256 uses "register mode control bit" (Reg00[12], Reg00[10])
 *			 to access register address bigger than 0xf. See "Appendix-4 in PHY Configuration
 *			 programming guide" for more details.
 *			 Thus, we define a sub-finction for RTL8526 register address conversion
 *		       ===========================================================
 *			 Register Mode		RegCTL[1]		RegCTL[0]		Note
 *								(Reg00[12])		(Reg00[10])
 *		       ===========================================================
 *			 Reg_Mode0				0				x			Reg 0 ~15(0x0 ~ 0xf)
 *		       ------------------------------------------------------------------
 *			 Reg_Mode1				1				0			Reg 16 ~30(0x1 ~ 0xf)
 *		       ------------------------------------------------------------------
 *			 Reg_Mode2				1				1			Reg 31 ~ 45(0x1 ~ 0xf)
 *		       ------------------------------------------------------------------
 *
 *	2008/09/02	MH	Add 92S RF definition
 *
 *
 *
*/
static	void
phy_RFSerialWrite(
		PADAPTER			Adapter,
		RF_RADIO_PATH_E	eRFPath,
		u32				Offset,
		u32				Data
	)
{
	u32						DataAndAddr = 0;
	struct hal_data_8188e				*pHalData = GET_HAL_DATA(Adapter);
	BB_REGISTER_DEFINITION_T	*pPhyReg = &pHalData->PHYRegDef[eRFPath];
	u32						NewOffset;


	/*  2009/06/17 MH We can not execute IO for power save or other accident mode. */

	Offset &= 0xff;

	/*  */
	/*  Switch page for 8256 RF IC */
	/*  */
	NewOffset = Offset;

	/*  */
	/*  Put write addr in [5:0]  and write data in [31:16] */
	/*  */
	DataAndAddr = ((NewOffset<<20) | (Data&0x000fffff)) & 0x0fffffff;	/*  T65 RF */

	/*  */
	/*  Write Operation */
	/*  */
	PHY_SetBBReg(Adapter, pPhyReg->rf3wireOffset, bMaskDWord, DataAndAddr);
}

/**
* Function:	PHY_QueryRFReg
*
* OverView:	Query "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			RegAddr,	The target address to be read
*			u4Byte			BitMask		The target bit position in the target address
*									to be read
*
* Output:	None
* Return:		u4Byte			Readback value
* Note:		This function is equal to "GetRFRegSetting" in PHY programming guide
*/
u32 rtl8188e_PHY_QueryRFReg(PADAPTER Adapter, RF_RADIO_PATH_E eRFPath,
			    u32 RegAddr, u32 BitMask)
{
	u32 Original_Value, Readback_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return 0;
#endif

	Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);

	BitShift =  phy_CalculateBitShift(BitMask);
	Readback_Value = (Original_Value & BitMask) >> BitShift;
	return (Readback_Value);
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			PADAPTER		Adapter,
*			RF_RADIO_PATH_E	eRFPath,	Radio path of A/B/C/D
*			u4Byte			RegAddr,	The target address to be modified
*			u4Byte			BitMask		The target bit position in the target address
*									to be modified
*			u4Byte			Data		The new register Data in the target bit position
*									of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void
rtl8188e_PHY_SetRFReg(
		PADAPTER			Adapter,
		RF_RADIO_PATH_E	eRFPath,
		u32				RegAddr,
		u32				BitMask,
		u32				Data
	)
{
	u32		Original_Value, BitShift;

#if (DISABLE_BB_RF == 1)
	return;
#endif

	/*  RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask)
	{
		Original_Value = phy_RFSerialRead(Adapter, eRFPath, RegAddr);
		BitShift =  phy_CalculateBitShift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data<< BitShift));
	}

	phy_RFSerialWrite(Adapter, eRFPath, RegAddr, Data);
}

/*  */
/*  3. Initial MAC/BB/RF config by reading MAC/BB/RF txt. */
/*  */

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigMACWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:	PADAPTER		Adapter
 *			ps1Byte				pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		The format of MACPHY_REG.txt is different from PHY and RF.
 *			[Register][Mask][Value]
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigMACWithParaFile(
		PADAPTER		Adapter,
		u8*			pFileName
)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	int		rtStatus = _FAIL;

	return rtStatus;
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_MACConfig8192C
 *
 * Overview:	Condig MAC by header file or parameter file.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 *  When		Who		Remark
 *  08/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
s32 PHY_MACConfig8188E(PADAPTER Adapter)
{
	int		rtStatus = _SUCCESS;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	s8			sz8188EMACRegFile[] = RTL8188E_PHY_MACREG;

	/*  */
	/*  Config MAC */
	/*  */
	if (HAL_STATUS_FAILURE == ODM_ConfigMACWithHeaderFile(&pHalData->odmpriv))
		rtStatus = _FAIL;

	/*  2010.07.13 AMPDU aggregation number B */
	rtw_write16(Adapter, REG_MAX_AGGR_NUM, MAX_AGGR_NUM);

	return rtStatus;
}

/**
* Function:	phy_InitBBRFRegisterDefinition
*
* OverView:	Initialize Register definition offset for Radio Path A/B/C/D
*
* Input:
*			PADAPTER		Adapter,
*
* Output:	None
* Return:		None
* Note:		The initialization value is constant and it should never be changes
*/
static	void
phy_InitBBRFRegisterDefinition(
		PADAPTER		Adapter
)
{
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);

	/*  RF Interface Sowrtware Control */
	pHalData->PHYRegDef[RF_PATH_A].rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 LSBs if read 32-bit from 0x870 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfs = rFPGA0_XAB_RFInterfaceSW; /*  16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */
	pHalData->PHYRegDef[RF_PATH_C].rfintfs = rFPGA0_XCD_RFInterfaceSW;/*  16 LSBs if read 32-bit from 0x874 */
	pHalData->PHYRegDef[RF_PATH_D].rfintfs = rFPGA0_XCD_RFInterfaceSW;/*  16 MSBs if read 32-bit from 0x874 (16-bit for 0x876) */

	/*  RF Interface Readback Value */
	pHalData->PHYRegDef[RF_PATH_A].rfintfi = rFPGA0_XAB_RFInterfaceRB; /*  16 LSBs if read 32-bit from 0x8E0 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfi = rFPGA0_XAB_RFInterfaceRB;/*  16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2) */
	pHalData->PHYRegDef[RF_PATH_C].rfintfi = rFPGA0_XCD_RFInterfaceRB;/*  16 LSBs if read 32-bit from 0x8E4 */
	pHalData->PHYRegDef[RF_PATH_D].rfintfi = rFPGA0_XCD_RFInterfaceRB;/*  16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6) */

	/*  RF Interface Output (and Enable) */
	pHalData->PHYRegDef[RF_PATH_A].rfintfo = rFPGA0_XA_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x860 */
	pHalData->PHYRegDef[RF_PATH_B].rfintfo = rFPGA0_XB_RFInterfaceOE; /*  16 LSBs if read 32-bit from 0x864 */

	/*  RF Interface (Output and)  Enable */
	pHalData->PHYRegDef[RF_PATH_A].rfintfe = rFPGA0_XA_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	pHalData->PHYRegDef[RF_PATH_B].rfintfe = rFPGA0_XB_RFInterfaceOE; /*  16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */

	/* Addr of LSSI. Wirte RF register by driver */
	pHalData->PHYRegDef[RF_PATH_A].rf3wireOffset = rFPGA0_XA_LSSIParameter; /* LSSI Parameter */
	pHalData->PHYRegDef[RF_PATH_B].rf3wireOffset = rFPGA0_XB_LSSIParameter;

	/*  RF parameter */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSI_Select = rFPGA0_XAB_RFParameter;  /* BB Band Select */
	pHalData->PHYRegDef[RF_PATH_B].rfLSSI_Select = rFPGA0_XAB_RFParameter;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSI_Select = rFPGA0_XCD_RFParameter;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSI_Select = rFPGA0_XCD_RFParameter;

	/*  Tx AGC Gain Stage (same for all path. Should we remove this?) */
	pHalData->PHYRegDef[RF_PATH_A].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */
	pHalData->PHYRegDef[RF_PATH_B].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */
	pHalData->PHYRegDef[RF_PATH_C].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */
	pHalData->PHYRegDef[RF_PATH_D].rfTxGainStage = rFPGA0_TxGainStage; /* Tx gain stage */

	/*  Tranceiver A~D HSSI Parameter-1 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara1 = rFPGA0_XA_HSSIParameter1;  /* wire control parameter1 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara1 = rFPGA0_XB_HSSIParameter1;  /* wire control parameter1 */

	/*  Tranceiver A~D HSSI Parameter-2 */
	pHalData->PHYRegDef[RF_PATH_A].rfHSSIPara2 = rFPGA0_XA_HSSIParameter2;  /* wire control parameter2 */
	pHalData->PHYRegDef[RF_PATH_B].rfHSSIPara2 = rFPGA0_XB_HSSIParameter2;  /* wire control parameter2 */

	/*  RF switch Control */
	pHalData->PHYRegDef[RF_PATH_A].rfSwitchControl = rFPGA0_XAB_SwitchControl; /* TR/Ant switch control */
	pHalData->PHYRegDef[RF_PATH_B].rfSwitchControl = rFPGA0_XAB_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_C].rfSwitchControl = rFPGA0_XCD_SwitchControl;
	pHalData->PHYRegDef[RF_PATH_D].rfSwitchControl = rFPGA0_XCD_SwitchControl;

	/*  AGC control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl1 = rOFDM0_XAAGCCore1;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl1 = rOFDM0_XBAGCCore1;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl1 = rOFDM0_XCAGCCore1;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl1 = rOFDM0_XDAGCCore1;

	/*  AGC control 2 */
	pHalData->PHYRegDef[RF_PATH_A].rfAGCControl2 = rOFDM0_XAAGCCore2;
	pHalData->PHYRegDef[RF_PATH_B].rfAGCControl2 = rOFDM0_XBAGCCore2;
	pHalData->PHYRegDef[RF_PATH_C].rfAGCControl2 = rOFDM0_XCAGCCore2;
	pHalData->PHYRegDef[RF_PATH_D].rfAGCControl2 = rOFDM0_XDAGCCore2;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfRxIQImbalance = rOFDM0_XARxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfRxIQImbalance = rOFDM0_XBRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfRxIQImbalance = rOFDM0_XCRxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfRxIQImbalance = rOFDM0_XDRxIQImbalance;

	/*  RX AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfRxAFE = rOFDM0_XARxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfRxAFE = rOFDM0_XBRxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfRxAFE = rOFDM0_XCRxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfRxAFE = rOFDM0_XDRxAFE;

	/*  Tx AFE control 1 */
	pHalData->PHYRegDef[RF_PATH_A].rfTxIQImbalance = rOFDM0_XATxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_B].rfTxIQImbalance = rOFDM0_XBTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_C].rfTxIQImbalance = rOFDM0_XCTxIQImbalance;
	pHalData->PHYRegDef[RF_PATH_D].rfTxIQImbalance = rOFDM0_XDTxIQImbalance;

	/*  Tx AFE control 2 */
	pHalData->PHYRegDef[RF_PATH_A].rfTxAFE = rOFDM0_XATxAFE;
	pHalData->PHYRegDef[RF_PATH_B].rfTxAFE = rOFDM0_XBTxAFE;
	pHalData->PHYRegDef[RF_PATH_C].rfTxAFE = rOFDM0_XCTxAFE;
	pHalData->PHYRegDef[RF_PATH_D].rfTxAFE = rOFDM0_XDTxAFE;

	/*  Tranceiver LSSI Readback SI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBack = rFPGA0_XA_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBack = rFPGA0_XB_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_C].rfLSSIReadBack = rFPGA0_XC_LSSIReadBack;
	pHalData->PHYRegDef[RF_PATH_D].rfLSSIReadBack = rFPGA0_XD_LSSIReadBack;

	/*  Tranceiver LSSI Readback PI mode */
	pHalData->PHYRegDef[RF_PATH_A].rfLSSIReadBackPi = TransceiverA_HSPI_Readback;
	pHalData->PHYRegDef[RF_PATH_B].rfLSSIReadBackPi = TransceiverB_HSPI_Readback;
}

/*-----------------------------------------------------------------------------
 * Function:    phy_ConfigBBWithParaFile()
 *
 * Overview:    This function read BB parameters from general file format, and do register
 *			  Read/Write
 *
 * Input:	PADAPTER		Adapter
 *			ps1Byte				pFileName
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *	2008/11/06	MH	For 92S we do not support silent reset now. Disable
 *					parameter file compare!!!!!!??
 *
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithParaFile(
		PADAPTER		Adapter,
		u8*			pFileName
)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	int		rtStatus = _SUCCESS;

	return rtStatus;
}



/*  */
/*  The following is for High Power PA */
/*  */
static void phy_ConfigBBExternalPA(PADAPTER Adapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u16 i=0;
	u32 temp=0;

	if (!pHalData->ExternalPA)
		return;

	/*  2010/10/19 MH According to Jenyu/EEChou 's opinion, we need not to execute the */
	/*  same code as SU. It is already updated in PHY_REG_1T_HP.txt. */
}

void
storePwrIndexDiffRateOffset(
		PADAPTER	Adapter,
		u32		RegAddr,
		u32		BitMask,
		u32		Data
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	if (RegAddr == rTxAGC_A_Rate18_06)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0] = Data;
	if (RegAddr == rTxAGC_A_Rate54_24)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][1] = Data;
	if (RegAddr == rTxAGC_A_CCK1_Mcs32)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][6] = Data;
	if (RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0xffffff00)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][7] = Data;
	if (RegAddr == rTxAGC_A_Mcs03_Mcs00)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][2] = Data;
	if (RegAddr == rTxAGC_A_Mcs07_Mcs04)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][3] = Data;
	if (RegAddr == rTxAGC_A_Mcs11_Mcs08)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][4] = Data;
	if (RegAddr == rTxAGC_A_Mcs15_Mcs12) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][5] = Data;
		if (pHalData->rf_type== RF_1T1R)
			pHalData->pwrGroupCnt++;
	}
	if (RegAddr == rTxAGC_B_Rate18_06)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][8] = Data;
	if (RegAddr == rTxAGC_B_Rate54_24)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][9] = Data;
	if (RegAddr == rTxAGC_B_CCK1_55_Mcs32)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][14] = Data;
	if (RegAddr == rTxAGC_B_CCK11_A_CCK2_11 && BitMask == 0x000000ff)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][15] = Data;
	if (RegAddr == rTxAGC_B_Mcs03_Mcs00)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][10] = Data;
	if (RegAddr == rTxAGC_B_Mcs07_Mcs04)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][11] = Data;
	if (RegAddr == rTxAGC_B_Mcs11_Mcs08)
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][12] = Data;
	if (RegAddr == rTxAGC_B_Mcs15_Mcs12) {
		pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][13] = Data;
		if (pHalData->rf_type != RF_1T1R)
			pHalData->pwrGroupCnt++;
	}
}
/*-----------------------------------------------------------------------------
 * Function:	phy_ConfigBBWithPgParaFile
 *
 * Overview:
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/06/2008	MHC		Create Version 0.
 * 2009/07/29	tynli		(porting from 92SE branch)2009/03/11 Add copy parameter file to buffer for silent reset
 *---------------------------------------------------------------------------*/
static	int
phy_ConfigBBWithPgParaFile(
		PADAPTER		Adapter,
		u8*			pFileName)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	int		rtStatus = _SUCCESS;


	return rtStatus;

}	/* phy_ConfigBBWithPgParaFile */

static void
phy_BB8192C_Config_1T(
	PADAPTER Adapter
	)
{
	/* for path - B */
	PHY_SetBBReg(Adapter, rFPGA0_TxInfo, 0x3, 0x2);
	PHY_SetBBReg(Adapter, rFPGA1_TxInfo, 0x300033, 0x200022);

	/*  20100519 Joseph: Add for 1T2R config. Suggested by Kevin, Jenyu and Yunan. */
	PHY_SetBBReg(Adapter, rCCK0_AFESetting, bMaskByte3, 0x45);
	PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x23);
	PHY_SetBBReg(Adapter, rOFDM0_AGCParameter1, 0x30, 0x1);	/*  B path first AGC */

	PHY_SetBBReg(Adapter, 0xe74, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe78, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe7c, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe80, 0x0c000000, 0x2);
	PHY_SetBBReg(Adapter, 0xe88, 0x0c000000, 0x2);


}

/*  Joseph test: new initialize order!! */
/*  Test only!! This part need to be re-organized. */
/*  Now it is just for 8256. */
static	int
phy_BB8190_Config_HardCode(
		PADAPTER	Adapter
	)
{
	return _SUCCESS;
}

static	int
phy_BB8188E_Config_ParaFile(
		PADAPTER	Adapter
	)
{
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);
	int			rtStatus = _SUCCESS;

	u8	sz8188EBBRegFile[] = RTL8188E_PHY_REG;
	u8	sz8188EAGCTableFile[] = RTL8188E_AGC_TAB;
	u8	sz8188EBBRegPgFile[] = RTL8188E_PHY_REG_PG;
	u8	sz8188EBBRegMpFile[] = RTL8188E_PHY_REG_MP;

	/*  */
	/*  1. Read PHY_REG.TXT BB INIT!! */
	/*  We will seperate as 88C / 92C according to chip version */
	/*  */
	if (HAL_STATUS_FAILURE ==ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG))
		rtStatus = _FAIL;
	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;

	/*  */
	/*  2. If EEPROM or EFUSE autoload OK, We must config by PHY_REG_PG.txt */
	/*  */
	if (pEEPROM->bautoload_fail_flag == false)
	{
		pHalData->pwrGroupCnt = 0;

		if (HAL_STATUS_FAILURE ==ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv, CONFIG_BB_PHY_REG_PG))
			rtStatus = _FAIL;
	}

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;

	/*  */
	/*  3. BB AGC table Initialization */
	/*  */
	if (HAL_STATUS_FAILURE ==ODM_ConfigBBWithHeaderFile(&pHalData->odmpriv,  CONFIG_BB_AGC_TAB))
		rtStatus = _FAIL;

	if (rtStatus != _SUCCESS)
		goto phy_BB8190_Config_ParaFile_Fail;

phy_BB8190_Config_ParaFile_Fail:

	return rtStatus;
}

int
PHY_BBConfig8188E(
		PADAPTER	Adapter
	)
{
	int	rtStatus = _SUCCESS;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u32	RegVal;
	u8	TmpU1B=0;
	u8	value8,CrystalCap;

	phy_InitBBRFRegisterDefinition(Adapter);


	/*  Enable BB and RF */
	RegVal = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, (u16)(RegVal|BIT13|BIT0|BIT1));

	/*  20090923 Joseph: Advised by Steven and Jenyu. Power sequence before init RF. */

	rtw_write8(Adapter, REG_RF_CTRL, RF_EN|RF_RSTB|RF_SDMRSTB);

	rtw_write8(Adapter, REG_SYS_FUNC_EN, FEN_USBA | FEN_USBD | FEN_BB_GLB_RSTn | FEN_BBRSTB);

	/*  */
	/*  Config BB and AGC */
	/*  */
	rtStatus = phy_BB8188E_Config_ParaFile(Adapter);

	/*  write 0x24[16:11] = 0x24[22:17] = CrystalCap */
	CrystalCap = pHalData->CrystalCap & 0x3F;
	PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, 0x7ff800, (CrystalCap | (CrystalCap << 6)));

	return rtStatus;
}

int
PHY_RFConfig8188E(
		PADAPTER	Adapter
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;

	/*  */
	/*  RF config */
	/*  */
	rtStatus = PHY_RF6052_Config8188E(Adapter);
	return rtStatus;
}


/*-----------------------------------------------------------------------------
 * Function:    PHY_ConfigRFWithParaFile()
 *
 * Overview:    This function read RF parameters from general file format, and do RF 3-wire
 *
 * Input:	PADAPTER			Adapter
 *			ps1Byte					pFileName
 *			RF_RADIO_PATH_E	eRFPath
 *
 * Output:      NONE
 *
 * Return:      RT_STATUS_SUCCESS: configuration file exist
 *
 * Note:		Delay may be required for RF configuration
 *---------------------------------------------------------------------------*/
int
rtl8188e_PHY_ConfigRFWithParaFile(
		PADAPTER			Adapter,
		u8*				pFileName,
	RF_RADIO_PATH_E		eRFPath
)
{
	return _SUCCESS;
}

static int PHY_ConfigRFExternalPA(PADAPTER Adapter, RF_RADIO_PATH_E eRFPath)
{
	int	rtStatus = _SUCCESS;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u16 i=0;

	if (!pHalData->ExternalPA)
		return rtStatus;

	/*  2010/10/19 MH According to Jenyu/EEChou 's opinion, we need not to execute the */
	/*  same code as SU. It is already updated in radio_a_1T_HP.txt. */
	return rtStatus;
}

void
rtl8192c_PHY_GetHWRegOriginalValue(
		PADAPTER		Adapter
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	/*  read rx initial gain */
	pHalData->DefaultInitialGain[0] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XAAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[1] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XBAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[2] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XCAGCCore1, bMaskByte0);
	pHalData->DefaultInitialGain[3] = (u8)PHY_QueryBBReg(Adapter, rOFDM0_XDAGCCore1, bMaskByte0);

	/*  read framesync */
	pHalData->framesync = (u8)PHY_QueryBBReg(Adapter, rOFDM0_RxDetector3, bMaskByte0);
	pHalData->framesyncC34 = PHY_QueryBBReg(Adapter, rOFDM0_RxDetector2, bMaskDWord);
}

/*  */
/* 	Description: */
/* 		Map dBm into Tx power index according to */
/* 		current HW model, for example, RF and PA, and */
/* 		current wireless mode. */
/* 	By Bruce, 2008-01-29. */
/*  */
static	u8
phy_DbmToTxPwrIdx(
		PADAPTER		Adapter,
		WIRELESS_MODE	WirelessMode,
		int			PowerInDbm
	)
{
	u8				TxPwrIdx = 0;
	int				Offset = 0;


	/*  */
	/*  Tested by MP, we found that CCK Index 0 equals to 8dbm, OFDM legacy equals to */
	/*  3dbm, and OFDM HT equals to 0dbm repectively. */
	/*  Note: */
	/* 	The mapping may be different by different NICs. Do not use this formula for what needs accurate result. */
	/*  By Bruce, 2008-01-29. */
	/*  */
	switch (WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
		break;
	default:
		Offset = -8;
		break;
	}

	if ((PowerInDbm - Offset) > 0)
		TxPwrIdx = (u8)((PowerInDbm - Offset) * 2);
	else
		TxPwrIdx = 0;

	/*  Tx Power Index is too large. */
	if (TxPwrIdx > MAX_TXPWR_IDX_NMODE_92S)
		TxPwrIdx = MAX_TXPWR_IDX_NMODE_92S;

	return TxPwrIdx;
}

/*  */
/* 	Description: */
/* 		Map Tx power index into dBm according to */
/* 		current HW model, for example, RF and PA, and */
/* 		current wireless mode. */
/* 	By Bruce, 2008-01-29. */
/*  */
static int phy_TxPwrIdxToDbm(PADAPTER Adapter, WIRELESS_MODE WirelessMode, u8 TxPwrIdx)
{
	int				Offset = 0;
	int				PwrOutDbm = 0;

	/*  */
	/*  Tested by MP, we found that CCK Index 0 equals to -7dbm, OFDM legacy equals to -8dbm. */
	/*  Note: */
	/* 	The mapping may be different by different NICs. Do not use this formula for what needs accurate result. */
	/*  By Bruce, 2008-01-29. */
	/*  */
	switch (WirelessMode)
	{
	case WIRELESS_MODE_B:
		Offset = -7;
		break;

	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		Offset = -8;
	default:
		Offset = -8;
		break;
	}

	PwrOutDbm = TxPwrIdx / 2 + Offset; /*  Discard the decimal part. */

	return PwrOutDbm;
}


/*-----------------------------------------------------------------------------
 * Function:    GetTxPowerLevel8190()
 *
 * Overview:    This function is export to "common" moudule
 *
 * Input:       PADAPTER		Adapter
 *			psByte			Power Level
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 *---------------------------------------------------------------------------*/
void
PHY_GetTxPowerLevel8188E(
		PADAPTER		Adapter,
	u32*		powerlevel
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u8			TxPwrLevel = 0;
	int			TxPwrDbm;

	/*  */
	/*  Because the Tx power indexes are different, we report the maximum of them to */
	/*  meet the CCX TPC request. By Bruce, 2008-01-31. */
	/*  */

	/*  CCK */
	TxPwrLevel = pHalData->CurrentCckTxPwrIdx;
	TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_B, TxPwrLevel);

	/*  Legacy OFDM */
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx + pHalData->LegacyHTTxPowerDiff;

	/*  Compare with Legacy OFDM Tx power. */
	if (phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_G, TxPwrLevel);

	/*  HT OFDM */
	TxPwrLevel = pHalData->CurrentOfdm24GTxPwrIdx;

	/*  Compare with HT OFDM Tx power. */
	if (phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel) > TxPwrDbm)
		TxPwrDbm = phy_TxPwrIdxToDbm(Adapter, WIRELESS_MODE_N_24G, TxPwrLevel);

	*powerlevel = TxPwrDbm;
}

static void getTxPowerIndex88E(PADAPTER Adapter, u8 channel, u8 *cckPowerLevel,
			       u8 *ofdmPowerLevel, u8 *BW20PowerLevel,
			       u8 *BW40PowerLevel)
{
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);
	u8				index = (channel -1);
	u8				TxCount=0,path_nums;

	if ((RF_1T2R == pHalData->rf_type) || (RF_1T1R ==pHalData->rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	for (TxCount=0; TxCount < path_nums; TxCount++) {
		if (TxCount==RF_PATH_A) {
			/*  1. CCK */
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->OFDM_24G_Diff[TxCount][RF_PATH_A];
			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[TxCount][RF_PATH_A];
			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount==RF_PATH_B) {
			/*  1. CCK */
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[TxCount][RF_PATH_A]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount==RF_PATH_C) {
			/*  1. CCK */
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount==RF_PATH_D) {
			/*  1. CCK */
			cckPowerLevel[TxCount]		= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]		= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_B][index]+
				pHalData->BW20_24G_Diff[RF_PATH_C][index]+
				pHalData->BW20_24G_Diff[TxCount][index];

			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[RF_PATH_B][index]+
				pHalData->BW20_24G_Diff[RF_PATH_C][index]+
				pHalData->BW20_24G_Diff[TxCount][index];

			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		}
	}
}

static void phy_PowerIndexCheck88E(PADAPTER Adapter, u8 channel, u8 *cckPowerLevel,
				   u8 *ofdmPowerLevel, u8 *BW20PowerLevel, u8 *BW40PowerLevel)
{
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);

	pHalData->CurrentCckTxPwrIdx = cckPowerLevel[0];
	pHalData->CurrentOfdm24GTxPwrIdx = ofdmPowerLevel[0];
	pHalData->CurrentBW2024GTxPwrIdx = BW20PowerLevel[0];
	pHalData->CurrentBW4024GTxPwrIdx = BW40PowerLevel[0];
}

/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       PADAPTER		Adapter
 *			u1Byte		channel
 *
 * Output:      NONE
 *
 * Return:      NONE
 *	2008/11/04	MHC		We remove EEPROM_93C56.
 *						We need to move CCX relative code to independet file.
 *	2009/01/21	MHC		Support new EEPROM format from SD3 requirement.
 *
 *---------------------------------------------------------------------------*/
void
PHY_SetTxPowerLevel8188E(
		PADAPTER		Adapter,
		u8				channel
	)
{
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);

	u8	cckPowerLevel[MAX_TX_COUNT], ofdmPowerLevel[MAX_TX_COUNT];/*  [0]:RF-A, [1]:RF-B */
	u8	BW20PowerLevel[MAX_TX_COUNT], BW40PowerLevel[MAX_TX_COUNT];
	u8	i=0;

	getTxPowerIndex88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0],&BW20PowerLevel[0],&BW40PowerLevel[0]);

	phy_PowerIndexCheck88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0],&BW20PowerLevel[0],&BW40PowerLevel[0]);

	rtl8188e_PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
	rtl8188e_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0],&BW20PowerLevel[0],&BW40PowerLevel[0], channel);
}

/*  */
/* 	Description: */
/* 		Update transmit power level of all channel supported. */
/*  */
/* 	TODO: */
/* 		A mode. */
/* 	By Bruce, 2008-02-04. */
/*  */
bool
PHY_UpdateTxPowerDbm8188E(
		PADAPTER	Adapter,
		int		powerInDbm
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u8				idx;
	u8			rf_path;

	/*  TODO: A mode Tx power. */
	u8	CckTxPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_B, powerInDbm);
	u8	OfdmTxPwrIdx = phy_DbmToTxPwrIdx(Adapter, WIRELESS_MODE_N_24G, powerInDbm);

	if (OfdmTxPwrIdx - pHalData->LegacyHTTxPowerDiff > 0)
		OfdmTxPwrIdx -= pHalData->LegacyHTTxPowerDiff;
	else
		OfdmTxPwrIdx = 0;

	for (idx = 0; idx < 14; idx++)
	{
		for (rf_path = 0; rf_path < 2; rf_path++)
		{
			pHalData->TxPwrLevelCck[rf_path][idx] = CckTxPwrIdx;
			pHalData->TxPwrLevelHT40_1S[rf_path][idx] =
			pHalData->TxPwrLevelHT40_2S[rf_path][idx] = OfdmTxPwrIdx;
		}
	}
	return true;
}

void
PHY_ScanOperationBackup8188E(
		PADAPTER	Adapter,
		u8		Operation
	)
{
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWModeCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode
 *
 * Input:		PRT_TIMER		pTimer
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		(1) We do not take j mode into consideration now
 *			(2) Will two workitem of "switch channel" and "switch channel bandwidth" run
 *			     concurrently?
 *---------------------------------------------------------------------------*/
static void
_PHY_SetBWMode92C(
		PADAPTER	Adapter
)
{
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);
	u8				regBwOpMode;
	u8				regRRSR_RSC;

	if (pHalData->rf_chip == RF_PSEUDO_11N)
		return;

	/*  There is no 40MHz mode in RF_8225. */
	if (pHalData->rf_chip==RF_8225)
		return;

	if (Adapter->bDriverStopped)
		return;

	/* 3 */
	/* 3<1>Set MAC register */
	/* 3 */

	regBwOpMode = rtw_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = rtw_read8(Adapter, REG_RRSR+2);

	switch (pHalData->CurrentChannelBW)
	{
		case HT_CHANNEL_WIDTH_20:
			regBwOpMode |= BW_OPMODE_20MHZ;
			   /*  2007/02/07 Mark by Emily becasue we have not verify whether this register works */
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);
			break;

		case HT_CHANNEL_WIDTH_40:
			regBwOpMode &= ~BW_OPMODE_20MHZ;
				/*  2007/02/07 Mark by Emily becasue we have not verify whether this register works */
			rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);

			regRRSR_RSC = (regRRSR_RSC&0x90) |(pHalData->nCur40MhzPrimeSC<<5);
			rtw_write8(Adapter, REG_RRSR+2, regRRSR_RSC);
			break;

		default:
			break;
	}

	/* 3  */
	/* 3 <2>Set PHY related register */
	/* 3 */
	switch (pHalData->CurrentChannelBW)
	{
		/* 20 MHz channel*/
		case HT_CHANNEL_WIDTH_20:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
			/* PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter2, BIT10, 1); */

			break;


		/* 40 MHz channel*/
		case HT_CHANNEL_WIDTH_40:
			PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
			PHY_SetBBReg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);

			/*  Set Control channel to upper or lower. These settings are required only for 40MHz */
			PHY_SetBBReg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
			PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);

			PHY_SetBBReg(Adapter, 0x818, (BIT26|BIT27), (pHalData->nCur40MhzPrimeSC==HAL_PRIME_CHNL_OFFSET_LOWER)?2:1);

			break;
		default:
			break;

	}
	/* Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315 */

	/* 3<3>Set RF related register */
	switch (pHalData->rf_chip)
	{
		case RF_8225:
			break;
		case RF_8256:
			/*  Please implement this function in Hal8190PciPhy8256.c */
			break;
		case RF_8258:
			/*  Please implement this function in Hal8190PciPhy8258.c */
			break;
		case RF_PSEUDO_11N:
			break;
		case RF_6052:
			rtl8188e_PHY_RF6052SetBandwidth(Adapter, pHalData->CurrentChannelBW);
			break;
		default:
			break;
	}
}

 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:		PADAPTER			Adapter
 *			HT_CHANNEL_WIDTH	Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
void
PHY_SetBWMode8188E(
		PADAPTER					Adapter,
		HT_CHANNEL_WIDTH	Bandwidth,	/*  20M or 40M */
		unsigned char	Offset		/*  Upper, Lower, or Don't care */
)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	HT_CHANNEL_WIDTH	tmpBW= pHalData->CurrentChannelBW;

	pHalData->CurrentChannelBW = Bandwidth;

	pHalData->nCur40MhzPrimeSC = Offset;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
		_PHY_SetBWMode92C(Adapter);
	else
		pHalData->CurrentChannelBW = tmpBW;
}

static void _PHY_SwChnl8192C(PADAPTER Adapter, u8 channel)
{
	u8 eRFPath;
	u32 param1, param2;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	if ( Adapter->bNotifyChannelChange )
	{
		DBG_88E( "[%s] ch = %d\n", __func__, channel );
	}

	/* s1. pre common command - CmdID_SetTxPowerLevel */
	PHY_SetTxPowerLevel8188E(Adapter, channel);

	/* s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel */
	param1 = RF_CHNLBW;
	param2 = channel;
	for (eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++) {
		pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | param2);
		PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
	}
}

void
PHY_SwChnl8188E(	/*  Call after initialization */
		PADAPTER	Adapter,
		u8		channel
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u8	tmpchannel = pHalData->CurrentChannel;
	bool  bResult = true;

	if (pHalData->rf_chip == RF_PSEUDO_11N)
		return;									/* return immediately if it is peudo-phy */

	/*  */
	switch (pHalData->CurrentWirelessMode) {
	case WIRELESS_MODE_A:
	case WIRELESS_MODE_N_5G:
		break;
	case WIRELESS_MODE_B:
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		break;
	default:
		break;
	}
	/*  */

	if (channel == 0)
		channel = 1;

	pHalData->CurrentChannel=channel;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved)) {
		_PHY_SwChnl8192C(Adapter, channel);

		if (bResult) {
			;
		} else {
			pHalData->CurrentChannel = tmpchannel;
		}

	}
	else
	{
		pHalData->CurrentChannel = tmpchannel;
	}
}

static	bool
phy_SwChnlStepByStep(
		PADAPTER	Adapter,
		u8		channel,
		u8		*stage,
		u8		*step,
	u32		*delay
	)
{
	return true;
}


static	bool
phy_SetSwChnlCmdArray(
	SwChnlCmd*		CmdTable,
	u32			CmdTableIdx,
	u32			CmdTableSz,
	SwChnlCmdID		CmdID,
	u32			Para1,
	u32			Para2,
	u32			msDelay
	)
{
	SwChnlCmd* pCmd;

	if (CmdTable == NULL)
		return false;
	if (CmdTableIdx >= CmdTableSz)
		return false;

	pCmd = CmdTable + CmdTableIdx;
	pCmd->CmdID = CmdID;
	pCmd->Para1 = Para1;
	pCmd->Para2 = Para2;
	pCmd->msDelay = msDelay;

	return true;
}

static	void
phy_FinishSwChnlNow(	/*  We should not call this function directly */
			PADAPTER	Adapter,
			u8		channel
		)
{
}

/*  */
/*  Description: */
/* 	Switch channel synchronously. Called by SwChnlByDelayHandler. */
/*  */
/*  Implemented by Bruce, 2008-02-14. */
/*  The following procedure is operted according to SwChanlCallback8190Pci(). */
/*  However, this procedure is performed synchronously  which should be running under */
/*  passive level. */
/*  */
void
PHY_SwChnlPhy8192C(	/*  Only called during initialize */
		PADAPTER	Adapter,
		u8		channel
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	/* return immediately if it is peudo-phy */
	if (pHalData->rf_chip == RF_PSEUDO_11N)
		return;

	if ( channel == 0)
		channel = 1;

	pHalData->CurrentChannel=channel;

	phy_FinishSwChnlNow(Adapter,channel);
}

/*  */
/* 	Description: */
/* 		Configure H/W functionality to enable/disable Monitor mode. */
/* 		Note, because we possibly need to configure BB and RF in this function, */
/* 		so caller should in PASSIVE_LEVEL. 080118, by rcnjko. */
/*  */
void
PHY_SetMonitorMode8192C(
		PADAPTER			pAdapter,
		bool				bEnableMonitorMode
	)
{
}


/*-----------------------------------------------------------------------------
 * Function:	PHYCheckIsLegalRfPath8190Pci()
 *
 * Overview:	Check different RF type to execute legal judgement. If RF Path is illegal
 *			We will return false.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	11/15/2007	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
bool PHY_CheckIsLegalRfPath8192C(PADAPTER pAdapter, u32	eRFPath)
{
	return	true;
}	/* PHY_CheckIsLegalRfPath8192C */

static void _PHY_SetRFPathSwitch(PADAPTER pAdapter, bool bMain, bool is2T)
{
	u8	u1bTmp;

	if (!pAdapter->hw_init_completed) {
		u1bTmp = rtw_read8(pAdapter, REG_LEDCFG2) | BIT7;
		rtw_write8(pAdapter, REG_LEDCFG2, u1bTmp);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}
	if (is2T) {
		if (bMain)
			PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x1);	/* 92C_Path_A */
		else
			PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x2);	/* BT */
	} else {
		if (bMain)
			PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x2);	/* Main */
		else
			PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300, 0x1);	/* Aux */
	}
}

static bool _PHY_QueryRFPathSwitch(PADAPTER pAdapter, bool is2T)
{
	if (!pAdapter->hw_init_completed) {
		PHY_SetBBReg(pAdapter, REG_LEDCFG0, BIT23, 0x01);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if (is2T) {
		if (PHY_QueryBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6) == 0x01)
			return true;
		else
			return false;
	} else {
		if (PHY_QueryBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, 0x300) == 0x02)
			return true;
		else
			return false;
	}
}

static void _PHY_DumpRFReg(PADAPTER pAdapter)
{
}
