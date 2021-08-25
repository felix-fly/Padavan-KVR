/****************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ****************************************************************************
 
    Module Name:
    vr_ikans.c
 
    Abstract:
    Only for IKANOS Vx160 or Vx180 platform.

	The fast path will check IP address/ IP port, etc. NOT only check MAC.
 
    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    Sample Lin	01-28-2008    Created

 */

#define MODULE_IKANOS

#include "rt_config.h"
#include <linux/version.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <netpro/apprehdr.h>


#ifdef IKANOS_VX_1X0

#define IKANOS_PERAP_ID		7 /* IKANOS Fix Peripheral ID */
#define K0_TO_K1(x)			((unsigned)(x)|0xA0000000) /* kseg0 to kseg1 */
//#define IKANOS_DEBUG


extern INT rt28xx_send_packets(
	IN struct sk_buff		*skb_p,
	IN struct net_device	*net_dev);

static INT32 IKANOS_WlanDataFramesTx(
	IN void					*_pAdBuf,
	IN struct net_device	*pNetDev);

static void IKANOS_WlanPktFromAp(
	IN apPreHeader_t 		*pFrame);

static INT32 GetSpecInfoIdxFromBssid(
	IN PRTMP_ADAPTER pAd,
	IN INT32 FromWhichBSSID);




/* --------------------------------- Public -------------------------------- */

/*
========================================================================
Routine Description:
	Init IKANOS fast path function.

Arguments:
	pApMac			- the MAC of AP

Return Value:
	None

Note:
	If you want to enable RX fast path, you must call the function.
========================================================================
*/
void VR_IKANOS_FP_Init(
	IN UINT8 BssNum,
	IN UINT8 *pApMac)
{
	UINT32 i;
	UINT8 mac[6];


	memcpy(mac, pApMac, 6);

	/* add all MAC of multiple BSS */
	for(i=0; i<BssNum; i++)
	{
		apMacAddrConfig(7, mac, 0xAD);
		mac[5] ++;
	} /* End of for */
} /* End of VR_IKANOS_FP_Init */


/*
========================================================================
Routine Description:
	Ikanos LAN --> WLAN transmit fast path function.

Arguments:
	skb				- the transmitted packet (SKB packet format)
	netdev			- our WLAN network device

Return Value:
	

Note:
========================================================================
*/
INT32 IKANOS_DataFramesTx(
	IN struct sk_buff		*pSkb,
	IN struct net_device	*pNetDev)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pNetDev->priv;
	IkanosWlanTxCbFuncP *fp = &IKANOS_WlanDataFramesTx;

	pSkb->apFlowData.txDev = pNetDev;
	pSkb->apFlowData.txApId = IKANOS_PERAP_ID;
	pAd->IkanosTxInfo.netdev = pNetDev;
	pAd->IkanosTxInfo.fp = fp;
	pSkb->apFlowData.txHandle = &(pAd->IkanosTxInfo);
	ap2apFlowProcess(pSkb, pNetDev);

#ifdef IKANOS_DEBUG
	printk("ikanos> tx no fp\n"); // debug use
#endif // IKANOS_DEBUG //

	return rt28xx_send_packets(pSkb, pNetDev);
} /* End of IKANOS_DataFramesTx */


/*
========================================================================
Routine Description:
	Ikanos WLAN --> LAN transmit fast path function.

Arguments:
	pAd				- WLAN control block
	pRxParam		-
	pSkb			- the transmitted packet (SKB packet format)
	Length			- packet length

Return Value:
	None

Note:
========================================================================
*/
/* Note: because no unsigned long private parameters in apPreHeader_t can be used,
	we use a global variable to record pAd.
	So we can not use multiple card function in Ikanos platform. */
PRTMP_ADAPTER	pIkanosAd;

void IKANOS_DataFrameRx(
	IN PRTMP_ADAPTER	pAd,
	IN void				*pRxParam,
	IN struct sk_buff	*pSkb,
	IN UINT32			Length)
{
    apPreHeader_t *apBuf;


    apBuf = (apPreHeader_t *)(translateMbuf2Apbuf(pSkb, 0));

    apBuf->flags1 = 1 << AP_FLAG1_IS_ETH_BIT;
    apBuf->specInfoElement = RTMP_GET_PACKET_NET_DEVICE_MBSSID(pSkb); // MBSS
	pIkanosAd = pAd;

//  apBuf->egressList[0].pEgress = NULL;
//  apBuf->egressList[0].pFlowID = NULL;
    apBuf->flags2 = 0;

    apClassify(IKANOS_PERAP_ID, apBuf, (void *)IKANOS_WlanPktFromAp);
    dev_kfree_skb(pSkb); 
} /* End of IKANOS_DataFrameRx */




/* --------------------------------- Private -------------------------------- */

/*
========================================================================
Routine Description:
	Ikanos LAN --> WLAN transmit fast path function.

Arguments:
	_pAdBuf			- the transmitted packet (Ikanos packet format)
	netdev			- our WLAN network device

Return Value:
	

Note:
========================================================================
*/
static INT32 IKANOS_WlanDataFramesTx(
	IN void					*_pAdBuf,
	IN struct net_device	*pNetDev)
{
	apPreHeader_t *pApBuf = (apPreHeader_t *)_pAdBuf;
	struct sk_buff *sk = NULL;

	sk = (struct sk_buff *)translateApbuf2Mbuf(pApBuf);
	if (sk == NULL)
	{
		printk("ikanos> translateApbuf2Mbuf returned NULL!\n");
		return 1;
	} /* End of if */

	sk->apFlowData.flags2 = 0;
	sk->apFlowData.wlanFlags = 0;
	sk->protocol = ETH_P_IP;
	sk->dev = pNetDev;
	sk->priority = 0;

	return rt28xx_send_packets(sk, pNetDev);
} /* End of IKANOS_WlanDataFramesTx */


static INT32 GetSpecInfoIdxFromBssid(
	IN PRTMP_ADAPTER pAd,
	IN INT32 FromWhichBSSID)
{
	INT32 IfIdx = MAIN_MBSSID;

#ifdef CONFIG_AP_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_AP(pAd)
	{
#ifdef APCLI_SUPPORT
		if(FromWhichBSSID >= MIN_NET_DEVICE_FOR_APCLI)
		{
			IfIdx = MAX_MBSSID_NUM(pAd) + MAX_WDS_ENTRY;
		} 
		else
#endif // APCLI_SUPPORT //
#ifdef WDS_SUPPORT
		if(FromWhichBSSID >= MIN_NET_DEVICE_FOR_WDS)
		{
			INT WdsIndex = FromWhichBSSID - MIN_NET_DEVICE_FOR_WDS;
			IfIdx = MAX_MBSSID_NUM(pAd) + WdsIndex;
		}
		else
#endif // WDS_SUPPORT //
		{
			IfIdx = FromWhichBSSID;
		}
	}
#endif // CONFIG_AP_SUPPORT //


	return IfIdx; /* return one of MBSS */
}

/*
========================================================================
Routine Description:
	Get real interface index, used in get_netdev_from_bssid()

Arguments:
	pAd				- 
	FromWhichBSSID	- 

Return Value:
	None

Note:
========================================================================
*/
static INT32 GetSpecInfoIdxFromBssid(
	IN PRTMP_ADAPTER	pAd,
	IN INT32			FromWhichBSSID)
{
	INT32 IfIdx = MAIN_MBSSID;

#ifdef CONFIG_AP_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_AP(pAd)
	{
#ifdef APCLI_SUPPORT
		if(FromWhichBSSID >= MIN_NET_DEVICE_FOR_APCLI)
		{
			IfIdx = MAX_MBSSID_NUM(pAd) + MAX_WDS_ENTRY;
		}
		else
#endif // APCLI_SUPPORT //
#ifdef WDS_SUPPORT
		if(FromWhichBSSID >= MIN_NET_DEVICE_FOR_WDS)
		{
			INT WdsIndex = FromWhichBSSID - MIN_NET_DEVICE_FOR_WDS;
			IfIdx = MAX_MBSSID_NUM(pAd) + WdsIndex;
		}
		else
#endif // WDS_SUPPORT //
		{
			IfIdx = FromWhichBSSID;
		}
	}
#endif // CONFIG_AP_SUPPORT //


	return IfIdx; /* return one of MBSS */
} /* End of GetSpecInfoIdxFromBssid */


/*
========================================================================
Routine Description:
	Ikanos WLAN --> LAN transmit fast path function.

Arguments:
	pFrame			- the received packet (Ikanos packet format)

Return Value:
	None

Note:
	Ikanos platform supports only 8 VAPs
========================================================================
*/
static void IKANOS_WlanPktFromAp(
	IN apPreHeader_t		*pFrame)
{
	PRTMP_ADAPTER pAd;
    struct net_device *dev = NULL;
    struct sk_buff *skb;
    INT32 index;
    apPreHeader_t *apBuf = K0_TO_K1(pFrame);


	pAd = pIkanosAd;
    //index = apBuf->specInfoElement;
	//dev = pAd->ApCfg.MBSSID[index].MSSIDDev;
	index = GetSpecInfoIdxFromBssid(pAd, apBuf->specInfoElement);
	dev = get_netdev_from_bssid(pAd, apBuf->specInfoElement);
    if (dev == NULL)
    {
        printk("ikanos> %s: ERROR null device ***************\n", __FUNCTION__);
        return;
    } /* End of if */

    skb = (struct sk_buff *)translateApbuf2Mbuf(apBuf);
    if (NULL == skb)
    {
        printk("ikanos> %s: skb is null *********************\n", __FUNCTION__);
        return;
    } /* End of if */

    pAd->IkanosRxInfo[index].netdev = dev;
    pAd->IkanosRxInfo[index].fp = &IKANOS_WlanDataFramesTx;

    skb->dev = dev;
    skb->apFlowData.rxApId = IKANOS_PERAP_ID;
    //skb->apFlowData.txHandle = &(txinforx[index]);
    skb->apFlowData.rxHandle = &(pAd->IkanosRxInfo[index]);
    skb->protocol = eth_type_trans(skb, skb->dev);

#ifdef IKANOS_DEBUG
	printk("ikanos> rx no fp!\n"); // debug use
#endif // IKANOS_DEBUG //

    netif_rx(skb);
    return;
} /* End of IKANOS_WlanPktFromAp */

#endif // IKANOS_VX_1X0 //

/* End of vr_ikans.c */