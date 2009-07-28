/* GSM Network Management (OML) messages on the A-bis interface 
 * 3GPP TS 12.21 version 8.0.0 Release 1999 / ETSI TS 100 623 V8.0.0 */

/* (C) 2008-2009 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <libgen.h>
#include <time.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openbsc/gsm_data.h>
#include <openbsc/debug.h>
#include <openbsc/msgb.h>
#include <openbsc/tlv.h>
#include <openbsc/abis_nm.h>
#include <openbsc/misdn.h>
#include <openbsc/signal.h>
#include <openbsc/talloc.h>

#define OM_ALLOC_SIZE		1024
#define OM_HEADROOM_SIZE	128

/* unidirectional messages from BTS to BSC */
static const enum abis_nm_msgtype reports[] = {
	NM_MT_SW_ACTIVATED_REP,
	NM_MT_TEST_REP,
	NM_MT_STATECHG_EVENT_REP,
	NM_MT_FAILURE_EVENT_REP,
};

/* messages without ACK/NACK */
static const enum abis_nm_msgtype no_ack_nack[] = {
	NM_MT_MEAS_RES_REQ,
	NM_MT_STOP_MEAS,
	NM_MT_START_MEAS,
};

/* Messages related to software load */
static const enum abis_nm_msgtype sw_load_msgs[] = {
	NM_MT_LOAD_INIT_ACK,
	NM_MT_LOAD_INIT_NACK,
	NM_MT_LOAD_SEG_ACK,
	NM_MT_LOAD_ABORT,
	NM_MT_LOAD_END_ACK,
	NM_MT_LOAD_END_NACK,
	//NM_MT_SW_ACT_REQ,
	NM_MT_ACTIVATE_SW_ACK,
	NM_MT_ACTIVATE_SW_NACK,
	NM_MT_SW_ACTIVATED_REP,
};

static const enum abis_nm_msgtype nacks[] = {
	NM_MT_LOAD_INIT_NACK,
	NM_MT_LOAD_END_NACK,
	NM_MT_SW_ACT_REQ_NACK,
	NM_MT_ACTIVATE_SW_NACK,
	NM_MT_ESTABLISH_TEI_NACK,
	NM_MT_CONN_TERR_SIGN_NACK,
	NM_MT_DISC_TERR_SIGN_NACK,
	NM_MT_CONN_TERR_TRAF_NACK,
	NM_MT_DISC_TERR_TRAF_NACK,
	NM_MT_CONN_MDROP_LINK_NACK,
	NM_MT_DISC_MDROP_LINK_NACK,
	NM_MT_SET_BTS_ATTR_NACK,
	NM_MT_SET_RADIO_ATTR_NACK,
	NM_MT_SET_CHAN_ATTR_NACK,
	NM_MT_PERF_TEST_NACK,
	NM_MT_SEND_TEST_REP_NACK,
	NM_MT_STOP_TEST_NACK,
	NM_MT_STOP_EVENT_REP_NACK,
	NM_MT_REST_EVENT_REP_NACK,
	NM_MT_CHG_ADM_STATE_NACK,
	NM_MT_CHG_ADM_STATE_REQ_NACK,
	NM_MT_REP_OUTST_ALARMS_NACK,
	NM_MT_CHANGEOVER_NACK,
	NM_MT_OPSTART_NACK,
	NM_MT_REINIT_NACK,
	NM_MT_SET_SITE_OUT_NACK,
	NM_MT_CHG_HW_CONF_NACK,
	NM_MT_GET_ATTR_NACK,
	NM_MT_SET_ALARM_THRES_NACK,
	NM_MT_BS11_BEGIN_DB_TX_NACK,
	NM_MT_BS11_END_DB_TX_NACK,
	NM_MT_BS11_CREATE_OBJ_NACK,
	NM_MT_BS11_DELETE_OBJ_NACK,
};

static const char *nack_names[0xff] = {
	[NM_MT_LOAD_INIT_NACK]		= "SOFTWARE LOAD INIT",
	[NM_MT_LOAD_END_NACK]		= "SOFTWARE LOAD END",
	[NM_MT_SW_ACT_REQ_NACK]		= "SOFTWARE ACTIVATE REQUEST",
	[NM_MT_ACTIVATE_SW_NACK]	= "ACTIVATE SOFTWARE",
	[NM_MT_ESTABLISH_TEI_NACK]	= "ESTABLISH TEI",
	[NM_MT_CONN_TERR_SIGN_NACK]	= "CONNECT TERRESTRIAL SIGNALLING",
	[NM_MT_DISC_TERR_SIGN_NACK]	= "DISCONNECT TERRESTRIAL SIGNALLING",
	[NM_MT_CONN_TERR_TRAF_NACK]	= "CONNECT TERRESTRIAL TRAFFIC",
	[NM_MT_DISC_TERR_TRAF_NACK]	= "DISCONNECT TERRESTRIAL TRAFFIC",
	[NM_MT_CONN_MDROP_LINK_NACK]	= "CONNECT MULTI-DROP LINK",
	[NM_MT_DISC_MDROP_LINK_NACK]	= "DISCONNECT MULTI-DROP LINK",
	[NM_MT_SET_BTS_ATTR_NACK]	= "SET BTS ATTRIBUTE",
	[NM_MT_SET_RADIO_ATTR_NACK]	= "SET RADIO ATTRIBUTE",
	[NM_MT_SET_CHAN_ATTR_NACK]	= "SET CHANNEL ATTRIBUTE",
	[NM_MT_PERF_TEST_NACK]		= "PERFORM TEST",
	[NM_MT_SEND_TEST_REP_NACK]	= "SEND TEST REPORT",
	[NM_MT_STOP_TEST_NACK]		= "STOP TEST",
	[NM_MT_STOP_EVENT_REP_NACK]	= "STOP EVENT REPORT",
	[NM_MT_REST_EVENT_REP_NACK]	= "RESET EVENT REPORT",
	[NM_MT_CHG_ADM_STATE_NACK]	= "CHANGE ADMINISTRATIVE STATE",
	[NM_MT_CHG_ADM_STATE_REQ_NACK]	= "CHANGE ADMINISTRATIVE STATE REQUEST",
	[NM_MT_REP_OUTST_ALARMS_NACK]	= "REPORT OUTSTANDING ALARMS",
	[NM_MT_CHANGEOVER_NACK]		= "CHANGEOVER",
	[NM_MT_OPSTART_NACK]		= "OPSTART",
	[NM_MT_REINIT_NACK]		= "REINIT",
	[NM_MT_SET_SITE_OUT_NACK]	= "SET SITE OUTPUT",
	[NM_MT_CHG_HW_CONF_NACK]	= "CHANGE HARDWARE CONFIGURATION",
	[NM_MT_GET_ATTR_NACK]		= "GET ATTRIBUTE",
	[NM_MT_SET_ALARM_THRES_NACK]	= "SET ALARM THRESHOLD",
	[NM_MT_BS11_BEGIN_DB_TX_NACK]	= "BS11 BEGIN DATABASE TRANSMISSION",
	[NM_MT_BS11_END_DB_TX_NACK]	= "BS11 END DATABASE TRANSMISSION",
	[NM_MT_BS11_CREATE_OBJ_NACK]	= "BS11 CREATE OBJECT",
	[NM_MT_BS11_DELETE_OBJ_NACK]	= "BS11 DELETE OBJECT",
};

/* Chapter 9.4.36 */
static const char *nack_cause_names[] = {
	/* General Nack Causes */
	[NM_NACK_INCORR_STRUCT]		= "Incorrect message structure",
	[NM_NACK_MSGTYPE_INVAL]		= "Invalid message type value",
	[NM_NACK_OBJCLASS_INVAL]	= "Invalid Object class value",
	[NM_NACK_OBJCLASS_NOTSUPP]	= "Object class not supported",
	[NM_NACK_BTSNR_UNKN]		= "BTS no. unknown",
	[NM_NACK_TRXNR_UNKN]		= "Baseband Transceiver no. unknown",
	[NM_NACK_OBJINST_UNKN]		= "Object Instance unknown",
	[NM_NACK_ATTRID_INVAL]		= "Invalid attribute identifier value",
	[NM_NACK_ATTRID_NOTSUPP]	= "Attribute identifier not supported",
	[NM_NACK_PARAM_RANGE]		= "Parameter value outside permitted range",
	[NM_NACK_ATTRLIST_INCONSISTENT]	= "Inconsistency in attribute list",
	[NM_NACK_SPEC_IMPL_NOTSUPP]	= "Specified implementation not supported",
	[NM_NACK_CANT_PERFORM]		= "Message cannot be performed",
	/* Specific Nack Causes */
	[NM_NACK_RES_NOTIMPL]		= "Resource not implemented",
	[NM_NACK_RES_NOTAVAIL]		= "Resource not available",
	[NM_NACK_FREQ_NOTAVAIL]		= "Frequency not available",
	[NM_NACK_TEST_NOTSUPP]		= "Test not supported",
	[NM_NACK_CAPACITY_RESTR]	= "Capacity restrictions",
	[NM_NACK_PHYSCFG_NOTPERFORM]	= "Physical configuration cannot be performed",
	[NM_NACK_TEST_NOTINIT]		= "Test not initiated",
	[NM_NACK_PHYSCFG_NOTRESTORE]	= "Physical configuration cannot be restored",
	[NM_NACK_TEST_NOSUCH]		= "No such test",
	[NM_NACK_TEST_NOSTOP]		= "Test cannot be stopped",
	[NM_NACK_MSGINCONSIST_PHYSCFG]	= "Message inconsistent with physical configuration",
	[NM_NACK_FILE_INCOMPLETE]	= "Complete file notreceived",
	[NM_NACK_FILE_NOTAVAIL]		= "File not available at destination",
	[NM_NACK_FILE_NOTACTIVATE]	= "File cannot be activate",
	[NM_NACK_REQ_NOT_GRANT]		= "Request not granted",
	[NM_NACK_WAIT]			= "Wait",
	[NM_NACK_NOTH_REPORT_EXIST]	= "Nothing reportable existing",
	[NM_NACK_MEAS_NOTSUPP]		= "Measurement not supported",
	[NM_NACK_MEAS_NOTSTART]		= "Measurement not started",
};

static char namebuf[255];
static const char *nack_cause_name(u_int8_t cause)
{
	if (cause < ARRAY_SIZE(nack_cause_names) && nack_cause_names[cause])
		return nack_cause_names[cause];

	snprintf(namebuf, sizeof(namebuf), "0x%02x\n", cause);
	return namebuf;
}

/* Chapter 9.4.16: Event Type */
static const char *event_type_names[] = {
	[NM_EVT_COMM_FAIL]		= "communication failure",
	[NM_EVT_QOS_FAIL]		= "quality of service failure",
	[NM_EVT_PROC_FAIL]		= "processing failure",
	[NM_EVT_EQUIP_FAIL]		= "equipment failure",
	[NM_EVT_ENV_FAIL]		= "environment failure",
};

static const char *event_type_name(u_int8_t cause)
{
	if (cause < ARRAY_SIZE(event_type_names) && event_type_names[cause])
		return event_type_names[cause];

	snprintf(namebuf, sizeof(namebuf), "0x%02x\n", cause);
	return namebuf;
}

/* Chapter 9.4.63: Perceived Severity */
static const char *severity_names[] = {
	[NM_SEVER_CEASED]		= "failure ceased",
	[NM_SEVER_CRITICAL]		= "critical failure",
	[NM_SEVER_MAJOR]		= "major failure",
	[NM_SEVER_MINOR]		= "minor failure",
	[NM_SEVER_WARNING]		= "warning level failure",
	[NM_SEVER_INDETERMINATE]	= "indeterminate failure",
};

static const char *severity_name(u_int8_t cause)
{
	if (cause < ARRAY_SIZE(severity_names) && severity_names[cause])
		return severity_names[cause];

	snprintf(namebuf, sizeof(namebuf), "0x%02x\n", cause);
	return namebuf;
}

/* Attributes that the BSC can set, not only get, according to Section 9.4 */
static const enum abis_nm_attr nm_att_settable[] = {
	NM_ATT_ADD_INFO,
	NM_ATT_ADD_TEXT,
	NM_ATT_DEST,
	NM_ATT_EVENT_TYPE,
	NM_ATT_FILE_DATA,
	NM_ATT_GET_ARI,
	NM_ATT_HW_CONF_CHG,
	NM_ATT_LIST_REQ_ATTR,
	NM_ATT_MDROP_LINK,
	NM_ATT_MDROP_NEXT,
	NM_ATT_NACK_CAUSES,
	NM_ATT_OUTST_ALARM,
	NM_ATT_PHYS_CONF,
	NM_ATT_PROB_CAUSE,
	NM_ATT_RAD_SUBC,
	NM_ATT_SOURCE,
	NM_ATT_SPEC_PROB,
	NM_ATT_START_TIME,
	NM_ATT_TEST_DUR,
	NM_ATT_TEST_NO,
	NM_ATT_TEST_REPORT,
	NM_ATT_WINDOW_SIZE,
	NM_ATT_SEVERITY,
	NM_ATT_MEAS_RES,
	NM_ATT_MEAS_TYPE,
};

static const struct tlv_definition nm_att_tlvdef = {
	.def = {
		[NM_ATT_ABIS_CHANNEL] =		{ TLV_TYPE_FIXED, 3 },
		[NM_ATT_ADD_INFO] =		{ TLV_TYPE_TL16V },
		[NM_ATT_ADD_TEXT] =		{ TLV_TYPE_TL16V },
		[NM_ATT_ADM_STATE] =		{ TLV_TYPE_TV },
		[NM_ATT_ARFCN_LIST]=		{ TLV_TYPE_TL16V },
		[NM_ATT_AUTON_REPORT] =		{ TLV_TYPE_TV },
		[NM_ATT_AVAIL_STATUS] =		{ TLV_TYPE_TL16V },
		[NM_ATT_BCCH_ARFCN] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_BSIC] =			{ TLV_TYPE_TV },
		[NM_ATT_BTS_AIR_TIMER] =	{ TLV_TYPE_TV },
		[NM_ATT_CCCH_L_I_P] =		{ TLV_TYPE_TV },
		[NM_ATT_CCCH_L_T] =		{ TLV_TYPE_TV },
		[NM_ATT_CHAN_COMB] =		{ TLV_TYPE_TV },
		[NM_ATT_CONN_FAIL_CRIT] =	{ TLV_TYPE_TL16V },
		[NM_ATT_DEST] =			{ TLV_TYPE_TL16V },
		[NM_ATT_EVENT_TYPE] =		{ TLV_TYPE_TV },
		[NM_ATT_FILE_DATA] =		{ TLV_TYPE_TL16V },
		[NM_ATT_FILE_ID] =		{ TLV_TYPE_TL16V },
		[NM_ATT_FILE_VERSION] =		{ TLV_TYPE_TL16V },
		[NM_ATT_GSM_TIME] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_HSN] =			{ TLV_TYPE_TV },
		[NM_ATT_HW_CONFIG] =		{ TLV_TYPE_TL16V },
		[NM_ATT_HW_DESC] =		{ TLV_TYPE_TL16V },
		[NM_ATT_INTAVE_PARAM] =		{ TLV_TYPE_TV },
		[NM_ATT_INTERF_BOUND] =		{ TLV_TYPE_FIXED, 6 },
		[NM_ATT_LIST_REQ_ATTR] =	{ TLV_TYPE_TL16V },
		[NM_ATT_MAIO] =			{ TLV_TYPE_TV },
		[NM_ATT_MANUF_STATE] =		{ TLV_TYPE_TV },
		[NM_ATT_MANUF_THRESH] =		{ TLV_TYPE_TL16V },
		[NM_ATT_MANUF_ID] =		{ TLV_TYPE_TL16V },
		[NM_ATT_MAX_TA] =		{ TLV_TYPE_TV },
		[NM_ATT_MDROP_LINK] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_MDROP_NEXT] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_NACK_CAUSES] =		{ TLV_TYPE_TV },
		[NM_ATT_NY1] =			{ TLV_TYPE_TV },
		[NM_ATT_OPER_STATE] =		{ TLV_TYPE_TV },
		[NM_ATT_OVERL_PERIOD] =		{ TLV_TYPE_TL16V },
		[NM_ATT_PHYS_CONF] =		{ TLV_TYPE_TL16V },
		[NM_ATT_POWER_CLASS] =		{ TLV_TYPE_TV },
		[NM_ATT_POWER_THRESH] =		{ TLV_TYPE_FIXED, 3 },
		[NM_ATT_PROB_CAUSE] =		{ TLV_TYPE_FIXED, 3 },
		[NM_ATT_RACH_B_THRESH] =	{ TLV_TYPE_TV },
		[NM_ATT_LDAVG_SLOTS] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_RAD_SUBC] =		{ TLV_TYPE_TV },
		[NM_ATT_RF_MAXPOWR_R] =		{ TLV_TYPE_TV },
		[NM_ATT_SITE_INPUTS] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SITE_OUTPUTS] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SOURCE] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SPEC_PROB] =		{ TLV_TYPE_TV },
		[NM_ATT_START_TIME] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_T200] =			{ TLV_TYPE_FIXED, 7 },
		[NM_ATT_TEI] =			{ TLV_TYPE_TV },
		[NM_ATT_TEST_DUR] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_TEST_NO] =		{ TLV_TYPE_TV },
		[NM_ATT_TEST_REPORT] =		{ TLV_TYPE_TL16V },
		[NM_ATT_VSWR_THRESH] =		{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_WINDOW_SIZE] = 		{ TLV_TYPE_TV },
		[NM_ATT_TSC] =			{ TLV_TYPE_TV },
		[NM_ATT_SW_CONFIG] =		{ TLV_TYPE_TL16V },
		[NM_ATT_SEVERITY] = 		{ TLV_TYPE_TV },
		[NM_ATT_GET_ARI] =		{ TLV_TYPE_TL16V },
		[NM_ATT_HW_CONF_CHG] = 		{ TLV_TYPE_TL16V },
		[NM_ATT_OUTST_ALARM] =		{ TLV_TYPE_TV },
		[NM_ATT_FILE_DATA] =		{ TLV_TYPE_TL16V },
		[NM_ATT_MEAS_RES] =		{ TLV_TYPE_TL16V },
		/* BS11 specifics */
		[NM_ATT_BS11_ESN_FW_CODE_NO] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_ESN_HW_CODE_NO] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_ESN_PCB_SERIAL] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_BOOT_SW_VERS] =	{ TLV_TYPE_TLV },
		[0xd5] =			{ TLV_TYPE_TLV },
		[0xa8] =			{ TLV_TYPE_TLV },
		[NM_ATT_BS11_PASSWORD] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_TXPWR] =		{ TLV_TYPE_TLV },
		[NM_ATT_BS11_RSSI_OFFS] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_LINE_CFG] = 	{ TLV_TYPE_TV },
		[NM_ATT_BS11_L1_PROT_TYPE] =	{ TLV_TYPE_TV },
		[NM_ATT_BS11_BIT_ERR_THESH] =	{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_BS11_DIVERSITY] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_LMT_LOGON_SESSION]={ TLV_TYPE_TLV },	
		[NM_ATT_BS11_LMT_LOGIN_TIME] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_LMT_USER_ACC_LEV] ={ TLV_TYPE_TLV },
		[NM_ATT_BS11_LMT_USER_NAME] =	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_BTS_STATE]	=	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_E1_STATE]	=	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_PLL_MODE]	=	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_PLL]	=	{ TLV_TYPE_TLV },
		[NM_ATT_BS11_CCLK_ACCURACY] =	{ TLV_TYPE_TV },
		[NM_ATT_BS11_CCLK_TYPE] =	{ TLV_TYPE_TV },
		/* ip.access specifics */
		[NM_ATT_IPACC_DST_IP] =		{ TLV_TYPE_FIXED, 4 },
		[NM_ATT_IPACC_DST_IP_PORT] =	{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_IPACC_PRIM_OML_CFG_LIST] = { TLV_TYPE_TL16V },
		[NM_ATT_IPACC_NV_FLAGS] =	{ TLV_TYPE_TL16V },
		[NM_ATT_IPACC_FREQ_CTRL] =	{ TLV_TYPE_FIXED, 2 },
		[NM_ATT_IPACC_PRIM_OML_FB_TOUT] = { TLV_TYPE_TL16V },
		[NM_ATT_IPACC_SEC_OML_CFG] =	{ TLV_TYPE_FIXED, 6 },
		[NM_ATT_IPACC_IP_IF_CFG] =	{ TLV_TYPE_FIXED, 8 },
		[NM_ATT_IPACC_IP_GW_CFG] =	{ TLV_TYPE_FIXED, 12 },
		[NM_ATT_IPACC_LOCATION] =	{ TLV_TYPE_TL16V },
		[NM_ATT_IPACC_UNIT_ID] =	{ TLV_TYPE_TL16V },
		[NM_ATT_IPACC_UNIT_NAME] =	{ TLV_TYPE_TL16V },
		[NM_ATT_IPACC_SNMP_CFG] =	{ TLV_TYPE_TL16V },
		[NM_ATT_IPACC_ALM_THRESH_LIST]=	{ TLV_TYPE_TL16V },
		//[0x95] =			{ TLV_TYPE_FIXED, 2 },
		[0x85] =			{ TLV_TYPE_TV },

	},
};

int abis_nm_tlv_parse(struct tlv_parsed *tp, const u_int8_t *buf, int len)
{
	return tlv_parse(tp, &nm_att_tlvdef, buf, len, 0, 0);
}

static int is_in_arr(enum abis_nm_msgtype mt, const enum abis_nm_msgtype *arr, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (arr[i] == mt)
			return 1;
	}

	return 0;
}

#if 0
/* is this msgtype the usual ACK/NACK type ? */
static int is_ack_nack(enum abis_nm_msgtype mt)
{
	return !is_in_arr(mt, no_ack_nack, ARRAY_SIZE(no_ack_nack));
}
#endif

/* is this msgtype a report ? */
static int is_report(enum abis_nm_msgtype mt)
{
	return is_in_arr(mt, reports, ARRAY_SIZE(reports));
}

#define MT_ACK(x)	(x+1)
#define MT_NACK(x)	(x+2)

static void fill_om_hdr(struct abis_om_hdr *oh, u_int8_t len)
{
	oh->mdisc = ABIS_OM_MDISC_FOM;
	oh->placement = ABIS_OM_PLACEMENT_ONLY;
	oh->sequence = 0;
	oh->length = len;
}

static void fill_om_fom_hdr(struct abis_om_hdr *oh, u_int8_t len,
			    u_int8_t msg_type, u_int8_t obj_class,
			    u_int8_t bts_nr, u_int8_t trx_nr, u_int8_t ts_nr)
{
	struct abis_om_fom_hdr *foh =
			(struct abis_om_fom_hdr *) oh->data;

	fill_om_hdr(oh, len+sizeof(*foh));
	foh->msg_type = msg_type;
	foh->obj_class = obj_class;
	foh->obj_inst.bts_nr = bts_nr;
	foh->obj_inst.trx_nr = trx_nr;
	foh->obj_inst.ts_nr = ts_nr;
}

static struct msgb *nm_msgb_alloc(void)
{
	return msgb_alloc_headroom(OM_ALLOC_SIZE, OM_HEADROOM_SIZE,
				   "OML");
}

/* Send a OML NM Message from BSC to BTS */
int abis_nm_sendmsg(struct gsm_bts *bts, struct msgb *msg)
{
	msg->trx = bts->c0;

	return _abis_nm_sendmsg(msg);
}

static int abis_nm_rcvmsg_sw(struct msgb *mb);

static const char *obj_class_name(u_int8_t oc)
{
	switch (oc) {
	case NM_OC_SITE_MANAGER:
		return "SITE MANAGER";
	case NM_OC_BTS:
		return "BTS";
	case NM_OC_RADIO_CARRIER:
		return "RADIO CARRIER";
	case NM_OC_BASEB_TRANSC:
		return "BASEBAND TRANSCEIVER";
	case NM_OC_CHANNEL:
		return "CHANNEL";
	case NM_OC_BS11_ADJC:
		return "ADJC";
	case NM_OC_BS11_HANDOVER:
		return "HANDOVER";
	case NM_OC_BS11_PWR_CTRL:
		return "POWER CONTROL";
	case NM_OC_BS11_BTSE:
		return "BTSE";
	case NM_OC_BS11_RACK:
		return "RACK";
	case NM_OC_BS11_TEST:
		return "TEST";
	case NM_OC_BS11_ENVABTSE:
		return "ENVABTSE";
	case NM_OC_BS11_BPORT:
		return "BPORT";
	case NM_OC_GPRS_NSE:
		return "GPRS NSE";
	case NM_OC_GPRS_CELL:
		return "GPRS CELL";
	case NM_OC_GPRS_NSVC:
		return "GPRS NSVC";
	case NM_OC_BS11:
		return "SIEMENSHW";
	}

	return "UNKNOWN";
}

const char *nm_opstate_name(u_int8_t os)
{
	switch (os) {
	case 1:
		return "Disabled";
	case 2:
		return "Enabled";
	case 0xff:
		return "NULL";
	default:
		return "RFU";
	}
}

/* Chapter 9.4.7 */
static const char *avail_names[] = {
	"In test",
	"Failed",
	"Power off",
	"Off line",
	"<not used>",
	"Dependency",
	"Degraded",
	"Not installed",
};

const char *nm_avail_name(u_int8_t avail)
{
	if (avail == 0xff)
		return "OK";
	if (avail >= ARRAY_SIZE(avail_names))
		return "UNKNOWN";
	return avail_names[avail];
}

const char *nm_adm_name(u_int8_t adm)
{
	switch (adm) {
	case 1:
		return "Locked";
	case 2:
		return "Unlocked";
	case 3:
		return "Shutdown";
	default:
		return "<not used>";
	}
}

/* obtain the gsm_nm_state data structure for a given object instance */
static struct gsm_nm_state *
objclass2nmstate(struct gsm_bts *bts, u_int8_t obj_class,
		 struct abis_om_obj_inst *obj_inst)
{
	struct gsm_bts_trx *trx;
	struct gsm_nm_state *nm_state = NULL;

	switch (obj_class) {
	case NM_OC_BTS:
		nm_state = &bts->nm_state;
		break;
	case NM_OC_RADIO_CARRIER:
		if (obj_inst->trx_nr >= bts->num_trx)
			return NULL;
		trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
		nm_state = &trx->nm_state;
		break;
	case NM_OC_BASEB_TRANSC:
		if (obj_inst->trx_nr >= bts->num_trx)
			return NULL;
		trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
		nm_state = &trx->bb_transc.nm_state;
		break;
	case NM_OC_CHANNEL:
		if (obj_inst->trx_nr > bts->num_trx)
			return NULL;
		trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
		if (obj_inst->ts_nr >= TRX_NR_TS)
			return NULL;
		nm_state = &trx->ts[obj_inst->ts_nr].nm_state;
		break;
	case NM_OC_SITE_MANAGER:
		nm_state = &bts->site_mgr.nm_state;
		break;
	case NM_OC_BS11:
		switch (obj_inst->bts_nr) {
		case BS11_OBJ_CCLK:
			nm_state = &bts->bs11.cclk.nm_state;
			break;
		case BS11_OBJ_BBSIG:
			if (obj_inst->ts_nr > bts->num_trx)
				return NULL;
			trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
			nm_state = &trx->bs11.bbsig.nm_state;
			break;
		case BS11_OBJ_PA:
			if (obj_inst->ts_nr > bts->num_trx)
				return NULL;
			trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
			nm_state = &trx->bs11.pa.nm_state;
			break;
		default:
			return NULL;
		}
	case NM_OC_BS11_RACK:
		nm_state = &bts->bs11.rack.nm_state;
		break;
	case NM_OC_BS11_ENVABTSE:
		if (obj_inst->trx_nr > ARRAY_SIZE(bts->bs11.envabtse))
			return NULL;
		nm_state = &bts->bs11.envabtse[obj_inst->trx_nr].nm_state;
		break;
	}
	return nm_state;
}

/* obtain the in-memory data structure of a given object instance */
static void *
objclass2obj(struct gsm_bts *bts, u_int8_t obj_class,
	     struct abis_om_obj_inst *obj_inst)
{
	struct gsm_bts_trx *trx;
	void *obj = NULL;

	switch (obj_class) {
	case NM_OC_BTS:
		obj = bts;
		break;
	case NM_OC_RADIO_CARRIER:
		if (obj_inst->trx_nr >= bts->num_trx)
			return NULL;
		trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
		obj = trx;
		break;
	case NM_OC_BASEB_TRANSC:
		if (obj_inst->trx_nr >= bts->num_trx)
			return NULL;
		trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
		obj = &trx->bb_transc;
		break;
	case NM_OC_CHANNEL:
		if (obj_inst->trx_nr > bts->num_trx)
			return NULL;
		trx = gsm_bts_trx_num(bts, obj_inst->trx_nr);
		if (obj_inst->ts_nr >= TRX_NR_TS)
			return NULL;
		obj = &trx->ts[obj_inst->ts_nr];
		break;
	case NM_OC_SITE_MANAGER:
		obj = &bts->site_mgr;
		break;
	}
	return obj;
}

/* Update the administrative state of a given object in our in-memory data
 * structures and send an event to the higher layer */
static int update_admstate(struct gsm_bts *bts, u_int8_t obj_class,
			   struct abis_om_obj_inst *obj_inst, u_int8_t adm_state)
{
	struct gsm_nm_state *nm_state, new_state;
	void *obj;
	int rc;

	obj = objclass2obj(bts, obj_class, obj_inst);
	nm_state = objclass2nmstate(bts, obj_class, obj_inst);
	if (!nm_state)
		return -1;

	new_state = *nm_state;
	new_state.administrative = adm_state;

	rc = nm_state_event(EVT_STATECHG_ADM, obj_class, obj, nm_state, &new_state);

	nm_state->administrative = adm_state;

	return rc;
}

static int abis_nm_rx_statechg_rep(struct msgb *mb)
{
	struct abis_om_hdr *oh = msgb_l2(mb);
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	struct gsm_bts *bts = mb->trx->bts;
	struct tlv_parsed tp;
	struct gsm_nm_state *nm_state, new_state;
	int rc;

	DEBUGPC(DNM, "STATE CHG: ");

	memset(&new_state, 0, sizeof(new_state));

	nm_state = objclass2nmstate(bts, foh->obj_class, &foh->obj_inst);
	if (!nm_state) {
		DEBUGPC(DNM, "\n");
		return -EINVAL;
	}

	new_state = *nm_state;
	
	abis_nm_tlv_parse(&tp, foh->data, oh->length-sizeof(*foh));
	if (TLVP_PRESENT(&tp, NM_ATT_OPER_STATE)) {
		new_state.operational = *TLVP_VAL(&tp, NM_ATT_OPER_STATE);
		DEBUGPC(DNM, "OP_STATE=%s ", nm_opstate_name(new_state.operational));
	}
	if (TLVP_PRESENT(&tp, NM_ATT_AVAIL_STATUS)) {
		if (TLVP_LEN(&tp, NM_ATT_AVAIL_STATUS) == 0)
			new_state.availability = 0xff;
		else
			new_state.availability = *TLVP_VAL(&tp, NM_ATT_AVAIL_STATUS);
		DEBUGPC(DNM, "AVAIL=%s(%02x) ", nm_avail_name(new_state.availability),
			new_state.availability);
	}
	if (TLVP_PRESENT(&tp, NM_ATT_ADM_STATE)) {
		new_state.administrative = *TLVP_VAL(&tp, NM_ATT_ADM_STATE);
		DEBUGPC(DNM, "ADM=%02s ", nm_adm_name(new_state.administrative));
	}
	DEBUGPC(DNM, "\n");

	if (memcmp(&new_state, nm_state, sizeof(new_state))) {
		/* Update the operational state of a given object in our in-memory data
 		* structures and send an event to the higher layer */
		void *obj = objclass2obj(bts, foh->obj_class, &foh->obj_inst);
		rc = nm_state_event(EVT_STATECHG_OPER, foh->obj_class, obj, nm_state, &new_state);
		*nm_state = new_state;
	}
#if 0
	if (op_state == 1) {
		/* try to enable objects that are disabled */
		abis_nm_opstart(bts, foh->obj_class,
				foh->obj_inst.bts_nr,
				foh->obj_inst.trx_nr,
				foh->obj_inst.ts_nr);
	}
#endif
	return 0;
}

static int rx_fail_evt_rep(struct msgb *mb)
{
	struct abis_om_hdr *oh = msgb_l2(mb);
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	struct tlv_parsed tp;

	DEBUGPC(DNM, "Failure Event Report ");
	
	abis_nm_tlv_parse(&tp, foh->data, oh->length-sizeof(*foh));

	if (TLVP_PRESENT(&tp, NM_ATT_EVENT_TYPE))
		DEBUGPC(DNM, "Type=%s ", event_type_name(*TLVP_VAL(&tp, NM_ATT_EVENT_TYPE)));
	if (TLVP_PRESENT(&tp, NM_ATT_SEVERITY))
		DEBUGPC(DNM, "Severity=%s ", severity_name(*TLVP_VAL(&tp, NM_ATT_SEVERITY)));

	DEBUGPC(DNM, "\n");

	return 0;
}

static int abis_nm_rcvmsg_report(struct msgb *mb)
{
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	u_int8_t mt = foh->msg_type;

	DEBUGP(DNM, "OC=%s(%02x) INST=(%02x,%02x,%02x) ",
		obj_class_name(foh->obj_class), foh->obj_class, 
		foh->obj_inst.bts_nr, foh->obj_inst.trx_nr,
		foh->obj_inst.ts_nr);

	//nmh->cfg->report_cb(mb, foh);

	switch (mt) {
	case NM_MT_STATECHG_EVENT_REP:
		return abis_nm_rx_statechg_rep(mb);
		break;
	case NM_MT_SW_ACTIVATED_REP:
		DEBUGPC(DNM, "Software Activated Report\n");
		dispatch_signal(SS_NM, S_NM_SW_ACTIV_REP, mb);
		break;
	case NM_MT_FAILURE_EVENT_REP:
		rx_fail_evt_rep(mb);
		dispatch_signal(SS_NM, S_NM_FAIL_REP, mb);
		break;
	default:
		DEBUGPC(DNM, "reporting NM MT 0x%02x\n", mt);
		break;
		
	};

	return 0;
}

/* Activate the specified software into the BTS */
static int ipacc_sw_activate(struct gsm_bts *bts, u_int8_t obj_class, u_int8_t i0, u_int8_t i1,
			     u_int8_t i2, u_int8_t *sw_desc, u_int8_t swdesc_len)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t len = swdesc_len;
	u_int8_t *trailer;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, len, NM_MT_ACTIVATE_SW, obj_class, i0, i1, i2);

	trailer = msgb_put(msg, swdesc_len);
	memcpy(trailer, sw_desc, swdesc_len);

	return abis_nm_sendmsg(bts, msg);
}

static int abis_nm_rx_sw_act_req(struct msgb *mb)
{
	struct abis_om_hdr *oh = msgb_l2(mb);
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	int nack = 0;
	int ret;

	DEBUGP(DNM, "Software Activate Request ");

	if (foh->obj_class >= 0xf0 && foh->obj_class <= 0xf3) {
		DEBUGPC(DNM, "NACKing for GPRS obj_class 0x%02x\n", foh->obj_class);
		nack = 1;
	} else
		DEBUGPC(DNM, "ACKing and Activating\n");

	ret = abis_nm_sw_act_req_ack(mb->trx->bts, foh->obj_class,
				      foh->obj_inst.bts_nr,
				      foh->obj_inst.trx_nr,
				      foh->obj_inst.ts_nr, nack,
				      foh->data, oh->length-sizeof(*foh));

	if (nack)
		return ret;

	/* FIXME: properly parse attributes */
	return ipacc_sw_activate(mb->trx->bts, foh->obj_class,
				 foh->obj_inst.bts_nr,
				 foh->obj_inst.trx_nr,
				 foh->obj_inst.ts_nr,
				 foh->data + oh->length-sizeof(*foh)-22, 22);
}

/* Receive a CHANGE_ADM_STATE_ACK, parse the TLV and update local state */
static int abis_nm_rx_chg_adm_state_ack(struct msgb *mb)
{
	struct abis_om_hdr *oh = msgb_l2(mb);
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	struct tlv_parsed tp;
	u_int8_t adm_state;

	abis_nm_tlv_parse(&tp, foh->data, oh->length-sizeof(*foh));
	if (!TLVP_PRESENT(&tp, NM_ATT_ADM_STATE))
		return -EINVAL;

	adm_state = *TLVP_VAL(&tp, NM_ATT_ADM_STATE);

	return update_admstate(mb->trx->bts, foh->obj_class, &foh->obj_inst, adm_state);
}

static int abis_nm_rx_lmt_event(struct msgb *mb)
{
	struct abis_om_hdr *oh = msgb_l2(mb);
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	struct tlv_parsed tp;

	DEBUGP(DNM, "LMT Event ");
	abis_nm_tlv_parse(&tp, foh->data, oh->length-sizeof(*foh));
	if (TLVP_PRESENT(&tp, NM_ATT_BS11_LMT_LOGON_SESSION) &&
	    TLVP_LEN(&tp, NM_ATT_BS11_LMT_LOGON_SESSION) >= 1) {
		u_int8_t onoff = *TLVP_VAL(&tp, NM_ATT_BS11_LMT_LOGON_SESSION);
		DEBUGPC(DNM, "LOG%s ", onoff ? "ON" : "OFF");
	}
	if (TLVP_PRESENT(&tp, NM_ATT_BS11_LMT_USER_ACC_LEV) &&
	    TLVP_LEN(&tp, NM_ATT_BS11_LMT_USER_ACC_LEV) >= 1) {
		u_int8_t level = *TLVP_VAL(&tp, NM_ATT_BS11_LMT_USER_ACC_LEV);
		DEBUGPC(DNM, "Level=%u ", level);
	}
	if (TLVP_PRESENT(&tp, NM_ATT_BS11_LMT_USER_NAME) &&
	    TLVP_LEN(&tp, NM_ATT_BS11_LMT_USER_NAME) >= 1) {
		char *name = (char *) TLVP_VAL(&tp, NM_ATT_BS11_LMT_USER_NAME);
		DEBUGPC(DNM, "Username=%s ", name);
	}
	DEBUGPC(DNM, "\n");
	/* FIXME: parse LMT LOGON TIME */
	return 0;
}

/* Receive a OML NM Message from BTS */
static int abis_nm_rcvmsg_fom(struct msgb *mb)
{
	struct abis_om_hdr *oh = msgb_l2(mb);
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	u_int8_t mt = foh->msg_type;

	/* check for unsolicited message */
	if (is_report(mt))
		return abis_nm_rcvmsg_report(mb);

	if (is_in_arr(mt, sw_load_msgs, ARRAY_SIZE(sw_load_msgs)))
		return abis_nm_rcvmsg_sw(mb);

	if (is_in_arr(mt, nacks, ARRAY_SIZE(nacks))) {
		struct tlv_parsed tp;
		if (nack_names[mt])
			DEBUGP(DNM, "%s NACK ", nack_names[mt]);
			/* FIXME: NACK cause */
		else
			DEBUGP(DNM, "NACK 0x%02x ", mt);

		abis_nm_tlv_parse(&tp, foh->data, oh->length-sizeof(*foh));
		if (TLVP_PRESENT(&tp, NM_ATT_NACK_CAUSES))
			DEBUGPC(DNM, "CAUSE=%s\n", 
				nack_cause_name(*TLVP_VAL(&tp, NM_ATT_NACK_CAUSES)));
		else
			DEBUGPC(DNM, "\n");

		dispatch_signal(SS_NM, S_NM_NACK, (void*) ((long)mt));
		return 0;
	}
#if 0
	/* check if last message is to be acked */
	if (is_ack_nack(nmh->last_msgtype)) {
		if (mt == MT_ACK(nmh->last_msgtype)) {
			fprintf(stderr, "received ACK (0x%x)\n",
				foh->msg_type);
			/* we got our ACK, continue sending the next msg */
		} else if (mt == MT_NACK(nmh->last_msgtype)) {
			/* we got a NACK, signal this to the caller */
			fprintf(stderr, "received NACK (0x%x)\n",
				foh->msg_type);
			/* FIXME: somehow signal this to the caller */
		} else {
			/* really strange things happen */
			return -EINVAL;
		}
	}
#endif

	switch (mt) {
	case NM_MT_CHG_ADM_STATE_ACK:
		return abis_nm_rx_chg_adm_state_ack(mb);
		break;
	case NM_MT_SW_ACT_REQ:
		return abis_nm_rx_sw_act_req(mb);
		break;
	case NM_MT_BS11_LMT_SESSION:
		return abis_nm_rx_lmt_event(mb);
		break;
	}

	return 0;
}

static int abis_nm_rx_ipacc(struct msgb *mb);

static int abis_nm_rcvmsg_manuf(struct msgb *mb)
{
	int rc;
	int bts_type = mb->trx->bts->type;

	switch (bts_type) {
	case GSM_BTS_TYPE_NANOBTS_900:
	case GSM_BTS_TYPE_NANOBTS_1800:
		rc = abis_nm_rx_ipacc(mb);
		break;
	default:
		fprintf(stderr, "don't know how to parse OML for this "
			 "BTS type (%u)\n", bts_type);
		rc = 0;
		break;
	}

	return rc;
}

/* High-Level API */
/* Entry-point where L2 OML from BTS enters the NM code */
int abis_nm_rcvmsg(struct msgb *msg)
{
	struct abis_om_hdr *oh = msgb_l2(msg);
	int rc = 0;

	/* Various consistency checks */
	if (oh->placement != ABIS_OM_PLACEMENT_ONLY) {
		fprintf(stderr, "ABIS OML placement 0x%x not supported\n",
			oh->placement);
		return -EINVAL;
	}
	if (oh->sequence != 0) {
		fprintf(stderr, "ABIS OML sequence 0x%x != 0x00\n",
			oh->sequence);
		return -EINVAL;
	}
#if 0
	unsigned int l2_len = msg->tail - (u_int8_t *)msgb_l2(msg);
	unsigned int hlen = sizeof(*oh) + sizeof(struct abis_om_fom_hdr);
	if (oh->length + hlen > l2_len) {
		fprintf(stderr, "ABIS OML truncated message (%u > %u)\n",
			oh->length + sizeof(*oh), l2_len);
		return -EINVAL;
	}
	if (oh->length + hlen < l2_len)
		fprintf(stderr, "ABIS OML message with extra trailer?!? (oh->len=%d, sizeof_oh=%d l2_len=%d\n", oh->length, sizeof(*oh), l2_len);
#endif
	msg->l3h = (unsigned char *)oh + sizeof(*oh);

	switch (oh->mdisc) {
	case ABIS_OM_MDISC_FOM:
		rc = abis_nm_rcvmsg_fom(msg);
		break;
	case ABIS_OM_MDISC_MANUF:
		rc = abis_nm_rcvmsg_manuf(msg);
		break;
	case ABIS_OM_MDISC_MMI:
	case ABIS_OM_MDISC_TRAU:
		fprintf(stderr, "unimplemented ABIS OML message discriminator 0x%x\n",
			oh->mdisc);
		break;
	default:
		fprintf(stderr, "unknown ABIS OML message discriminator 0x%x\n",
			oh->mdisc);
		return -EINVAL;
	}

	msgb_free(msg);
	return rc;
}

#if 0
/* initialized all resources */
struct abis_nm_h *abis_nm_init(struct abis_nm_cfg *cfg)
{
	struct abis_nm_h *nmh;

	nmh = malloc(sizeof(*nmh));
	if (!nmh)
		return NULL;

	nmh->cfg = cfg;

	return nmh;
}

/* free all resources */
void abis_nm_fini(struct abis_nm_h *nmh)
{
	free(nmh);
}
#endif

/* Here we are trying to define a high-level API that can be used by
 * the actual BSC implementation.  However, the architecture is currently
 * still under design.  Ideally the calls to this API would be synchronous,
 * while the underlying stack behind the APi runs in a traditional select
 * based state machine.
 */

/* 6.2 Software Load: */
enum sw_state {
	SW_STATE_NONE,
	SW_STATE_WAIT_INITACK,
	SW_STATE_WAIT_SEGACK,
	SW_STATE_WAIT_ENDACK,
	SW_STATE_WAIT_ACTACK,
	SW_STATE_ERROR,
};

struct abis_nm_sw {
	struct gsm_bts *bts;
	gsm_cbfn *cbfn;
	void *cb_data;
	int forced;

	/* this will become part of the SW LOAD INITIATE */
	u_int8_t obj_class;
	u_int8_t obj_instance[3];

	u_int8_t file_id[255];
	u_int8_t file_id_len;

	u_int8_t file_version[255];
	u_int8_t file_version_len;

	u_int8_t window_size;
	u_int8_t seg_in_window;

	int fd;
	FILE *stream;
	enum sw_state state;
	int last_seg;
};

static struct abis_nm_sw g_sw;

/* 6.2.1 / 8.3.1: Load Data Initiate */
static int sw_load_init(struct abis_nm_sw *sw)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t len = 3*2 + sw->file_id_len + sw->file_version_len;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, len, NM_MT_LOAD_INIT, sw->obj_class,
			sw->obj_instance[0], sw->obj_instance[1],
			sw->obj_instance[2]);
	
	/* FIXME: this is BS11 specific format */
	msgb_tlv_put(msg, NM_ATT_FILE_ID, sw->file_id_len, sw->file_id);
	msgb_tlv_put(msg, NM_ATT_FILE_VERSION, sw->file_version_len,
		     sw->file_version);
	msgb_tv_put(msg, NM_ATT_WINDOW_SIZE, sw->window_size);
	
	return abis_nm_sendmsg(sw->bts, msg);
}

static int is_last_line(FILE *stream)
{
	char next_seg_buf[256];
	long pos;

	/* check if we're sending the last line */
	pos = ftell(stream);
	if (!fgets(next_seg_buf, sizeof(next_seg_buf)-2, stream)) {
		fseek(stream, pos, SEEK_SET);
		return 1;
	}

	fseek(stream, pos, SEEK_SET);
	return 0;
}

/* 6.2.2 / 8.3.2 Load Data Segment */
static int sw_load_segment(struct abis_nm_sw *sw)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	char seg_buf[256];
	char *line_buf = seg_buf+2;
	unsigned char *tlv;
	u_int8_t len;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);

	switch (sw->bts->type) {
	case GSM_BTS_TYPE_BS11:
		if (fgets(line_buf, sizeof(seg_buf)-2, sw->stream) == NULL) {
			perror("fgets reading segment");
			return -EINVAL;
		}
		seg_buf[0] = 0x00;

		/* check if we're sending the last line */
		sw->last_seg = is_last_line(sw->stream);
		if (sw->last_seg)
			seg_buf[1] = 0;
		else
			seg_buf[1] = 1 + sw->seg_in_window++;

		len = strlen(line_buf) + 2;
		tlv = msgb_put(msg, TLV_GROSS_LEN(len));
		tlv_put(tlv, NM_ATT_BS11_FILE_DATA, len, (u_int8_t *)seg_buf);
		/* BS11 wants CR + LF in excess of the TLV length !?! */
		tlv[1] -= 2;

		/* we only now know the exact length for the OM hdr */
		len = strlen(line_buf)+2;
		break;
	default:
		/* FIXME: Other BTS types */
		return -1;
	}

	fill_om_fom_hdr(oh, len, NM_MT_LOAD_SEG, sw->obj_class,
			sw->obj_instance[0], sw->obj_instance[1],
			sw->obj_instance[2]);

	return abis_nm_sendmsg(sw->bts, msg);
}

/* 6.2.4 / 8.3.4 Load Data End */
static int sw_load_end(struct abis_nm_sw *sw)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t len = 2*2 + sw->file_id_len + sw->file_version_len;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, len, NM_MT_LOAD_END, sw->obj_class,
			sw->obj_instance[0], sw->obj_instance[1],
			sw->obj_instance[2]);

	/* FIXME: this is BS11 specific format */
	msgb_tlv_put(msg, NM_ATT_FILE_ID, sw->file_id_len, sw->file_id);
	msgb_tlv_put(msg, NM_ATT_FILE_VERSION, sw->file_version_len,
		     sw->file_version);

	return abis_nm_sendmsg(sw->bts, msg);
}

/* Activate the specified software into the BTS */
static int sw_activate(struct abis_nm_sw *sw)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t len = 2*2 + sw->file_id_len + sw->file_version_len;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, len, NM_MT_ACTIVATE_SW, sw->obj_class,
			sw->obj_instance[0], sw->obj_instance[1],
			sw->obj_instance[2]);

	/* FIXME: this is BS11 specific format */
	msgb_tlv_put(msg, NM_ATT_FILE_ID, sw->file_id_len, sw->file_id);
	msgb_tlv_put(msg, NM_ATT_FILE_VERSION, sw->file_version_len,
		     sw->file_version);

	return abis_nm_sendmsg(sw->bts, msg);
}

static int sw_open_file(struct abis_nm_sw *sw, const char *fname)
{
	char file_id[12+1];
	char file_version[80+1];
	int rc;

	sw->fd = open(fname, O_RDONLY);
	if (sw->fd < 0)
		return sw->fd;

	switch (sw->bts->type) {
	case GSM_BTS_TYPE_BS11:
		sw->stream = fdopen(sw->fd, "r");
		if (!sw->stream) {
			perror("fdopen");
			return -1;
		}
		/* read first line and parse file ID and VERSION */
		rc = fscanf(sw->stream, "@(#)%12s:%80s\r\n", 
			    file_id, file_version);
		if (rc != 2) {
			perror("parsing header line of software file");
			return -1;
		}
		strcpy((char *)sw->file_id, file_id);
		sw->file_id_len = strlen(file_id);
		strcpy((char *)sw->file_version, file_version);
		sw->file_version_len = strlen(file_version);
		/* rewind to start of file */
		rewind(sw->stream);
		break;	
	default:
		/* We don't know how to treat them yet */
		close(sw->fd);
		return -EINVAL;
	}

	return 0;
}
	
static void sw_close_file(struct abis_nm_sw *sw)
{
	switch (sw->bts->type) {
	case GSM_BTS_TYPE_BS11:
		fclose(sw->stream);
		break;
	default:
		close(sw->fd);
		break;
	}
}

/* Fill the window */
static int sw_fill_window(struct abis_nm_sw *sw)
{
	int rc;

	while (sw->seg_in_window < sw->window_size) {
		rc = sw_load_segment(sw);
		if (rc < 0)
			return rc;
		if (sw->last_seg)
			break;
	}
	return 0;
}

/* callback function from abis_nm_rcvmsg() handler */
static int abis_nm_rcvmsg_sw(struct msgb *mb)
{
	struct abis_om_fom_hdr *foh = msgb_l3(mb);
	int rc = -1;
	struct abis_nm_sw *sw = &g_sw;
	enum sw_state old_state = sw->state;
	
	//DEBUGP(DNM, "state %u, NM MT 0x%02x\n", sw->state, foh->msg_type);

	switch (sw->state) {
	case SW_STATE_WAIT_INITACK:
		switch (foh->msg_type) {
		case NM_MT_LOAD_INIT_ACK:
			/* fill window with segments */
			if (sw->cbfn)
				sw->cbfn(GSM_HOOK_NM_SWLOAD,
					 NM_MT_LOAD_INIT_ACK, mb,
					 sw->cb_data, NULL);
			rc = sw_fill_window(sw);
			sw->state = SW_STATE_WAIT_SEGACK;
			break;
		case NM_MT_LOAD_INIT_NACK:
			if (sw->forced) {
				DEBUGP(DNM, "FORCED: Ignoring Software Load "
					"Init NACK\n");
				if (sw->cbfn)
					sw->cbfn(GSM_HOOK_NM_SWLOAD,
						 NM_MT_LOAD_INIT_ACK, mb,
						 sw->cb_data, NULL);
				rc = sw_fill_window(sw);
				sw->state = SW_STATE_WAIT_SEGACK;
			} else {
				DEBUGP(DNM, "Software Load Init NACK\n");
				/* FIXME: cause */
				if (sw->cbfn)
					sw->cbfn(GSM_HOOK_NM_SWLOAD,
						 NM_MT_LOAD_INIT_NACK, mb,
						 sw->cb_data, NULL);
				sw->state = SW_STATE_ERROR;
			}
			break;
		}
		break;
	case SW_STATE_WAIT_SEGACK:
		switch (foh->msg_type) {
		case NM_MT_LOAD_SEG_ACK:
			if (sw->cbfn)
				sw->cbfn(GSM_HOOK_NM_SWLOAD,
					 NM_MT_LOAD_SEG_ACK, mb,
					 sw->cb_data, NULL);
			sw->seg_in_window = 0;
			if (!sw->last_seg) {
				/* fill window with more segments */
				rc = sw_fill_window(sw);
				sw->state = SW_STATE_WAIT_SEGACK;
			} else {
				/* end the transfer */
				sw->state = SW_STATE_WAIT_ENDACK;
				rc = sw_load_end(sw);
			}
			break;
		}
		break;
	case SW_STATE_WAIT_ENDACK:
		switch (foh->msg_type) {
		case NM_MT_LOAD_END_ACK:
			sw_close_file(sw);
			DEBUGP(DNM, "Software Load End (BTS %u)\n",
				sw->bts->nr);
			sw->state = SW_STATE_NONE;
			if (sw->cbfn)
				sw->cbfn(GSM_HOOK_NM_SWLOAD,
					 NM_MT_LOAD_END_ACK, mb,
					 sw->cb_data, NULL);
			break;
		case NM_MT_LOAD_END_NACK:
			if (sw->forced) {
				DEBUGP(DNM, "FORCED: Ignoring Software Load"
					"End NACK\n");
				sw->state = SW_STATE_NONE;
				if (sw->cbfn)
					sw->cbfn(GSM_HOOK_NM_SWLOAD,
						 NM_MT_LOAD_END_ACK, mb,
						 sw->cb_data, NULL);
			} else {
				DEBUGP(DNM, "Software Load End NACK\n");
				/* FIXME: cause */
				sw->state = SW_STATE_ERROR;
				if (sw->cbfn)
					sw->cbfn(GSM_HOOK_NM_SWLOAD,
						 NM_MT_LOAD_END_NACK, mb,
						 sw->cb_data, NULL);
			}
			break;
		}
	case SW_STATE_WAIT_ACTACK:
		switch (foh->msg_type) {
		case NM_MT_ACTIVATE_SW_ACK:
			/* we're done */
			DEBUGP(DNM, "Activate Software DONE!\n");
			sw->state = SW_STATE_NONE;
			rc = 0;
			if (sw->cbfn)
				sw->cbfn(GSM_HOOK_NM_SWLOAD,
					 NM_MT_ACTIVATE_SW_ACK, mb,
					 sw->cb_data, NULL);
			break;
		case NM_MT_ACTIVATE_SW_NACK:
			DEBUGP(DNM, "Activate Software NACK\n");
			/* FIXME: cause */
			sw->state = SW_STATE_ERROR;
			if (sw->cbfn)
				sw->cbfn(GSM_HOOK_NM_SWLOAD,
					 NM_MT_ACTIVATE_SW_NACK, mb,
					 sw->cb_data, NULL);
			break;
		}
	case SW_STATE_NONE:
		switch (foh->msg_type) {
		case NM_MT_ACTIVATE_SW_ACK:
			rc = 0;
			break;
		}
		break;
	case SW_STATE_ERROR:
		break;
	}

	if (rc)
		DEBUGP(DNM, "unexpected NM MT 0x%02x in state %u -> %u\n",
			foh->msg_type, old_state, sw->state);

	return rc;
}

/* Load the specified software into the BTS */
int abis_nm_software_load(struct gsm_bts *bts, const char *fname,
			  u_int8_t win_size, int forced,
			  gsm_cbfn *cbfn, void *cb_data)
{
	struct abis_nm_sw *sw = &g_sw;
	int rc;

	DEBUGP(DNM, "Software Load (BTS %u, File \"%s\")\n",
		bts->nr, fname);

	if (sw->state != SW_STATE_NONE)
		return -EBUSY;

	sw->bts = bts;
	sw->obj_class = NM_OC_SITE_MANAGER;
	sw->obj_instance[0] = 0xff;
	sw->obj_instance[1] = 0xff;
	sw->obj_instance[2] = 0xff;
	sw->window_size = win_size;
	sw->state = SW_STATE_WAIT_INITACK;
	sw->cbfn = cbfn;
	sw->cb_data = cb_data;
	sw->forced = forced;

	rc = sw_open_file(sw, fname);
	if (rc < 0) {
		sw->state = SW_STATE_NONE;
		return rc;
	}

	return sw_load_init(sw);
}

int abis_nm_software_load_status(struct gsm_bts *bts)
{
	struct abis_nm_sw *sw = &g_sw;
	struct stat st;
	int rc, percent;

	rc = fstat(sw->fd, &st);
	if (rc < 0) {
		perror("ERROR during stat");
		return rc;
	}

	percent = (ftell(sw->stream) * 100) / st.st_size;
	return percent;
}

/* Activate the specified software into the BTS */
int abis_nm_software_activate(struct gsm_bts *bts, const char *fname,
			      gsm_cbfn *cbfn, void *cb_data)
{
	struct abis_nm_sw *sw = &g_sw;
	int rc;

	DEBUGP(DNM, "Activating Software (BTS %u, File \"%s\")\n",
		bts->nr, fname);

	if (sw->state != SW_STATE_NONE)
		return -EBUSY;

	sw->bts = bts;
	sw->obj_class = NM_OC_SITE_MANAGER;
	sw->obj_instance[0] = 0xff;
	sw->obj_instance[1] = 0xff;
	sw->obj_instance[2] = 0xff;
	sw->state = SW_STATE_WAIT_ACTACK;
	sw->cbfn = cbfn;
	sw->cb_data = cb_data;

	/* Open the file in order to fill some sw struct members */
	rc = sw_open_file(sw, fname);
	if (rc < 0) {
		sw->state = SW_STATE_NONE;
		return rc;
	}
	sw_close_file(sw);

	return sw_activate(sw);
}

static void fill_nm_channel(struct abis_nm_channel *ch, u_int8_t bts_port,
		       u_int8_t ts_nr, u_int8_t subslot_nr)
{
	ch->attrib = NM_ATT_ABIS_CHANNEL;
	ch->bts_port = bts_port;
	ch->timeslot = ts_nr;
	ch->subslot = subslot_nr;	
}

int abis_nm_establish_tei(struct gsm_bts *bts, u_int8_t trx_nr,
			  u_int8_t e1_port, u_int8_t e1_timeslot, u_int8_t e1_subslot,
			  u_int8_t tei)
{
	struct abis_om_hdr *oh;
	struct abis_nm_channel *ch;
	u_int8_t len = sizeof(*ch) + 2;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, len, NM_MT_ESTABLISH_TEI, NM_OC_RADIO_CARRIER,
			bts->bts_nr, trx_nr, 0xff);
	
	msgb_tv_put(msg, NM_ATT_TEI, tei);

	ch = (struct abis_nm_channel *) msgb_put(msg, sizeof(*ch));
	fill_nm_channel(ch, e1_port, e1_timeslot, e1_subslot);

	return abis_nm_sendmsg(bts, msg);
}

/* connect signalling of one (BTS,TRX) to a particular timeslot on the E1 */
int abis_nm_conn_terr_sign(struct gsm_bts_trx *trx,
			   u_int8_t e1_port, u_int8_t e1_timeslot, u_int8_t e1_subslot)
{
	struct gsm_bts *bts = trx->bts;
	struct abis_om_hdr *oh;
	struct abis_nm_channel *ch;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, sizeof(*ch), NM_MT_CONN_TERR_SIGN,
			NM_OC_RADIO_CARRIER, bts->bts_nr, trx->nr, 0xff);
	
	ch = (struct abis_nm_channel *) msgb_put(msg, sizeof(*ch));
	fill_nm_channel(ch, e1_port, e1_timeslot, e1_subslot);

	return abis_nm_sendmsg(bts, msg);
}

#if 0
int abis_nm_disc_terr_sign(struct abis_nm_h *h, struct abis_om_obj_inst *inst,
			   struct abis_nm_abis_channel *chan)
{
}
#endif

int abis_nm_conn_terr_traf(struct gsm_bts_trx_ts *ts,
			   u_int8_t e1_port, u_int8_t e1_timeslot,
			   u_int8_t e1_subslot)
{
	struct gsm_bts *bts = ts->trx->bts;
	struct abis_om_hdr *oh;
	struct abis_nm_channel *ch;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, sizeof(*ch), NM_MT_CONN_TERR_TRAF,
			NM_OC_CHANNEL, bts->bts_nr, ts->trx->nr, ts->nr);

	ch = (struct abis_nm_channel *) msgb_put(msg, sizeof(*ch));
	fill_nm_channel(ch, e1_port, e1_timeslot, e1_subslot);

	DEBUGP(DNM, "CONNECT TERR TRAF Um=%s E1=(%u,%u,%u)\n",
		gsm_ts_name(ts),
		e1_port, e1_timeslot, e1_subslot);

	return abis_nm_sendmsg(bts, msg);
}

#if 0
int abis_nm_disc_terr_traf(struct abis_nm_h *h, struct abis_om_obj_inst *inst,
			   struct abis_nm_abis_channel *chan,
			   u_int8_t subchan)
{
}
#endif

/* Chapter 8.6.1 */
int abis_nm_set_bts_attr(struct gsm_bts *bts, u_int8_t *attr, int attr_len)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t *cur;

	DEBUGP(DNM, "Set BTS Attr (bts=%d)\n", bts->nr);

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, attr_len, NM_MT_SET_BTS_ATTR, NM_OC_BTS, bts->bts_nr, 0xff, 0xff);
	cur = msgb_put(msg, attr_len);
	memcpy(cur, attr, attr_len);

	return abis_nm_sendmsg(bts, msg);
}

/* Chapter 8.6.2 */
int abis_nm_set_radio_attr(struct gsm_bts_trx *trx, u_int8_t *attr, int attr_len)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t *cur;

	DEBUGP(DNM, "Set TRX Attr (bts=%d,trx=%d)\n", trx->bts->nr, trx->nr);

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, attr_len, NM_MT_SET_RADIO_ATTR, NM_OC_RADIO_CARRIER,
			trx->bts->bts_nr, trx->nr, 0xff);
	cur = msgb_put(msg, attr_len);
	memcpy(cur, attr, attr_len);

	return abis_nm_sendmsg(trx->bts, msg);
}

/* Chapter 8.6.3 */
int abis_nm_set_channel_attr(struct gsm_bts_trx_ts *ts, u_int8_t chan_comb)
{
	struct gsm_bts *bts = ts->trx->bts;
	struct abis_om_hdr *oh;
	u_int16_t arfcn = htons(ts->trx->arfcn);
	u_int8_t zero = 0x00;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t len = 2 + 2;

	if (bts->type == GSM_BTS_TYPE_BS11)
		len += 4 + 2 + 2 + 3;

	DEBUGP(DNM, "Set Chan Attr %s\n", gsm_ts_name(ts));

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, len, NM_MT_SET_CHAN_ATTR,
			NM_OC_CHANNEL, bts->bts_nr,
			ts->trx->nr, ts->nr);
	/* FIXME: don't send ARFCN list, hopping sequence, mAIO, ...*/
	if (bts->type == GSM_BTS_TYPE_BS11)
		msgb_tlv16_put(msg, NM_ATT_ARFCN_LIST, 1, &arfcn);
	msgb_tv_put(msg, NM_ATT_CHAN_COMB, chan_comb);
	if (bts->type == GSM_BTS_TYPE_BS11) {
		msgb_tv_put(msg, NM_ATT_HSN, 0x00);
		msgb_tv_put(msg, NM_ATT_MAIO, 0x00);
	}
	msgb_tv_put(msg, NM_ATT_TSC, bts->tsc);	/* training sequence */
	if (bts->type == GSM_BTS_TYPE_BS11)
		msgb_tlv_put(msg, 0x59, 1, &zero);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_sw_act_req_ack(struct gsm_bts *bts, u_int8_t obj_class, u_int8_t i1,
			u_int8_t i2, u_int8_t i3, int nack, u_int8_t *attr, int att_len)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t msgtype = NM_MT_SW_ACT_REQ_ACK;
	u_int8_t len = att_len;

	if (nack) {
		len += 2;
		msgtype = NM_MT_SW_ACT_REQ_NACK;
	}

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, att_len, msgtype, obj_class, i1, i2, i3);

	if (attr) {
		u_int8_t *ptr = msgb_put(msg, att_len);
		memcpy(ptr, attr, att_len);
	}
	if (nack)
		msgb_tv_put(msg, NM_ATT_NACK_CAUSES, NM_NACK_OBJCLASS_NOTSUPP);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_raw_msg(struct gsm_bts *bts, int len, u_int8_t *rawmsg)
{
	struct msgb *msg = nm_msgb_alloc();
	struct abis_om_hdr *oh;
	u_int8_t *data;

	oh = (struct abis_om_hdr *) msgb_put(msg, sizeof(*oh));
	fill_om_hdr(oh, len);
	data = msgb_put(msg, len);
	memcpy(data, rawmsg, len);

	return abis_nm_sendmsg(bts, msg);
}

/* Siemens specific commands */
static int __simple_cmd(struct gsm_bts *bts, u_int8_t msg_type)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 0, msg_type, NM_OC_SITE_MANAGER,
			0xff, 0xff, 0xff);

	return abis_nm_sendmsg(bts, msg);
}

/* Chapter 8.9.2 */
int abis_nm_opstart(struct gsm_bts *bts, u_int8_t obj_class, u_int8_t i0, u_int8_t i1, u_int8_t i2)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	DEBUGP(DNM, "Sending OPSTART obj_class=0x%02x obj_inst=(0x%02x, 0x%02x, 0x%02x)\n",
		obj_class, i0, i1, i2);
	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 0, NM_MT_OPSTART, obj_class, i0, i1, i2);

	return abis_nm_sendmsg(bts, msg);
}

/* Chapter 8.8.5 */
int abis_nm_chg_adm_state(struct gsm_bts *bts, u_int8_t obj_class, u_int8_t i0,
			  u_int8_t i1, u_int8_t i2, u_int8_t adm_state)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 2, NM_MT_CHG_ADM_STATE, obj_class, i0, i1, i2);
	msgb_tv_put(msg, NM_ATT_ADM_STATE, adm_state);

	return abis_nm_sendmsg(bts, msg);
}


int abis_nm_event_reports(struct gsm_bts *bts, int on)
{
	if (on == 0)
		return __simple_cmd(bts, NM_MT_STOP_EVENT_REP);
	else
		return __simple_cmd(bts, NM_MT_REST_EVENT_REP);
}

/* Siemens (or BS-11) specific commands */

int abis_nm_bs11_bsc_disconnect(struct gsm_bts *bts, int reconnect)
{
	if (reconnect == 0)
		return __simple_cmd(bts, NM_MT_BS11_DISCONNECT);
	else
		return __simple_cmd(bts, NM_MT_BS11_RECONNECT);
}

int abis_nm_bs11_restart(struct gsm_bts *bts)
{
	return __simple_cmd(bts, NM_MT_BS11_RESTART);
}


struct bs11_date_time {
	u_int16_t	year;
	u_int8_t	month;
	u_int8_t	day;
	u_int8_t	hour;
	u_int8_t	min;
	u_int8_t	sec;
} __attribute__((packed));


void get_bs11_date_time(struct bs11_date_time *aet)
{
	time_t t;
	struct tm *tm;

	t = time(NULL);
	tm = localtime(&t);
	aet->sec = tm->tm_sec;
	aet->min = tm->tm_min;
	aet->hour = tm->tm_hour;
	aet->day = tm->tm_mday;
	aet->month = tm->tm_mon;
	aet->year = htons(1900 + tm->tm_year);
}

int abis_nm_bs11_reset_resource(struct gsm_bts *bts)
{
	return __simple_cmd(bts, NM_MT_BS11_RESET_RESOURCE);
}

int abis_nm_bs11_db_transmission(struct gsm_bts *bts, int begin)
{
	if (begin)
		return __simple_cmd(bts, NM_MT_BS11_BEGIN_DB_TX);
	else
		return __simple_cmd(bts, NM_MT_BS11_END_DB_TX);
}

int abis_nm_bs11_create_object(struct gsm_bts *bts,
				enum abis_bs11_objtype type, u_int8_t idx,
				u_int8_t attr_len, const u_int8_t *attr)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t *cur;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, attr_len, NM_MT_BS11_CREATE_OBJ,
			NM_OC_BS11, type, 0, idx);
	cur = msgb_put(msg, attr_len);
	memcpy(cur, attr, attr_len);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_delete_object(struct gsm_bts *bts,
				enum abis_bs11_objtype type, u_int8_t idx)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 0, NM_MT_BS11_DELETE_OBJ,
			NM_OC_BS11, type, 0, idx);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_create_envaBTSE(struct gsm_bts *bts, u_int8_t idx)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t zero = 0x00;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 3, NM_MT_BS11_CREATE_OBJ,
			NM_OC_BS11_ENVABTSE, 0, idx, 0xff);
	msgb_tlv_put(msg, 0x99, 1, &zero);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_create_bport(struct gsm_bts *bts, u_int8_t idx)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 0, NM_MT_BS11_CREATE_OBJ, NM_OC_BS11_BPORT,
			idx, 0, 0);

	return abis_nm_sendmsg(bts, msg);
}

static const u_int8_t sm_attr[] = { NM_ATT_TEI, NM_ATT_ABIS_CHANNEL };
int abis_nm_bs11_get_oml_tei_ts(struct gsm_bts *bts)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 2+sizeof(sm_attr), NM_MT_GET_ATTR, NM_OC_SITE_MANAGER,
			0xff, 0xff, 0xff);
	msgb_tlv_put(msg, NM_ATT_LIST_REQ_ATTR, sizeof(sm_attr), sm_attr);

	return abis_nm_sendmsg(bts, msg);
}

/* like abis_nm_conn_terr_traf + set_tei */
int abis_nm_bs11_conn_oml_tei(struct gsm_bts *bts, u_int8_t e1_port, 
			  u_int8_t e1_timeslot, u_int8_t e1_subslot,
			  u_int8_t tei)
{
	struct abis_om_hdr *oh;
	struct abis_nm_channel *ch;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, sizeof(*ch)+2, NM_MT_BS11_SET_ATTR,
			NM_OC_SITE_MANAGER, 0xff, 0xff, 0xff);

	ch = (struct abis_nm_channel *) msgb_put(msg, sizeof(*ch));
	fill_nm_channel(ch, e1_port, e1_timeslot, e1_subslot);
	msgb_tv_put(msg, NM_ATT_TEI, tei);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_set_trx_power(struct gsm_bts_trx *trx, u_int8_t level)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 3, NM_MT_BS11_SET_ATTR,
			NM_OC_BS11, BS11_OBJ_PA, 0x00, trx->nr);
	msgb_tlv_put(msg, NM_ATT_BS11_TXPWR, 1, &level);

	return abis_nm_sendmsg(trx->bts, msg);
}

int abis_nm_bs11_get_trx_power(struct gsm_bts_trx *trx)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t attr = NM_ATT_BS11_TXPWR;

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 2+sizeof(attr), NM_MT_GET_ATTR,
			NM_OC_BS11, BS11_OBJ_PA, 0x00, trx->nr);
	msgb_tlv_put(msg, NM_ATT_LIST_REQ_ATTR, sizeof(attr), &attr);

	return abis_nm_sendmsg(trx->bts, msg);
}

int abis_nm_bs11_get_pll_mode(struct gsm_bts *bts)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t attr[] = { NM_ATT_BS11_PLL_MODE };

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 2+sizeof(attr), NM_MT_GET_ATTR,
			NM_OC_BS11, BS11_OBJ_LI, 0x00, 0x00);
	msgb_tlv_put(msg, NM_ATT_LIST_REQ_ATTR, sizeof(attr), attr);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_get_cclk(struct gsm_bts *bts)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	u_int8_t attr[] = { NM_ATT_BS11_CCLK_ACCURACY,
			    NM_ATT_BS11_CCLK_TYPE };

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 2+sizeof(attr), NM_MT_GET_ATTR,
			NM_OC_BS11, BS11_OBJ_CCLK, 0x00, 0x00);
	msgb_tlv_put(msg, NM_ATT_LIST_REQ_ATTR, sizeof(attr), attr);

	return abis_nm_sendmsg(bts, msg);

}

//static const u_int8_t bs11_logon_c7[] = { 0x07, 0xd9, 0x01, 0x11, 0x0d, 0x10, 0x20 };
static const u_int8_t bs11_logon_c8[] = { 0x02 };
static const u_int8_t bs11_logon_c9[] = "FACTORY";

int abis_nm_bs11_factory_logon(struct gsm_bts *bts, int on)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	struct bs11_date_time bdt;

	get_bs11_date_time(&bdt);

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	if (on) {
		u_int8_t len = 3*2 + sizeof(bdt)
				+ sizeof(bs11_logon_c8) + sizeof(bs11_logon_c9);
		fill_om_fom_hdr(oh, len, NM_MT_BS11_LMT_LOGON,
				NM_OC_BS11_BTSE, 0xff, 0xff, 0xff);
		msgb_tlv_put(msg, NM_ATT_BS11_LMT_LOGIN_TIME,
			     sizeof(bdt), (u_int8_t *) &bdt);
		msgb_tlv_put(msg, NM_ATT_BS11_LMT_USER_ACC_LEV,
			     sizeof(bs11_logon_c8), bs11_logon_c8);
		msgb_tlv_put(msg, NM_ATT_BS11_LMT_USER_NAME,
			     sizeof(bs11_logon_c9), bs11_logon_c9);
	} else {
		fill_om_fom_hdr(oh, 0, NM_MT_BS11_LMT_LOGOFF,
				NM_OC_BS11_BTSE, 0xff, 0xff, 0xff);
	}
	
	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_set_trx1_pw(struct gsm_bts *bts, const char *password)
{
	struct abis_om_hdr *oh;
	struct msgb *msg;

	if (strlen(password) != 10)
		return -EINVAL;

 	msg = nm_msgb_alloc();
	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 2+strlen(password), NM_MT_BS11_SET_ATTR,
			NM_OC_BS11, BS11_OBJ_TRX1, 0x00, 0x00);
	msgb_tlv_put(msg, NM_ATT_BS11_PASSWORD, 10, (const u_int8_t *)password);

	return abis_nm_sendmsg(bts, msg);
}

/* change the BS-11 PLL Mode to either locked (E1 derived) or standalone */
int abis_nm_bs11_set_pll_locked(struct gsm_bts *bts, int locked)
{
	struct abis_om_hdr *oh;
	struct msgb *msg;
	u_int8_t tlv_value;
	
	msg = nm_msgb_alloc();
	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	fill_om_fom_hdr(oh, 3, NM_MT_BS11_SET_ATTR, NM_OC_BS11,
			BS11_OBJ_LI, 0x00, 0x00);

	if (locked)
		tlv_value = BS11_LI_PLL_LOCKED;
	else
		tlv_value = BS11_LI_PLL_STANDALONE;
	
	msgb_tlv_put(msg, NM_ATT_BS11_PLL_MODE, 1, &tlv_value);
	
	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_get_state(struct gsm_bts *bts)
{
	return __simple_cmd(bts, NM_MT_BS11_GET_STATE);
}

/* BS11 SWL */

static void *tall_fle_ctx;

struct abis_nm_bs11_sw {
	struct gsm_bts *bts;
	char swl_fname[PATH_MAX];
	u_int8_t win_size;
	int forced;
	struct llist_head file_list;
	gsm_cbfn *user_cb;	/* specified by the user */
};
static struct abis_nm_bs11_sw _g_bs11_sw, *g_bs11_sw = &_g_bs11_sw;

struct file_list_entry {
	struct llist_head list;
	char fname[PATH_MAX];
};

struct file_list_entry *fl_dequeue(struct llist_head *queue)
{
	struct llist_head *lh;

	if (llist_empty(queue))
		return NULL;

	lh = queue->next;
	llist_del(lh);
	
	return llist_entry(lh, struct file_list_entry, list);
}

static int bs11_read_swl_file(struct abis_nm_bs11_sw *bs11_sw)
{
	char linebuf[255];
	struct llist_head *lh, *lh2;
	FILE *swl;
	int rc = 0;

	swl = fopen(bs11_sw->swl_fname, "r");
	if (!swl)
		return -ENODEV;

	/* zero the stale file list, if any */
	llist_for_each_safe(lh, lh2, &bs11_sw->file_list) {
		llist_del(lh);
		talloc_free(lh);
	}

	while (fgets(linebuf, sizeof(linebuf), swl)) {
		char file_id[12+1];
		char file_version[80+1];
		struct file_list_entry *fle;
		static char dir[PATH_MAX];

		if (strlen(linebuf) < 4)
			continue;
	
		rc = sscanf(linebuf+4, "%12s:%80s\r\n", file_id, file_version);
		if (rc < 0) {
			perror("ERR parsing SWL file");
			rc = -EINVAL;
			goto out;
		}
		if (rc < 2)
			continue;

		fle = talloc_zero(tall_fle_ctx, struct file_list_entry);
		if (!fle) {
			rc = -ENOMEM;
			goto out;
		}

		/* construct new filename */
		strncpy(dir, bs11_sw->swl_fname, sizeof(dir));
		strncat(fle->fname, dirname(dir), sizeof(fle->fname) - 1);
		strcat(fle->fname, "/");
		strncat(fle->fname, file_id, sizeof(fle->fname) - 1 -strlen(fle->fname));
		
		llist_add_tail(&fle->list, &bs11_sw->file_list);
	}

out:
	fclose(swl);
	return rc;
}

/* bs11 swload specific callback, passed to abis_nm core swload */
static int bs11_swload_cbfn(unsigned int hook, unsigned int event,
			    struct msgb *msg, void *data, void *param)
{
	struct abis_nm_bs11_sw *bs11_sw = data;
	struct file_list_entry *fle;
	int rc = 0;

	switch (event) {
	case NM_MT_LOAD_END_ACK:
		fle = fl_dequeue(&bs11_sw->file_list);
		if (fle) {
			/* start download the next file of our file list */
			rc = abis_nm_software_load(bs11_sw->bts, fle->fname,
						   bs11_sw->win_size,
						   bs11_sw->forced,
						   &bs11_swload_cbfn, bs11_sw);
			free(fle);
		} else {
			/* activate the SWL */
			rc = abis_nm_software_activate(bs11_sw->bts,
							bs11_sw->swl_fname,
							bs11_swload_cbfn,
							bs11_sw);
		}
		break;
	case NM_MT_LOAD_SEG_ACK:
	case NM_MT_LOAD_END_NACK:
	case NM_MT_LOAD_INIT_ACK:
	case NM_MT_LOAD_INIT_NACK:
	case NM_MT_ACTIVATE_SW_NACK:
	case NM_MT_ACTIVATE_SW_ACK:
	default:
		/* fallthrough to the user callback */
		if (bs11_sw->user_cb)
			rc = bs11_sw->user_cb(hook, event, msg, NULL, NULL);
		break;
	}

	return rc;
}

/* Siemens provides a SWL file that is a mere listing of all the other
 * files that are part of a software release.  We need to upload first
 * the list file, and then each file that is listed in the list file */
int abis_nm_bs11_load_swl(struct gsm_bts *bts, const char *fname,
			  u_int8_t win_size, int forced, gsm_cbfn *cbfn)
{
	struct abis_nm_bs11_sw *bs11_sw = g_bs11_sw;
	struct file_list_entry *fle;
	int rc = 0;

	INIT_LLIST_HEAD(&bs11_sw->file_list);
	bs11_sw->bts = bts;
	bs11_sw->win_size = win_size;
	bs11_sw->user_cb = cbfn;
	bs11_sw->forced = forced;

	strncpy(bs11_sw->swl_fname, fname, sizeof(bs11_sw->swl_fname));
	rc = bs11_read_swl_file(bs11_sw);
	if (rc < 0)
		return rc;

	/* dequeue next item in file list */
	fle = fl_dequeue(&bs11_sw->file_list);
	if (!fle)
		return -EINVAL;

	/* start download the next file of our file list */
	rc = abis_nm_software_load(bts, fle->fname, win_size, forced,
				   bs11_swload_cbfn, bs11_sw);
	free(fle);
	return rc;
}

#if 0
static u_int8_t req_attr_btse[] = {
	NM_ATT_ADM_STATE, NM_ATT_BS11_LMT_LOGON_SESSION,
	NM_ATT_BS11_LMT_LOGIN_TIME, NM_ATT_BS11_LMT_USER_ACC_LEV,
	NM_ATT_BS11_LMT_USER_NAME,

	0xaf, NM_ATT_BS11_RX_OFFSET, NM_ATT_BS11_VENDOR_NAME,

	NM_ATT_BS11_SW_LOAD_INTENDED, NM_ATT_BS11_SW_LOAD_SAFETY,

	NM_ATT_BS11_SW_LOAD_STORED };

static u_int8_t req_attr_btsm[] = {
	NM_ATT_ABIS_CHANNEL, NM_ATT_TEI, NM_ATT_BS11_ABIS_EXT_TIME,
	NM_ATT_ADM_STATE, NM_ATT_AVAIL_STATUS, 0xce, NM_ATT_FILE_ID,
	NM_ATT_FILE_VERSION, NM_ATT_OPER_STATE, 0xe8, NM_ATT_BS11_ALL_TEST_CATG,
	NM_ATT_SW_DESCR, NM_ATT_GET_ARI };
#endif
	
static u_int8_t req_attr[] = { 
	NM_ATT_ADM_STATE, NM_ATT_AVAIL_STATUS, 0xa8, NM_ATT_OPER_STATE,
	0xd5, 0xa1, NM_ATT_BS11_ESN_FW_CODE_NO, NM_ATT_BS11_ESN_HW_CODE_NO,
	0x42, NM_ATT_BS11_ESN_PCB_SERIAL, NM_ATT_BS11_PLL };

int abis_nm_bs11_get_serno(struct gsm_bts *bts)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();

	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	/* SiemensHW CCTRL object */
	fill_om_fom_hdr(oh, 2+sizeof(req_attr), NM_MT_GET_ATTR, NM_OC_BS11,
			0x03, 0x00, 0x00);
	msgb_tlv_put(msg, NM_ATT_LIST_REQ_ATTR, sizeof(req_attr), req_attr);

	return abis_nm_sendmsg(bts, msg);
}

int abis_nm_bs11_set_ext_time(struct gsm_bts *bts)
{
	struct abis_om_hdr *oh;
	struct msgb *msg = nm_msgb_alloc();
	struct bs11_date_time aet;

	get_bs11_date_time(&aet);
	oh = (struct abis_om_hdr *) msgb_put(msg, ABIS_OM_FOM_HDR_SIZE);
	/* SiemensHW CCTRL object */
	fill_om_fom_hdr(oh, 2+sizeof(aet), NM_MT_BS11_SET_ATTR, NM_OC_SITE_MANAGER,
			0xff, 0xff, 0xff);
	msgb_tlv_put(msg, NM_ATT_BS11_ABIS_EXT_TIME, sizeof(aet), (u_int8_t *) &aet);

	return abis_nm_sendmsg(bts, msg);
}

/* ip.access nanoBTS specific commands */
static const char ipaccess_magic[] = "com.ipaccess";


static int abis_nm_rx_ipacc(struct msgb *msg)
{
	struct abis_om_hdr *oh = msgb_l2(msg);
	struct abis_om_fom_hdr *foh;
	u_int8_t idstrlen = oh->data[0];
	struct tlv_parsed tp;

	if (strncmp((char *)&oh->data[1], ipaccess_magic, idstrlen)) {
		DEBUGP(DNM, "id string is not com.ipaccess !?!\n");
		return -EINVAL;
	}

	foh = (struct abis_om_fom_hdr *) (oh->data + 1 + idstrlen);
	abis_nm_tlv_parse(&tp, foh->data, oh->length-sizeof(*foh));

	DEBUGP(DNM, "IPACCESS(0x%02x): ", foh->msg_type);

	switch (foh->msg_type) {
	case NM_MT_IPACC_RSL_CONNECT_ACK:
		DEBUGPC(DNM, "RSL CONNECT ACK ");
		if (TLVP_PRESENT(&tp, NM_ATT_IPACC_DST_IP))
			DEBUGPC(DNM, "IP=%s ",
				inet_ntoa(*((struct in_addr *) 
					TLVP_VAL(&tp, NM_ATT_IPACC_DST_IP))));
		if (TLVP_PRESENT(&tp, NM_ATT_IPACC_DST_IP_PORT))
			DEBUGPC(DNM, "PORT=%u ",
				ntohs(*((u_int16_t *) 
					TLVP_VAL(&tp, NM_ATT_IPACC_DST_IP_PORT))));
		DEBUGPC(DNM, "\n");
		break;
	case NM_MT_IPACC_RSL_CONNECT_NACK:
		DEBUGPC(DNM, "RSL CONNECT NACK ");
		if (TLVP_PRESENT(&tp, NM_ATT_NACK_CAUSES))
			DEBUGPC(DNM, " CAUSE=%s\n", 
				nack_cause_name(*TLVP_VAL(&tp, NM_ATT_NACK_CAUSES)));
		else
			DEBUGPC(DNM, "\n");
		break;
	case NM_MT_IPACC_SET_NVATTR_ACK:
		DEBUGPC(DNM, "SET NVATTR ACK\n");
		/* FIXME: decode and show the actual attributes */
		break;
	case NM_MT_IPACC_SET_NVATTR_NACK:
		DEBUGPC(DNM, "SET NVATTR NACK ");
		if (TLVP_PRESENT(&tp, NM_ATT_NACK_CAUSES))
			DEBUGPC(DNM, " CAUSE=%s\n", 
				nack_cause_name(*TLVP_VAL(&tp, NM_ATT_NACK_CAUSES)));
		else
			DEBUGPC(DNM, "\n");
		break;
	case NM_MT_IPACC_GET_NVATTR_ACK:
		DEBUGPC(DNM, "GET NVATTR ACK\n");
		/* FIXME: decode and show the actual attributes */
		break;
	case NM_MT_IPACC_GET_NVATTR_NACK:
		DEBUGPC(DNM, "GET NVATTR NACK ");
		if (TLVP_PRESENT(&tp, NM_ATT_NACK_CAUSES))
			DEBUGPC(DNM, " CAUSE=%s\n", 
				nack_cause_name(*TLVP_VAL(&tp, NM_ATT_NACK_CAUSES)));
		else
			DEBUGPC(DNM, "\n");
		break;
	default:
		DEBUGPC(DNM, "unknown\n");
		break;
	}

	/* signal handling */
	switch  (foh->msg_type) {
	case NM_MT_IPACC_RSL_CONNECT_NACK:
	case NM_MT_IPACC_SET_NVATTR_NACK:
	case NM_MT_IPACC_GET_NVATTR_NACK:
		dispatch_signal(SS_NM, S_NM_IPACC_NACK, (void*) ((long)foh->msg_type));
		break;
	default:
		break;
	}

	return 0;
}

/* send an ip-access manufacturer specific message */
int abis_nm_ipaccess_msg(struct gsm_bts *bts, u_int8_t msg_type,
			 u_int8_t obj_class, u_int8_t bts_nr,
			 u_int8_t trx_nr, u_int8_t ts_nr,
			 u_int8_t *attr, int attr_len)
{
	struct msgb *msg = nm_msgb_alloc();
	struct abis_om_hdr *oh;
	struct abis_om_fom_hdr *foh;
	u_int8_t *data;

	/* construct the 12.21 OM header, observe the erroneous length */
	oh = (struct abis_om_hdr *) msgb_put(msg, sizeof(*oh));
	fill_om_hdr(oh, sizeof(*foh) + attr_len);
	oh->mdisc = ABIS_OM_MDISC_MANUF;

	/* add the ip.access magic */
	data = msgb_put(msg, sizeof(ipaccess_magic)+1);
	*data++ = sizeof(ipaccess_magic);
	memcpy(data, ipaccess_magic, sizeof(ipaccess_magic));

	/* fill the 12.21 FOM header */
	foh = (struct abis_om_fom_hdr *) msgb_put(msg, sizeof(*foh));
	foh->msg_type = msg_type;
	foh->obj_class = obj_class;
	foh->obj_inst.bts_nr = bts_nr;
	foh->obj_inst.trx_nr = trx_nr;
	foh->obj_inst.ts_nr = ts_nr;

	if (attr && attr_len) {
		data = msgb_put(msg, attr_len);
		memcpy(data, attr, attr_len);
	}

	return abis_nm_sendmsg(bts, msg);
}

/* set some attributes in NVRAM */
int abis_nm_ipaccess_set_nvattr(struct gsm_bts *bts, u_int8_t *attr,
				int attr_len)
{
	return abis_nm_ipaccess_msg(bts, NM_MT_IPACC_SET_NVATTR,
				    NM_OC_BASEB_TRANSC, 0, 0, 0xff, attr,
				    attr_len);
}

/* restart / reboot an ip.access nanoBTS */
int abis_nm_ipaccess_restart(struct gsm_bts *bts)
{
	return __simple_cmd(bts, NM_MT_IPACC_RESTART);
}


static __attribute__((constructor)) void on_dso_load_abis_nm(void)
{
	tall_fle_ctx = talloc_named_const(tall_bsc_ctx, 1, 
					  "bs11_file_list_entry");
}
