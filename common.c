/***********************************************************************
** Copyright (C) 2003  ACX100 Open Source Project
**
** The contents of this file are subject to the Mozilla Public
** License Version 1.1 (the "License"); you may not use this file
** except in compliance with the License. You may obtain a copy of
** the License at http://www.mozilla.org/MPL/
**
** Software distributed under the License is distributed on an "AS
** IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
** implied. See the License for the specific language governing
** rights and limitations under the License.
**
** Alternatively, the contents of this file may be used under the
** terms of the GNU Public License version 2 (the "GPL"), in which
** case the provisions of the GPL are applicable instead of the
** above.  If you wish to allow the use of your version of this file
** only under the terms of the GPL and not to allow others to use
** your version of this file under the MPL, indicate your decision
** by deleting the provisions above and replace them with the notice
** and other provisions required by the GPL.  If you do not delete
** the provisions above, a recipient may use your version of this
** file under either the MPL or the GPL.
** ---------------------------------------------------------------------
** Inquiries regarding the ACX100 Open Source Project can be
** made directly to:
**
** acx100-users@lists.sf.net
** http://acx100.sf.net
** ---------------------------------------------------------------------
*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/pm.h>
#include <linux/vmalloc.h>
#if WIRELESS_EXT >= 13
#include <net/iw_handler.h>
#endif /* WE >= 13 */

#include "acx.h"


/***********************************************************************
*/
static client_t *acx_l_sta_list_alloc(wlandevice_t *priv);
static client_t *acx_l_sta_list_get_from_hash(wlandevice_t *priv, const u8 *address);

static int acx_l_process_data_frame_master(wlandevice_t *priv, rxbuffer_t *rxbuf);
static int acx_l_process_data_frame_client(wlandevice_t *priv, rxbuffer_t *rxbuf);
/* static int acx_l_process_NULL_frame(wlandevice_t *priv, rxbuffer_t *rxbuf, int vala); */
static int acx_l_process_mgmt_frame(wlandevice_t *priv, rxbuffer_t *rxbuf);
static void acx_l_process_disassoc_from_sta(wlandevice_t *priv, const wlan_fr_disassoc_t *req);
static void acx_l_process_disassoc_from_ap(wlandevice_t *priv, const wlan_fr_disassoc_t *req);
static void acx_l_process_deauth_from_sta(wlandevice_t *priv, const wlan_fr_deauthen_t *req);
static void acx_l_process_deauth_from_ap(wlandevice_t *priv, const wlan_fr_deauthen_t *req);
static int acx_l_process_probe_response(wlandevice_t *priv, wlan_fr_proberesp_t *req, const rxbuffer_t *rxbuf);
static int acx_l_process_assocresp(wlandevice_t *priv, const wlan_fr_assocresp_t *req);
static int acx_l_process_reassocresp(wlandevice_t *priv, const wlan_fr_reassocresp_t *req);
static int acx_l_process_authen(wlandevice_t *priv, const wlan_fr_authen_t *req);
static int acx_l_transmit_assocresp(wlandevice_t *priv, const wlan_fr_assocreq_t *req);
static int acx_l_transmit_reassocresp(wlandevice_t *priv, const wlan_fr_reassocreq_t *req);
static int acx_l_transmit_deauthen(wlandevice_t *priv, const u8 *addr, u16 reason);
static int acx_l_transmit_authen1(wlandevice_t *priv);
static int acx_l_transmit_authen2(wlandevice_t *priv, const wlan_fr_authen_t *req, client_t *clt);
static int acx_l_transmit_authen3(wlandevice_t *priv, const wlan_fr_authen_t *req);
static int acx_l_transmit_authen4(wlandevice_t *priv, const wlan_fr_authen_t *req);
static int acx_l_transmit_assoc_req(wlandevice_t *priv);


/***********************************************************************
*/
#if ACX_DEBUG
unsigned int acx_debug = L_ASSOC|L_INIT;
#endif
#if USE_FW_LOADER_LEGACY
static char *firmware_dir;
#endif
#if SEPARATE_DRIVER_INSTANCES
static int card;
#endif

/* introduced earlier than 2.6.10, but takes more memory, so don't use it
 * if there's no compile warning by kernel */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)

#if ACX_DEBUG
/* parameter is 'debug', corresponding var is acx_debug */
module_param_named(debug, acx_debug, uint, 0);
#endif
#if USE_FW_LOADER_LEGACY
module_param(firmware_dir, charp, 0);
#endif

#else

#if ACX_DEBUG
/* doh, 2.6.x screwed up big time: here the define has its own ";"
 * ("double ; detected"), yet in 2.4.x it DOESN'T (the sane thing to do),
 * grrrrr! */
MODULE_PARM(acx_debug, "i");
#endif
#if USE_FW_LOADER_LEGACY
MODULE_PARM(firmware_dir, "s");
#endif

#endif

#if ACX_DEBUG
MODULE_PARM_DESC(debug, "Debug level mask (see L_xxx constants)");
#endif
#if USE_FW_LOADER_LEGACY
MODULE_PARM_DESC(firmware_dir, "Directory to load acx100 firmware files from");
#endif
#if SEPARATE_DRIVER_INSTANCES
MODULE_PARM(card, "i");
MODULE_PARM_DESC(card, "Associate only with card-th acx100 card from this driver instance");
#endif

#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual MPL/GPL");
#endif
/* USB had this: MODULE_AUTHOR("Martin Wawro <martin.wawro AT uni-dortmund.de>"); */
MODULE_AUTHOR("ACX100 Open Source Driver development team");
MODULE_DESCRIPTION("Driver for TI ACX1xx based wireless cards (CardBus/PCI/USB)");


/***********************************************************************
*/
/* Probably a number of acx's itermediate buffers for USB transfers,
** not to be confused with number of descriptors in tx/rx rings
** (which are not directly accessible to host in USB devices) */
#define USB_RX_CNT 10
#define USB_TX_CNT 10


/***********************************************************************
*/

/* minutes to wait until next radio recalibration: */
#define RECALIB_PAUSE	5

const u8 acx_reg_domain_ids[] =
	{ 0x10, 0x20, 0x30, 0x31, 0x32, 0x40, 0x41, 0x51 };
/* stupid workaround for the fact that in C the size of an external array
 * cannot be determined from within a second file */
const u8 acx_reg_domain_ids_len = sizeof(acx_reg_domain_ids);
static const u16 reg_domain_channel_masks[] =
	{ 0x07ff, 0x07ff, 0x1fff, 0x0600, 0x1e00, 0x2000, 0x3fff, 0x01fc };


/***********************************************************************
** Debugging support
*/
#ifdef PARANOID_LOCKING
static unsigned max_lock_time;
static unsigned max_sem_time;

void
acx_lock_unhold() { max_lock_time = 0; }
void
acx_sem_unhold() { max_sem_time = 0; }

static inline const char*
sanitize_str(const char *s)
{
	const char* t = strrchr(s, '/');
	if (t) return t + 1;
	return s;
}

void
acx_lock_debug(wlandevice_t *priv, const char* where)
{
	int count = 100*1000*1000;
	where = sanitize_str(where);
	while (--count) {
		if (!spin_is_locked(&priv->lock)) break;
		cpu_relax();
	}
	if (!count) {
		printk(KERN_EMERG "LOCKUP: already taken at %s!\n", priv->last_lock);
		BUG();
	}
	priv->last_lock = where;
	rdtscl(priv->lock_time);
}
void
acx_unlock_debug(wlandevice_t *priv, const char* where)
{
#ifdef SMP
	if (!spin_is_locked(&priv->lock)) {
		where = sanitize_str(where);
		printk(KERN_EMERG "STRAY UNLOCK at %s!\n", where);
		BUG();
	}
#endif
	if (acx_debug & L_LOCK) {
		unsigned diff;
		rdtscl(diff);
		diff -= priv->lock_time;
		if (diff > max_lock_time) {
			where = sanitize_str(where);
			printk("max lock hold time %d CPU ticks from %s "
				"to %s\n", diff, priv->last_lock, where);
			max_lock_time = diff;
		}
	}
}
void
acx_down_debug(wlandevice_t *priv, const char* where)
{
	int sem_count;
	int count = 5000/5;
	where = sanitize_str(where);

	while (--count) {
		sem_count = atomic_read(&priv->sem.count);
		if (sem_count) break;
		msleep(5);
	}
	if (!count) {
		printk(KERN_EMERG "D STATE at %s! last sem at %s\n",
			where, priv->last_sem);
		dump_stack();
	}
	priv->last_sem = where;
	priv->sem_time = jiffies;
	down(&priv->sem);
	if (acx_debug & L_LOCK) {
		printk("%s: sem_down %d -> %d\n",
			where, sem_count, atomic_read(&priv->sem.count));
	}
}
void
acx_up_debug(wlandevice_t *priv, const char* where)
{
	int sem_count = atomic_read(&priv->sem.count);
	if (sem_count) {
		where = sanitize_str(where);
		printk(KERN_EMERG "STRAY UP at %s! sem.count=%d\n", where, sem_count);
		dump_stack();
	}
	if (acx_debug & L_LOCK) {
		unsigned diff = jiffies - priv->sem_time;
		if (diff > max_sem_time) {
			where = sanitize_str(where);
			printk("max sem hold time %d jiffies from %s "
				"to %s\n", diff, priv->last_sem, where);
			max_sem_time = diff;
		}
	}
	up(&priv->sem);
	if (acx_debug & L_LOCK) {
		where = sanitize_str(where);
		printk("%s: sem_up %d -> %d\n",
			where, sem_count, atomic_read(&priv->sem.count));
	}
}
#endif /* PARANOID_LOCKING */


/***********************************************************************
*/
#if ACX_DEBUG > 1

static int acx_debug_func_indent;
#define DEBUG_TSC 0
#define FUNC_INDENT_INCREMENT 2

#if DEBUG_TSC
#define TIMESTAMP(d) unsigned long d; rdtscl(d)
#else
#define TIMESTAMP(d) unsigned long d = jiffies
#endif

static const char
spaces[] = "          " "          "; /* Nx10 spaces */

void
log_fn_enter(const char *funcname)
{
	int indent;
	TIMESTAMP(d);

	indent = acx_debug_func_indent;
	if (indent >= sizeof(spaces))
		indent = sizeof(spaces)-1;

	printk("%08ld %s==> %s\n",
		d % 100000000,
		spaces + (sizeof(spaces)-1) - indent,
		funcname
	);

	acx_debug_func_indent += FUNC_INDENT_INCREMENT;
}
void
log_fn_exit(const char *funcname)
{
	int indent;
	TIMESTAMP(d);

	acx_debug_func_indent -= FUNC_INDENT_INCREMENT;

	indent = acx_debug_func_indent;
	if (indent >= sizeof(spaces))
		indent = sizeof(spaces)-1;

	printk("%08ld %s<== %s\n",
		d % 100000000,
		spaces + (sizeof(spaces)-1) - indent,
		funcname
	);
}
void
log_fn_exit_v(const char *funcname, int v)
{
	int indent;
	TIMESTAMP(d);

	acx_debug_func_indent -= FUNC_INDENT_INCREMENT;

	indent = acx_debug_func_indent;
	if (indent >= sizeof(spaces))
		indent = sizeof(spaces)-1;

	printk("%08ld %s<== %s: %08X\n",
		d % 100000000,
		spaces + (sizeof(spaces)-1) - indent,
		funcname,
		v
	);
}
#endif /* ACX_DEBUG > 1 */


/***********************************************************************
** Basically a msleep with logging
*/
void
acx_s_msleep(int ms)
{
	FN_ENTER;
	msleep(ms);
	FN_EXIT0;
}


/***********************************************************************
** Not inlined: it's larger than it seems
*/
void
acx_print_mac(const char *head, const u8 *mac, const char *tail)
{
	printk("%s"MACSTR"%s", head, MAC(mac), tail);
}


/***********************************************************************
** acx_get_status_name
*/
static const char*
acx_get_status_name(u16 status)
{
	static const char * const str[] = {
		"STOPPED", "SCANNING", "WAIT_AUTH",
		"AUTHENTICATED", "ASSOCIATED", "INVALID??"
	};
	return str[(status < VEC_SIZE(str)) ? status : VEC_SIZE(str)-1];
}


/***********************************************************************
** acx_get_packet_type_string
*/
#if ACX_DEBUG
const char*
acx_get_packet_type_string(u16 fc)
{
	static const char * const mgmt_arr[] = {
		"MGMT/AssocReq", "MGMT/AssocResp", "MGMT/ReassocReq",
		"MGMT/ReassocResp", "MGMT/ProbeReq", "MGMT/ProbeResp",
		"MGMT/UNKNOWN", "MGMT/UNKNOWN", "MGMT/Beacon", "MGMT/ATIM",
		"MGMT/Disassoc", "MGMT/Authen", "MGMT/Deauthen"
	};
	static const char * const ctl_arr[] = {
		"CTL/PSPoll", "CTL/RTS", "CTL/CTS", "CTL/Ack", "CTL/CFEnd",
		"CTL/CFEndCFAck"
	};
	static const char * const data_arr[] = {
		"DATA/DataOnly", "DATA/Data CFAck", "DATA/Data CFPoll",
		"DATA/Data CFAck/CFPoll", "DATA/Null", "DATA/CFAck",
		"DATA/CFPoll", "DATA/CFAck/CFPoll"
	};
	const char *str = "UNKNOWN";
	u8 fstype = (WF_FC_FSTYPE & fc) >> 4;
	u8 ctl;

	switch (WF_FC_FTYPE & fc) {
	case WF_FTYPE_MGMT:
		str = "MGMT/UNKNOWN";
		if (fstype < VEC_SIZE(mgmt_arr))
			str = mgmt_arr[fstype];
		break;
	case WF_FTYPE_CTL:
		ctl = fstype - 0x0a;
		str = "CTL/UNKNOWN";
		if (ctl < VEC_SIZE(ctl_arr))
			str = ctl_arr[ctl];
		break;
	case WF_FTYPE_DATA:
		str = "DATA/UNKNOWN";
		if (fstype < VEC_SIZE(data_arr))
			str = data_arr[fstype];
		break;
	}
	return str;
}
#endif


/***********************************************************************
** acx_cmd_status_str
*/
const char*
acx_cmd_status_str(unsigned int state)
{
	static const char * const cmd_error_strings[] = {
		"Idle",
		"Success",
		"Unknown Command",
		"Invalid Information Element",
		"Channel rejected",
		"Channel invalid in current regulatory domain",
		"MAC invalid",
		"Command rejected (read-only information element)",
		"Command rejected",
		"Already asleep",
		"TX in progress",
		"Already awake",
		"Write only",
		"RX in progress",
		"Invalid parameter",
		"Scan in progress",
		"Failed"
	};
	return state < VEC_SIZE(cmd_error_strings) ?
			cmd_error_strings[state] : "UNKNOWN REASON";
}


/***********************************************************************
** get_status_string
*/
static const char*
get_status_string(unsigned int status)
{
	/* A bit shortened, but hopefully still understandable */
	static const char * const status_str[] = {
	/* 0 */	"Successful",
	/* 1 */	"Unspecified failure",
	/* 2 */	"reserved",
	/* 3 */	"reserved",
	/* 4 */	"reserved",
	/* 5 */	"reserved",
	/* 6 */	"reserved",
	/* 7 */	"reserved",
	/* 8 */	"reserved",
	/* 9 */	"reserved",
	/*10 */	"Cannot support all requested capabilities in Capability Information field",
	/*11 */	"Reassoc denied (reason outside of 802.11b scope)",
	/*12 */	"Assoc denied (reason outside of 802.11b scope), maybe MAC filtering by peer?",
	/*13 */	"Responding station doesnt support specified auth algorithm",
	/*14 */	"Auth rejected: wrong transaction sequence number",
	/*15 */	"Auth rejected: challenge failure",
	/*16 */	"Auth rejected: timeout for next frame in sequence",
	/*17 */	"Assoc denied: too many STAs on this AP",
	/*18 */	"Assoc denied: requesting STA doesnt support all data rates in basic set",
	/*19 */	"Assoc denied: requesting STA doesnt support Short Preamble",
	/*20 */	"Assoc denied: requesting STA doesnt support PBCC Modulation",
	/*21 */	"Assoc denied: requesting STA doesnt support Channel Agility"
	/*22 */	"reserved",
	/*23 */	"reserved",
	/*24 */	"reserved",
	/*25 */	"Assoc denied: requesting STA doesnt support Short Slot Time",
	/*26 */	"Assoc denied: requesting STA doesnt support DSSS-OFDM"
	};

	return status_str[status < VEC_SIZE(status_str) ? status : 2];
}


/***********************************************************************
*/
void
acx_log_bad_eid(wlan_hdr_t* hdr, int len, wlan_ie_t* ie_ptr)
{
	if (acx_debug & L_ASSOC) {
		int offset = (u8*)ie_ptr - (u8*)hdr;
		printk("acx: unknown EID %d in mgmt frame at offset %d. IE: ",
				ie_ptr->eid, offset);
	/* IE len can be bogus, IE can extend past packet end. Oh well... */
		acx_dump_bytes(ie_ptr, ie_ptr->len + 2);
		if (acx_debug & L_DATA) {
			printk("frame (%s): ",
			acx_get_packet_type_string(le16_to_cpu(hdr->fc)));
			acx_dump_bytes(hdr, len);
		}
	}
}


/***********************************************************************
*/
#if ACX_DEBUG
void
acx_dump_bytes(const void *data, int num)
{
	const u8* ptr = (const u8*)data;

	if (num <= 0) {
		printk("\n");
		return;
	}

	while (num >= 16) {
		printk( "%02X %02X %02X %02X %02X %02X %02X %02X "
			"%02X %02X %02X %02X %02X %02X %02X %02X\n",
			ptr[0], ptr[1], ptr[2], ptr[3],
			ptr[4], ptr[5], ptr[6], ptr[7],
			ptr[8], ptr[9], ptr[10], ptr[11],
			ptr[12], ptr[13], ptr[14], ptr[15]);
		num -= 16;
		ptr += 16;
	}
	if (num > 0) {
		while (--num > 0)
			printk("%02X ", *ptr++);
		printk("%02X\n", *ptr);
	}
}
#endif


/***********************************************************************
** acx_s_get_firmware_version
*/
void
acx_s_get_firmware_version(wlandevice_t *priv)
{
	fw_ver_t fw;
	u8 hexarr[4] = { 0, 0, 0, 0 };
	int hexidx = 0, val = 0;
	const char *num;
	char c;

	FN_ENTER;

	memset(fw.fw_id, 'E', FW_ID_SIZE);
	acx_s_interrogate(priv, &fw, ACX1xx_IE_FWREV);
	memcpy(priv->firmware_version, fw.fw_id, FW_ID_SIZE);
	priv->firmware_version[FW_ID_SIZE] = '\0';

	acxlog(L_DEBUG, "fw_ver: fw_id='%s' hw_id=%08X\n",
				priv->firmware_version, fw.hw_id);

	if (strncmp(fw.fw_id, "Rev ", 4) != 0) {
		printk("acx: strange firmware version string "
			"'%s', please report\n", priv->firmware_version);
		priv->firmware_numver = 0x01090407; /* assume 1.9.4.7 */
	} else {
		num = &fw.fw_id[4];
		while (1) {
			c = *num++;
			if ((c == '.') || (c == '\0')) {
				hexarr[hexidx++] = val;
				if ((hexidx > 3) || (c == '\0')) /* end? */
					break;
				val = 0;
				continue;
			}
			if ((c >= '0') && (c <= '9'))
				c -= '0';
			else
				c = c - 'a' + (char)10;
			val = val*16 + c;
		}

		priv->firmware_numver = (u32)(
				(hexarr[0] << 24) + (hexarr[1] << 16)
				+ (hexarr[2] << 8) + hexarr[3]);
		acxlog(L_DEBUG, "firmware_numver 0x%08X\n", priv->firmware_numver);
	}
	if (IS_ACX111(priv)) {
		if (priv->firmware_numver == 0x00010011) {
			/* This one does not survive floodpinging */
			printk("acx: firmware '%s' is known to be buggy, "
				"please upgrade\n", priv->firmware_version);
		}
		if (priv->firmware_numver == 0x02030131) {
			/* With this one, all rx packets look mangled
			** Most probably we simply do not know how to use it
			** properly */
			printk("acx: firmware '%s' does not work well "
				"with this driver\n", priv->firmware_version);
		}
	}

	priv->firmware_id = le32_to_cpu(fw.hw_id);

	/* we're able to find out more detailed chip names now */
	switch (priv->firmware_id & 0xffff0000) {
		case 0x01010000:
		case 0x01020000:
			priv->chip_name = "TNETW1100A";
			break;
		case 0x01030000:
			priv->chip_name = "TNETW1100B";
			break;
		case 0x03000000:
		case 0x03010000:
			priv->chip_name = "TNETW1130";
			break;
		default:
			printk("acx: unknown chip ID 0x%08X, "
				"please report\n", priv->firmware_id);
			break;
	}

	FN_EXIT0;
}


/***********************************************************************
** acx_display_hardware_details
**
** Displays hw/fw version, radio type etc...
*/
void
acx_display_hardware_details(wlandevice_t *priv)
{
	const char *radio_str, *form_str;

	FN_ENTER;

	switch (priv->radio_type) {
	case RADIO_MAXIM_0D:
		/* hmm, the DWL-650+ seems to have two variants,
		 * according to a windows driver changelog comment:
		 * RFMD and Maxim. */
		radio_str = "Maxim";
		break;
	case RADIO_RFMD_11:
		radio_str = "RFMD";
		break;
	case RADIO_RALINK_15:
		radio_str = "Ralink";
		break;
	case RADIO_RADIA_16:
		radio_str = "Radia";
		break;
	case RADIO_UNKNOWN_17:
		/* TI seems to have a radio which is
		 * additionally 802.11a capable, too */
		radio_str = "802.11a/b/g radio?! Please report";
		break;
	case RADIO_UNKNOWN_19:
		radio_str = "A radio used by Safecom cards?! Please report";
		break;
	default:
		radio_str = "UNKNOWN, please report the radio type name!";
		break;
	}

	switch (priv->form_factor) {
	case 0x00:
		form_str = "unspecified";
		break;
	case 0x01:
		form_str = "(mini-)PCI / CardBus";
		break;
	case 0x02:
		form_str = "USB";
		break;
	case 0x03:
		form_str = "Compact Flash";
		break;
	default:
		form_str = "UNKNOWN, please report";
		break;
	}

	printk("acx: form factor 0x%02X (%s), "
		"radio type 0x%02X (%s), EEPROM version 0x%02X, "
		"uploaded firmware '%s' (0x%08X)\n",
		priv->form_factor, form_str, priv->radio_type, radio_str,
		priv->eeprom_version, priv->firmware_version,
		priv->firmware_id);

	FN_EXIT0;
}


/***********************************************************************
*/
int
acx_e_change_mtu(struct net_device *dev, int mtu)
{
	enum {
		MIN_MTU = 256,
		MAX_MTU = WLAN_DATA_MAXLEN - (ETH_HLEN)
	};

	if (mtu < MIN_MTU || mtu > MAX_MTU)
		return -EINVAL;

	dev->mtu = mtu;
	return 0;
}


/***********************************************************************
** acx_e_get_stats, acx_e_get_wireless_stats
*/
struct net_device_stats*
acx_e_get_stats(netdevice_t *dev)
{
	wlandevice_t *priv = netdev_priv(dev);
	return &priv->stats;
}

struct iw_statistics*
acx_e_get_wireless_stats(netdevice_t *dev)
{
	wlandevice_t *priv = netdev_priv(dev);
	return &priv->wstats;
}


/***********************************************************************
** maps acx111 tx descr rate field to acx100 one
*/
const u8
acx_bitpos2rate100[] = {
	RATE100_1	,/* 0 */
	RATE100_2	,/* 1 */
	RATE100_5	,/* 2 */
	RATE100_2	,/* 3, should not happen */
	RATE100_2	,/* 4, should not happen */
	RATE100_11	,/* 5 */
	RATE100_2	,/* 6, should not happen */
	RATE100_2	,/* 7, should not happen */
	RATE100_22	,/* 8 */
	RATE100_2	,/* 9, should not happen */
	RATE100_2	,/* 10, should not happen */
	RATE100_2	,/* 11, should not happen */
	RATE100_2	,/* 12, should not happen */
	RATE100_2	,/* 13, should not happen */
	RATE100_2	,/* 14, should not happen */
	RATE100_2	,/* 15, should not happen */
};

u8
acx_rate111to100(u16 r) {
	return acx_bitpos2rate100[highest_bit(r)];
}


/***********************************************************************
** Calculate level like the feb 2003 windows driver seems to do
*/
static u8
acx_signal_to_winlevel(u8 rawlevel)
{
	/* u8 winlevel = (u8) (0.5 + 0.625 * rawlevel); */
	u8 winlevel = ((4 + (rawlevel * 5)) / 8);

	if (winlevel > 100)
		winlevel = 100;
	return winlevel;
}

u8
acx_signal_determine_quality(u8 signal, u8 noise)
{
	int qual;

	qual = (((signal - 30) * 100 / 70) + (100 - noise * 4)) / 2;

	if (qual > 100)
		return 100;
	if (qual < 0)
		return 0;
	return qual;
}


/***********************************************************************
** Interrogate/configure commands
*/
static const u16
CtlLength[] = {
	0,
	ACX100_IE_ACX_TIMER_LEN,
	ACX1xx_IE_POWER_MGMT_LEN,
	ACX1xx_IE_QUEUE_CONFIG_LEN,
	ACX100_IE_BLOCK_SIZE_LEN,
	ACX1xx_IE_MEMORY_CONFIG_OPTIONS_LEN,
	ACX1xx_IE_RATE_FALLBACK_LEN,
	ACX100_IE_WEP_OPTIONS_LEN,
	ACX1xx_IE_MEMORY_MAP_LEN, /*	ACX1xx_IE_SSID_LEN, */
	0,
	ACX1xx_IE_ASSOC_ID_LEN,
	0,
	ACX111_IE_CONFIG_OPTIONS_LEN,
	ACX1xx_IE_FWREV_LEN,
	ACX1xx_IE_FCS_ERROR_COUNT_LEN,
	ACX1xx_IE_MEDIUM_USAGE_LEN,
	ACX1xx_IE_RXCONFIG_LEN,
	0,
	0,
	ACX1xx_IE_FIRMWARE_STATISTICS_LEN,
	0,
	ACX1xx_IE_FEATURE_CONFIG_LEN,
	ACX111_IE_KEY_CHOOSE_LEN,
};

static const u16
CtlLengthDot11[] = {
	0,
	ACX1xx_IE_DOT11_STATION_ID_LEN,
	0,
	ACX100_IE_DOT11_BEACON_PERIOD_LEN,
	ACX1xx_IE_DOT11_DTIM_PERIOD_LEN,
	ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT_LEN,
	ACX1xx_IE_DOT11_LONG_RETRY_LIMIT_LEN,
	ACX100_IE_DOT11_WEP_DEFAULT_KEY_WRITE_LEN,
	ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME_LEN,
	0,
	ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN_LEN,
	ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN,
	0,
	ACX1xx_IE_DOT11_TX_POWER_LEVEL_LEN,
	ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN,
	ACX100_IE_DOT11_ED_THRESHOLD_LEN,
	ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET_LEN,
	0,
	0,
	0,
};

#undef FUNC
#define FUNC "configure"
#if !ACX_DEBUG
int
acx_s_configure(wlandevice_t *priv, void *pdr, int type)
{
#else
int
acx_s_configure_debug(wlandevice_t *priv, void *pdr, int type, const char* typestr)
{
#endif
	u16 len;
	int res;

	if (type < 0x1000)
		len = CtlLength[type];
	else
		len = CtlLengthDot11[type - 0x1000];

	acxlog(L_CTL, FUNC"(type:%s,len:%u)\n", typestr, len);
	if (unlikely(!len)) {
		acxlog(L_DEBUG, "zero-length type %s?!\n", typestr);
	}

	((acx_ie_generic_t *)pdr)->type = cpu_to_le16(type);
	((acx_ie_generic_t *)pdr)->len = cpu_to_le16(len);
	res = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIGURE, pdr, len + 4);
	if (OK != res) {
#if ACX_DEBUG
		printk("%s: "FUNC"(type:%s) FAILED\n", priv->netdev->name, typestr);
#else
		printk("%s: "FUNC"(type:0x%X) FAILED\n", priv->netdev->name, type);
#endif
		/* dump_stack() is already done in issue_cmd() */
	}
	return res;
}

#undef FUNC
#define FUNC "interrogate"
#if !ACX_DEBUG
int
acx_s_interrogate(wlandevice_t *priv, void *pdr, int type)
{
#else
int
acx_s_interrogate_debug(wlandevice_t *priv, void *pdr, int type,
		const char* typestr)
{
#endif
	u16 len;
	int res;

	if (type < 0x1000)
		len = CtlLength[type];
	else
		len = CtlLengthDot11[type-0x1000];
	acxlog(L_CTL, FUNC"(type:%s,len:%u)\n", typestr, len);

	((acx_ie_generic_t *)pdr)->type = cpu_to_le16(type);
	((acx_ie_generic_t *)pdr)->len = cpu_to_le16(len);
	res = acx_s_issue_cmd(priv, ACX1xx_CMD_INTERROGATE, pdr, len + 4);
	if (OK != res) {
#if ACX_DEBUG
		printk("%s: "FUNC"(type:%s) FAILED\n", priv->netdev->name, typestr);
#else
		printk("%s: "FUNC"(type:0x%X) FAILED\n", priv->netdev->name, type);
#endif
		/* dump_stack() is already done in issue_cmd() */
	}
	return res;
}

#if CMD_DISCOVERY
void
great_inquisitor(wlandevice_t *priv)
{
	static struct {
		u16	type ACX_PACKED;
		u16	len ACX_PACKED;
		/* 0x200 was too large here: */
		u8	data[0x100 - 4] ACX_PACKED;
	} ie;
	u16 type;

	FN_ENTER;

	/* 0..0x20, 0x1000..0x1020 */
	for (type = 0; type <= 0x1020; type++) {
		if (type == 0x21)
			type = 0x1000;
		ie.type = cpu_to_le16(type);
		ie.len = cpu_to_le16(sizeof(ie) - 4);
		acx_s_issue_cmd(priv, ACX1xx_CMD_INTERROGATE, &ie, sizeof(ie));
	}
	FN_EXIT0;
}
#endif


#ifdef CONFIG_PROC_FS
/***********************************************************************
** /proc files
*/
/***********************************************************************
** acx_l_proc_output
** Generate content for our /proc entry
**
** Arguments:
**	buf is a pointer to write output to
**	priv is the usual pointer to our private struct wlandevice
** Returns:
**	number of bytes actually written to buf
** Side effects:
**	none
*/
static int
acx_l_proc_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	int i;

	FN_ENTER;

	p += sprintf(p,
		"acx driver version:\t\t" ACX_RELEASE "\n"
		"Wireless extension version:\t" STRING(WIRELESS_EXT) "\n"
		"chip name:\t\t\t%s (0x%08X)\n"
		"radio type:\t\t\t0x%02X\n"
		"form factor:\t\t\t0x%02X\n"
		"EEPROM version:\t\t\t0x%02X\n"
		"firmware version:\t\t%s (0x%08X)\n",
		priv->chip_name, priv->firmware_id,
		priv->radio_type,
		priv->form_factor,
		priv->eeprom_version,
		priv->firmware_version, priv->firmware_numver);

	for (i = 0; i < VEC_SIZE(priv->sta_list); i++) {
		struct client *bss = &priv->sta_list[i];
		if (!bss->used) continue;
		p += sprintf(p, "BSS %u BSSID "MACSTR" ESSID %s channel %u "
			"Cap 0x%X SIR %u SNR %u\n",
			i, MAC(bss->bssid), (char*)bss->essid, bss->channel,
			bss->cap_info, bss->sir, bss->snr);
	}
	p += sprintf(p, "status:\t\t\t%u (%s)\n",
			priv->status, acx_get_status_name(priv->status));

	FN_EXIT1(p - buf);
	return p - buf;
}


/***********************************************************************
*/
static int
acx_s_proc_diag_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	fw_stats_t *fw_stats;
	unsigned long flags;

	FN_ENTER;

	fw_stats = kmalloc(sizeof(fw_stats_t), GFP_KERNEL);
	if (!fw_stats) {
		FN_EXIT1(0);
		return 0;
	}
	memset(fw_stats, 0, sizeof(fw_stats_t));

	acx_lock(priv, flags);

	if (IS_PCI(priv))
		p = acxpci_s_proc_diag_output(p, priv);

	p += sprintf(p,
		"\n"
		"** network status **\n"
		"dev_state_mask 0x%04X\n"
		"status %u (%s), "
		"mode %u, channel %u, "
		"reg_dom_id 0x%02X, reg_dom_chanmask 0x%04X, ",
		priv->dev_state_mask,
		priv->status, acx_get_status_name(priv->status),
		priv->mode, priv->channel,
		priv->reg_dom_id, priv->reg_dom_chanmask
		);
	p += sprintf(p,
		"ESSID \"%s\", essid_active %d, essid_len %d, "
		"essid_for_assoc \"%s\", nick \"%s\"\n"
		"WEP ena %d, restricted %d, idx %d\n",
		priv->essid, priv->essid_active, (int)priv->essid_len,
		priv->essid_for_assoc, priv->nick,
		priv->wep_enabled, priv->wep_restricted,
		priv->wep_current_index);
	p += sprintf(p, "dev_addr  "MACSTR"\n", MAC(priv->dev_addr));
	p += sprintf(p, "bssid     "MACSTR"\n", MAC(priv->bssid));
	p += sprintf(p, "ap_filter "MACSTR"\n", MAC(priv->ap));

	p += sprintf(p,
		"\n"
		"** PHY status **\n"
		"tx_disabled %d, tx_level_dbm %d\n" /* "tx_level_val %d, tx_level_auto %d\n" */
		"sensitivity %d, antenna 0x%02X, ed_threshold %d, cca %d, preamble_mode %d\n"
		"rts_threshold %d, short_retry %d, long_retry %d, msdu_lifetime %d, listen_interval %d, beacon_interval %d\n",
		priv->tx_disabled, priv->tx_level_dbm, /* priv->tx_level_val, priv->tx_level_auto, */
		priv->sensitivity, priv->antenna, priv->ed_threshold, priv->cca, priv->preamble_mode,
		priv->rts_threshold, priv->short_retry, priv->long_retry, priv->msdu_lifetime, priv->listen_interval, priv->beacon_interval);

	acx_unlock(priv, flags);

	if (OK != acx_s_interrogate(priv, fw_stats, ACX1xx_IE_FIRMWARE_STATISTICS))
		p += sprintf(p,
			"\n"
			"** Firmware **\n"
			"QUERY FAILED!!\n");
	else {
		p += sprintf(p,
			"\n"
			"** Firmware **\n"
			"version \"%s\"\n"
			"tx_desc_overfl %u, rx_OutOfMem %u, rx_hdr_overfl %u, rx_hdr_use_next %u\n"
			"rx_dropped_frame %u, rx_frame_ptr_err %u, rx_xfr_hint_trig %u, rx_dma_req %u\n"
			"rx_dma_err %u, tx_dma_req %u, tx_dma_err %u, cmd_cplt %u, fiq %u\n"
			"rx_hdrs %u, rx_cmplt %u, rx_mem_overfl %u, rx_rdys %u, irqs %u\n"
			"acx_trans_procs %u, decrypt_done %u, dma_0_done %u, dma_1_done %u\n",
			priv->firmware_version,
			le32_to_cpu(fw_stats->tx_desc_of),
			le32_to_cpu(fw_stats->rx_oom),
			le32_to_cpu(fw_stats->rx_hdr_of),
			le32_to_cpu(fw_stats->rx_hdr_use_next),
			le32_to_cpu(fw_stats->rx_dropped_frame),
			le32_to_cpu(fw_stats->rx_frame_ptr_err),
			le32_to_cpu(fw_stats->rx_xfr_hint_trig),
			le32_to_cpu(fw_stats->rx_dma_req),
			le32_to_cpu(fw_stats->rx_dma_err),
			le32_to_cpu(fw_stats->tx_dma_req),
			le32_to_cpu(fw_stats->tx_dma_err),
			le32_to_cpu(fw_stats->cmd_cplt),
			le32_to_cpu(fw_stats->fiq),
			le32_to_cpu(fw_stats->rx_hdrs),
			le32_to_cpu(fw_stats->rx_cmplt),
			le32_to_cpu(fw_stats->rx_mem_of),
			le32_to_cpu(fw_stats->rx_rdys),
			le32_to_cpu(fw_stats->irqs),
			le32_to_cpu(fw_stats->acx_trans_procs),
			le32_to_cpu(fw_stats->decrypt_done),
			le32_to_cpu(fw_stats->dma_0_done),
			le32_to_cpu(fw_stats->dma_1_done));
		p += sprintf(p,
			"tx_exch_complet %u, commands %u, acx_rx_procs %u\n"
			"hw_pm_mode_changes %u, host_acks %u, pci_pm %u, acm_wakeups %u\n"
			"wep_key_count %u, wep_default_key_count %u, dot11_def_key_mib %u\n"
			"wep_key_not_found %u, wep_decrypt_fail %u\n",
			le32_to_cpu(fw_stats->tx_exch_complet),
			le32_to_cpu(fw_stats->commands),
			le32_to_cpu(fw_stats->acx_rx_procs),
			le32_to_cpu(fw_stats->hw_pm_mode_changes),
			le32_to_cpu(fw_stats->host_acks),
			le32_to_cpu(fw_stats->pci_pm),
			le32_to_cpu(fw_stats->acm_wakeups),
			le32_to_cpu(fw_stats->wep_key_count),
			le32_to_cpu(fw_stats->wep_default_key_count),
			le32_to_cpu(fw_stats->dot11_def_key_mib),
			le32_to_cpu(fw_stats->wep_key_not_found),
			le32_to_cpu(fw_stats->wep_decrypt_fail));
	}

	kfree(fw_stats);

	FN_EXIT1(p - buf);
	return p - buf;
}


/***********************************************************************
*/
static int
acx_s_proc_phy_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	int i;

	FN_ENTER;

	/*
	if (RADIO_RFMD_11 != priv->radio_type) {
		printk("sorry, not yet adapted for radio types "
			"other than RFMD, please verify "
			"PHY size etc. first!\n");
		goto end;
	}
	*/

	/* The PHY area is only 0x80 bytes long; further pages after that
	 * only have some page number registers with altered value,
	 * all other registers remain the same. */
	for (i = 0; i < 0x80; i++) {
		acx_s_read_phy_reg(priv, i, p++);
	}

	FN_EXIT1(p - buf);
	return p - buf;
}


/***********************************************************************
** acx_e_read_proc_XXXX
** Handle our /proc entry
**
** Arguments:
**	standard kernel read_proc interface
** Returns:
**	number of bytes written to buf
** Side effects:
**	none
*/
static int
acx_e_read_proc(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	unsigned long flags;
	int length;

	FN_ENTER;

	acx_sem_lock(priv);
	acx_lock(priv, flags);
	/* fill buf */
	length = acx_l_proc_output(buf, priv);
	acx_unlock(priv, flags);
	acx_sem_unlock(priv);

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}

static int
acx_e_read_proc_diag(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	int length;

	FN_ENTER;

	acx_sem_lock(priv);
	/* fill buf */
	length = acx_s_proc_diag_output(buf, priv);
	acx_sem_unlock(priv);

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}

static int
acx_e_read_proc_eeprom(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	int length;

	FN_ENTER;

	/* fill buf */
	length = 0;
	if (IS_PCI(priv)) {
		acx_sem_lock(priv);
		length = acxpci_proc_eeprom_output(buf, priv);
		acx_sem_unlock(priv);
	}

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}

static int
acx_e_read_proc_phy(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	int length;

	FN_ENTER;

	acx_sem_lock(priv);
	/* fill buf */
	length = acx_s_proc_phy_output(buf, priv);
	acx_sem_unlock(priv);

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}


/***********************************************************************
** /proc files registration
*/
static const char * const
proc_files[] = { "", "_diag", "_eeprom", "_phy" };

static read_proc_t * const
proc_funcs[] = {
	acx_e_read_proc,
	acx_e_read_proc_diag,
	acx_e_read_proc_eeprom,
	acx_e_read_proc_phy
};

static int
manage_proc_entries(const struct net_device *dev, int remove)
{
	/* doh, netdev_priv() doesn't have const! */
	wlandevice_t *priv = netdev_priv((struct net_device *)dev);
	char procbuf[80];
	int i;

	for (i = 0; i < VEC_SIZE(proc_files); i++)	{
		sprintf(procbuf, "driver/acx_%s%s", dev->name, proc_files[i]);
		if (!remove) {
			acxlog(L_INIT, "creating /proc entry %s\n", procbuf);
			if (!create_proc_read_entry(procbuf, 0, 0, proc_funcs[i], priv)) {
				printk("acx: cannot register /proc entry %s\n", procbuf);
				return NOT_OK;
			}
		} else {
			acxlog(L_INIT, "removing /proc entry %s\n", procbuf);
			remove_proc_entry(procbuf, NULL);
		}
	}
	return OK;
}

int
acx_proc_register_entries(const struct net_device *dev)
{
	return manage_proc_entries(dev, 0);
}

int
acx_proc_unregister_entries(const struct net_device *dev)
{
	return manage_proc_entries(dev, 1);
}
#endif /* CONFIG_PROC_FS */


/***********************************************************************
** acx_cmd_join_bssid
**
** Common code for both acx100 and acx111.
*/
/* NB: does NOT match RATE100_nn but matches ACX[111]_SCAN_RATE_n */
static const u8
bitpos2genframe_txrate[] = {
	10,	/*  0.  1 Mbit/s */
	20,	/*  1.  2 Mbit/s */
	55,	/*  2.  5.5 Mbit/s */
	0x0B,	/*  3.  6 Mbit/s */
	0x0F,	/*  4.  9 Mbit/s */
	110,	/*  5. 11 Mbit/s */
	0x0A,	/*  6. 12 Mbit/s */
	0x0E,	/*  7. 18 Mbit/s */
	220,	/*  8. 22 Mbit/s */
	0x09,	/*  9. 24 Mbit/s */
	0x0D,	/* 10. 36 Mbit/s */
	0x08,	/* 11. 48 Mbit/s */
	0x0C,	/* 12. 54 Mbit/s */
	10,	/* 13.  1 Mbit/s, should never happen */
	10,	/* 14.  1 Mbit/s, should never happen */
	10,	/* 15.  1 Mbit/s, should never happen */
};

/* Looks scary, eh?
** Actually, each one compiled into one AND and one SHIFT,
** 31 bytes in x86 asm (more if uints are replaced by u16/u8) */
static unsigned int
rate111to5bits(unsigned int rate)
{
	return (rate & 0x7)
	| ( (rate & RATE111_11) / (RATE111_11/JOINBSS_RATES_11) )
	| ( (rate & RATE111_22) / (RATE111_22/JOINBSS_RATES_22) )
	;
}

static void
acx_s_cmd_join_bssid(wlandevice_t *priv, const u8 *bssid)
{
	acx_joinbss_t tmp;
	int dtim_interval;
	int i;

	if (mac_is_zero(bssid))
		return;

	FN_ENTER;

	dtim_interval =	(ACX_MODE_0_ADHOC == priv->mode) ?
			1 : priv->dtim_interval;

	memset(&tmp, 0, sizeof(tmp));

	for (i = 0; i < ETH_ALEN; i++) {
		tmp.bssid[i] = bssid[ETH_ALEN-1 - i];
	}

	tmp.beacon_interval = cpu_to_le16(priv->beacon_interval);

	/* Basic rate set. Control frame responses (such as ACK or CTS frames)
	** are sent with one of these rates */
	if (IS_ACX111(priv)) {
		/* It was experimentally determined that rates_basic
		** can take 11g rates as well, not only rates
		** defined with JOINBSS_RATES_BASIC111_nnn.
		** Just use RATE111_nnn constants... */
		tmp.u.acx111.dtim_interval = dtim_interval;
		tmp.u.acx111.rates_basic = cpu_to_le16(priv->rate_basic);
		acxlog(L_ASSOC, "rates_basic:%04X, rates_supported:%04X\n",
			priv->rate_basic, priv->rate_oper);
	} else {
		tmp.u.acx100.dtim_interval = dtim_interval;
		tmp.u.acx100.rates_basic = rate111to5bits(priv->rate_basic);
		tmp.u.acx100.rates_supported = rate111to5bits(priv->rate_oper);
		acxlog(L_ASSOC, "rates_basic:%04X->%02X, "
			"rates_supported:%04X->%02X\n",
			priv->rate_basic, tmp.u.acx100.rates_basic,
			priv->rate_oper, tmp.u.acx100.rates_supported);
	}

	/* Setting up how Beacon, Probe Response, RTS, and PS-Poll frames
	** will be sent (rate/modulation/preamble) */
	tmp.genfrm_txrate = bitpos2genframe_txrate[lowest_bit(priv->rate_basic)];
	tmp.genfrm_mod_pre = 0; /* FIXME: was = priv->capab_short (which is always 0); */
	/* we can use short pre *if* all peers can understand it */
	/* FIXME #2: we need to correctly set PBCC/OFDM bits here too */

	/* we switch fw to STA mode in MONITOR mode, it seems to be
	** the only mode where fw does not emit beacons by itself
	** but allows us to send anything (we really want to retain
	** ability to tx arbitrary frames in MONITOR mode)
	*/
	tmp.macmode = (priv->mode != ACX_MODE_MONITOR ? priv->mode : ACX_MODE_2_STA);
	tmp.channel = priv->channel;
	tmp.essid_len = priv->essid_len;
	/* NOTE: the code memcpy'd essid_len + 1 before, which is WRONG! */
	memcpy(tmp.essid, priv->essid, tmp.essid_len);
	acx_s_issue_cmd(priv, ACX1xx_CMD_JOIN, &tmp, tmp.essid_len + 0x11);

	acxlog(L_ASSOC|L_DEBUG, "BSS_Type = %u\n", tmp.macmode);
	acxlog_mac(L_ASSOC|L_DEBUG, "JoinBSSID MAC:", priv->bssid, "\n");

	acx_update_capabilities(priv);
	FN_EXIT0;
}


/***********************************************************************
** acx_s_cmd_start_scan
**
** Issue scan command to the hardware
*/
static void
acx100_s_scan_chan(wlandevice_t *priv)
{
	acx100_scan_t s;

	FN_ENTER;

	memset(&s, 0, sizeof(s));
	s.count = cpu_to_le16(priv->scan_count);
	s.start_chan = cpu_to_le16(1);
	s.flags = cpu_to_le16(0x8000);
	s.max_rate = priv->scan_rate;
	s.options = priv->scan_mode;
	s.chan_duration = cpu_to_le16(priv->scan_duration);
	s.max_probe_delay = cpu_to_le16(priv->scan_probe_delay);

	acx_s_issue_cmd(priv, ACX1xx_CMD_SCAN, &s, sizeof(s));
	FN_EXIT0;
}

static void
acx111_s_scan_chan(wlandevice_t *priv)
{
	acx111_scan_t s;

	FN_ENTER;

	memset(&s, 0, sizeof(s));
	s.count = cpu_to_le16(priv->scan_count);
	s.channel_list_select = 0; /* scan every allowed channel */
	/*s.channel_list_select = 1;*/ /* scan given channels */
	s.rate = priv->scan_rate;
	s.options = priv->scan_mode;
	s.chan_duration = cpu_to_le16(priv->scan_duration);
	s.max_probe_delay = cpu_to_le16(priv->scan_probe_delay);
	/*s.modulation = 0x40;*/ /* long preamble? OFDM? -> only for active scan */
	s.modulation = 0;
	/*s.channel_list[0] = 6;
	s.channel_list[1] = 4;*/

	acx_s_issue_cmd(priv, ACX1xx_CMD_SCAN, &s, sizeof(s));
	FN_EXIT0;
}

void
acx_s_cmd_start_scan(wlandevice_t *priv)
{
	/* time_before check is 'just in case' thing */
	if (!(priv->irq_status & HOST_INT_SCAN_COMPLETE)
	 && time_before(jiffies, priv->scan_start + 10*HZ)
	) {
		acxlog(L_INIT, "start_scan: seems like previous scan "
		"is still running. Not starting anew. Please report\n");
		return;
	}

	acxlog(L_INIT, "starting radio scan\n");
	/* remember that fw is commanded to do scan */
	priv->scan_start = jiffies;
	CLEAR_BIT(priv->irq_status, HOST_INT_SCAN_COMPLETE);
	/* issue it */
	if (IS_ACX100(priv)) {
		acx100_s_scan_chan(priv);
	} else {
		acx111_s_scan_chan(priv);
	}
}


/***********************************************************************
** acx111 feature config
*/
static int
acx111_s_get_feature_config(wlandevice_t *priv,
		u32 *feature_options, u32 *data_flow_options)
{
	struct acx111_ie_feature_config fc;

	if (!IS_ACX111(priv)) {
		return NOT_OK;
	}

	memset(&fc, 0, sizeof(fc));

	if (OK != acx_s_interrogate(priv, &fc, ACX1xx_IE_FEATURE_CONFIG)) {
		return NOT_OK;
	}
	acxlog(L_DEBUG,
		"got Feature option:0x%X, DataFlow option: 0x%X\n",
		fc.feature_options,
		fc.data_flow_options);

	if (feature_options)
		*feature_options = le32_to_cpu(fc.feature_options);
	if (data_flow_options)
		*data_flow_options = le32_to_cpu(fc.data_flow_options);

	return OK;
}

static int
acx111_s_set_feature_config(wlandevice_t *priv,
	u32 feature_options, u32 data_flow_options,
	unsigned int mode /* 0 == remove, 1 == add, 2 == set */)
{
	struct acx111_ie_feature_config fc;

	if (!IS_ACX111(priv)) {
		return NOT_OK;
	}

	if ((mode < 0) || (mode > 2))
		return NOT_OK;

	if (mode != 2)
		/* need to modify old data */
		acx111_s_get_feature_config(priv, &fc.feature_options, &fc.data_flow_options);
	else {
		/* need to set a completely new value */
		fc.feature_options = 0;
		fc.data_flow_options = 0;
	}

	if (mode == 0) { /* remove */
		CLEAR_BIT(fc.feature_options, cpu_to_le32(feature_options));
		CLEAR_BIT(fc.data_flow_options, cpu_to_le32(data_flow_options));
	} else { /* add or set */
		SET_BIT(fc.feature_options, cpu_to_le32(feature_options));
		SET_BIT(fc.data_flow_options, cpu_to_le32(data_flow_options));
	}

	acxlog(L_DEBUG,
		"old: feature 0x%08X dataflow 0x%08X. mode: %u\n"
		"new: feature 0x%08X dataflow 0x%08X\n",
		feature_options, data_flow_options, mode,
		le32_to_cpu(fc.feature_options),
		le32_to_cpu(fc.data_flow_options));

	if (OK != acx_s_configure(priv, &fc, ACX1xx_IE_FEATURE_CONFIG)) {
		return NOT_OK;
	}

	return OK;
}

static inline int
acx111_s_feature_off(wlandevice_t *priv, u32 f, u32 d)
{
	return acx111_s_set_feature_config(priv, f, d, 0);
}
static inline int
acx111_s_feature_on(wlandevice_t *priv, u32 f, u32 d)
{
	return acx111_s_set_feature_config(priv, f, d, 1);
}
static inline int
acx111_s_feature_set(wlandevice_t *priv, u32 f, u32 d)
{
	return acx111_s_set_feature_config(priv, f, d, 2);
}


/***********************************************************************
** acx100_s_init_memory_pools
*/
static int
acx100_s_init_memory_pools(wlandevice_t *priv, const acx_ie_memmap_t *mmt)
{
	acx100_ie_memblocksize_t MemoryBlockSize;
	acx100_ie_memconfigoption_t MemoryConfigOption;
	int TotalMemoryBlocks;
	int RxBlockNum;
	int TotalRxBlockSize;
	int TxBlockNum;
	int TotalTxBlockSize;

	FN_ENTER;

	/* Let's see if we can follow this:
	   first we select our memory block size (which I think is
	   completely arbitrary) */
	MemoryBlockSize.size = cpu_to_le16(priv->memblocksize);

	/* Then we alert the card to our decision of block size */
	if (OK != acx_s_configure(priv, &MemoryBlockSize, ACX100_IE_BLOCK_SIZE)) {
		goto bad;
	}

	/* We figure out how many total blocks we can create, using
	   the block size we chose, and the beginning and ending
	   memory pointers, i.e.: end-start/size */
	TotalMemoryBlocks = (le32_to_cpu(mmt->PoolEnd) - le32_to_cpu(mmt->PoolStart)) / priv->memblocksize;

	acxlog(L_DEBUG, "TotalMemoryBlocks=%u (%u bytes)\n",
		TotalMemoryBlocks, TotalMemoryBlocks*priv->memblocksize);

	/* MemoryConfigOption.DMA_config bitmask:
			// access to ACX memory is to be done:
	0x00080000	//  using PCI conf space?!
	0x00040000	//  using IO instructions?
	0x00000000	//  using memory access instructions
	0x00020000	// use local memory block linked list (else what?)
	0x00010000	// use host indirect descriptors (else host must access ACX memory?)
	*/
	if (IS_PCI(priv)) {
		MemoryConfigOption.DMA_config = cpu_to_le32(0x30000);
		/* Declare start of the Rx host pool */
		MemoryConfigOption.pRxHostDesc = cpu2acx(priv->rxhostdesc_startphy);
		acxlog(L_DEBUG, "pRxHostDesc 0x%08X, rxhostdesc_startphy 0x%lX\n",
				acx2cpu(MemoryConfigOption.pRxHostDesc),
				(long)priv->rxhostdesc_startphy);
	} else {
		MemoryConfigOption.DMA_config = cpu_to_le32(0x20000);
	}

	/* 50% of the allotment of memory blocks go to tx descriptors */
	TxBlockNum = TotalMemoryBlocks / 2;
	MemoryConfigOption.TxBlockNum = cpu_to_le16(TxBlockNum);

	/* and 50% go to the rx descriptors */
	RxBlockNum = TotalMemoryBlocks - TxBlockNum;
	MemoryConfigOption.RxBlockNum = cpu_to_le16(RxBlockNum);

	/* size of the tx and rx descriptor queues */
	TotalTxBlockSize = TxBlockNum * priv->memblocksize;
	TotalRxBlockSize = RxBlockNum * priv->memblocksize;
	acxlog(L_DEBUG, "TxBlockNum %u RxBlockNum %u TotalTxBlockSize %u "
		"TotalTxBlockSize %u\n", TxBlockNum, RxBlockNum,
		TotalTxBlockSize, TotalRxBlockSize);


	/* align the tx descriptor queue to an alignment of 0x20 (32 bytes) */
	MemoryConfigOption.rx_mem =
		cpu_to_le32((le32_to_cpu(mmt->PoolStart) + 0x1f) & ~0x1f);

	/* align the rx descriptor queue to units of 0x20
	 * and offset it by the tx descriptor queue */
	MemoryConfigOption.tx_mem =
		cpu_to_le32((le32_to_cpu(mmt->PoolStart) + TotalRxBlockSize + 0x1f) & ~0x1f);
	acxlog(L_DEBUG, "rx_mem %08X rx_mem %08X\n",
		MemoryConfigOption.tx_mem, MemoryConfigOption.rx_mem);

	/* alert the device to our decision */
	if (OK != acx_s_configure(priv, &MemoryConfigOption, ACX1xx_IE_MEMORY_CONFIG_OPTIONS)) {
		goto bad;
	}

	/* and tell the device to kick it into gear */
	if (OK != acx_s_issue_cmd(priv, ACX100_CMD_INIT_MEMORY, NULL, 0)) {
		goto bad;
	}
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx100_s_create_dma_regions
**
** Note that this fn messes up heavily with hardware, but we cannot
** lock it (we need to sleep). Not a problem since IRQs can't happen
*/
static int
acx100_s_create_dma_regions(wlandevice_t *priv)
{
	acx100_ie_queueconfig_t queueconf;
	acx_ie_memmap_t memmap;
	int res = NOT_OK;
	u32 tx_queue_start, rx_queue_start;

	FN_ENTER;

	/* read out the acx100 physical start address for the queues */
	if (OK != acx_s_interrogate(priv, &memmap, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	tx_queue_start = le32_to_cpu(memmap.QueueStart);
	rx_queue_start = tx_queue_start + TX_CNT * sizeof(txdesc_t);

	acxlog(L_DEBUG, "initializing Queue Indicator\n");

	memset(&queueconf, 0, sizeof(queueconf));

	/* Not needed for PCI, so we can avoid setting them altogether */
	if (IS_USB(priv)) {
		queueconf.NumTxDesc = USB_TX_CNT;
		queueconf.NumRxDesc = USB_RX_CNT;
	}

	/* calculate size of queues */
	queueconf.AreaSize = cpu_to_le32(
			TX_CNT * sizeof(txdesc_t) +
			RX_CNT * sizeof(rxdesc_t) + 8
			);
	queueconf.NumTxQueues = 1;  /* number of tx queues */
	/* sets the beginning of the tx descriptor queue */
	queueconf.TxQueueStart = memmap.QueueStart;
	/* done by memset: queueconf.TxQueuePri = 0; */
	queueconf.RxQueueStart = cpu_to_le32(rx_queue_start);
	queueconf.QueueOptions = 1;		/* auto reset descriptor */
	/* sets the end of the rx descriptor queue */
	queueconf.QueueEnd = cpu_to_le32(
			rx_queue_start + RX_CNT * sizeof(rxdesc_t)
			);
	/* sets the beginning of the next queue */
	queueconf.HostQueueEnd = cpu_to_le32(le32_to_cpu(queueconf.QueueEnd) + 8);
	if (OK != acx_s_configure(priv, &queueconf, ACX1xx_IE_QUEUE_CONFIG)) {
		goto fail;
	}

	if (IS_PCI(priv)) {
	/* sets the beginning of the rx descriptor queue, after the tx descrs */
		if (OK != acxpci_s_create_hostdesc_queues(priv))
			goto fail;
		acxpci_create_desc_queues(priv, tx_queue_start, rx_queue_start);
	}

	if (OK != acx_s_interrogate(priv, &memmap, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	memmap.PoolStart = cpu_to_le32(
			(le32_to_cpu(memmap.QueueEnd) + 4 + 0x1f) & ~0x1f
			);

	if (OK != acx_s_configure(priv, &memmap, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	if (OK != acx100_s_init_memory_pools(priv, &memmap)) {
		goto fail;
	}

	res = OK;
	goto end;

fail:
	acx_s_msleep(1000); /* ? */
	if (IS_PCI(priv))
		acxpci_free_desc_queues(priv);
end:
	FN_EXIT1(res);
	return res;
}


/***********************************************************************
** acx111_s_create_dma_regions
**
** Note that this fn messes up heavily with hardware, but we cannot
** lock it (we need to sleep). Not a problem since IRQs can't happen
*/
#define ACX111_PERCENT(percent) ((percent)/5)

static int
acx111_s_create_dma_regions(wlandevice_t *priv)
{
	struct acx111_ie_memoryconfig memconf;
	struct acx111_ie_queueconfig queueconf;
	u32 tx_queue_start, rx_queue_start;

	FN_ENTER;

	/* Calculate memory positions and queue sizes */

	/* Set up our host descriptor pool + data pool */
	if (IS_PCI(priv)) {
		if (OK != acxpci_s_create_hostdesc_queues(priv))
			goto fail;
	}

	memset(&memconf, 0, sizeof(memconf));
	/* the number of STAs (STA contexts) to support
	** NB: was set to 1 and everything seemed to work nevertheless... */
	memconf.no_of_stations = cpu_to_le16(VEC_SIZE(priv->sta_list));
	/* specify the memory block size. Default is 256 */
	memconf.memory_block_size = cpu_to_le16(priv->memblocksize);
	/* let's use 50%/50% for tx/rx (specify percentage, units of 5%) */
	memconf.tx_rx_memory_block_allocation = ACX111_PERCENT(50);
	/* set the count of our queues
	** NB: struct acx111_ie_memoryconfig shall be modified
	** if we ever will switch to more than one rx and/or tx queue */
	memconf.count_rx_queues = 1;
	memconf.count_tx_queues = 1;
	/* 0 == Busmaster Indirect Memory Organization, which is what we want
	 * (using linked host descs with their allocated mem).
	 * 2 == Generic Bus Slave */
	/* done by memset: memconf.options = 0; */
	/* let's use 25% for fragmentations and 75% for frame transfers
	 * (specified in units of 5%) */
	memconf.fragmentation = ACX111_PERCENT(75);
	/* Rx descriptor queue config */
	memconf.rx_queue1_count_descs = RX_CNT;
	memconf.rx_queue1_type = 7; /* must be set to 7 */
	/* done by memset: memconf.rx_queue1_prio = 0; low prio */
	if (IS_PCI(priv)) {
		memconf.rx_queue1_host_rx_start = cpu2acx(priv->rxhostdesc_startphy);
	}
	/* Tx descriptor queue config */
	memconf.tx_queue1_count_descs = TX_CNT;
	/* done by memset: memconf.tx_queue1_attributes = 0; lowest priority */

	/* NB1: this looks wrong: (memconf,ACX1xx_IE_QUEUE_CONFIG),
	** (queueconf,ACX1xx_IE_MEMORY_CONFIG_OPTIONS) look swapped, eh?
	** But it is actually correct wrt IE numbers.
	** NB2: sizeof(memconf) == 28 == 0x1c but configure(ACX1xx_IE_QUEUE_CONFIG)
	** writes 0x20 bytes (because same IE for acx100 uses struct acx100_ie_queueconfig
	** which is 4 bytes larger. what a mess. TODO: clean it up) */
	if (OK != acx_s_configure(priv, &memconf, ACX1xx_IE_QUEUE_CONFIG)) {
		goto fail;
	}

	acx_s_interrogate(priv, &queueconf, ACX1xx_IE_MEMORY_CONFIG_OPTIONS);

	tx_queue_start = le32_to_cpu(queueconf.tx1_queue_address);
	rx_queue_start = le32_to_cpu(queueconf.rx1_queue_address);

	acxlog(L_INIT, "dump queue head (from card):\n"
		       "len: %u\n"
		       "tx_memory_block_address: %X\n"
		       "rx_memory_block_address: %X\n"
		       "tx1_queue address: %X\n"
		       "rx1_queue address: %X\n",
		       le16_to_cpu(queueconf.len),
		       le32_to_cpu(queueconf.tx_memory_block_address),
		       le32_to_cpu(queueconf.rx_memory_block_address),
		       tx_queue_start,
		       rx_queue_start);

	if (IS_PCI(priv))
		acxpci_create_desc_queues(priv, tx_queue_start, rx_queue_start);

	FN_EXIT1(OK);
	return OK;
fail:
	if (IS_PCI(priv))
		acxpci_free_desc_queues(priv);

	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx_s_set_defaults
*/
void
acx_s_set_defaults(wlandevice_t *priv)
{
	unsigned long flags;

	FN_ENTER;

	/* query some settings from the card.
	 * NOTE: for some settings, e.g. CCA and ED (ACX100!), an initial
	 * query is REQUIRED, otherwise the card won't work correctly!! */
	priv->get_mask = GETSET_ANTENNA|GETSET_SENSITIVITY|GETSET_STATION_ID|GETSET_REG_DOMAIN;
	/* Only ACX100 supports ED and CCA */
	if (IS_ACX100(priv))
		priv->get_mask |= GETSET_CCA|GETSET_ED_THRESH;

	acx_s_update_card_settings(priv, 0, 0);

	acx_lock(priv, flags);

	/* set our global interrupt mask */
	if (IS_PCI(priv))
		acxpci_set_interrupt_mask(priv);

	priv->led_power = 1; /* LED is active on startup */
	priv->brange_max_quality = 60; /* LED blink max quality is 60 */
	priv->brange_time_last_state_change = jiffies;

	/* copy the MAC address we just got from the card
	 * into our MAC address used during current 802.11 session */
	MAC_COPY(priv->dev_addr, priv->netdev->dev_addr);
	sprintf(priv->essid, "STA%02X%02X%02X",
		priv->dev_addr[3], priv->dev_addr[4], priv->dev_addr[5]);
	priv->essid_len = sizeof("STAxxxxxx") - 1; /* make sure to adapt if changed above! */
	priv->essid_active = 1;

	/* we have a nick field to waste, so why not abuse it
	 * to announce the driver version? ;-) */
	strncpy(priv->nick, "acx " ACX_RELEASE, IW_ESSID_MAX_SIZE);

	if (IS_PCI(priv)) {
		if (IS_ACX111(priv)) {
			/* Hope this is correct, only tested with domain 0x30 */
			acxpci_read_eeprom_byte(priv, 0x16F, &priv->reg_dom_id);
		} else if (priv->eeprom_version < 5) {
			acxpci_read_eeprom_byte(priv, 0x16F, &priv->reg_dom_id);
		} else {
			acxpci_read_eeprom_byte(priv, 0x171, &priv->reg_dom_id);
		}
	}

	priv->channel = 1;
	/* 0xffff would be better, but then we won't get a "scan complete"
	 * interrupt, so our current infrastructure will fail: */
	priv->scan_count = 1;
	priv->scan_mode = ACX_SCAN_OPT_PASSIVE;
	/* Doesn't work for acx100, do it only for acx111 for now */
	if (IS_ACX111(priv)) {
		priv->scan_mode = ACX_SCAN_OPT_ACTIVE;
	}
	priv->scan_duration = 100;
	priv->scan_probe_delay = 200;
	priv->scan_rate = ACX_SCAN_RATE_1;

	priv->auth_alg = WLAN_AUTH_ALG_OPENSYSTEM;
	priv->preamble_mode = 2; /* auto */
	priv->listen_interval = 100;
	priv->beacon_interval = DEFAULT_BEACON_INTERVAL;
	priv->mode = ACX_MODE_2_STA;
	priv->dtim_interval = DEFAULT_DTIM_INTERVAL;

	priv->msdu_lifetime = DEFAULT_MSDU_LIFETIME;
	SET_BIT(priv->set_mask, SET_MSDU_LIFETIME);

	priv->rts_threshold = DEFAULT_RTS_THRESHOLD;

	/* use standard default values for retry limits */
	priv->short_retry = 7; /* max. retries for (short) non-RTS packets */
	priv->long_retry = 4; /* max. retries for long (RTS) packets */
	SET_BIT(priv->set_mask, GETSET_RETRY);

	priv->fallback_threshold = 3;
	priv->stepup_threshold = 10;
	priv->rate_bcast = RATE111_1;
	priv->rate_bcast100 = RATE100_1;
	priv->rate_basic = RATE111_1 | RATE111_2;
	priv->rate_auto = 1;
	if (IS_ACX111(priv)) {
		priv->rate_oper = RATE111_ALL;
	} else {
		priv->rate_oper = RATE111_ACX100_COMPAT;
	}

	/* configure card to do rate fallback when in auto rate mode. */
	SET_BIT(priv->set_mask, SET_RATE_FALLBACK);

	/* Supported Rates element - the rates here are given in units of
	 * 500 kbit/s, plus 0x80 added. See 802.11-1999.pdf item 7.3.2.2 */
	acx_l_update_ratevector(priv);

	priv->capab_short = 0;
	priv->capab_pbcc = 1;
	priv->capab_agility = 0;

	SET_BIT(priv->set_mask, SET_RXCONFIG);

	/* set some more defaults */
	if (IS_ACX111(priv)) {
		/* 30mW (15dBm) is default, at least in my acx111 card: */
		priv->tx_level_dbm = 15;
	} else {
		/* don't use max. level, since it might be dangerous
		 * (e.g. WRT54G people experience
		 * excessive Tx power damage!) */
		priv->tx_level_dbm = 18;
	}
	/* priv->tx_level_auto = 1; */
	SET_BIT(priv->set_mask, GETSET_TXPOWER);

	if (IS_ACX111(priv)) {
		/* start with sensitivity level 1 out of 3: */
		priv->sensitivity = 1;
	}

	/* better re-init the antenna value we got above */
	SET_BIT(priv->set_mask, GETSET_ANTENNA);

	priv->ps_wakeup_cfg = 0;
	priv->ps_listen_interval = 0;
	priv->ps_options = 0;
	priv->ps_hangover_period = 0;
	priv->ps_enhanced_transition_time = 0;
#ifdef POWER_SAVE_80211
	SET_BIT(priv->set_mask, GETSET_POWER_80211);
#endif

	MAC_BCAST(priv->ap);

	acx_unlock(priv, flags);
	acx_lock_unhold(); // hold time 844814 CPU ticks @2GHz

	acx_s_initialize_rx_config(priv);

	FN_EXIT0;
}


/***********************************************************************
** FIXME: this should be solved in a general way for all radio types
** by decoding the radio firmware module,
** since it probably has some standard structure describing how to
** set the power level of the radio module which it controls.
** Or maybe not, since the radio module probably has a function interface
** instead which then manages Tx level programming :-\
*/
static int
acx111_s_set_tx_level(wlandevice_t *priv, u8 level_dbm)
{
	struct acx111_ie_tx_level tx_level;

	/* my acx111 card has two power levels in its configoptions (== EEPROM):
	 * 1 (30mW) [15dBm]
	 * 2 (10mW) [10dBm]
	 * For now, just assume all other acx111 cards have the same.
	 * Ideally we would query it here, but we first need a
	 * standard way to query individual configoptions easily. */
	if (level_dbm <= 12) {
		tx_level.level = 2; /* 10 dBm */
		priv->tx_level_dbm = 10;
	} else {
		tx_level.level = 1; /* 15 dBm */
		priv->tx_level_dbm = 15;
	}
	if (level_dbm != priv->tx_level_dbm)
		acxlog(L_INIT, "acx111 firmware has specific "
			"power levels only: adjusted %d dBm to %d dBm!\n",
			level_dbm, priv->tx_level_dbm);

	return acx_s_configure(priv, &tx_level, ACX1xx_IE_DOT11_TX_POWER_LEVEL);
}

static int
acx_s_set_tx_level(wlandevice_t *priv, u8 level_dbm)
{
	if (IS_ACX111(priv)) {
		return acx111_s_set_tx_level(priv, level_dbm);
	}
	if (IS_PCI(priv)) {
		return acx100pci_s_set_tx_level(priv, level_dbm);
	}
	return OK;
}


/***********************************************************************
*/
#ifdef UNUSED
/* Returns the current tx level (ACX111) */
static u8
acx111_s_get_tx_level(wlandevice_t *priv)
{
	struct acx111_ie_tx_level tx_level;

	tx_level.level = 0;
	acx_s_interrogate(priv, &tx_level, ACX1xx_IE_DOT11_TX_POWER_LEVEL);
	return tx_level.level;
}
#endif


/***********************************************************************
** acx_s_init_mac
*/
int
acx_s_init_mac(netdevice_t *dev)
{
	wlandevice_t *priv = netdev_priv(dev);
	int result = NOT_OK;

	FN_ENTER;

	if (IS_PCI(priv)) {
		priv->memblocksize = 256; /* 256 is default */
		/* try to load radio for both ACX100 and ACX111, since both
		 * chips have at least some firmware versions making use of an
		 * external radio module */
		acxpci_s_upload_radio(priv);
	} else {
		priv->memblocksize = 128;
	}

	if (IS_ACX111(priv)) {
		/* for ACX111, the order is different from ACX100
		   1. init packet templates
		   2. create station context and create dma regions
		   3. init wep default keys
		*/
		if (OK != acx111_s_init_packet_templates(priv))
			goto fail;
		if (OK != acx111_s_create_dma_regions(priv)) {
			printk("%s: acx111_create_dma_regions FAILED\n",
							dev->name);
			goto fail;
		}
	} else {
		if (OK != acx100_s_init_wep(priv))
			goto fail;
		if (OK != acx100_s_init_packet_templates(priv))
			goto fail;
		if (OK != acx100_s_create_dma_regions(priv)) {
			printk("%s: acx100_create_dma_regions FAILED\n",
							dev->name);
			goto fail;
		}
	}

	MAC_COPY(dev->dev_addr, priv->dev_addr);
	result = OK;

fail:
	if (result)
		printk("acx: init_mac() FAILED\n");
	FN_EXIT1(result);
	return result;
}


/*----------------------------------------------------------------
* acx_l_rxmonitor
* Called from IRQ context only
*----------------------------------------------------------------*/
static void
acx_l_rxmonitor(wlandevice_t *priv, const rxbuffer_t *rxbuf)
{
	wlansniffrm_t *msg;
	struct sk_buff *skb;
	void *datap;
	unsigned int skb_len;
	int payload_offset;

	FN_ENTER;

	/* we are in big luck: the acx100 doesn't modify any of the fields */
	/* in the 802.11 frame. just pass this packet into the PF_PACKET */
	/* subsystem. yeah. */
	payload_offset = ((u8*)acx_get_wlan_hdr(priv, rxbuf) - (u8*)rxbuf);
	skb_len = RXBUF_BYTES_USED(rxbuf) - payload_offset;

	/* sanity check */
	if (skb_len > WLAN_A4FR_MAXLEN_WEP) {
		printk("%s: monitor mode panic: oversized frame!\n",
				priv->netdev->name);
		goto end;
	}

	if (priv->netdev->type == ARPHRD_IEEE80211_PRISM)
		skb_len += sizeof(*msg);

	/* allocate skb */
	skb = dev_alloc_skb(skb_len);
	if (!skb) {
		printk("%s: no memory for skb (%u bytes)\n",
				priv->netdev->name, skb_len);
		goto end;
	}

	skb_put(skb, skb_len);

		/* when in raw 802.11 mode, just copy frame as-is */
	if (priv->netdev->type == ARPHRD_IEEE80211)
		datap = skb->data;
	else { /* otherwise, emulate prism header */
		msg = (wlansniffrm_t*)skb->data;
		datap = msg + 1;

		msg->msgcode = WLANSNIFFFRM;
		msg->msglen = sizeof(*msg);
		strncpy(msg->devname, priv->netdev->name, sizeof(msg->devname)-1);
		msg->devname[sizeof(msg->devname)-1] = '\0';

		msg->hosttime.did = WLANSNIFFFRM_hosttime;
		msg->hosttime.status = WLANITEM_STATUS_data_ok;
		msg->hosttime.len = 4;
		msg->hosttime.data = jiffies;

		msg->mactime.did = WLANSNIFFFRM_mactime;
		msg->mactime.status = WLANITEM_STATUS_data_ok;
		msg->mactime.len = 4;
		msg->mactime.data = rxbuf->time;

		msg->channel.did = WLANSNIFFFRM_channel;
		msg->channel.status = WLANITEM_STATUS_data_ok;
		msg->channel.len = 4;
		msg->channel.data = priv->channel;

		msg->rssi.did = WLANSNIFFFRM_rssi;
		msg->rssi.status = WLANITEM_STATUS_no_value;
		msg->rssi.len = 4;
		msg->rssi.data = 0;

		msg->sq.did = WLANSNIFFFRM_sq;
		msg->sq.status = WLANITEM_STATUS_no_value;
		msg->sq.len = 4;
		msg->sq.data = 0;

		msg->signal.did = WLANSNIFFFRM_signal;
		msg->signal.status = WLANITEM_STATUS_data_ok;
		msg->signal.len = 4;
		msg->signal.data = rxbuf->phy_snr;

		msg->noise.did = WLANSNIFFFRM_noise;
		msg->noise.status = WLANITEM_STATUS_data_ok;
		msg->noise.len = 4;
		msg->noise.data = rxbuf->phy_level;

		msg->rate.did = WLANSNIFFFRM_rate;
		msg->rate.status = WLANITEM_STATUS_data_ok;
		msg->rate.len = 4;
		msg->rate.data = rxbuf->phy_plcp_signal / 5;

		msg->istx.did = WLANSNIFFFRM_istx;
		msg->istx.status = WLANITEM_STATUS_data_ok;
		msg->istx.len = 4;
		msg->istx.data = 0;	/* tx=0: it's not a tx packet */

		skb_len -= sizeof(*msg);

		msg->frmlen.did = WLANSNIFFFRM_signal;
		msg->frmlen.status = WLANITEM_STATUS_data_ok;
		msg->frmlen.len = 4;
		msg->frmlen.data = skb_len;
	}

	memcpy(datap, ((unsigned char*)rxbuf)+payload_offset, skb_len);

	skb->dev = priv->netdev;
	skb->dev->last_rx = jiffies;

	skb->mac.raw = skb->data;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_80211_RAW);
	netif_rx(skb);

	priv->stats.rx_packets++;
	priv->stats.rx_bytes += skb->len;
end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_rx_ieee802_11_frame
**
** Called from IRQ context only
*/

/* All these contortions are for saner dup logging
**
** We want: (a) to know about excessive dups
** (b) to not spam kernel log about occasional dups
**
** 1/64 threshold was chosen by running "ping -A"
** It gave "rx: 59 DUPs in 2878 packets" only with 4 parallel
** "ping -A" streams running. */
/* 2005-10-11: bumped up to 1/8
** subtract a $smallint from dup_count in order to
** avoid "2 DUPs in 19 packets" messages */
static inline int
acx_l_handle_dup(wlandevice_t *priv, u16 seq)
{
	if (priv->dup_count) {
		priv->nondup_count++;
		if (time_after(jiffies, priv->dup_msg_expiry)) {
			/* Log only if more than 1 dup in 64 packets */
			if (priv->nondup_count/8 < priv->dup_count-5) {
				printk(KERN_INFO "%s: rx: %d DUPs in "
					"%d packets received in 10 secs\n",
					priv->netdev->name,
					priv->dup_count,
					priv->nondup_count);
			}
			priv->dup_count = 0;
			priv->nondup_count = 0;
		}
	}
	if (unlikely(seq == priv->last_seq_ctrl)) {
		if (!priv->dup_count++)
			priv->dup_msg_expiry = jiffies + 10*HZ;
		priv->stats.rx_errors++;
		return 1; /* a dup */
	}
	priv->last_seq_ctrl = seq;
	return 0;
}

static int
acx_l_rx_ieee802_11_frame(wlandevice_t *priv, rxbuffer_t *rxbuf)
{
	unsigned int ftype, fstype;
	const wlan_hdr_t *hdr;
	int result = NOT_OK;

	FN_ENTER;

	hdr = acx_get_wlan_hdr(priv, rxbuf);

	/* see IEEE 802.11-1999.pdf chapter 7 "MAC frame formats" */
	if ((hdr->fc & WF_FC_PVERi) != 0) {
		printk_ratelimited(KERN_INFO "rx: unsupported 802.11 protocol\n");
		goto end;
	}

	ftype = hdr->fc & WF_FC_FTYPEi;
	fstype = hdr->fc & WF_FC_FSTYPEi;

	switch (ftype) {
	/* check data frames first, for speed */
	case WF_FTYPE_DATAi:
		switch (fstype) {
		case WF_FSTYPE_DATAONLYi:
			if (acx_l_handle_dup(priv, hdr->seq))
				break; /* a dup, simply discard it */

			/* TODO:
			if (WF_FC_FROMTODSi == (hdr->fc & WF_FC_FROMTODSi)) {
				result = acx_l_process_data_frame_wds(priv, rxbuf);
				break;
			}
			*/

			switch (priv->mode) {
			case ACX_MODE_3_AP:
				result = acx_l_process_data_frame_master(priv, rxbuf);
				break;
			case ACX_MODE_0_ADHOC:
			case ACX_MODE_2_STA:
				result = acx_l_process_data_frame_client(priv, rxbuf);
				break;
			}
		case WF_FSTYPE_DATA_CFACKi:
		case WF_FSTYPE_DATA_CFPOLLi:
		case WF_FSTYPE_DATA_CFACK_CFPOLLi:
		case WF_FSTYPE_CFPOLLi:
		case WF_FSTYPE_CFACK_CFPOLLi:
		/*   see above.
			acx_process_class_frame(priv, rxbuf, 3); */
			break;
		case WF_FSTYPE_NULLi:
			/* acx_l_process_NULL_frame(priv, rxbuf, 3); */
			break;
		/* FIXME: same here, see above */
		case WF_FSTYPE_CFACKi:
		default:
			break;
		}
		break;
	case WF_FTYPE_MGMTi:
		result = acx_l_process_mgmt_frame(priv, rxbuf);
		break;
	case WF_FTYPE_CTLi:
		if (fstype == WF_FSTYPE_PSPOLLi)
			result = OK;
		/*   this call is irrelevant, since
		 *   acx_process_class_frame is a stub, so return
		 *   immediately instead.
		 * return acx_process_class_frame(priv, rxbuf, 3); */
		break;
	default:
		break;
	}
end:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_l_process_rxbuf
**
** NB: used by USB code also
*/
void
acx_l_process_rxbuf(wlandevice_t *priv, rxbuffer_t *rxbuf)
{
	struct wlan_hdr *hdr;
	unsigned int buf_len;
	unsigned int qual;
	u16 fc;

	hdr = acx_get_wlan_hdr(priv, rxbuf);
	/* length of frame from control field to last byte of FCS */
	buf_len = RXBUF_BYTES_RCVD(rxbuf);
	fc = le16_to_cpu(hdr->fc);

	if ( ((WF_FC_FSTYPE & fc) != WF_FSTYPE_BEACON)
	  || (acx_debug & L_XFER_BEACON)
	) {
		acxlog(L_XFER|L_DATA, "rx: %s "
			"time:%u len:%u signal:%u SNR:%u macstat:%02X "
			"phystat:%02X phyrate:%u status:%u\n",
			acx_get_packet_type_string(fc),
			le32_to_cpu(rxbuf->time),
			buf_len,
			acx_signal_to_winlevel(rxbuf->phy_level),
			acx_signal_to_winlevel(rxbuf->phy_snr),
			rxbuf->mac_status,
			rxbuf->phy_stat_baseband,
			rxbuf->phy_plcp_signal,
			priv->status);
	}

	if (unlikely(acx_debug & L_DATA)) {
		printk("rx: 802.11 buf[%u]: ", buf_len);
		acx_dump_bytes(hdr, buf_len);
	}

	/* FIXME: should check for Rx errors (rxbuf->mac_status?
	 * discard broken packets - but NOT for monitor!)
	 * and update Rx packet statistics here */

	if (unlikely(priv->mode == ACX_MODE_MONITOR)) {
		acx_l_rxmonitor(priv, rxbuf);
	} else if (likely(buf_len >= WLAN_HDR_A3_LEN)) {
		acx_l_rx_ieee802_11_frame(priv, rxbuf);
	} else {
		acxlog(L_DEBUG|L_XFER|L_DATA,
		       "rx: NOT receiving packet (%s): "
		       "size too small (%u)\n",
		       acx_get_packet_type_string(fc),
		       buf_len);
	}

	/* Now check Rx quality level, AFTER processing packet.
	 * I tried to figure out how to map these levels to dBm
	 * values, but for the life of me I really didn't
	 * manage to get it. Either these values are not meant to
	 * be expressed in dBm, or it's some pretty complicated
	 * calculation. */

#ifdef FROM_SCAN_SOURCE_ONLY
	/* only consider packets originating from the MAC
	 * address of the device that's managing our BSSID.
	 * Disable it for now, since it removes information (levels
	 * from different peers) and slows the Rx path. */
	if (priv->ap_client
	 && mac_is_equal(hdr->a2, priv->ap_client->address)) {
#endif
		priv->wstats.qual.level = acx_signal_to_winlevel(rxbuf->phy_level);
		priv->wstats.qual.noise = acx_signal_to_winlevel(rxbuf->phy_snr);
#ifndef OLD_QUALITY
		qual = acx_signal_determine_quality(priv->wstats.qual.level,
				priv->wstats.qual.noise);
#else
		qual = (priv->wstats.qual.noise <= 100) ?
				100 - priv->wstats.qual.noise : 0;
#endif
		priv->wstats.qual.qual = qual;
		priv->wstats.qual.updated = 7; /* all 3 indicators updated */
#ifdef FROM_SCAN_SOURCE_ONLY
	}
#endif
}


/***********************************************************************
** acx_l_handle_txrate_auto
**
** Theory of operation:
** client->rate_cap is a bitmask of rates client is capable of.
** client->rate_cfg is a bitmask of allowed (configured) rates.
** It is set as a result of iwconfig rate N [auto]
** or iwpriv set_rates "N,N,N N,N,N" commands.
** It can be fixed (e.g. 0x0080 == 18Mbit only),
** auto (0x00ff == 18Mbit or any lower value),
** and code handles any bitmask (0x1081 == try 54Mbit,18Mbit,1Mbit _only_).
**
** client->rate_cur is a value for rate111 field in tx descriptor.
** It is always set to txrate_cfg sans zero or more most significant
** bits. This routine handles selection of new rate_cur value depending on
** outcome of last tx event.
**
** client->rate_100 is a precalculated rate value for acx100
** (we can do without it, but will need to calculate it on each tx).
**
** You cannot configure mixed usage of 5.5 and/or 11Mbit rate
** with PBCC and CCK modulation. Either both at CCK or both at PBCC.
** In theory you can implement it, but so far it is considered not worth doing.
**
** 22Mbit, of course, is PBCC always. */

/* maps acx100 tx descr rate field to acx111 one */
static u16
rate100to111(u8 r)
{
	switch (r) {
	case RATE100_1:	return RATE111_1;
	case RATE100_2:	return RATE111_2;
	case RATE100_5:
	case (RATE100_5 | RATE100_PBCC511):	return RATE111_5;
	case RATE100_11:
	case (RATE100_11 | RATE100_PBCC511):	return RATE111_11;
	case RATE100_22:	return RATE111_22;
	default:
		printk("acx: unexpected acx100 txrate: %u! "
			"Please report\n", r);
		return RATE111_1;
	}
}


void
acx_l_handle_txrate_auto(wlandevice_t *priv, struct client *txc,
			u16 cur, u8 rate100, u16 rate111,
			u8 error, int pkts_to_ignore)
{
	u16 sent_rate;
	int slower_rate_was_used;

	/* vda: hmm. current code will do this:
	** 1. send packets at 11 Mbit, stepup++
	** 2. will try to send at 22Mbit. hardware will see no ACK,
	**    retries at 11Mbit, success. code notes that used rate
	**    is lower. stepup = 0, fallback++
	** 3. repeat step 2 fallback_count times. Fall back to
	**    11Mbit. go to step 1.
	** If stepup_count is large (say, 16) and fallback_count
	** is small (3), this wouldn't be too bad wrt throughput */

	if (unlikely(!cur)) {
		printk("acx: BUG! ratemask is empty\n");
		return; /* or else we may lock up the box */
	}

	/* do some preparations, i.e. calculate the one rate that was
	 * used to send this packet */
	if (IS_ACX111(priv)) {
		sent_rate = 1 << highest_bit(rate111 & RATE111_ALL);
	} else {
		sent_rate = rate100to111(rate100);
	}
	/* sent_rate has only one bit set now, corresponding to tx rate
	 * which was used by hardware to tx this particular packet */

	/* now do the actual auto rate management */
	acxlog(L_XFER, "tx: %sclient=%p/"MACSTR" used=%04X cur=%04X cfg=%04X "
		"__=%u/%u ^^=%u/%u\n",
		(txc->ignore_count > 0) ? "[IGN] " : "",
		txc, MAC(txc->address), sent_rate, cur, txc->rate_cfg,
		txc->fallback_count, priv->fallback_threshold,
		txc->stepup_count, priv->stepup_threshold
	);

	/* we need to ignore old packets already in the tx queue since
	 * they use older rate bytes configured before our last rate change,
	 * otherwise our mechanism will get confused by interpreting old data.
	 * Do it after logging above */
	if (txc->ignore_count) {
		txc->ignore_count--;
		return;
	}

	/* true only if the only nonzero bit in sent_rate is
	** less significant than highest nonzero bit in cur */
	slower_rate_was_used = ( cur > ((sent_rate<<1)-1) );

	if (slower_rate_was_used || error) {
		txc->stepup_count = 0;
		if (++txc->fallback_count <= priv->fallback_threshold)
			return;
		txc->fallback_count = 0;

		/* clear highest 1 bit in cur */
		sent_rate = RATE111_54;
		while (!(cur & sent_rate)) sent_rate >>= 1;
		CLEAR_BIT(cur, sent_rate);
		if (!cur) /* we can't disable all rates! */
			cur = sent_rate;
		acxlog(L_XFER, "tx: falling back to ratemask %04X\n", cur);
		
	} else { /* there was neither lower rate nor error */
		txc->fallback_count = 0;
		if (++txc->stepup_count <= priv->stepup_threshold)
			return;
		txc->stepup_count = 0;

		/* Sanitize. Sort of not needed, but I dont trust hw that much...
		** what if it can report bogus tx rates sometimes? */
		while (!(cur & sent_rate)) sent_rate >>= 1;

		/* try to find a higher sent_rate that isn't yet in our
		 * current set, but is an allowed cfg */
		while (1) {
			sent_rate <<= 1;
			if (sent_rate > txc->rate_cfg)
				/* no higher rates allowed by config */
				return;
			if (!(cur & sent_rate) && (txc->rate_cfg & sent_rate))
				/* found */
				break;
			/* not found, try higher one */
		}
		SET_BIT(cur, sent_rate);
		acxlog(L_XFER, "tx: stepping up to ratemask %04X\n", cur);
	}

	txc->rate_cur = cur;
	txc->ignore_count = pkts_to_ignore;
	/* calculate acx100 style rate byte if needed */
	if (IS_ACX100(priv)) {
		txc->rate_100 = acx_bitpos2rate100[highest_bit(cur)];
	}
}


/***********************************************************************
** acx_i_start_xmit
**
** Called by network core. Can be called outside of process context.
*/
int
acx_i_start_xmit(struct sk_buff *skb, netdevice_t *dev)
{
	wlandevice_t *priv = netdev_priv(dev);
	tx_t *tx;
	void *txbuf;
	unsigned long flags;
	int txresult = NOT_OK;
	int len;

	FN_ENTER;

	if (unlikely(!skb)) {
		/* indicate success */
		txresult = OK;
		goto end_no_unlock;
	}
	if (unlikely(!priv)) {
		goto end_no_unlock;
	}

	acx_lock(priv, flags);

	if (unlikely(!(priv->dev_state_mask & ACX_STATE_IFACE_UP))) {
		goto end;
	}
	if (unlikely(priv->mode == ACX_MODE_OFF)) {
		goto end;
	}
	if (unlikely(acx_queue_stopped(dev))) {
		acxlog(L_DEBUG, "%s: called when queue stopped\n", __func__);
		goto end;
	}
	if (unlikely(ACX_STATUS_4_ASSOCIATED != priv->status)) {
		acxlog(L_XFER, "trying to xmit, but not associated yet: "
			"aborting...\n");
		/* silently drop the packet, since we're not connected yet */
		txresult = OK;
		/* ...but indicate an error nevertheless */
		priv->stats.tx_errors++;
		goto end;
	}

	tx = acx_l_alloc_tx(priv);
	if (unlikely(!tx)) {
		printk_ratelimited("%s: start_xmit: txdesc ring is full, "
			"dropping tx\n", dev->name);
		txresult = NOT_OK;
		goto end;
	}

	txbuf = acx_l_get_txbuf(priv, tx);
	if (!txbuf) {
		/* Card was removed */
		txresult = NOT_OK;
		acx_l_dealloc_tx(priv, tx);
		goto end;
	}
	len = acx_ether_to_txbuf(priv, txbuf, skb);
	if (len < 0) {
		/* Error in packet conversion */
		txresult = NOT_OK;
		acx_l_dealloc_tx(priv, tx);
		goto end;
	}
	acx_l_tx_data(priv, tx, len);
	dev->trans_start = jiffies;

	txresult = OK;
	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;

end:
	acx_unlock(priv, flags);

end_no_unlock:
	if ((txresult == OK) && skb)
		dev_kfree_skb_any(skb);

	FN_EXIT1(txresult);
	return txresult;
}


/***********************************************************************
** acx_l_update_ratevector
**
** Updates priv->rate_supported[_len] according to rate_{basic,oper}
*/
const u8
acx_bitpos2ratebyte[] = {
	DOT11RATEBYTE_1,
	DOT11RATEBYTE_2,
	DOT11RATEBYTE_5_5,
	DOT11RATEBYTE_6_G,
	DOT11RATEBYTE_9_G,
	DOT11RATEBYTE_11,
	DOT11RATEBYTE_12_G,
	DOT11RATEBYTE_18_G,
	DOT11RATEBYTE_22,
	DOT11RATEBYTE_24_G,
	DOT11RATEBYTE_36_G,
	DOT11RATEBYTE_48_G,
	DOT11RATEBYTE_54_G,
};

void
acx_l_update_ratevector(wlandevice_t *priv)
{
	u16 bcfg = priv->rate_basic;
	u16 ocfg = priv->rate_oper;
	u8 *supp = priv->rate_supported;
	const u8 *dot11 = acx_bitpos2ratebyte;

	FN_ENTER;

	while (ocfg) {
		if (ocfg & 1) {
			*supp = *dot11;
			if (bcfg & 1) {
				*supp |= 0x80;
			}
			supp++;
		}
		dot11++;
		ocfg >>= 1;
		bcfg >>= 1;
	}
	priv->rate_supported_len = supp - priv->rate_supported;
	if (acx_debug & L_ASSOC) {
		printk("new ratevector: ");
		acx_dump_bytes(priv->rate_supported, priv->rate_supported_len);
	}
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_sta_list_init
*----------------------------------------------------------------*/
static void
acx_l_sta_list_init(wlandevice_t *priv)
{
	FN_ENTER;
	memset(priv->sta_hash_tab, 0, sizeof(priv->sta_hash_tab));
	memset(priv->sta_list, 0, sizeof(priv->sta_list));
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_sta_list_get_from_hash
*----------------------------------------------------------------*/
static inline client_t*
acx_l_sta_list_get_from_hash(wlandevice_t *priv, const u8 *address)
{
	return priv->sta_hash_tab[address[5] % VEC_SIZE(priv->sta_hash_tab)];
}


/*----------------------------------------------------------------
* acx_l_sta_list_get
*----------------------------------------------------------------*/
client_t*
acx_l_sta_list_get(wlandevice_t *priv, const u8 *address)
{
	client_t *client;
	FN_ENTER;
	client = acx_l_sta_list_get_from_hash(priv, address);
	while (client) {
		if (mac_is_equal(address, client->address)) {
			client->mtime = jiffies;
			break;
		}
		client = client->next;
	}
	FN_EXIT0;
	return client;
}


/*----------------------------------------------------------------
* acx_l_sta_list_del
*----------------------------------------------------------------*/
void
acx_l_sta_list_del(wlandevice_t *priv, client_t *victim)
{
	client_t *client, *next;

	client = acx_l_sta_list_get_from_hash(priv, victim->address);
	next = client;
	/* tricky. next = client on first iteration only,
	** on all other iters next = client->next */
	while (next) {
		if (next == victim) {
			client->next = victim->next;
			/* Overkill */
			memset(victim, 0, sizeof(*victim));
			break;
		}
		client = next;
		next = client->next;
	}
}


/*----------------------------------------------------------------
* acx_l_sta_list_alloc
*
* Never fails - will evict oldest client if needed
*----------------------------------------------------------------*/
static client_t*
acx_l_sta_list_alloc(wlandevice_t *priv)
{
	int i;
	unsigned long age, oldest_age;
	client_t *client, *oldest;

	FN_ENTER;

	oldest = &priv->sta_list[0];
	oldest_age = 0;
	for (i = 0; i < VEC_SIZE(priv->sta_list); i++) {
		client = &priv->sta_list[i];

		if (!client->used) {
			goto found;
		} else {
			age = jiffies - client->mtime;
			if (oldest_age < age) {
				oldest_age = age;
				oldest = client;
			}
		}
	}
	acx_l_sta_list_del(priv, oldest);
	client = oldest;
found:
	memset(client, 0, sizeof(*client));
	FN_EXIT0;
	return client;
}


/*----------------------------------------------------------------
* acx_l_sta_list_add
*
* Never fails - will evict oldest client if needed
*----------------------------------------------------------------*/
/* In case we will reimplement it differently... */
#define STA_LIST_ADD_CAN_FAIL 0

static client_t*
acx_l_sta_list_add(wlandevice_t *priv, const u8 *address)
{
	client_t *client;
	int index;

	FN_ENTER;

	client = acx_l_sta_list_alloc(priv);

	client->mtime = jiffies;
	MAC_COPY(client->address, address);
	client->used = CLIENT_EXIST_1;
	client->auth_alg = WLAN_AUTH_ALG_SHAREDKEY;
	client->auth_step = 1;
	/* give some tentative peer rate values
	** (needed because peer may do auth without probing us first,
	** thus we'll have no idea of peer's ratevector yet).
	** Will be overwritten by scanning or assoc code */
	client->rate_cap = priv->rate_basic;
	client->rate_cfg = priv->rate_basic;
	client->rate_cur = 1 << lowest_bit(priv->rate_basic);

	index = address[5] % VEC_SIZE(priv->sta_hash_tab);
	client->next = priv->sta_hash_tab[index];
	priv->sta_hash_tab[index] = client;

	acxlog_mac(L_ASSOC, "sta_list_add: sta=", address, "\n");

	FN_EXIT0;
	return client;
}


/*----------------------------------------------------------------
* acx_l_sta_list_get_or_add
*
* Never fails - will evict oldest client if needed
*----------------------------------------------------------------*/
static client_t*
acx_l_sta_list_get_or_add(wlandevice_t *priv, const u8 *address)
{
	client_t *client = acx_l_sta_list_get(priv, address);
	if (!client)
		client = acx_l_sta_list_add(priv, address);
	return client;
}


/***********************************************************************
** acx_set_status
**
** This function is called in many atomic regions, must not sleep
**
** This function does not need locking UNLESS you call it
** as acx_set_status(ACX_STATUS_4_ASSOCIATED), bacause this can
** wake queue. This can race with stop_queue elsewhere.
** See acx_stop_queue comment. */
void
acx_set_status(wlandevice_t *priv, u16 new_status)
{
#define QUEUE_OPEN_AFTER_ASSOC 1 /* this really seems to be needed now */
	u16 old_status = priv->status;

	FN_ENTER;

	acxlog(L_ASSOC, "%s(%d):%s\n",
	       __func__, new_status, acx_get_status_name(new_status));

#if WIRELESS_EXT > 13 /* wireless_send_event() and SIOCGIWSCAN */
	/* wireless_send_event never sleeps */
	if (ACX_STATUS_4_ASSOCIATED == new_status) {
		union iwreq_data wrqu;

		wrqu.data.length = 0;
		wrqu.data.flags = 0;
		wireless_send_event(priv->netdev, SIOCGIWSCAN, &wrqu, NULL);

		wrqu.data.length = 0;
		wrqu.data.flags = 0;
		MAC_COPY(wrqu.ap_addr.sa_data, priv->bssid);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->netdev, SIOCGIWAP, &wrqu, NULL);
	} else {
		union iwreq_data wrqu;

		/* send event with empty BSSID to indicate we're not associated */
		MAC_ZERO(wrqu.ap_addr.sa_data);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->netdev, SIOCGIWAP, &wrqu, NULL);
	}
#endif

	priv->status = new_status;

	switch (new_status) {
	case ACX_STATUS_1_SCANNING:
		priv->scan_retries = 0;
		/* 1.0 s initial scan time */
		acx_set_timer(priv, 1000000);
		break;
	case ACX_STATUS_2_WAIT_AUTH:
	case ACX_STATUS_3_AUTHENTICATED:
		priv->auth_or_assoc_retries = 0;
		acx_set_timer(priv, 1500000); /* 1.5 s */
		break;
	}

#if QUEUE_OPEN_AFTER_ASSOC
	if (new_status == ACX_STATUS_4_ASSOCIATED)	{
		if (old_status < ACX_STATUS_4_ASSOCIATED) {
			/* ah, we're newly associated now,
			 * so let's indicate carrier */
			acx_carrier_on(priv->netdev, "after association");
			acx_wake_queue(priv->netdev, "after association");
		}
	} else {
		/* not associated any more, so let's kill carrier */
		if (old_status >= ACX_STATUS_4_ASSOCIATED) {
			acx_carrier_off(priv->netdev, "after losing association");
			acx_stop_queue(priv->netdev, "after losing association");
		}
	}
#endif
	FN_EXIT0;
}


/*------------------------------------------------------------------------------
 * acx_i_timer
 *
 * Fires up periodically. Used to kick scan/auth/assoc if something goes wrong
 *----------------------------------------------------------------------------*/
void
acx_i_timer(unsigned long address)
{
	unsigned long flags;
	wlandevice_t *priv = (wlandevice_t *)address;

	FN_ENTER;

	acx_lock(priv, flags);

	acxlog(L_DEBUG|L_ASSOC, "%s: priv->status=%d (%s)\n",
		__func__, priv->status, acx_get_status_name(priv->status));

	switch (priv->status) {
	case ACX_STATUS_1_SCANNING:
		/* was set to 0 by set_status() */
		if (++priv->scan_retries < 7) {
			acx_set_timer(priv, 1000000);
			/* used to interrogate for scan status.
			** We rely on SCAN_COMPLETE IRQ instead */
			acxlog(L_ASSOC, "continuing scan (%d sec)\n",
					priv->scan_retries);
		} else {
			acxlog(L_ASSOC, "stopping scan\n");
			/* send stop_scan cmd when we leave the interrupt context,
			 * and make a decision what to do next (COMPLETE_SCAN) */
			acx_schedule_task(priv,
				ACX_AFTER_IRQ_CMD_STOP_SCAN + ACX_AFTER_IRQ_COMPLETE_SCAN);
		}
		break;
	case ACX_STATUS_2_WAIT_AUTH:
		/* was set to 0 by set_status() */
		if (++priv->auth_or_assoc_retries < 10) {
			acxlog(L_ASSOC, "resend authen1 request (attempt %d)\n",
					priv->auth_or_assoc_retries + 1);
			acx_l_transmit_authen1(priv);
		} else {
			/* time exceeded: fall back to scanning mode */
			acxlog(L_ASSOC,
			       "authen1 request reply timeout, giving up\n");
			/* we are a STA, need to find AP anyhow */
			acx_set_status(priv, ACX_STATUS_1_SCANNING);
			acx_schedule_task(priv, ACX_AFTER_IRQ_RESTART_SCAN);
		}
		/* used to be 1500000, but some other driver uses 2.5s */
		acx_set_timer(priv, 2500000);
		break;
	case ACX_STATUS_3_AUTHENTICATED:
		/* was set to 0 by set_status() */
		if (++priv->auth_or_assoc_retries < 10) {
			acxlog(L_ASSOC,	"resend assoc request (attempt %d)\n",
					priv->auth_or_assoc_retries + 1);
			acx_l_transmit_assoc_req(priv);
		} else {
			/* time exceeded: give up */
			acxlog(L_ASSOC,
				"association request reply timeout, giving up\n");
			/* we are a STA, need to find AP anyhow */
			acx_set_status(priv, ACX_STATUS_1_SCANNING);
			acx_schedule_task(priv, ACX_AFTER_IRQ_RESTART_SCAN);
		}
		acx_set_timer(priv, 2500000); /* see above */
		break;
	case ACX_STATUS_4_ASSOCIATED:
	default:
		break;
	}

	acx_unlock(priv, flags);

	FN_EXIT0;
}


/*------------------------------------------------------------------------------
 * acx_set_timer
 *
 * Sets the 802.11 state management timer's timeout.
 *----------------------------------------------------------------------------*/
void
acx_set_timer(wlandevice_t *priv, int timeout_us)
{
	FN_ENTER;

	acxlog(L_DEBUG|L_IRQ, "%s(%u ms)\n", __func__, timeout_us/1000);
	if (!(priv->dev_state_mask & ACX_STATE_IFACE_UP)) {
		printk("attempt to set the timer "
			"when the card interface is not up!\n");
		goto end;
	}

	/* first check if the timer was already initialized, THEN modify it */
	if (priv->mgmt_timer.function) {
		mod_timer(&priv->mgmt_timer,
				jiffies + (timeout_us * HZ / 1000000));
	}
end:
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_transmit_assocresp
*
* We are an AP here
*----------------------------------------------------------------*/
static const u8
dot11ratebyte[] = {
	DOT11RATEBYTE_1,
	DOT11RATEBYTE_2,
	DOT11RATEBYTE_5_5,
	DOT11RATEBYTE_6_G,
	DOT11RATEBYTE_9_G,
	DOT11RATEBYTE_11,
	DOT11RATEBYTE_12_G,
	DOT11RATEBYTE_18_G,
	DOT11RATEBYTE_22,
	DOT11RATEBYTE_24_G,
	DOT11RATEBYTE_36_G,
	DOT11RATEBYTE_48_G,
	DOT11RATEBYTE_54_G,
};

static int
find_pos(const u8 *p, int size, u8 v)
{
	int i;
	for (i = 0; i < size; i++)
		if (p[i] == v)
			return i;
	/* printk a message about strange byte? */
	return 0;
}

static void
add_bits_to_ratemasks(u8* ratevec, int len, u16* brate, u16* orate)
{
	while (len--) {
		int n = 1 << find_pos(dot11ratebyte,
				sizeof(dot11ratebyte), *ratevec & 0x7f);
		if (*ratevec & 0x80)
			*brate |= n;
		*orate |= n;
		ratevec++;
	}
}

static int
acx_l_transmit_assocresp(wlandevice_t *priv, const wlan_fr_assocreq_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct assocresp_frame_body *body;
	u8 *p;
	const u8 *da;
	/* const u8 *sa; */
	const u8 *bssid;
	client_t *clt;

	FN_ENTER;

	/* sa = req->hdr->a1; */
	da = req->hdr->a2;
	bssid = req->hdr->a3;

	clt = acx_l_sta_list_get(priv, da);
	if (!clt)
		goto ok;

	/* Assoc without auth is a big no-no */
	/* Let's be liberal: if already assoc'ed STA sends assoc req again,
	** we won't be rude */
	if (clt->used != CLIENT_AUTHENTICATED_2
	 && clt->used != CLIENT_ASSOCIATED_3) {
		acx_l_transmit_deauthen(priv, da, WLAN_MGMT_REASON_CLASS2_NONAUTH);
		goto bad;
	}

	clt->used = CLIENT_ASSOCIATED_3;

	if (clt->aid == 0)
		clt->aid = ++priv->aid;
	clt->cap_info = ieee2host16(*(req->cap_info));

	/* We cheat here a bit. We don't really care which rates are flagged
	** as basic by the client, so we stuff them in single ratemask */
	clt->rate_cap = 0;
	if (req->supp_rates)
		add_bits_to_ratemasks(req->supp_rates->rates,
			req->supp_rates->len, &clt->rate_cap, &clt->rate_cap);
	if (req->ext_rates)
		add_bits_to_ratemasks(req->ext_rates->rates,
			req->ext_rates->len, &clt->rate_cap, &clt->rate_cap);
	/* We can check that client supports all basic rates,
	** and deny assoc if not. But let's be liberal, right? ;) */
	clt->rate_cfg = clt->rate_cap & priv->rate_oper;
	if (!clt->rate_cfg) clt->rate_cfg = 1 << lowest_bit(priv->rate_oper);
	clt->rate_cur = 1 << lowest_bit(clt->rate_cfg);
	if (IS_ACX100(priv))
		clt->rate_100 = acx_bitpos2rate100[lowest_bit(clt->rate_cfg)];
	clt->fallback_count = clt->stepup_count = 0;
	clt->ignore_count = 16;

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_ASSOCRESPi;
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, da);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, bssid);
	head->seq = req->hdr->seq;

	body->cap_info = host2ieee16(priv->capabilities);
	body->status = host2ieee16(0);
	body->aid = host2ieee16(clt->aid);
	p = wlan_fill_ie_rates((u8*)&body->rates, priv->rate_supported_len,
							priv->rate_supported);
	p = wlan_fill_ie_rates_ext(p, priv->rate_supported_len,
							priv->rate_supported);

	acx_l_tx_data(priv, tx, p - (u8*)head);
ok:
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_reassocresp

You may be wondering, just like me, what a hell is ReAuth.
In practice it was seen sent by STA when STA feels like losing connection.

[802.11]

5.4.2.3 Reassociation

Association is sufficient for no-transition message delivery between
IEEE 802.11 stations. Additional functionality is needed to support
BSS-transition mobility. The additional required functionality
is provided by the reassociation service. Reassociation is a DSS.
The reassociation service is invoked to 'move' a current association
from one AP to another. This keeps the DS informed of the current
mapping between AP and STA as the station moves from BSS to BSS within
an ESS. Reassociation also enables changing association attributes
of an established association while the STA remains associated with
the same AP. Reassociation is always initiated by the mobile STA.

5.4.3.1 Authentication
...
A STA may be authenticated with many other STAs at any given instant.

5.4.3.1.1 Preauthentication

Because the authentication process could be time-consuming (depending
on the authentication protocol in use), the authentication service can
be invoked independently of the association service. Preauthentication
is typically done by a STA while it is already associated with an AP
(with which it previously authenticated). IEEE 802.11 does not require
that STAs preauthenticate with APs. However, authentication is required
before an association can be established. If the authentication is left
until reassociation time, this may impact the speed with which a STA can
reassociate between APs, limiting BSS-transition mobility performance.
The use of preauthentication takes the authentication service overhead
out of the time-critical reassociation process.

5.7.3 Reassociation

For a STA to reassociate, the reassociation service causes the following
message to occur:

  Reassociation request

* Message type: Management
* Message subtype: Reassociation request
* Information items:
  - IEEE address of the STA
  - IEEE address of the AP with which the STA will reassociate
  - IEEE address of the AP with which the STA is currently associated
  - ESSID
* Direction of message: From STA to 'new' AP

The address of the current AP is included for efficiency. The inclusion
of the current AP address facilitates MAC reassociation to be independent
of the DS implementation.

  Reassociation response
* Message type: Management
* Message subtype: Reassociation response
* Information items:
  - Result of the requested reassociation. (success/failure)
  - If the reassociation is successful, the response shall include the AID.
* Direction of message: From AP to STA

7.2.3.6 Reassociation Request frame format

The frame body of a management frame of subtype Reassociation Request
contains the information shown in Table 9.

Table 9 Reassociation Request frame body
Order Information
1 Capability information
2 Listen interval
3 Current AP address
4 SSID
5 Supported rates

7.2.3.7 Reassociation Response frame format

The frame body of a management frame of subtype Reassociation Response
contains the information shown in Table 10.

Table 10 Reassociation Response frame body
Order Information
1 Capability information
2 Status code
3 Association ID (AID)
4 Supported rates

*----------------------------------------------------------------*/
static int
acx_l_transmit_reassocresp(wlandevice_t *priv, const wlan_fr_reassocreq_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct reassocresp_frame_body *body;
	u8 *p;
	const u8 *da;
	/* const u8 *sa; */
	const u8 *bssid;
	client_t *clt;

	FN_ENTER;

	/* sa = req->hdr->a1; */
	da = req->hdr->a2;
	bssid = req->hdr->a3;

	/* Must be already authenticated, so it must be in the list */
	clt = acx_l_sta_list_get(priv, da);
	if (!clt)
		goto ok;

	/* Assoc without auth is a big no-no */
	/* Already assoc'ed STAs sending ReAssoc req are ok per 802.11 */
	if (clt->used != CLIENT_AUTHENTICATED_2
	 && clt->used != CLIENT_ASSOCIATED_3) {
		acx_l_transmit_deauthen(priv, da, WLAN_MGMT_REASON_CLASS2_NONAUTH);
		goto bad;
	}

	clt->used = CLIENT_ASSOCIATED_3;
	if (clt->aid == 0) {
		clt->aid = ++priv->aid;
	}
	if (req->cap_info)
		clt->cap_info = ieee2host16(*(req->cap_info));

	/* We cheat here a bit. We don't really care which rates are flagged
	** as basic by the client, so we stuff them in single ratemask */
	clt->rate_cap = 0;
	if (req->supp_rates)
		add_bits_to_ratemasks(req->supp_rates->rates,
			req->supp_rates->len, &clt->rate_cap, &clt->rate_cap);
	if (req->ext_rates)
		add_bits_to_ratemasks(req->ext_rates->rates,
			req->ext_rates->len, &clt->rate_cap, &clt->rate_cap);
	/* We can check that client supports all basic rates,
	** and deny assoc if not. But let's be liberal, right? ;) */
	clt->rate_cfg = clt->rate_cap & priv->rate_oper;
	if (!clt->rate_cfg) clt->rate_cfg = 1 << lowest_bit(priv->rate_oper);
	clt->rate_cur = 1 << lowest_bit(clt->rate_cfg);
	if (IS_ACX100(priv))
		clt->rate_100 = acx_bitpos2rate100[lowest_bit(clt->rate_cfg)];

	clt->fallback_count = clt->stepup_count = 0;
	clt->ignore_count = 16;

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto ok;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto ok;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_REASSOCRESPi;
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, da);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, bssid);
	head->seq = req->hdr->seq;

	/* IEs: 1. caps */
	body->cap_info = host2ieee16(priv->capabilities);
	/* 2. status code */
	body->status = host2ieee16(0);
	/* 3. AID */
	body->aid = host2ieee16(clt->aid);
	/* 4. supp rates */
	p = wlan_fill_ie_rates((u8*)&body->rates, priv->rate_supported_len,
							priv->rate_supported);
	/* 5. ext supp rates */
	p = wlan_fill_ie_rates_ext(p, priv->rate_supported_len,
							priv->rate_supported);

	acx_l_tx_data(priv, tx, p - (u8*)head);
ok:
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx_l_process_disassoc_from_sta
*----------------------------------------------------------------*/
static void
acx_l_process_disassoc_from_sta(wlandevice_t *priv, const wlan_fr_disassoc_t *req)
{
	const u8 *ta;
	client_t *clt;

	FN_ENTER;

	ta = req->hdr->a2;
	clt = acx_l_sta_list_get(priv, ta);
	if (!clt)
		goto end;

	if (clt->used != CLIENT_ASSOCIATED_3
	 && clt->used != CLIENT_AUTHENTICATED_2) {
		/* it's disassociating, but it's
		** not even authenticated! Let it know that */
		acxlog_mac(L_ASSOC|L_XFER, "peer ", ta, "has sent disassoc "
			"req but it is not even auth'ed! sending deauth\n");
		acx_l_transmit_deauthen(priv, ta,
			WLAN_MGMT_REASON_CLASS2_NONAUTH);
		clt->used = CLIENT_EXIST_1;
	} else {
		/* mark it as auth'ed only */
		clt->used = CLIENT_AUTHENTICATED_2;
	}
end:
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_process_deauthen_from_sta
*----------------------------------------------------------------*/
static void
acx_l_process_deauth_from_sta(wlandevice_t *priv, const wlan_fr_deauthen_t *req)
{
	const wlan_hdr_t *hdr;
	client_t *client;

	FN_ENTER;

	hdr = req->hdr;

	if (acx_debug & L_ASSOC) {
		acx_print_mac("DEAUTHEN priv->addr=", priv->dev_addr, " ");
		acx_print_mac("a1", hdr->a1, " ");
		acx_print_mac("a2", hdr->a2, " ");
		acx_print_mac("a3", hdr->a3, " ");
		acx_print_mac("priv->bssid", priv->bssid, "\n");
	}

	if (!mac_is_equal(priv->dev_addr, hdr->a1)) {
		goto end;
	}

	acxlog_mac(L_DEBUG, "STA ", hdr->a2, " sent us deauthen packet\n");

	client = acx_l_sta_list_get(priv, hdr->a2);
	if (!client) {
		goto end;
	}
	client->used = CLIENT_EXIST_1;
end:
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_process_disassoc_from_ap
*----------------------------------------------------------------*/
static void
acx_l_process_disassoc_from_ap(wlandevice_t *priv, const wlan_fr_disassoc_t *req)
{
	FN_ENTER;

	if (!priv->ap_client) {
		/* Hrm, we aren't assoc'ed yet anyhow... */
		goto end;
	}
	if (mac_is_equal(priv->dev_addr, req->hdr->a1)) {
		acx_l_transmit_deauthen(priv, priv->bssid,
				WLAN_MGMT_REASON_DEAUTH_LEAVING);
		/* Start scan anew */
		SET_BIT(priv->set_mask, GETSET_RESCAN);
		acx_schedule_task(priv, ACX_AFTER_IRQ_UPDATE_CARD_CFG);
	}
end:
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_process_deauth_from_ap
*----------------------------------------------------------------*/
static void
acx_l_process_deauth_from_ap(wlandevice_t *priv, const wlan_fr_deauthen_t *req)
{
	FN_ENTER;

	if (!priv->ap_client) {
		/* Hrm, we aren't assoc'ed yet anyhow... */
		goto end;
	}
	/* Chk: is ta is verified to be from our AP? */
	if (mac_is_equal(priv->dev_addr, req->hdr->a1)) {
		acxlog(L_DEBUG, "AP sent us deauth packet\n");
		/* not needed: acx_set_status(priv, ACX_STATUS_1_SCANNING) */
		SET_BIT(priv->set_mask, GETSET_RESCAN);
		acx_schedule_task(priv, ACX_AFTER_IRQ_UPDATE_CARD_CFG);
	}
end:
	FN_EXIT0;
}


/*------------------------------------------------------------------------------
 * acx_l_rx
 *
 * The end of the Rx path. Pulls data from a rxhostdesc into a socket
 * buffer and feeds it to the network stack via netif_rx().
 *
 * Arguments:
 *	rxdesc:	the rxhostdesc to pull the data from
 *	priv:	the acx100 private struct of the interface
 *----------------------------------------------------------------------------*/
static void
acx_l_rx(wlandevice_t *priv, rxbuffer_t *rxbuf)
{
	FN_ENTER;
	if (likely(priv->dev_state_mask & ACX_STATE_IFACE_UP)) {
		struct sk_buff *skb;
		skb = acx_rxbuf_to_ether(priv, rxbuf);
		if (likely(skb)) {
			netif_rx(skb);
			priv->netdev->last_rx = jiffies;
			priv->stats.rx_packets++;
			priv->stats.rx_bytes += skb->len;
		}
	}
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_process_data_frame_master
*----------------------------------------------------------------*/
static int
acx_l_process_data_frame_master(wlandevice_t *priv, rxbuffer_t *rxbuf)
{
	struct wlan_hdr *hdr;
	struct tx *tx;
	void *txbuf;
	int len;
	int result = NOT_OK;

	FN_ENTER;

	hdr = acx_get_wlan_hdr(priv, rxbuf);

	switch (WF_FC_FROMTODSi & hdr->fc) {
	case 0:
	case WF_FC_FROMDSi:
		acxlog(L_DEBUG, "ap->sta or adhoc->adhoc data frame ignored\n");
		goto done;
	case WF_FC_TODSi:
		break;
	default: /* WF_FC_FROMTODSi */
		acxlog(L_DEBUG, "wds data frame ignored (todo)\n");
		goto done;
	}

	/* check if it is our BSSID, if not, leave */
	if (!mac_is_equal(priv->bssid, hdr->a1)) {
		goto done;
	}

	if (mac_is_equal(priv->dev_addr, hdr->a3)) {
		/* this one is for us */
		acx_l_rx(priv, rxbuf);
	} else {
		if (mac_is_bcast(hdr->a3)) {
			/* this one is bcast, rx it too */
			acx_l_rx(priv, rxbuf);
		}
		tx = acx_l_alloc_tx(priv);
		if (!tx) {
			goto fail;
		}
		/* repackage, tx, and hope it someday reaches its destination */
		/* order is important, we do it in-place */
		MAC_COPY(hdr->a1, hdr->a3);
		MAC_COPY(hdr->a3, hdr->a2);
		MAC_COPY(hdr->a2, priv->bssid);
		/* To_DS = 0, From_DS = 1 */
		hdr->fc = WF_FC_FROMDSi + WF_FTYPE_DATAi;

		len = RXBUF_BYTES_RCVD(rxbuf);
		txbuf = acx_l_get_txbuf(priv, tx);
		if (txbuf) {
			memcpy(txbuf, &rxbuf->hdr_a3, len);
			acx_l_tx_data(priv, tx, len);
		} else {
			acx_l_dealloc_tx(priv, tx);
		}
	}
done:
	result = OK;
fail:
	FN_EXIT1(result);
	return result;
}


/*----------------------------------------------------------------
* acx_l_process_data_frame_client
*----------------------------------------------------------------*/
static int
acx_l_process_data_frame_client(wlandevice_t *priv, rxbuffer_t *rxbuf)
{
	const u8 *da, *bssid;
	const wlan_hdr_t *hdr;
	netdevice_t *dev = priv->netdev;
	int result = NOT_OK;

	FN_ENTER;

	if (ACX_STATUS_4_ASSOCIATED != priv->status)
		goto drop;

	hdr = acx_get_wlan_hdr(priv, rxbuf);

	switch (WF_FC_FROMTODSi & hdr->fc) {
	case 0:
		if (priv->mode != ACX_MODE_0_ADHOC) {
			acxlog(L_DEBUG, "adhoc->adhoc data frame ignored\n");
			goto drop;
		}
		bssid = hdr->a3;
		break;
	case WF_FC_FROMDSi:
		if (priv->mode != ACX_MODE_2_STA) {
			acxlog(L_DEBUG, "ap->sta data frame ignored\n");
			goto drop;
		}
		bssid = hdr->a2;
		break;
	case WF_FC_TODSi:
		acxlog(L_DEBUG, "sta->ap data frame ignored\n");
		goto drop;
	default: /* WF_FC_FROMTODSi: wds->wds */
		acxlog(L_DEBUG, "wds data frame ignored (todo)\n");
		goto drop;
	}

	da = hdr->a1;

	if (unlikely(acx_debug & L_DEBUG)) {
		acx_print_mac("rx: da=", da, "");
		acx_print_mac(" bssid=", bssid, "");
		acx_print_mac(" priv->bssid=", priv->bssid, "");
		acx_print_mac(" priv->addr=", priv->dev_addr, "\n");
	}

	/* promiscuous mode --> receive all packets */
	if (unlikely(dev->flags & IFF_PROMISC))
		goto process;

	/* FIRST, check if it is our BSSID */
	if (!mac_is_equal(priv->bssid, bssid)) {
		/* is not our BSSID, so bail out */
		goto drop;
	}

	/* then, check if it is our address */
	if (mac_is_equal(priv->dev_addr, da)) {
		goto process;
	}

	/* then, check if it is broadcast */
	if (mac_is_bcast(da)) {
		goto process;
	}

	if (mac_is_mcast(da)) {
		/* unconditionally receive all multicasts */
		if (dev->flags & IFF_ALLMULTI)
			goto process;

		/* FIXME: check against the list of
		 * multicast addresses that are configured
		 * for the interface (ifconfig) */
		acxlog(L_XFER, "FIXME: multicast packet, need to check "
			"against a list of multicast addresses "
			"(to be created!); accepting packet for now\n");
		/* for now, just accept it here */
		goto process;
	}

	acxlog(L_DEBUG, "rx: foreign packet, dropping\n");
	goto drop;
process:
	/* receive packet */
	acx_l_rx(priv, rxbuf);

	result = OK;
drop:
	FN_EXIT1(result);
	return result;
}


/*----------------------------------------------------------------
* acx_l_process_mgmt_frame
*
* Theory of operation: mgmt packet gets parsed (to make it easy
* to access variable-sized IEs), results stored in 'parsed'.
* Then we react to the packet.
* NB: wlan_mgmt_decode_XXX are dev-independent (shoudnt have been named acx_XXX)
*----------------------------------------------------------------*/
typedef union parsed_mgmt_req {
	wlan_fr_mgmt_t mgmt;
	wlan_fr_assocreq_t assocreq;
	wlan_fr_reassocreq_t reassocreq;
	wlan_fr_assocresp_t assocresp;
	wlan_fr_reassocresp_t reassocresp;
	wlan_fr_beacon_t beacon;
	wlan_fr_disassoc_t disassoc;
	wlan_fr_authen_t authen;
	wlan_fr_deauthen_t deauthen;
	wlan_fr_proberesp_t proberesp;
} parsed_mgmt_req_t;

void BUG_excessive_stack_usage(void);

static int
acx_l_process_mgmt_frame(wlandevice_t *priv, rxbuffer_t *rxbuf)
{
	parsed_mgmt_req_t parsed;	/* takes ~100 bytes of stack */
	wlan_hdr_t *hdr;
	int adhoc, sta_scan, sta, ap;
	int len;

	if (sizeof(parsed) > 256)
		BUG_excessive_stack_usage();

	FN_ENTER;

	hdr = acx_get_wlan_hdr(priv, rxbuf);

	/* Management frames never have these set */
	if (WF_FC_FROMTODSi & hdr->fc) {
		FN_EXIT1(NOT_OK);
		return NOT_OK;
	}

	len = RXBUF_BYTES_RCVD(rxbuf);
	if (WF_FC_ISWEPi & hdr->fc)
		len -= 0x10;

	adhoc = (priv->mode == ACX_MODE_0_ADHOC);
	sta_scan = ((priv->mode == ACX_MODE_2_STA)
		 && (priv->status != ACX_STATUS_4_ASSOCIATED));
	sta = ((priv->mode == ACX_MODE_2_STA)
	    && (priv->status == ACX_STATUS_4_ASSOCIATED));
	ap = (priv->mode == ACX_MODE_3_AP);

	switch (WF_FC_FSTYPEi & hdr->fc) {
	/* beacons first, for speed */
	case WF_FSTYPE_BEACONi:
		memset(&parsed.beacon, 0, sizeof(parsed.beacon));
		parsed.beacon.hdr = hdr;
		parsed.beacon.len = len;
		if (acx_debug & L_DATA) {
			printk("beacon len:%d fc:%04X dur:%04X seq:%04X",
			       len, hdr->fc, hdr->dur, hdr->seq);
			acx_print_mac(" a1:", hdr->a1, "");
			acx_print_mac(" a2:", hdr->a2, "");
			acx_print_mac(" a3:", hdr->a3, "\n");
		}
		wlan_mgmt_decode_beacon(&parsed.beacon);
		/* beacon and probe response are very similar, so... */
		acx_l_process_probe_response(priv, &parsed.beacon, rxbuf);
		break;
	case WF_FSTYPE_ASSOCREQi:
		if (!ap)
			break;
		memset(&parsed.assocreq, 0, sizeof(parsed.assocreq));
		parsed.assocreq.hdr = hdr;
		parsed.assocreq.len = len;
		wlan_mgmt_decode_assocreq(&parsed.assocreq);
		if (mac_is_equal(hdr->a1, priv->bssid)
		 && mac_is_equal(hdr->a3, priv->bssid)) {
			acx_l_transmit_assocresp(priv, &parsed.assocreq);
		}
		break;
	case WF_FSTYPE_REASSOCREQi:
		if (!ap)
			break;
		memset(&parsed.assocreq, 0, sizeof(parsed.assocreq));
		parsed.assocreq.hdr = hdr;
		parsed.assocreq.len = len;
		wlan_mgmt_decode_assocreq(&parsed.assocreq);
		/* reassocreq and assocreq are equivalent */
		acx_l_transmit_reassocresp(priv, &parsed.reassocreq);
		break;
	case WF_FSTYPE_ASSOCRESPi:
		if (!sta_scan)
			break;
		memset(&parsed.assocresp, 0, sizeof(parsed.assocresp));
		parsed.assocresp.hdr = hdr;
		parsed.assocresp.len = len;
		wlan_mgmt_decode_assocresp(&parsed.assocresp);
		acx_l_process_assocresp(priv, &parsed.assocresp);
		break;
	case WF_FSTYPE_REASSOCRESPi:
		if (!sta_scan)
			break;
		memset(&parsed.assocresp, 0, sizeof(parsed.assocresp));
		parsed.assocresp.hdr = hdr;
		parsed.assocresp.len = len;
		wlan_mgmt_decode_assocresp(&parsed.assocresp);
		acx_l_process_reassocresp(priv, &parsed.reassocresp);
		break;
	case WF_FSTYPE_PROBEREQi:
		if (ap || adhoc) {
			/* FIXME: since we're supposed to be an AP,
			** we need to return a Probe Response packet.
			** Currently firmware is doing it for us,
			** but firmware is buggy! See comment elsewhere --vda */
		}
		break;
	case WF_FSTYPE_PROBERESPi:
		memset(&parsed.proberesp, 0, sizeof(parsed.proberesp));
		parsed.proberesp.hdr = hdr;
		parsed.proberesp.len = len;
		wlan_mgmt_decode_proberesp(&parsed.proberesp);
		acx_l_process_probe_response(priv, &parsed.proberesp, rxbuf);
		break;
	case 6:
	case 7:
		/* exit */
		break;
	case WF_FSTYPE_ATIMi:
		/* exit */
		break;
	case WF_FSTYPE_DISASSOCi:
		if (!sta && !ap)
			break;
		memset(&parsed.disassoc, 0, sizeof(parsed.disassoc));
		parsed.disassoc.hdr = hdr;
		parsed.disassoc.len = len;
		wlan_mgmt_decode_disassoc(&parsed.disassoc);
		if (sta)
			acx_l_process_disassoc_from_ap(priv, &parsed.disassoc);
		else
			acx_l_process_disassoc_from_sta(priv, &parsed.disassoc);
		break;
	case WF_FSTYPE_AUTHENi:
		if (!sta_scan && !ap)
			break;
		memset(&parsed.authen, 0, sizeof(parsed.authen));
		parsed.authen.hdr = hdr;
		parsed.authen.len = len;
		wlan_mgmt_decode_authen(&parsed.authen);
		acx_l_process_authen(priv, &parsed.authen);
		break;
	case WF_FSTYPE_DEAUTHENi:
		if (!sta && !ap)
			break;
		memset(&parsed.deauthen, 0, sizeof(parsed.deauthen));
		parsed.deauthen.hdr = hdr;
		parsed.deauthen.len = len;
		wlan_mgmt_decode_deauthen(&parsed.deauthen);
		if (sta)
			acx_l_process_deauth_from_ap(priv, &parsed.deauthen);
		else
			acx_l_process_deauth_from_sta(priv, &parsed.deauthen);
		break;
	}

	FN_EXIT1(OK);
	return OK;
}


#ifdef UNUSED
/*----------------------------------------------------------------
* acx_process_class_frame
*
* Called from IRQ context only
*----------------------------------------------------------------*/
static int
acx_process_class_frame(wlandevice_t *priv, rxbuffer_t *rxbuf, int vala)
{
	return OK;
}
#endif


/*----------------------------------------------------------------
* acx_l_process_NULL_frame
*----------------------------------------------------------------*/
#ifdef BOGUS_ITS_NOT_A_NULL_FRAME_HANDLER_AT_ALL
static int
acx_l_process_NULL_frame(wlandevice_t *priv, rxbuffer_t *rxbuf, int vala)
{
	const signed char *esi;
	const u8 *ebx;
	const wlan_hdr_t *hdr;
	const client_t *client;
	int result = NOT_OK;

	hdr = acx_get_wlan_hdr(priv, rxbuf);

	switch (WF_FC_FROMTODSi & hdr->fc) {
	case 0:
		esi = hdr->a1;
		ebx = hdr->a2;
		break;
	case WF_FC_FROMDSi:
		esi = hdr->a1;
		ebx = hdr->a3;
		break;
	case WF_FC_TODSi:
		esi = hdr->a1;
		ebx = hdr->a2;
		break;
	default: /* WF_FC_FROMTODSi */
		esi = hdr->a1; /* added by me! --vda */
		ebx = hdr->a2;
	}

	if (esi[0x0] < 0) {
		result = OK;
		goto done;
	}

	client = acx_l_sta_list_get(priv, ebx);
	if (client)
		result = NOT_OK;
	else {
#ifdef IS_IT_BROKEN
		acxlog(L_DEBUG|L_XFER, "<transmit_deauth 7>\n");
		acx_l_transmit_deauthen(priv, ebx,
			WLAN_MGMT_REASON_CLASS2_NONAUTH);
#else
		acxlog(L_DEBUG, "received NULL frame from unknown client! "
			"We really shouldn't send deauthen here, right?\n");
#endif
		result = OK;
	}
done:
	return result;
}
#endif


/*----------------------------------------------------------------
* acx_l_process_probe_response
*----------------------------------------------------------------*/
static int
acx_l_process_probe_response(wlandevice_t *priv, wlan_fr_proberesp_t *req,
			const rxbuffer_t *rxbuf)
{
	struct client *bss;
	wlan_hdr_t *hdr;

	FN_ENTER;

	hdr = req->hdr;

	if (mac_is_equal(hdr->a3, priv->dev_addr)) {
		acxlog(L_ASSOC, "huh, scan found our own MAC!?\n");
		goto ok; /* just skip this one silently */
	}

	bss = acx_l_sta_list_get_or_add(priv, hdr->a2);

	/* NB: be careful modifying bss data! It may be one
	** of already known clients (like our AP is we are a STA)
	** Thus do not blindly modify e.g. current ratemask! */

	if (STA_LIST_ADD_CAN_FAIL && !bss) {
		/* uh oh, we found more sites/stations than we can handle with
		 * our current setup: pull the emergency brake and stop scanning! */
		acx_schedule_task(priv, ACX_AFTER_IRQ_CMD_STOP_SCAN);
		/* TODO: a nice comment what below call achieves --vda */
		acx_set_status(priv, ACX_STATUS_2_WAIT_AUTH);
		goto ok;
	}
	/* NB: get_or_add already filled bss->address = hdr->a2 */
	MAC_COPY(bss->bssid, hdr->a3);

	/* copy the ESSID element */
	if (req->ssid && req->ssid->len <= IW_ESSID_MAX_SIZE) {
		bss->essid_len = req->ssid->len;
		memcpy(bss->essid, req->ssid->ssid, req->ssid->len);
		bss->essid[req->ssid->len] = '\0';
	} else {
		/* Either no ESSID IE or oversized one */
		printk("%s: received packet has bogus ESSID\n",
						    priv->netdev->name);
	}

	if (req->ds_parms)
		bss->channel = req->ds_parms->curr_ch;
	if (req->cap_info)
		bss->cap_info = ieee2host16(*req->cap_info);

	bss->sir = acx_signal_to_winlevel(rxbuf->phy_level);
	bss->snr = acx_signal_to_winlevel(rxbuf->phy_snr);

	bss->rate_cap = 0;	/* operational mask */
	bss->rate_bas = 0;	/* basic mask */
	if (req->supp_rates)
		add_bits_to_ratemasks(req->supp_rates->rates,
			req->supp_rates->len, &bss->rate_bas, &bss->rate_cap);
	if (req->ext_rates)
		add_bits_to_ratemasks(req->ext_rates->rates,
			req->ext_rates->len, &bss->rate_bas, &bss->rate_cap);
	/* Fix up any possible bogosity - code elsewhere
	 * is not expecting empty masks */
	if (!bss->rate_cap)
		bss->rate_cap = priv->rate_basic;
	if (!bss->rate_bas)
		bss->rate_bas = 1 << lowest_bit(bss->rate_cap);
	if (!bss->rate_cur)
		bss->rate_cur = 1 << lowest_bit(bss->rate_bas);

	/* People moan about this being too noisy at L_ASSOC */
	acxlog(L_DEBUG,
		"found %s: ESSID='%s' ch=%d "
		"BSSID="MACSTR" caps=0x%04X SIR=%d SNR=%d\n",
		(bss->cap_info & WF_MGMT_CAP_IBSS) ? "Ad-Hoc peer" : "AP",
		bss->essid, bss->channel, MAC(bss->bssid), bss->cap_info,
		bss->sir, bss->snr);
ok:
	FN_EXIT0;
	return OK;
}


/*----------------------------------------------------------------
* acx_l_process_assocresp
*----------------------------------------------------------------*/
static int
acx_l_process_assocresp(wlandevice_t *priv, const wlan_fr_assocresp_t *req)
{
	const wlan_hdr_t *hdr;
	int res = OK;

	FN_ENTER;
	hdr = req->hdr;

	if ((ACX_MODE_2_STA == priv->mode)
	 && mac_is_equal(priv->dev_addr, hdr->a1)) {
		u16 st = ieee2host16(*(req->status));
		if (WLAN_MGMT_STATUS_SUCCESS == st) {
			priv->aid = ieee2host16(*(req->aid));
			/* tell the card we are associated when
			** we are out of interrupt context */
			acx_schedule_task(priv, ACX_AFTER_IRQ_CMD_ASSOCIATE);
		} else {

			/* TODO: we shall delete peer from sta_list, and try
			** other candidates... */

			printk("%s: association FAILED: peer sent "
				"response code %d (%s)\n",
				priv->netdev->name, st, get_status_string(st));
			res = NOT_OK;
		}
	}

	FN_EXIT1(res);
	return res;
}


/*----------------------------------------------------------------
* acx_l_process_reassocresp
*----------------------------------------------------------------*/
static int
acx_l_process_reassocresp(wlandevice_t *priv, const wlan_fr_reassocresp_t *req)
{
	const wlan_hdr_t *hdr;
	int result = NOT_OK;
	u16 st;

	FN_ENTER;
	hdr = req->hdr;

	if (!mac_is_equal(priv->dev_addr, hdr->a1)) {
		goto end;
	}
	st = ieee2host16(*(req->status));
	if (st == WLAN_MGMT_STATUS_SUCCESS) {
		acx_set_status(priv, ACX_STATUS_4_ASSOCIATED);
		result = OK;
	} else {
		printk("%s: reassociation FAILED: peer sent "
			"response code %d (%s)\n",
			priv->netdev->name, st, get_status_string(st));
	}
end:
	FN_EXIT1(result);
	return result;
}


/*----------------------------------------------------------------
* acx_l_process_authen
*
* Called only in STA_SCAN or AP mode
*----------------------------------------------------------------*/
static int
acx_l_process_authen(wlandevice_t *priv, const wlan_fr_authen_t *req)
{
	const wlan_hdr_t *hdr;
	client_t *clt;
	wlan_ie_challenge_t *chal;
	u16 alg, seq, status;
	int ap, result;

	FN_ENTER;

	hdr = req->hdr;

	if (acx_debug & L_ASSOC) {
		acx_print_mac("AUTHEN priv->addr=", priv->dev_addr, " ");
		acx_print_mac("a1=", hdr->a1, " ");
		acx_print_mac("a2=", hdr->a2, " ");
		acx_print_mac("a3=", hdr->a3, " ");
		acx_print_mac("priv->bssid=", priv->bssid, "\n");
	}

	if (!mac_is_equal(priv->dev_addr, hdr->a1)
	 || !mac_is_equal(priv->bssid, hdr->a3)) {
		result = OK;
		goto end;
	}

	alg = ieee2host16(*(req->auth_alg));
	seq = ieee2host16(*(req->auth_seq));
	status = ieee2host16(*(req->status));

	ap = (priv->mode == ACX_MODE_3_AP);

	if (priv->auth_alg <= 1) {
		if (priv->auth_alg != alg) {
			acxlog(L_ASSOC, "auth algorithm mismatch: "
				"our:%d peer:%d\n", priv->auth_alg, alg);
			result = NOT_OK;
			goto end;
		}
	}
	acxlog(L_ASSOC, "algorithm is ok\n");

	if (ap) {
		clt = acx_l_sta_list_get_or_add(priv, hdr->a2);
		if (STA_LIST_ADD_CAN_FAIL && !clt) {
			acxlog(L_ASSOC, "could not allocate room for client\n");
			result = NOT_OK;
			goto end;
		}
	} else {
		clt = priv->ap_client;
		if (!mac_is_equal(clt->address, hdr->a2)) {
			printk("%s: malformed auth frame from AP?!\n",
					priv->netdev->name);
			result = NOT_OK;
			goto end;
		}
	}

	/* now check which step in the authentication sequence we are
	 * currently in, and act accordingly */
	acxlog(L_ASSOC, "acx_process_authen auth seq step %d\n", seq);
	switch (seq) {
	case 1:
		if (!ap)
			break;
		acx_l_transmit_authen2(priv, req, clt);
		break;
	case 2:
		if (ap)
			break;
		if (status == WLAN_MGMT_STATUS_SUCCESS) {
			if (alg == WLAN_AUTH_ALG_OPENSYSTEM) {
				acx_set_status(priv, ACX_STATUS_3_AUTHENTICATED);
				acx_l_transmit_assoc_req(priv);
			} else
			if (alg == WLAN_AUTH_ALG_SHAREDKEY) {
				acx_l_transmit_authen3(priv, req);
			}
		} else {
			printk("%s: auth FAILED: peer sent "
				"response code %d (%s), "
				"still waiting for authentication\n",
				priv->netdev->name,
				status,	get_status_string(status));
			acx_set_status(priv, ACX_STATUS_2_WAIT_AUTH);
		}
		break;
	case 3:
		if (!ap)
			break;
		if ((clt->auth_alg != WLAN_AUTH_ALG_SHAREDKEY)
		 || (alg != WLAN_AUTH_ALG_SHAREDKEY)
		 || (clt->auth_step != 2))
			break;
		chal = req->challenge;
		if (!chal
		 || memcmp(chal->challenge, clt->challenge_text, WLAN_CHALLENGE_LEN)
		 || (chal->eid != WLAN_EID_CHALLENGE)
		 || (chal->len != WLAN_CHALLENGE_LEN)
		)
			break;
		acx_l_transmit_authen4(priv, req);
		MAC_COPY(clt->address, hdr->a2);
		clt->used = CLIENT_AUTHENTICATED_2;
		clt->auth_step = 4;
		clt->seq = ieee2host16(hdr->seq);
		break;
	case 4:
		if (ap)
			break;
		/* ok, we're through: we're authenticated. Woohoo!! */
		acx_set_status(priv, ACX_STATUS_3_AUTHENTICATED);
		acxlog(L_ASSOC, "Authenticated!\n");
		/* now that we're authenticated, request association */
		acx_l_transmit_assoc_req(priv);
		break;
	}
	result = NOT_OK;
end:
	FN_EXIT1(result);
	return result;
}


/*----------------------------------------------------------------
* acx_gen_challenge
*----------------------------------------------------------------*/
static void
acx_gen_challenge(wlan_ie_challenge_t* d)
{
	FN_ENTER;
	d->eid = WLAN_EID_CHALLENGE;
	d->len = WLAN_CHALLENGE_LEN;
	get_random_bytes(d->challenge, WLAN_CHALLENGE_LEN);
	FN_EXIT0;
}


/*----------------------------------------------------------------
* acx_l_transmit_deauthen
*----------------------------------------------------------------*/
static int
acx_l_transmit_deauthen(wlandevice_t *priv, const u8 *addr, u16 reason)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct deauthen_frame_body *body;

	FN_ENTER;

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = (WF_FTYPE_MGMTi | WF_FSTYPE_DEAUTHENi);
	head->dur = 0;
	MAC_COPY(head->da, addr);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, priv->bssid);
	head->seq = 0;

	acxlog(L_DEBUG|L_ASSOC|L_XFER,
		"sending deauthen to "MACSTR" for %d\n",
		MAC(addr), reason);

	body->reason = host2ieee16(reason);

	/* body is fixed size here, but beware of cutting-and-pasting this -
	** do not use sizeof(*body) for variable sized mgmt packets! */
	acx_l_tx_data(priv, tx, WLAN_HDR_A3_LEN + sizeof(*body));

	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_authen1
*----------------------------------------------------------------*/
static int
acx_l_transmit_authen1(wlandevice_t *priv)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;

	FN_ENTER;

	acxlog(L_ASSOC, "sending authentication1 request, "
		"awaiting response\n");

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_AUTHENi;
	head->dur = host2ieee16(0x8000);
	MAC_COPY(head->da, priv->bssid);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, priv->bssid);
	head->seq = 0;

	body->auth_alg = host2ieee16(priv->auth_alg);
	body->auth_seq = host2ieee16(1);
	body->status = host2ieee16(0);

	acx_l_tx_data(priv, tx, WLAN_HDR_A3_LEN + 2 + 2 + 2);

	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_authen2
*----------------------------------------------------------------*/
static int
acx_l_transmit_authen2(wlandevice_t *priv, const wlan_fr_authen_t *req,
		      client_t *clt)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;
	unsigned int packet_len;

	FN_ENTER;

	if (!clt)
		goto ok;

	MAC_COPY(clt->address, req->hdr->a2);
#ifdef UNUSED
	clt->ps = ((WF_FC_PWRMGTi & req->hdr->fc) != 0);
#endif
	clt->auth_alg = ieee2host16(*(req->auth_alg));
	clt->auth_step = 2;
	clt->seq = ieee2host16(req->hdr->seq);

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_AUTHENi;
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, req->hdr->a2);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, req->hdr->a3);
	head->seq = req->hdr->seq;

	/* already in IEEE format, no endianness conversion */
	body->auth_alg = *(req->auth_alg);
	body->auth_seq = host2ieee16(2);
	body->status = host2ieee16(0);

	packet_len = WLAN_HDR_A3_LEN + 2 + 2 + 2;
	if (ieee2host16(*(req->auth_alg)) == WLAN_AUTH_ALG_OPENSYSTEM) {
		clt->used = CLIENT_AUTHENTICATED_2;
	} else {	/* shared key */
		acx_gen_challenge(&body->challenge);
		memcpy(&clt->challenge_text, body->challenge.challenge, WLAN_CHALLENGE_LEN);
		packet_len += 2 + 2 + 2 + 1+1+WLAN_CHALLENGE_LEN;
	}

	acxlog_mac(L_ASSOC|L_XFER,
		"transmit_auth2: BSSID=", head->bssid, "\n");

	acx_l_tx_data(priv, tx, packet_len);
ok:
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_authen3
*----------------------------------------------------------------*/
static int
acx_l_transmit_authen3(wlandevice_t *priv, const wlan_fr_authen_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;
	unsigned int packet_len;

	FN_ENTER;

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto ok;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto ok;
	}
	body = (void*)(head + 1);

	head->fc = WF_FC_ISWEPi + WF_FSTYPE_AUTHENi;
	/* FIXME: is this needed?? authen4 does it...
	head->dur = req->hdr->dur;
	head->seq = req->hdr->seq;
	*/
	MAC_COPY(head->da, priv->bssid);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, priv->bssid);

	/* already in IEEE format, no endianness conversion */
	body->auth_alg = *(req->auth_alg);
	body->auth_seq = host2ieee16(3);
	body->status = host2ieee16(0);
	memcpy(&body->challenge, req->challenge, req->challenge->len + 2);
	packet_len = WLAN_HDR_A3_LEN + 8 + req->challenge->len;

	acxlog(L_ASSOC|L_XFER, "transmit_authen3!\n");

	acx_l_tx_data(priv, tx, packet_len);
ok:
	FN_EXIT1(OK);
	return OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_authen4
*----------------------------------------------------------------*/
static int
acx_l_transmit_authen4(wlandevice_t *priv, const wlan_fr_authen_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;

	FN_ENTER;

	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto ok;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto ok;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_AUTHENi; /* 0xb0 */
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, req->hdr->a2);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, req->hdr->a3);
	head->seq = req->hdr->seq;

	/* already in IEEE format, no endianness conversion */
	body->auth_alg = *(req->auth_alg);
	body->auth_seq = host2ieee16(4);
	body->status = host2ieee16(0);

	acx_l_tx_data(priv, tx, WLAN_HDR_A3_LEN + 2 + 2 + 2);
ok:
	FN_EXIT1(OK);
	return OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_assoc_req
*
* priv->ap_client is a current candidate AP here
*----------------------------------------------------------------*/
static int
acx_l_transmit_assoc_req(wlandevice_t *priv)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	u8 *body, *p, *prate;
	unsigned int packet_len;
	u16 cap;

	FN_ENTER;

	acxlog(L_ASSOC, "sending association request, "
			"awaiting response. NOT ASSOCIATED YET\n");
	tx = acx_l_alloc_tx(priv);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(priv, tx);
	if (!head) {
		acx_l_dealloc_tx(priv, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_ASSOCREQi;
	head->dur = host2ieee16(0x8000);
	MAC_COPY(head->da, priv->bssid);
	MAC_COPY(head->sa, priv->dev_addr);
	MAC_COPY(head->bssid, priv->bssid);
	head->seq = 0;

	p = body;
	/* now start filling the AssocReq frame body */

	/* since this assoc request will most likely only get
	 * sent in the STA to AP case (and not when Ad-Hoc IBSS),
	 * the cap combination indicated here will thus be
	 * WF_MGMT_CAP_ESSi *always* (no IBSS ever)
	 * The specs are more than non-obvious on all that:
	 *
	 * 802.11 7.3.1.4 Capability Information field
	** APs set the ESS subfield to 1 and the IBSS subfield to 0 within
	** Beacon or Probe Response management frames. STAs within an IBSS
	** set the ESS subfield to 0 and the IBSS subfield to 1 in transmitted
	** Beacon or Probe Response management frames
	**
	** APs set the Privacy subfield to 1 within transmitted Beacon,
	** Probe Response, Association Response, and Reassociation Response
	** if WEP is required for all data type frames within the BSS.
	** STAs within an IBSS set the Privacy subfield to 1 in Beacon
	** or Probe Response management frames if WEP is required
	** for all data type frames within the IBSS */

	/* note that returning 0 will be refused by several APs...
	 * (so this indicates that you're probably supposed to
	 * "confirm" the ESS mode) */
	cap = WF_MGMT_CAP_ESSi;

	/* this one used to be a check on wep_restricted,
	 * but more likely it's wep_enabled instead */
	if (priv->wep_enabled)
		SET_BIT(cap, WF_MGMT_CAP_PRIVACYi);

	/* Probably we can just set these always, because our hw is
	** capable of shortpre and PBCC --vda */
	/* only ask for short preamble if the peer station supports it */
	if (priv->ap_client->cap_info & WF_MGMT_CAP_SHORT)
		SET_BIT(cap, WF_MGMT_CAP_SHORTi);
	/* only ask for PBCC support if the peer station supports it */
	if (priv->ap_client->cap_info & WF_MGMT_CAP_PBCC)
		SET_BIT(cap, WF_MGMT_CAP_PBCCi);

	/* IEs: 1. caps */
	*(u16*)p = cap;	p += 2;
	/* 2. listen interval */
	*(u16*)p = host2ieee16(priv->listen_interval); p += 2;
	/* 3. ESSID */
	p = wlan_fill_ie_ssid(p,
			strlen(priv->essid_for_assoc), priv->essid_for_assoc);
	/* 4. supp rates */
	prate = p;
	p = wlan_fill_ie_rates(p,
			priv->rate_supported_len, priv->rate_supported);
	/* 5. ext supp rates */
	p = wlan_fill_ie_rates_ext(p,
			priv->rate_supported_len, priv->rate_supported);

	if (acx_debug & L_DEBUG) {
		printk("association: rates element\n");
		acx_dump_bytes(prate, p - prate);
	}

	/* calculate lengths */
	packet_len = WLAN_HDR_A3_LEN + (p - body);

	acxlog(L_ASSOC, "association: requesting caps 0x%04X, ESSID '%s'\n",
		cap, priv->essid_for_assoc);

	acx_l_tx_data(priv, tx, packet_len);
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/*----------------------------------------------------------------
* acx_l_transmit_disassoc
*
* FIXME: looks like incomplete implementation of a helper:
* acx_l_transmit_disassoc(priv, clt) - kick this client (we're an AP)
* acx_l_transmit_disassoc(priv, NULL) - leave BSSID (we're a STA)
*----------------------------------------------------------------*/
#ifdef BROKEN
int
acx_l_transmit_disassoc(wlandevice_t *priv, client_t *clt)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct disassoc_frame_body *body;

	FN_ENTER;
/*	if (clt != NULL) { */
		tx = acx_l_alloc_tx(priv);
		if (!tx)
			goto bad;
		head = acx_l_get_txbuf(priv, tx);
		if (!head) {
			acx_l_dealloc_tx(priv, tx);
			goto bad;
		}
		body = (void*)(head + 1);

/*		clt->used = CLIENT_AUTHENTICATED_2; - not (yet?) associated */

		head->fc = WF_FSTYPE_DISASSOCi;
		head->dur = 0;
		/* huh? It muchly depends on whether we're STA or AP...
		** sta->ap: da=bssid, sa=own, bssid=bssid
		** ap->sta: da=sta, sa=bssid, bssid=bssid. FIXME! */
		MAC_COPY(head->da, priv->bssid);
		MAC_COPY(head->sa, priv->dev_addr);
		MAC_COPY(head->bssid, priv->dev_addr);
		head->seq = 0;

		/* "Class 3 frame received from nonassociated station." */
		body->reason = host2ieee16(7);

		/* fixed size struct, ok to sizeof */
		acx_l_tx_data(priv, tx, WLAN_HDR_A3_LEN + sizeof(*body));
/*	} */
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}
#endif


/*----------------------------------------------------------------
* acx_s_complete_scan
*
* Called either from after_interrupt_task() if:
* 1) there was Scan_Complete IRQ, or
* 2) scanning expired in timer()
* We need to decide which ESS or IBSS to join.
* Iterates thru priv->sta_list:
*	if priv->ap is not bcast, will join only specified
*	ESS or IBSS with this bssid
*	checks peers' caps for ESS/IBSS bit
*	checks peers' SSID, allows exact match or hidden SSID
* If station to join is chosen:
*	points priv->ap_client to the chosen struct client
*	sets priv->essid_for_assoc for future assoc attempt
* Auth/assoc is not yet performed
* Returns OK if there is no need to restart scan
*----------------------------------------------------------------*/
int
acx_s_complete_scan(wlandevice_t *priv)
{
	struct client *bss;
	unsigned long flags;
	u16 needed_cap;
	int i;
	int idx_found = -1;
	int result = OK;

	FN_ENTER;

	switch (priv->mode) {
	case ACX_MODE_0_ADHOC:
		needed_cap = WF_MGMT_CAP_IBSS; /* 2, we require Ad-Hoc */
		break;
	case ACX_MODE_2_STA:
		needed_cap = WF_MGMT_CAP_ESS; /* 1, we require Managed */
		break;
	default:
		printk("acx: driver bug: mode=%d in complete_scan()\n", priv->mode);
		dump_stack();
		goto end;
	}

	acx_lock(priv, flags);

	/* TODO: sta_iterator hiding implementation would be nice here... */

	for (i = 0; i < VEC_SIZE(priv->sta_list); i++) {
		bss = &priv->sta_list[i];
		if (!bss->used) continue;

		acxlog(L_ASSOC, "scan table: SSID='%s' CH=%d SIR=%d SNR=%d\n",
			bss->essid, bss->channel, bss->sir, bss->snr);

		if (!mac_is_bcast(priv->ap))
			if (!mac_is_equal(bss->bssid, priv->ap))
				continue; /* keep looking */

		/* broken peer with no mode flags set? */
		if (unlikely(!(bss->cap_info & (WF_MGMT_CAP_ESS | WF_MGMT_CAP_IBSS)))) {
			printk("%s: strange peer "MACSTR" found with "
				"neither ESS (AP) nor IBSS (Ad-Hoc) "
				"capability - skipped\n",
				priv->netdev->name, MAC(bss->address));
			continue;
		}
		acxlog(L_ASSOC, "peer_cap 0x%04X, needed_cap 0x%04X\n",
		       bss->cap_info, needed_cap);

		/* does peer station support what we need? */
		if ((bss->cap_info & needed_cap) != needed_cap)
			continue; /* keep looking */

		/* strange peer with NO basic rates?! */
		if (unlikely(!bss->rate_bas)) {
			printk("%s: strange peer "MACSTR" with empty rate set "
				"- skipped\n",
				priv->netdev->name, MAC(bss->address));
			continue;
		}

		/* do we support all basic rates of this peer? */
		if ((bss->rate_bas & priv->rate_oper) != bss->rate_bas)	{
/* we probably need to have all rates as operational rates,
   even in case of an 11M-only configuration */
#ifdef THIS_IS_TROUBLESOME
			printk("%s: peer "MACSTR": incompatible basic rates "
				"(AP requests 0x%04X, we have 0x%04X) "
				"- skipped\n",
				priv->netdev->name, MAC(bss->address),
				bss->rate_bas, priv->rate_oper);
			continue;
#else
			printk("%s: peer "MACSTR": incompatible basic rates "
				"(AP requests 0x%04X, we have 0x%04X). "
				"Considering anyway...\n",
				priv->netdev->name, MAC(bss->address),
				bss->rate_bas, priv->rate_oper);
#endif
		}

		if ( !(priv->reg_dom_chanmask & (1<<(bss->channel-1))) ) {
			printk("%s: warning: peer "MACSTR" is on channel %d "
				"outside of channel range of current "
				"regulatory domain - couldn't join "
				"even if other settings match. "
				"You might want to adapt your config\n",
				priv->netdev->name, MAC(bss->address),
				bss->channel);
			continue; /* keep looking */
		}

		if (!priv->essid_active || !strcmp(bss->essid, priv->essid)) {
			acxlog(L_ASSOC,
			       "found station with matching ESSID! ('%s' "
			       "station, '%s' config)\n",
			       bss->essid,
			       (priv->essid_active) ? priv->essid : "[any]");
			/* TODO: continue looking for peer with better SNR */
			bss->used = CLIENT_JOIN_CANDIDATE;
			idx_found = i;

			/* stop searching if this station is
			 * on the current channel, otherwise
			 * keep looking for an even better match */
			if (bss->channel == priv->channel)
				break;
		} else
		if (!bss->essid[0]
		 || ((' ' == bss->essid[0]) && !bss->essid[1])
		) {
			/* hmm, station with empty or single-space SSID:
			 * using hidden SSID broadcast?
			 */
			/* This behaviour is broken: which AP from zillion
			** of APs with hidden SSID you'd try?
			** We should use Probe requests to get Probe responses
			** and check for real SSID (are those never hidden?) */
			bss->used = CLIENT_JOIN_CANDIDATE;
			if (idx_found == -1)
				idx_found = i;
			acxlog(L_ASSOC, "found station with empty or "
				"single-space (hidden) SSID, considering "
				"for assoc attempt\n");
			/* ...and keep looking for better matches */
		} else {
			acxlog(L_ASSOC, "ESSID doesn't match! ('%s' "
				"station, '%s' config)\n",
				bss->essid,
				(priv->essid_active) ? priv->essid : "[any]");
		}
	}

	/* TODO: iterate thru join candidates instead */
	/* TODO: rescan if not associated within some timeout */
	if (idx_found != -1) {
		char *essid_src;
		size_t essid_len;

		bss = &priv->sta_list[idx_found];
		priv->ap_client = bss;

		if (bss->essid[0] == '\0') {
			/* if the ESSID of the station we found is empty
			 * (no broadcast), then use user configured ESSID
			 * instead */
			essid_src = priv->essid;
			essid_len = priv->essid_len;
		} else {
			essid_src = bss->essid;
			essid_len = strlen(bss->essid);
		}

		acx_update_capabilities(priv);

		memcpy(priv->essid_for_assoc, essid_src, essid_len);
		priv->essid_for_assoc[essid_len] = '\0';
		priv->channel = bss->channel;
		MAC_COPY(priv->bssid, bss->bssid);

		bss->rate_cfg = (bss->rate_cap & priv->rate_oper);
		bss->rate_cur = 1 << lowest_bit(bss->rate_cfg);
		bss->rate_100 = acx_rate111to100(bss->rate_cur);

		acxlog_mac(L_ASSOC,
			"matching station found: ", priv->bssid, ", joining\n");

		/* TODO: do we need to switch to the peer's channel first? */

		if (ACX_MODE_0_ADHOC == priv->mode) {
			acx_set_status(priv, ACX_STATUS_4_ASSOCIATED);
		} else {
			acx_l_transmit_authen1(priv);
			acx_set_status(priv, ACX_STATUS_2_WAIT_AUTH);
		}
	} else { /* idx_found == -1 */
		/* uh oh, no station found in range */
		if (ACX_MODE_0_ADHOC == priv->mode) {
			printk("%s: no matching station found in range, "
				"generating our own IBSS instead\n",
				priv->netdev->name);
			/* we do it hostap way: */
			MAC_COPY(priv->bssid, priv->dev_addr);
			priv->bssid[0] |= 0x02; /* 'local assigned addr' bit */
			/* add IBSS bit to our caps... */
			acx_update_capabilities(priv);
			acx_set_status(priv, ACX_STATUS_4_ASSOCIATED);
			/* In order to cmd_join be called below */
			idx_found = 0;
		} else {
			/* we shall scan again, AP can be
			** just temporarily powered off */
			acxlog(L_ASSOC,
				"no matching station found in range yet\n");
			acx_set_status(priv, ACX_STATUS_1_SCANNING);
			result = NOT_OK;
		}
	}

	acx_unlock(priv, flags);

	if (idx_found != -1) {
		if (ACX_MODE_0_ADHOC == priv->mode) {
			/* need to update channel in beacon template */
			SET_BIT(priv->set_mask, SET_TEMPLATES);
			if (ACX_STATE_IFACE_UP & priv->dev_state_mask)
				acx_s_update_card_settings(priv, 0, 0);
		}
		/* Inform firmware on our decision to start or join BSS */
		acx_s_cmd_join_bssid(priv, priv->bssid);
	}

end:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_s_read_fw
**
** Loads a firmware image
**
** Returns:
**  0				unable to load file
**  pointer to firmware		success
*/
#if USE_FW_LOADER_26
firmware_image_t*
acx_s_read_fw(struct device *dev, const char *file, u32 *size)
#else
#undef acx_s_read_fw
firmware_image_t*
acx_s_read_fw(const char *file, u32 *size)
#endif
{
	firmware_image_t *res;

#if USE_FW_LOADER_LEGACY
	mm_segment_t orgfs;
	unsigned long page;
	char *buffer;
	struct file *inf;
	int retval;
	int offset;
	char *filename;
#endif

#if USE_FW_LOADER_26
	const struct firmware *fw_entry;

	res = NULL;
	acxlog(L_INIT, "requesting firmware image '%s'\n", file);
	if (!request_firmware(&fw_entry, file, dev)) {
		*size = 8;
		if (fw_entry->size >= 8)
			*size = 8 + le32_to_cpu(*(u32 *)(fw_entry->data + 4));
		if (fw_entry->size != *size) {
			printk("acx: firmware size does not match "
				"firmware header: %d != %d, "
				"aborting fw upload\n",
				(int) fw_entry->size, (int) *size);
			goto release_ret;
		}
		res = vmalloc(*size);
		if (!res) {
			printk("acx: no memory for firmware "
				"(%u bytes)\n", *size);
			goto release_ret;
		}
		memcpy(res, fw_entry->data, fw_entry->size);
release_ret:
		release_firmware(fw_entry);
		return res;
	}
	printk("acx: firmware image '%s' was not provided. "
		"Check your hotplug scripts\n", file);
#endif

#if USE_FW_LOADER_LEGACY
	printk("acx: firmware upload via firmware_dir module parameter "
		"is deprecated. Switch to using hotplug\n");

	res = NULL;
	orgfs = get_fs(); /* store original fs */
	set_fs(KERNEL_DS);

	/* Read in whole file then check the size */
	page = __get_free_page(GFP_KERNEL);
	if (unlikely(0 == page)) {
		printk("acx: no memory for firmware upload\n");
		goto fail;
	}

	filename = kmalloc(PATH_MAX, GFP_KERNEL);
	if (unlikely(!filename)) {
		printk("acx: no memory for firmware upload\n");
		goto fail;
	}
	if (!firmware_dir) {
		firmware_dir = "/usr/share/acx";
		acxlog(L_DEBUG, "no firmware directory specified "
			"via module parameter firmware_dir, "
			"using default %s\n", firmware_dir);
	}
	snprintf(filename, PATH_MAX, "%s/%s", firmware_dir, file);
	acxlog(L_INIT, "reading firmware image '%s'\n", filename);

	buffer = (char*)page;

	/* Note that file must be given as absolute path:
	 * a relative path works on first loading,
	 * but any subsequent firmware loading during card
	 * eject/insert will fail, most likely since the first
	 * module loading happens in user space (and thus
	 * filp_open can figure out the absolute path from a
	 * relative path) whereas the card reinsert processing
	 * probably happens in kernel space where you don't have
	 * a current directory to be able to figure out an
	 * absolute path from a relative path... */
	inf = filp_open(filename, O_RDONLY, 0);
	kfree(filename);
	if (OK != IS_ERR(inf)) {
		const char *err;

		switch (-PTR_ERR(inf)) {
			case 2: err = "file not found";
				break;
			default:
				err = "unknown error";
				break;
		}
		printk("acx: error %ld trying to open file '%s': %s\n",
					-PTR_ERR(inf), file, err);
		goto fail;
	}

	if (unlikely((NULL == inf->f_op) || (NULL == inf->f_op->read))) {
		printk("acx: %s does not have a read method?!\n", file);
		goto fail_close;
	}

	offset = 0;
	do {
		retval = inf->f_op->read(inf, buffer, PAGE_SIZE, &inf->f_pos);

		if (unlikely(0 > retval)) {
			printk("acx: error %d reading file '%s'\n",
							-retval, file);
			vfree(res);
			res = NULL;
		} else if (0 == retval) {
			if (0 == offset) {
				printk("acx: firmware image file "
					"'%s' is empty?!\n", file);
			}
		} else if (0 < retval) {
			/* allocate result buffer here if needed,
			 * since we don't want to waste resources/time
			 * (in case file opening/reading fails)
			 * by doing allocation in front of the loop instead. */
			if (NULL == res) {
				*size = 8 + le32_to_cpu(*(u32 *)(4 + buffer));

				res = vmalloc(*size);
				if (NULL == res) {
					printk("acx: unable to "
						"allocate %u bytes for "
						"firmware module upload\n",
						*size);
					goto fail_close;
				}
				acxlog(L_DEBUG, "allocated %u bytes "
					"for firmware module loading\n",
					*size);
			}
			if ((unlikely(offset + retval > *size))) {
				printk("acx: ERROR: allocation "
					"was less than firmware image size?!\n");
				goto fail_close;
			}
			memcpy((u8*)res + offset, buffer, retval);
			offset += retval;
		}
	} while (0 < retval);

fail_close:
	retval = filp_close(inf, NULL);

	if (unlikely(retval)) {
		printk("acx: error %d closing file '%s'\n", -retval, file);
	}

	if (unlikely((NULL != res) && (offset != le32_to_cpu(res->size) + 8))) {
		printk("acx: firmware is reporting a different size "
			"(0x%08X; 0x%08X was read)\n",
			le32_to_cpu(res->size) + 8, offset);
		vfree(res);
		res = NULL;
	}

fail:
	if (page)
		free_page(page);
	set_fs(orgfs);
#endif

	/* checksum will be verified in write_fw, so don't bother here */
	return res;
}


#ifdef POWER_SAVE_80211
/*----------------------------------------------------------------
* acx_s_activate_power_save_mode
*----------------------------------------------------------------*/
static void
acx_s_activate_power_save_mode(wlandevice_t *priv)
{
	acx100_ie_powermgmt_t pm;

	FN_ENTER;

	acx_s_interrogate(priv, &pm, ACX1xx_IE_POWER_MGMT);
	if (pm.wakeup_cfg != 0x81)
		goto end;

	pm.wakeup_cfg = 0;
	pm.options = 0;
	pm.hangover_period = 0;
	acx_s_configure(priv, &pm, ACX1xx_IE_POWER_MGMT);
end:
	FN_EXIT0;
}
#endif


/***********************************************************************
** acx_s_set_wepkey
*/
static void
acx100_s_set_wepkey(wlandevice_t *priv)
{
	ie_dot11WEPDefaultKey_t dk;
	int i;

	for (i = 0; i < DOT11_MAX_DEFAULT_WEP_KEYS; i++) {
		if (priv->wep_keys[i].size != 0) {
			acxlog(L_INIT, "setting WEP key: %d with "
				"total size: %d\n", i, (int) priv->wep_keys[i].size);
			dk.action = 1;
			dk.keySize = priv->wep_keys[i].size;
			dk.defaultKeyNum = i;
			memcpy(dk.key, priv->wep_keys[i].key, dk.keySize);
			acx_s_configure(priv, &dk, ACX100_IE_DOT11_WEP_DEFAULT_KEY_WRITE);
		}
	}
}

static void
acx111_s_set_wepkey(wlandevice_t *priv)
{
	acx111WEPDefaultKey_t dk;
	int i;

	for (i = 0; i < DOT11_MAX_DEFAULT_WEP_KEYS; i++) {
		if (priv->wep_keys[i].size != 0) {
			acxlog(L_INIT, "setting WEP key: %d with "
				"total size: %d\n", i, (int) priv->wep_keys[i].size);
			memset(&dk, 0, sizeof(dk));
			dk.action = cpu_to_le16(1); /* "add key"; yes, that's a 16bit value */
			dk.keySize = priv->wep_keys[i].size;

			/* are these two lines necessary? */
			dk.type = 0;              /* default WEP key */
			dk.index = 0;             /* ignored when setting default key */

			dk.defaultKeyNum = i;
			memcpy(dk.key, priv->wep_keys[i].key, dk.keySize);
			acx_s_issue_cmd(priv, ACX1xx_CMD_WEP_MGMT, &dk, sizeof(dk));
		}
	}
}

static void
acx_s_set_wepkey(wlandevice_t *priv)
{
	if (IS_ACX111(priv))
		acx111_s_set_wepkey(priv);
	else
		acx100_s_set_wepkey(priv);
}


/***********************************************************************
** acx100_s_init_wep
**
** FIXME: this should probably be moved into the new card settings
** management, but since we're also modifying the memory map layout here
** due to the WEP key space we want, we should take care...
*/
int
acx100_s_init_wep(wlandevice_t *priv)
{
	acx100_ie_wep_options_t options;
	ie_dot11WEPDefaultKeyID_t dk;
	acx_ie_memmap_t pt;
	int res = NOT_OK;

	FN_ENTER;

	if (OK != acx_s_interrogate(priv, &pt, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	acxlog(L_DEBUG, "CodeEnd:%X\n", pt.CodeEnd);

	pt.WEPCacheStart = cpu_to_le32(le32_to_cpu(pt.CodeEnd) + 0x4);
	pt.WEPCacheEnd   = cpu_to_le32(le32_to_cpu(pt.CodeEnd) + 0x4);

	if (OK != acx_s_configure(priv, &pt, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	/* let's choose maximum setting: 4 default keys, plus 10 other keys: */
	options.NumKeys = cpu_to_le16(DOT11_MAX_DEFAULT_WEP_KEYS + 10);
	options.WEPOption = 0x00;

	acxlog(L_ASSOC, "%s: writing WEP options\n", __func__);
	acx_s_configure(priv, &options, ACX100_IE_WEP_OPTIONS);

	acx100_s_set_wepkey(priv);

	if (priv->wep_keys[priv->wep_current_index].size != 0) {
		acxlog(L_ASSOC, "setting active default WEP key number: %d\n",
				priv->wep_current_index);
		dk.KeyID = priv->wep_current_index;
		acx_s_configure(priv, &dk, ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET); /* 0x1010 */
	}
	/* FIXME!!! wep_key_struct is filled nowhere! But priv
	 * is initialized to 0, and we don't REALLY need those keys either */
/*		for (i = 0; i < 10; i++) {
		if (priv->wep_key_struct[i].len != 0) {
			MAC_COPY(wep_mgmt.MacAddr, priv->wep_key_struct[i].addr);
			wep_mgmt.KeySize = cpu_to_le16(priv->wep_key_struct[i].len);
			memcpy(&wep_mgmt.Key, priv->wep_key_struct[i].key, le16_to_cpu(wep_mgmt.KeySize));
			wep_mgmt.Action = cpu_to_le16(1);
			acxlog(L_ASSOC, "writing WEP key %d (len %d)\n", i, le16_to_cpu(wep_mgmt.KeySize));
			if (OK == acx_s_issue_cmd(priv, ACX1xx_CMD_WEP_MGMT, &wep_mgmt, sizeof(wep_mgmt))) {
				priv->wep_key_struct[i].index = i;
			}
		}
	}
*/

	/* now retrieve the updated WEPCacheEnd pointer... */
	if (OK != acx_s_interrogate(priv, &pt, ACX1xx_IE_MEMORY_MAP)) {
		printk("%s: ACX1xx_IE_MEMORY_MAP read #2 FAILED\n",
				priv->netdev->name);
		goto fail;
	}
	/* ...and tell it to start allocating templates at that location */
	/* (no endianness conversion needed) */
	pt.PacketTemplateStart = pt.WEPCacheEnd;

	if (OK != acx_s_configure(priv, &pt, ACX1xx_IE_MEMORY_MAP)) {
		printk("%s: ACX1xx_IE_MEMORY_MAP write #2 FAILED\n",
				priv->netdev->name);
		goto fail;
	}
	res = OK;

fail:
	FN_EXIT1(res);
	return res;
}


/***********************************************************************
*/
static int
acx_s_init_max_null_data_template(wlandevice_t *priv)
{
	struct acx_template_nullframe b;
	int result;

	FN_ENTER;
	memset(&b, 0, sizeof(b));
	b.size = cpu_to_le16(sizeof(b) - 2);
	result = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_NULL_DATA, &b, sizeof(b));
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_s_init_max_beacon_template
*/
static int
acx_s_init_max_beacon_template(wlandevice_t *priv)
{
	struct acx_template_beacon b;
	int result;

	FN_ENTER;
	memset(&b, 0, sizeof(b));
	b.size = cpu_to_le16(sizeof(b) - 2);
	result = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_BEACON, &b, sizeof(b));

	FN_EXIT1(result);
	return result;
}

/***********************************************************************
** acx_s_init_max_tim_template
*/
static int
acx_s_init_max_tim_template(wlandevice_t *priv)
{
	acx_template_tim_t t;

	memset(&t, 0, sizeof(t));
	t.size = cpu_to_le16(sizeof(t) - 2);
	return acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_TIM, &t, sizeof(t));
}


/***********************************************************************
** acx_s_init_max_probe_response_template
*/
static int
acx_s_init_max_probe_response_template(wlandevice_t *priv)
{
	struct acx_template_proberesp pr;

	memset(&pr, 0, sizeof(pr));
	pr.size = cpu_to_le16(sizeof(pr) - 2);

	return acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_PROBE_RESPONSE, &pr, sizeof(pr));
}


/***********************************************************************
** acx_s_init_max_probe_request_template
*/
static int
acx_s_init_max_probe_request_template(wlandevice_t *priv)
{
	union {
		acx100_template_probereq_t p100;
		acx111_template_probereq_t p111;
	} pr;
	int res;

	FN_ENTER;
	memset(&pr, 0, sizeof(pr));
	pr.p100.size = cpu_to_le16(sizeof(pr) - 2);
	res = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_PROBE_REQUEST, &pr, sizeof(pr));
	FN_EXIT1(res);
	return res;
}


/***********************************************************************
** acx_s_set_tim_template
**
** In full blown driver we will regularly update partial virtual bitmap
** by calling this function
** (it can be done by irq handler on each DTIM irq or by timer...)

[802.11 7.3.2.6] TIM information element:
- 1 EID
- 1 Length
1 1 DTIM Count
    indicates how many beacons (including this) appear before next DTIM
    (0=this one is a DTIM)
2 1 DTIM Period
    number of beacons between successive DTIMs
    (0=reserved, 1=all TIMs are DTIMs, 2=every other, etc)
3 1 Bitmap Control
    bit0: Traffic Indicator bit associated with Assoc ID 0 (Bcast AID?)
    set to 1 in TIM elements with a value of 0 in the DTIM Count field
    when one or more broadcast or multicast frames are buffered at the AP.
    bit1-7: Bitmap Offset (logically Bitmap_Offset = Bitmap_Control & 0xFE).
4 n Partial Virtual Bitmap
    Visible part of traffic-indication bitmap.
    Full bitmap consists of 2008 bits (251 octets) such that bit number N
    (0<=N<=2007) in the bitmap corresponds to bit number (N mod 8)
    in octet number N/8 where the low-order bit of each octet is bit0,
    and the high order bit is bit7.
    Each set bit in virtual bitmap corresponds to traffic buffered by AP
    for a specific station (with corresponding AID?).
    Partial Virtual Bitmap shows a part of bitmap which has non-zero.
    Bitmap Offset is a number of skipped zero octets (see above).
    'Missing' octets at the tail are also assumed to be zero.
    Example: Length=6, Bitmap_Offset=2, Partial_Virtual_Bitmap=55 55 55
    This means that traffic-indication bitmap is:
    00000000 00000000 01010101 01010101 01010101 00000000 00000000...
    (is bit0 in the map is always 0 and real value is in Bitmap Control bit0?)
*/
static int
acx_s_set_tim_template(wlandevice_t *priv)
{
/* For now, configure smallish test bitmap, all zero ("no pending data") */
	enum { bitmap_size = 5 };

	acx_template_tim_t t;
	int result;

	FN_ENTER;

	memset(&t, 0, sizeof(t));
	t.size = 5 + bitmap_size; /* eid+len+count+period+bmap_ctrl + bmap */
	t.tim_eid = WLAN_EID_TIM;
	t.len = 3 + bitmap_size; /* count+period+bmap_ctrl + bmap */
	result = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_TIM, &t, sizeof(t));
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_fill_beacon_or_proberesp_template
**
** For frame format info, please see 802.11-1999.pdf item 7.2.3.9 and below!!
**
** NB: we use the fact that
** struct acx_template_proberesp and struct acx_template_beacon are the same
** (well, almost...)
**
** [802.11] Beacon's body consist of these IEs:
** 1 Timestamp
** 2 Beacon interval
** 3 Capability information
** 4 SSID
** 5 Supported rates (up to 8 rates)
** 6 FH Parameter Set (frequency-hopping PHYs only)
** 7 DS Parameter Set (direct sequence PHYs only)
** 8 CF Parameter Set (only if PCF is supported)
** 9 IBSS Parameter Set (ad-hoc only)
**
** Beacon only:
** 10 TIM (AP only) (see 802.11 7.3.2.6)
** 11 Country Information (802.11d)
** 12 FH Parameters (802.11d)
** 13 FH Pattern Table (802.11d)
** ... (?!! did not yet find relevant PDF file... --vda)
** 19 ERP Information (extended rate PHYs)
** 20 Extended Supported Rates (if more than 8 rates)
**
** Proberesp only:
** 10 Country information (802.11d)
** 11 FH Parameters (802.11d)
** 12 FH Pattern Table (802.11d)
** 13-n Requested information elements (802.11d)
** ????
** 18 ERP Information (extended rate PHYs)
** 19 Extended Supported Rates (if more than 8 rates)
*/
static int
acx_fill_beacon_or_proberesp_template(wlandevice_t *priv,
					struct acx_template_beacon *templ,
					u16 fc /* in host order! */)
{
	int len;
	u8 *p;

	FN_ENTER;

	memset(templ, 0, sizeof(*templ));
	MAC_BCAST(templ->da);
	MAC_COPY(templ->sa, priv->dev_addr);
	MAC_COPY(templ->bssid, priv->bssid);

	templ->beacon_interval = cpu_to_le16(priv->beacon_interval);
	acx_update_capabilities(priv);
	templ->cap = cpu_to_le16(priv->capabilities);

	p = templ->variable;
	p = wlan_fill_ie_ssid(p, priv->essid_len, priv->essid);
	p = wlan_fill_ie_rates(p, priv->rate_supported_len, priv->rate_supported);
	p = wlan_fill_ie_ds_parms(p, priv->channel);
	/* NB: should go AFTER tim, but acx seem to keep tim last always */
	p = wlan_fill_ie_rates_ext(p, priv->rate_supported_len, priv->rate_supported);

	switch (priv->mode) {
	case ACX_MODE_0_ADHOC:
		/* ATIM window */
		p = wlan_fill_ie_ibss_parms(p, 0); break;
	case ACX_MODE_3_AP:
		/* TIM IE is set up as separate template */
		break;
	}

	len = p - (u8*)templ;
	templ->fc = cpu_to_le16(WF_FTYPE_MGMT | fc);
	/* - 2: do not count 'u16 size' field */
	templ->size = cpu_to_le16(len - 2);

	FN_EXIT1(len);
	return len;
}


/***********************************************************************
** acx_s_set_beacon_template
*/
static int
acx_s_set_beacon_template(wlandevice_t *priv)
{
	struct acx_template_beacon bcn;
	int len, result;

	FN_ENTER;

	len = acx_fill_beacon_or_proberesp_template(priv, &bcn, WF_FSTYPE_BEACON);
	result = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_BEACON, &bcn, len);

	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_s_set_probe_response_template
*/
static int
acx_s_set_probe_response_template(wlandevice_t *priv)
{
	struct acx_template_proberesp pr;
	int len, result;

	FN_ENTER;

	len = acx_fill_beacon_or_proberesp_template(priv, &pr, WF_FSTYPE_PROBERESP);
	result = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_PROBE_RESPONSE, &pr, len);

	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx100_s_init_packet_templates()
**
** NOTE: order is very important here, to have a correct memory layout!
** init templates: max Probe Request (station mode), max NULL data,
** max Beacon, max TIM, max Probe Response.
*/
int
acx100_s_init_packet_templates(wlandevice_t *priv)
{
	acx_ie_memmap_t mm;
	int result = NOT_OK;

	FN_ENTER;

	acxlog(L_DEBUG, "sizeof(memmap)=%d bytes\n", (int)sizeof(mm));

	/* acx100 still do not emit probe requests, thus this call
	** is sourt of not needed. But we want it to work someday */
	if (OK != acx_s_init_max_probe_request_template(priv))
		goto failed;

#ifdef NOT_WORKING_YET
	/* FIXME: creating the NULL data template breaks
	 * communication right now, needs further testing.
	 * Also, need to set the template once we're joining a network. */
	if (OK != acx_s_init_max_null_data_template(priv))
		goto failed;
#endif

	if (OK != acx_s_init_max_beacon_template(priv))
		goto failed;

	if (OK != acx_s_init_max_tim_template(priv))
		goto failed;

	if (OK != acx_s_init_max_probe_response_template(priv))
		goto failed;

	if (OK != acx_s_set_tim_template(priv))
		goto failed;

	if (OK != acx_s_interrogate(priv, &mm, ACX1xx_IE_MEMORY_MAP)) {
		goto failed;
	}

	mm.QueueStart = cpu_to_le32(le32_to_cpu(mm.PacketTemplateEnd) + 4);
	if (OK != acx_s_configure(priv, &mm, ACX1xx_IE_MEMORY_MAP)) {
		goto failed;
	}

	result = OK;
	goto success;

failed:
	acxlog(L_DEBUG|L_INIT,
		/* "cb=0x%X\n" */
		"pACXMemoryMap:\n"
		".CodeStart=0x%X\n"
		".CodeEnd=0x%X\n"
		".WEPCacheStart=0x%X\n"
		".WEPCacheEnd=0x%X\n"
		".PacketTemplateStart=0x%X\n"
		".PacketTemplateEnd=0x%X\n",
		/* len, */
		le32_to_cpu(mm.CodeStart),
		le32_to_cpu(mm.CodeEnd),
		le32_to_cpu(mm.WEPCacheStart),
		le32_to_cpu(mm.WEPCacheEnd),
		le32_to_cpu(mm.PacketTemplateStart),
		le32_to_cpu(mm.PacketTemplateEnd));

success:
	FN_EXIT1(result);
	return result;
}

int
acx111_s_init_packet_templates(wlandevice_t *priv)
{
	int result = NOT_OK;

	FN_ENTER;

	acxlog(L_DEBUG|L_INIT, "initializing max packet templates\n");

	if (OK != acx_s_init_max_probe_request_template(priv))
		goto failed;

	if (OK != acx_s_init_max_null_data_template(priv))
		goto failed;

	if (OK != acx_s_init_max_beacon_template(priv))
		goto failed;

	if (OK != acx_s_init_max_tim_template(priv))
		goto failed;

	if (OK != acx_s_init_max_probe_response_template(priv))
		goto failed;

	/* the other templates will be set later (acx_start) */
	/*
	if (OK != acx_s_set_tim_template(priv))
		goto failed;*/

	result = OK;
	goto success;

failed:
	printk("%s: acx111_init_packet_templates() FAILED\n", priv->netdev->name);

success:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
*/
static int
acx100_s_set_probe_request_template(wlandevice_t *priv)
{
	struct acx100_template_probereq probereq;
	char *p;
	int res;
	int frame_len;

	FN_ENTER;

	memset(&probereq, 0, sizeof(probereq));

	probereq.fc = WF_FTYPE_MGMTi | WF_FSTYPE_PROBEREQi;
	MAC_BCAST(probereq.da);
	MAC_COPY(probereq.sa, priv->dev_addr);
	MAC_BCAST(probereq.bssid);

	probereq.beacon_interval = cpu_to_le16(priv->beacon_interval);
	acx_update_capabilities(priv);
	probereq.cap = cpu_to_le16(priv->capabilities);

	p = probereq.variable;
	acxlog(L_ASSOC, "SSID='%s' len=%d\n", priv->essid, priv->essid_len);
	p = wlan_fill_ie_ssid(p, priv->essid_len, priv->essid);
	p = wlan_fill_ie_rates(p, priv->rate_supported_len, priv->rate_supported);
	p = wlan_fill_ie_rates_ext(p, priv->rate_supported_len, priv->rate_supported);
	frame_len = p - (char*)&probereq;
	probereq.size = frame_len - 2;

	res = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_PROBE_REQUEST, &probereq, frame_len);
	FN_EXIT0;
	return res;
}

static int
acx111_s_set_probe_request_template(wlandevice_t *priv)
{
	struct acx111_template_probereq probereq;
	char *p;
	int res;
	int frame_len;

	FN_ENTER;

	memset(&probereq, 0, sizeof(probereq));

	probereq.fc = WF_FTYPE_MGMTi | WF_FSTYPE_PROBEREQi;
	MAC_BCAST(probereq.da);
	MAC_COPY(probereq.sa, priv->dev_addr);
	MAC_BCAST(probereq.bssid);

	p = probereq.variable;
	p = wlan_fill_ie_ssid(p, priv->essid_len, priv->essid);
	p = wlan_fill_ie_rates(p, priv->rate_supported_len, priv->rate_supported);
	p = wlan_fill_ie_rates_ext(p, priv->rate_supported_len, priv->rate_supported);
	frame_len = p - (char*)&probereq;
	probereq.size = frame_len - 2;

	res = acx_s_issue_cmd(priv, ACX1xx_CMD_CONFIG_PROBE_REQUEST, &probereq, frame_len);
	FN_EXIT0;
	return res;
}

static int
acx_s_set_probe_request_template(wlandevice_t *priv)
{
	if (IS_ACX111(priv)) {
		return acx111_s_set_probe_request_template(priv);
	} else {
		return acx100_s_set_probe_request_template(priv);
	}
}


/***********************************************************************
** acx_s_update_card_settings
**
** Applies accumulated changes in various priv->xxxx members
** Called by ioctl commit handler, acx_start, acx_set_defaults,
** acx_s_after_interrupt_task (if IRQ_CMD_UPDATE_CARD_CFG),
*/
static void
acx111_s_sens_radio_16_17(wlandevice_t *priv)
{
	u32 feature1, feature2;

	if ((priv->sensitivity < 1) || (priv->sensitivity > 3)) {
		printk("%s: invalid sensitivity setting (1..3), "
			"setting to 1\n", priv->netdev->name);
		priv->sensitivity = 1;
	}
	acx111_s_get_feature_config(priv, &feature1, &feature2);
	CLEAR_BIT(feature1, FEATURE1_LOW_RX|FEATURE1_EXTRA_LOW_RX);
	if (priv->sensitivity > 1)
		SET_BIT(feature1, FEATURE1_LOW_RX);
	if (priv->sensitivity > 2)
		SET_BIT(feature1, FEATURE1_EXTRA_LOW_RX);
	acx111_s_feature_set(priv, feature1, feature2);
}

void
acx_s_update_card_settings(wlandevice_t *priv, int get_all, int set_all)
{
	unsigned long flags;
	unsigned int start_scan = 0;
	int i;

	FN_ENTER;

	if (get_all)
		SET_BIT(priv->get_mask, GETSET_ALL);
	if (set_all)
		SET_BIT(priv->set_mask, GETSET_ALL);
	/* Why not just set masks to 0xffffffff? We can get rid of GETSET_ALL */

	acxlog(L_INIT, "get_mask 0x%08X, set_mask 0x%08X\n",
			priv->get_mask, priv->set_mask);

	/* Track dependencies betweed various settings */

	if (priv->set_mask & (GETSET_MODE|GETSET_RESCAN|GETSET_WEP)) {
		acxlog(L_INIT, "important setting has been changed. "
			"Need to update packet templates, too\n");
		SET_BIT(priv->set_mask, SET_TEMPLATES);
	}
	if (priv->set_mask & (GETSET_CHANNEL|GETSET_ALL)) {
		/* This will actually tune RX/TX to the channel */
		SET_BIT(priv->set_mask, GETSET_RX|GETSET_TX);
		switch (priv->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_3_AP:
			/* Beacons contain channel# - update them */
			SET_BIT(priv->set_mask, SET_TEMPLATES);
		}
		switch (priv->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			start_scan = 1;
		}
	}

	/* Apply settings */

#ifdef WHY_SHOULD_WE_BOTHER /* imagine we were just powered off */
	/* send a disassoc request in case it's required */
	if (priv->set_mask & (GETSET_MODE|GETSET_RESCAN|GETSET_CHANNEL|GETSET_WEP|GETSET_ALL)) {
		if (ACX_MODE_2_STA == priv->mode) {
			if (ACX_STATUS_4_ASSOCIATED == priv->status) {
				acxlog(L_ASSOC, "we were ASSOCIATED - "
					"sending disassoc request\n");
				acx_lock(priv, flags);
				acx_l_transmit_disassoc(priv, NULL);
				/* FIXME: deauth? */
				acx_unlock(priv, flags);
			}
			/* need to reset some other stuff as well */
			acxlog(L_DEBUG, "resetting bssid\n");
			MAC_ZERO(priv->bssid);
			SET_BIT(priv->set_mask, SET_TEMPLATES|SET_STA_LIST);
			start_scan = 1;
		}
	}
#endif

	if (priv->get_mask & (GETSET_STATION_ID|GETSET_ALL)) {
		u8 stationID[4 + ACX1xx_IE_DOT11_STATION_ID_LEN];
		const u8 *paddr;

		acx_s_interrogate(priv, &stationID, ACX1xx_IE_DOT11_STATION_ID);
		paddr = &stationID[4];
		for (i = 0; i < ETH_ALEN; i++) {
			/* we copy the MAC address (reversed in
			 * the card) to the netdevice's MAC
			 * address, and on ifup it will be
			 * copied into iwpriv->dev_addr */
			priv->netdev->dev_addr[ETH_ALEN - 1 - i] = paddr[i];
		}
		CLEAR_BIT(priv->get_mask, GETSET_STATION_ID);
	}

	if (priv->get_mask & (GETSET_SENSITIVITY|GETSET_ALL)) {
		if ((RADIO_RFMD_11 == priv->radio_type)
		|| (RADIO_MAXIM_0D == priv->radio_type)
		|| (RADIO_RALINK_15 == priv->radio_type)) {
			acx_s_read_phy_reg(priv, 0x30, &priv->sensitivity);
		} else {
			acxlog(L_INIT, "don't know how to get sensitivity "
				"for radio type 0x%02X\n", priv->radio_type);
			priv->sensitivity = 0;
		}
		acxlog(L_INIT, "got sensitivity value %u\n", priv->sensitivity);

		CLEAR_BIT(priv->get_mask, GETSET_SENSITIVITY);
	}

	if (priv->get_mask & (GETSET_ANTENNA|GETSET_ALL)) {
		u8 antenna[4 + ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN];

		memset(antenna, 0, sizeof(antenna));
		acx_s_interrogate(priv, antenna, ACX1xx_IE_DOT11_CURRENT_ANTENNA);
		priv->antenna = antenna[4];
		acxlog(L_INIT, "got antenna value 0x%02X\n", priv->antenna);
		CLEAR_BIT(priv->get_mask, GETSET_ANTENNA);
	}

	if (priv->get_mask & (GETSET_ED_THRESH|GETSET_ALL)) {
		if (IS_ACX100(priv))	{
			u8 ed_threshold[4 + ACX100_IE_DOT11_ED_THRESHOLD_LEN];

			memset(ed_threshold, 0, sizeof(ed_threshold));
			acx_s_interrogate(priv, ed_threshold, ACX100_IE_DOT11_ED_THRESHOLD);
			priv->ed_threshold = ed_threshold[4];
		} else {
			acxlog(L_INIT, "acx111 doesn't support ED\n");
			priv->ed_threshold = 0;
		}
		acxlog(L_INIT, "got Energy Detect (ED) threshold %u\n", priv->ed_threshold);
		CLEAR_BIT(priv->get_mask, GETSET_ED_THRESH);
	}

	if (priv->get_mask & (GETSET_CCA|GETSET_ALL)) {
		if (IS_ACX100(priv))	{
			u8 cca[4 + ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN];

			memset(cca, 0, sizeof(priv->cca));
			acx_s_interrogate(priv, cca, ACX1xx_IE_DOT11_CURRENT_CCA_MODE);
			priv->cca = cca[4];
		} else {
			acxlog(L_INIT, "acx111 doesn't support CCA\n");
			priv->cca = 0;
		}
		acxlog(L_INIT, "got Channel Clear Assessment (CCA) value %u\n", priv->cca);
		CLEAR_BIT(priv->get_mask, GETSET_CCA);
	}

	if (priv->get_mask & (GETSET_REG_DOMAIN|GETSET_ALL)) {
		acx_ie_generic_t dom;

		acx_s_interrogate(priv, &dom, ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN);
		priv->reg_dom_id = dom.m.bytes[0];
		/* FIXME: should also set chanmask somehow */
		acxlog(L_INIT, "got regulatory domain 0x%02X\n", priv->reg_dom_id);
		CLEAR_BIT(priv->get_mask, GETSET_REG_DOMAIN);
	}

	if (priv->set_mask & (GETSET_STATION_ID|GETSET_ALL)) {
		u8 stationID[4 + ACX1xx_IE_DOT11_STATION_ID_LEN];
		u8 *paddr;

		paddr = &stationID[4];
		for (i = 0; i < ETH_ALEN; i++) {
			/* copy the MAC address we obtained when we noticed
			 * that the ethernet iface's MAC changed
			 * to the card (reversed in
			 * the card!) */
			paddr[i] = priv->dev_addr[ETH_ALEN - 1 - i];
		}
		acx_s_configure(priv, &stationID, ACX1xx_IE_DOT11_STATION_ID);
		CLEAR_BIT(priv->set_mask, GETSET_STATION_ID);
	}

	if (priv->set_mask & (SET_TEMPLATES|GETSET_ALL)) {
		acxlog(L_INIT, "updating packet templates\n");
		/* Doesn't work for acx100, do it only for acx111 for now */
		if (IS_ACX111(priv)) {
			switch (priv->mode) {
			case ACX_MODE_0_ADHOC:
			case ACX_MODE_2_STA:
				acx_s_set_probe_request_template(priv);
			}
		}
		switch (priv->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_3_AP:
			acx_s_set_beacon_template(priv);
			acx_s_set_tim_template(priv);
			/* BTW acx111 firmware would not send probe responses
			** if probe request does not have all basic rates flagged
			** by 0x80! Thus firmware does not conform to 802.11,
			** it should ignore 0x80 bit in ratevector from STA.
			** We can 'fix' it by not using this template and
			** sending probe responses by hand. TODO --vda */
			acx_s_set_probe_response_template(priv);
		}
		/* Needed if generated frames are to be emitted at different tx rate now */
		acxlog(L_IRQ, "redoing cmd_join_bssid() after template cfg\n");
		acx_s_cmd_join_bssid(priv, priv->bssid);
		CLEAR_BIT(priv->set_mask, SET_TEMPLATES);
	}
	if (priv->set_mask & (SET_STA_LIST|GETSET_ALL)) {
		acx_lock(priv, flags);
		acx_l_sta_list_init(priv);
		CLEAR_BIT(priv->set_mask, SET_STA_LIST);
		acx_unlock(priv, flags);
	}
	if (priv->set_mask & (SET_RATE_FALLBACK|GETSET_ALL)) {
		u8 rate[4 + ACX1xx_IE_RATE_FALLBACK_LEN];

		/* configure to not do fallbacks when not in auto rate mode */
		rate[4] = (priv->rate_auto) ? /* priv->txrate_fallback_retries */ 1 : 0;
		acxlog(L_INIT, "updating Tx fallback to %u retries\n", rate[4]);
		acx_s_configure(priv, &rate, ACX1xx_IE_RATE_FALLBACK);
		CLEAR_BIT(priv->set_mask, SET_RATE_FALLBACK);
	}
	if (priv->set_mask & (GETSET_TXPOWER|GETSET_ALL)) {
		acxlog(L_INIT, "updating transmit power: %u dBm\n",
					priv->tx_level_dbm);
		acx_s_set_tx_level(priv, priv->tx_level_dbm);
		CLEAR_BIT(priv->set_mask, GETSET_TXPOWER);
	}

	if (priv->set_mask & (GETSET_SENSITIVITY|GETSET_ALL)) {
		acxlog(L_INIT, "updating sensitivity value: %u\n",
					priv->sensitivity);
		switch (priv->radio_type) {
		case RADIO_RFMD_11:
		case RADIO_MAXIM_0D:
		case RADIO_RALINK_15:
			acx_s_write_phy_reg(priv, 0x30, priv->sensitivity);
			break;
		case RADIO_RADIA_16:
		case RADIO_UNKNOWN_17:
			acx111_s_sens_radio_16_17(priv);
			break;
		default:
			acxlog(L_INIT, "don't know how to modify sensitivity "
				"for radio type 0x%02X\n", priv->radio_type);
		}
		CLEAR_BIT(priv->set_mask, GETSET_SENSITIVITY);
	}

	if (priv->set_mask & (GETSET_ANTENNA|GETSET_ALL)) {
		/* antenna */
		u8 antenna[4 + ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN];

		memset(antenna, 0, sizeof(antenna));
		antenna[4] = priv->antenna;
		acxlog(L_INIT, "updating antenna value: 0x%02X\n",
					priv->antenna);
		acx_s_configure(priv, &antenna, ACX1xx_IE_DOT11_CURRENT_ANTENNA);
		CLEAR_BIT(priv->set_mask, GETSET_ANTENNA);
	}

	if (priv->set_mask & (GETSET_ED_THRESH|GETSET_ALL)) {
		/* ed_threshold */
		acxlog(L_INIT, "updating Energy Detect (ED) threshold: %u\n",
					priv->ed_threshold);
		if (IS_ACX100(priv)) {
			u8 ed_threshold[4 + ACX100_IE_DOT11_ED_THRESHOLD_LEN];

			memset(ed_threshold, 0, sizeof(ed_threshold));
			ed_threshold[4] = priv->ed_threshold;
			acx_s_configure(priv, &ed_threshold, ACX100_IE_DOT11_ED_THRESHOLD);
		}
		else
			acxlog(L_INIT, "acx111 doesn't support ED!\n");
		CLEAR_BIT(priv->set_mask, GETSET_ED_THRESH);
	}

	if (priv->set_mask & (GETSET_CCA|GETSET_ALL)) {
		/* CCA value */
		acxlog(L_INIT, "updating Channel Clear Assessment "
				"(CCA) value: 0x%02X\n", priv->cca);
		if (IS_ACX100(priv))	{
			u8 cca[4 + ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN];

			memset(cca, 0, sizeof(cca));
			cca[4] = priv->cca;
			acx_s_configure(priv, &cca, ACX1xx_IE_DOT11_CURRENT_CCA_MODE);
		}
		else
			acxlog(L_INIT, "acx111 doesn't support CCA!\n");
		CLEAR_BIT(priv->set_mask, GETSET_CCA);
	}

	if (priv->set_mask & (GETSET_LED_POWER|GETSET_ALL)) {
		/* Enable Tx */
		acxlog(L_INIT, "updating power LED status: %u\n", priv->led_power);

		acx_lock(priv, flags);
		if (IS_PCI(priv))
			acxpci_l_power_led(priv, priv->led_power);
		CLEAR_BIT(priv->set_mask, GETSET_LED_POWER);
		acx_unlock(priv, flags);
	}

/* this seems to cause Tx lockup after some random time (Tx error 0x20),
 * so let's disable it for now until further investigation */
/* Maybe fixed now after locking is fixed. Need to retest */
#ifdef POWER_SAVE_80211
	if (priv->set_mask & (GETSET_POWER_80211|GETSET_ALL)) {
		acx100_ie_powermgmt_t pm;

		/* change 802.11 power save mode settings */
		acxlog(L_INIT, "updating 802.11 power save mode settings: "
			"wakeup_cfg 0x%02X, listen interval %u, "
			"options 0x%02X, hangover period %u, "
			"enhanced_ps_transition_time %d\n",
			priv->ps_wakeup_cfg, priv->ps_listen_interval,
			priv->ps_options, priv->ps_hangover_period,
			priv->ps_enhanced_transition_time);
		acx_s_interrogate(priv, &pm, ACX100_IE_POWER_MGMT);
		acxlog(L_INIT, "Previous PS mode settings: wakeup_cfg 0x%02X, "
			"listen interval %u, options 0x%02X, "
			"hangover period %u, "
			"enhanced_ps_transition_time %d\n",
			pm.wakeup_cfg, pm.listen_interval, pm.options,
			pm.hangover_period, pm.enhanced_ps_transition_time);
		pm.wakeup_cfg = priv->ps_wakeup_cfg;
		pm.listen_interval = priv->ps_listen_interval;
		pm.options = priv->ps_options;
		pm.hangover_period = priv->ps_hangover_period;
		pm.enhanced_ps_transition_time = cpu_to_le16(priv->ps_enhanced_transition_time);
		acx_s_configure(priv, &pm, ACX100_IE_POWER_MGMT);
		acx_s_interrogate(priv, &pm, ACX100_IE_POWER_MGMT);
		acxlog(L_INIT, "wakeup_cfg: 0x%02X\n", pm.wakeup_cfg);
		acx_s_msleep(40);
		acx_s_interrogate(priv, &pm, ACX100_IE_POWER_MGMT);
		acxlog(L_INIT, "power save mode change %s\n",
			(pm.wakeup_cfg & PS_CFG_PENDING) ? "FAILED" : "was successful");
		/* FIXME: maybe verify via PS_CFG_PENDING bit here
		 * that power save mode change was successful. */
		/* FIXME: we shouldn't trigger a scan immediately after
		 * fiddling with power save mode (since the firmware is sending
		 * a NULL frame then). Does this need locking?? */
		CLEAR_BIT(priv->set_mask, GETSET_POWER_80211);
	}
#endif

	if (priv->set_mask & (GETSET_CHANNEL|GETSET_ALL)) {
		/* channel */
		acxlog(L_INIT, "updating channel to: %u\n", priv->channel);
		CLEAR_BIT(priv->set_mask, GETSET_CHANNEL);
	}

	if (priv->set_mask & (GETSET_TX|GETSET_ALL)) {
		/* set Tx */
		acxlog(L_INIT, "updating: %s Tx\n",
				priv->tx_disabled ? "disable" : "enable");
		if (priv->tx_disabled)
			acx_s_issue_cmd(priv, ACX1xx_CMD_DISABLE_TX, NULL, 0);
		else
			acx_s_issue_cmd(priv, ACX1xx_CMD_ENABLE_TX, &(priv->channel), 1);
		CLEAR_BIT(priv->set_mask, GETSET_TX);
	}

	if (priv->set_mask & (GETSET_RX|GETSET_ALL)) {
		/* Enable Rx */
		acxlog(L_INIT, "updating: enable Rx on channel: %u\n",
				priv->channel);
		acx_s_issue_cmd(priv, ACX1xx_CMD_ENABLE_RX, &(priv->channel), 1);
		CLEAR_BIT(priv->set_mask, GETSET_RX);
	}

	if (priv->set_mask & (GETSET_RETRY|GETSET_ALL)) {
		u8 short_retry[4 + ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT_LEN];
		u8 long_retry[4 + ACX1xx_IE_DOT11_LONG_RETRY_LIMIT_LEN];

		acxlog(L_INIT, "updating short retry limit: %u, long retry limit: %u\n",
					priv->short_retry, priv->long_retry);
		short_retry[0x4] = priv->short_retry;
		long_retry[0x4] = priv->long_retry;
		acx_s_configure(priv, &short_retry, ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT);
		acx_s_configure(priv, &long_retry, ACX1xx_IE_DOT11_LONG_RETRY_LIMIT);
		CLEAR_BIT(priv->set_mask, GETSET_RETRY);
	}

	if (priv->set_mask & (SET_MSDU_LIFETIME|GETSET_ALL)) {
		u8 xmt_msdu_lifetime[4 + ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME_LEN];

		acxlog(L_INIT, "updating tx MSDU lifetime: %u\n",
					priv->msdu_lifetime);
		*(u32 *)&xmt_msdu_lifetime[4] = cpu_to_le32((u32)priv->msdu_lifetime);
		acx_s_configure(priv, &xmt_msdu_lifetime, ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME);
		CLEAR_BIT(priv->set_mask, SET_MSDU_LIFETIME);
	}

	if (priv->set_mask & (GETSET_REG_DOMAIN|GETSET_ALL)) {
		/* reg_domain */
		acx_ie_generic_t dom;
		unsigned mask;

		acxlog(L_INIT, "updating regulatory domain: 0x%02X\n",
					priv->reg_dom_id);
		for (i = 0; i < sizeof(acx_reg_domain_ids); i++)
			if (acx_reg_domain_ids[i] == priv->reg_dom_id)
				break;

		if (sizeof(acx_reg_domain_ids) == i) {
			acxlog(L_INIT, "Invalid or unsupported regulatory "
				"domain 0x%02X specified, falling back to "
				"FCC (USA)! Please report if this sounds "
				"fishy!\n", priv->reg_dom_id);
			i = 0;
			priv->reg_dom_id = acx_reg_domain_ids[i];
		}

		priv->reg_dom_chanmask = reg_domain_channel_masks[i];
		dom.m.bytes[0] = priv->reg_dom_id;
		acx_s_configure(priv, &dom, ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN);

		mask = (1 << (priv->channel - 1));
		if (!(priv->reg_dom_chanmask & mask)) {
		/* hmm, need to adjust our channel to reside within domain */
			mask = 1;
			for (i = 1; i <= 14; i++) {
				if (priv->reg_dom_chanmask & mask) {
					printk("%s: adjusting "
						"selected channel from %d "
						"to %d due to new regulatory "
						"domain\n", priv->netdev->name,
						priv->channel, i);
					priv->channel = i;
					break;
				}
				mask <<= 1;
			}
		}
		CLEAR_BIT(priv->set_mask, GETSET_REG_DOMAIN);
	}

	if (priv->set_mask & (GETSET_MODE|GETSET_ALL)) {
		priv->netdev->type = ARPHRD_ETHER;

		switch (priv->mode) {
		case ACX_MODE_3_AP:

			acx_lock(priv, flags);
			acx_l_sta_list_init(priv);
			priv->aid = 0;
			priv->ap_client = NULL;
			MAC_COPY(priv->bssid, priv->dev_addr);
			/* this basically says "we're connected" */
			acx_set_status(priv, ACX_STATUS_4_ASSOCIATED);
			acx_unlock(priv, flags);

			acx111_s_feature_off(priv, 0, FEATURE2_NO_TXCRYPT|FEATURE2_SNIFFER);
			/* start sending beacons */
			acx_s_cmd_join_bssid(priv, priv->bssid);
			break;
		case ACX_MODE_MONITOR:
			/* priv->netdev->type = ARPHRD_ETHER; */
			/* priv->netdev->type = ARPHRD_IEEE80211; */
			priv->netdev->type = ARPHRD_IEEE80211_PRISM;
			acx111_s_feature_on(priv, 0, FEATURE2_NO_TXCRYPT|FEATURE2_SNIFFER);
			/* this stops beacons */
			acx_s_cmd_join_bssid(priv, priv->bssid);
			/* this basically says "we're connected" */
			acx_set_status(priv, ACX_STATUS_4_ASSOCIATED);
			SET_BIT(priv->set_mask, SET_RXCONFIG|SET_WEP_OPTIONS);
			break;
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			acx111_s_feature_off(priv, 0, FEATURE2_NO_TXCRYPT|FEATURE2_SNIFFER);

			acx_lock(priv, flags);
			priv->aid = 0;
			priv->ap_client = NULL;
			acx_unlock(priv, flags);

			/* we want to start looking for peer or AP */
			start_scan = 1;
			break;
		case ACX_MODE_OFF:
			/* TODO: disable RX/TX, stop any scanning activity etc: */
			/* priv->tx_disabled = 1; */
			/* SET_BIT(priv->set_mask, GETSET_RX|GETSET_TX); */

			/* This stops beacons (invalid macmode...) */
			acx_s_cmd_join_bssid(priv, priv->bssid);
			acx_set_status(priv, ACX_STATUS_0_STOPPED);
			break;
		}
		CLEAR_BIT(priv->set_mask, GETSET_MODE);
	}

	if (priv->set_mask & (SET_RXCONFIG|GETSET_ALL)) {
		acx_s_initialize_rx_config(priv);
		CLEAR_BIT(priv->set_mask, SET_RXCONFIG);
	}

	if (priv->set_mask & (GETSET_RESCAN|GETSET_ALL)) {
		switch (priv->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			start_scan = 1;
			break;
		}
		CLEAR_BIT(priv->set_mask, GETSET_RESCAN);
	}

	if (priv->set_mask & (GETSET_WEP|GETSET_ALL)) {
		/* encode */

		ie_dot11WEPDefaultKeyID_t dkey;
#ifdef DEBUG_WEP
		struct {
			u16 type ACX_PACKED;
			u16 len ACX_PACKED;
			u8  val ACX_PACKED;
		} keyindic;
#endif
		acxlog(L_INIT, "updating WEP key settings\n");

		acx_s_set_wepkey(priv);

		dkey.KeyID = priv->wep_current_index;
		acxlog(L_INIT, "setting WEP key %u as default\n", dkey.KeyID);
		acx_s_configure(priv, &dkey, ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET);
#ifdef DEBUG_WEP
		keyindic.val = 3;
		acx_s_configure(priv, &keyindic, ACX111_IE_KEY_CHOOSE);
#endif
		start_scan = 1;
		CLEAR_BIT(priv->set_mask, GETSET_WEP);
	}

	if (priv->set_mask & (SET_WEP_OPTIONS|GETSET_ALL)) {
		acx100_ie_wep_options_t options;

		if (IS_ACX111(priv)) {
			acxlog(L_DEBUG, "setting WEP Options for acx111 is not supported\n");
		} else {
			acxlog(L_INIT, "setting WEP Options\n");

			/* let's choose maximum setting: 4 default keys,
			 * plus 10 other keys: */
			options.NumKeys = cpu_to_le16(DOT11_MAX_DEFAULT_WEP_KEYS + 10);
			/* don't decrypt default key only,
			 * don't override decryption: */
			options.WEPOption = 0;
			if (priv->mode == ACX_MODE_MONITOR) {
				/* don't decrypt default key only,
				 * override decryption mechanism: */
				options.WEPOption = 2;
			}

			acx_s_configure(priv, &options, ACX100_IE_WEP_OPTIONS);
		}
		CLEAR_BIT(priv->set_mask, SET_WEP_OPTIONS);
	}

	/* Rescan was requested */
	if (start_scan) {
		switch (priv->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			/* We can avoid clearing list if join code
			** will be a bit more clever about not picking
			** 'bad' AP over and over again */
			acx_lock(priv, flags);
			priv->ap_client = NULL;
			acx_l_sta_list_init(priv);
			acx_set_status(priv, ACX_STATUS_1_SCANNING);
			acx_unlock(priv, flags);

			acx_s_cmd_start_scan(priv);
		}
	}

	/* debug, rate, and nick don't need any handling */
	/* what about sniffing mode?? */

	acxlog(L_INIT, "get_mask 0x%08X, set_mask 0x%08X - after update\n",
			priv->get_mask, priv->set_mask);

/* end: */
	FN_EXIT0;
}


/***********************************************************************
*/
void
acx_s_initialize_rx_config(wlandevice_t *priv)
{
	struct {
		u16	id ACX_PACKED;
		u16	len ACX_PACKED;
		u16	rx_cfg1 ACX_PACKED;
		u16	rx_cfg2 ACX_PACKED;
	} cfg;

	switch (priv->mode) {
	case ACX_MODE_OFF:
		priv->rx_config_1 = (u16) (0
			/* | RX_CFG1_INCLUDE_RXBUF_HDR	*/
			/* | RX_CFG1_FILTER_SSID	*/
			/* | RX_CFG1_FILTER_BCAST	*/
			/* | RX_CFG1_RCV_MC_ADDR1	*/
			/* | RX_CFG1_RCV_MC_ADDR0	*/
			/* | RX_CFG1_FILTER_ALL_MULTI	*/
			/* | RX_CFG1_FILTER_BSSID	*/
			/* | RX_CFG1_FILTER_MAC		*/
			/* | RX_CFG1_RCV_PROMISCUOUS	*/
			/* | RX_CFG1_INCLUDE_FCS	*/
			/* | RX_CFG1_INCLUDE_PHY_HDR	*/
			);
		priv->rx_config_2 = (u16) (0
			/*| RX_CFG2_RCV_ASSOC_REQ	*/
			/*| RX_CFG2_RCV_AUTH_FRAMES	*/
			/*| RX_CFG2_RCV_BEACON_FRAMES	*/
			/*| RX_CFG2_RCV_CONTENTION_FREE	*/
			/*| RX_CFG2_RCV_CTRL_FRAMES	*/
			/*| RX_CFG2_RCV_DATA_FRAMES	*/
			/*| RX_CFG2_RCV_BROKEN_FRAMES	*/
			/*| RX_CFG2_RCV_MGMT_FRAMES	*/
			/*| RX_CFG2_RCV_PROBE_REQ	*/
			/*| RX_CFG2_RCV_PROBE_RESP	*/
			/*| RX_CFG2_RCV_ACK_FRAMES	*/
			/*| RX_CFG2_RCV_OTHER		*/
			);
		break;
	case ACX_MODE_MONITOR:
		priv->rx_config_1 = (u16) (0
			/* | RX_CFG1_INCLUDE_RXBUF_HDR	*/
			/* | RX_CFG1_FILTER_SSID	*/
			/* | RX_CFG1_FILTER_BCAST	*/
			/* | RX_CFG1_RCV_MC_ADDR1	*/
			/* | RX_CFG1_RCV_MC_ADDR0	*/
			/* | RX_CFG1_FILTER_ALL_MULTI	*/
			/* | RX_CFG1_FILTER_BSSID	*/
			/* | RX_CFG1_FILTER_MAC		*/
			| RX_CFG1_RCV_PROMISCUOUS
			/* | RX_CFG1_INCLUDE_FCS	*/
			/* | RX_CFG1_INCLUDE_PHY_HDR	*/
			);
		priv->rx_config_2 = (u16) (0
			| RX_CFG2_RCV_ASSOC_REQ
			| RX_CFG2_RCV_AUTH_FRAMES
			| RX_CFG2_RCV_BEACON_FRAMES
			| RX_CFG2_RCV_CONTENTION_FREE
			| RX_CFG2_RCV_CTRL_FRAMES
			| RX_CFG2_RCV_DATA_FRAMES
			| RX_CFG2_RCV_BROKEN_FRAMES
			| RX_CFG2_RCV_MGMT_FRAMES
			| RX_CFG2_RCV_PROBE_REQ
			| RX_CFG2_RCV_PROBE_RESP
			| RX_CFG2_RCV_ACK_FRAMES
			| RX_CFG2_RCV_OTHER
			);
		break;
	default:
		priv->rx_config_1 = (u16) (0
			/* | RX_CFG1_INCLUDE_RXBUF_HDR	*/
			/* | RX_CFG1_FILTER_SSID	*/
			/* | RX_CFG1_FILTER_BCAST	*/
			/* | RX_CFG1_RCV_MC_ADDR1	*/
			/* | RX_CFG1_RCV_MC_ADDR0	*/
			/* | RX_CFG1_FILTER_ALL_MULTI	*/
			/* | RX_CFG1_FILTER_BSSID	*/
			| RX_CFG1_FILTER_MAC
			/* | RX_CFG1_RCV_PROMISCUOUS	*/
			/* | RX_CFG1_INCLUDE_FCS	*/
			/* | RX_CFG1_INCLUDE_PHY_HDR	*/
			);
		priv->rx_config_2 = (u16) (0
			| RX_CFG2_RCV_ASSOC_REQ
			| RX_CFG2_RCV_AUTH_FRAMES
			| RX_CFG2_RCV_BEACON_FRAMES
			| RX_CFG2_RCV_CONTENTION_FREE
			| RX_CFG2_RCV_CTRL_FRAMES
			| RX_CFG2_RCV_DATA_FRAMES
			/*| RX_CFG2_RCV_BROKEN_FRAMES	*/
			| RX_CFG2_RCV_MGMT_FRAMES
			| RX_CFG2_RCV_PROBE_REQ
			| RX_CFG2_RCV_PROBE_RESP
			/*| RX_CFG2_RCV_ACK_FRAMES	*/
			| RX_CFG2_RCV_OTHER
			);
		break;
	}
	priv->rx_config_1 |= RX_CFG1_INCLUDE_RXBUF_HDR;

	acxlog(L_INIT, "setting RXconfig to %04X:%04X\n",
			priv->rx_config_1, priv->rx_config_2);
	cfg.rx_cfg1 = cpu_to_le16(priv->rx_config_1);
	cfg.rx_cfg2 = cpu_to_le16(priv->rx_config_2);
	acx_s_configure(priv, &cfg, ACX1xx_IE_RXCONFIG);
}


/***********************************************************************
** acx_e_after_interrupt_task
*/
static int
acx_s_recalib_radio(wlandevice_t *priv)
{
	if (IS_ACX111(priv)) {
		acx111_cmd_radiocalib_t cal;

		printk("%s: recalibrating radio\n", priv->netdev->name);
		/* automatic recalibration, choose all methods: */
		cal.methods = cpu_to_le32(0x8000000f);
		/* automatic recalibration every 60 seconds (value in TUs)
		 * I wonder what is the firmware default here? */
		cal.interval = cpu_to_le32(58594);
		return acx_s_issue_cmd_timeo(priv, ACX111_CMD_RADIOCALIB,
			&cal, sizeof(cal), CMD_TIMEOUT_MS(100));
	} else {
		if (/* (OK == acx_s_issue_cmd(priv, ACX1xx_CMD_DISABLE_TX, NULL, 0)) &&
		    (OK == acx_s_issue_cmd(priv, ACX1xx_CMD_DISABLE_RX, NULL, 0)) && */
		    (OK == acx_s_issue_cmd(priv, ACX1xx_CMD_ENABLE_TX, &(priv->channel), 1)) &&
		    (OK == acx_s_issue_cmd(priv, ACX1xx_CMD_ENABLE_RX, &(priv->channel), 1)) )
			return OK;
		return NOT_OK;
	}
}

static void
acx_s_after_interrupt_recalib(wlandevice_t *priv)
{
	int res;

	/* this helps with ACX100 at least;
	 * hopefully ACX111 also does a
	 * recalibration here */

	/* clear flag beforehand, since we want to make sure
	 * it's cleared; then only set it again on specific circumstances */
	CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_CMD_RADIO_RECALIB);

	/* better wait a bit between recalibrations to
	 * prevent overheating due to torturing the card
	 * into working too long despite high temperature
	 * (just a safety measure) */
	if (priv->recalib_time_last_success
	 && time_before(jiffies, priv->recalib_time_last_success
					+ RECALIB_PAUSE * 60 * HZ)) {
		priv->recalib_msg_ratelimit++;
		if (priv->recalib_msg_ratelimit <= 5)
			printk("%s: less than " STRING(RECALIB_PAUSE)
				" minutes since last radio recalibration, "
				"not recalibrating (maybe card is too hot?)\n",
				priv->netdev->name);
		if (priv->recalib_msg_ratelimit == 5)
			printk("disabling above message\n");
		return;
	}

	priv->recalib_msg_ratelimit = 0;

	/* note that commands sometimes fail (card busy),
	 * so only clear flag if we were fully successful */
	res = acx_s_recalib_radio(priv);
	if (res == OK) {
		printk("%s: successfully recalibrated radio\n",
						priv->netdev->name);
		priv->recalib_time_last_success = jiffies;
		priv->recalib_failure_count = 0;
	} else {
		/* failed: resubmit, but only limited
		 * amount of times within some time range
		 * to prevent endless loop */

		priv->recalib_time_last_success = 0; /* we failed */

		/* if some time passed between last
		 * attempts, then reset failure retry counter
		 * to be able to do next recalib attempt */
		if (time_after(jiffies, priv->recalib_time_last_attempt + HZ))
			priv->recalib_failure_count = 0;

		if (++priv->recalib_failure_count <= 5) {
			priv->recalib_time_last_attempt = jiffies;
			acx_schedule_task(priv, ACX_AFTER_IRQ_CMD_RADIO_RECALIB);
		}
	}
}

static void
acx_e_after_interrupt_task(void *data)
{
	netdevice_t *dev = (netdevice_t *) data;
	wlandevice_t *priv = netdev_priv(dev);

	FN_ENTER;

	acx_sem_lock(priv);

	if (!priv->after_interrupt_jobs)
		goto end; /* no jobs to do */

#if TX_CLEANUP_IN_SOFTIRQ
	/* can happen only on PCI */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_TX_CLEANUP) {
		acx_lock(priv, flags);
		acxpci_l_clean_txdesc(priv);
		CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_TX_CLEANUP);
		acx_unlock(priv, flags);
	}
#endif
	/* we see lotsa tx errors */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_CMD_RADIO_RECALIB) {
		acx_s_after_interrupt_recalib(priv);
	}

	/* a poor interrupt code wanted to do update_card_settings() */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_UPDATE_CARD_CFG) {
		if (ACX_STATE_IFACE_UP & priv->dev_state_mask)
			acx_s_update_card_settings(priv, 0, 0);
		CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_UPDATE_CARD_CFG);
	}

	/* 1) we detected that no Scan_Complete IRQ came from fw, or
	** 2) we found too many STAs */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_CMD_STOP_SCAN) {
		acxlog(L_IRQ, "sending a stop scan cmd...\n");
		acx_s_issue_cmd(priv, ACX1xx_CMD_STOP_SCAN, NULL, 0);
		/* HACK: set the IRQ bit, since we won't get a
		 * scan complete IRQ any more on ACX111 (works on ACX100!),
		 * since _we_, not a fw, have stopped the scan */
		SET_BIT(priv->irq_status, HOST_INT_SCAN_COMPLETE);
		CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_CMD_STOP_SCAN);
	}

	/* either fw sent Scan_Complete or we detected that
	** no Scan_Complete IRQ came from fw. Finish scanning,
	** pick join partner if any */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_COMPLETE_SCAN) {
		if (priv->status == ACX_STATUS_1_SCANNING) {
			if (OK != acx_s_complete_scan(priv)) {
				SET_BIT(priv->after_interrupt_jobs,
					ACX_AFTER_IRQ_RESTART_SCAN);
			}
		} else {
			/* + scan kills current join status - restore it
			**   (do we need it for STA?) */
			/* + does it happen only with active scans?
			**   active and passive scans? ALL scans including
			**   background one? */
			/* + was not verified that everything is restored
			**   (but at least we start to emit beacons again) */
			switch (priv->mode) {
			case ACX_MODE_0_ADHOC:
			case ACX_MODE_3_AP:
				acxlog(L_IRQ, "redoing cmd_join_bssid() after scan\n");
				acx_s_cmd_join_bssid(priv, priv->bssid);
			}
		}
		CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_COMPLETE_SCAN);
	}

	/* STA auth or assoc timed out, start over again */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_RESTART_SCAN) {
		acxlog(L_IRQ, "sending a start_scan cmd...\n");
		acx_s_cmd_start_scan(priv);
		CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_RESTART_SCAN);
	}

	/* whee, we got positive assoc response! 8) */
	if (priv->after_interrupt_jobs & ACX_AFTER_IRQ_CMD_ASSOCIATE) {
		acx_ie_generic_t pdr;
		/* tiny race window exists, checking that we still a STA */
		switch (priv->mode) {
		case ACX_MODE_2_STA:
			pdr.m.aid = cpu_to_le16(priv->aid);
			acx_s_configure(priv, &pdr, ACX1xx_IE_ASSOC_ID);
			acx_set_status(priv, ACX_STATUS_4_ASSOCIATED);
			acxlog(L_ASSOC|L_DEBUG, "ASSOCIATED!\n");
			CLEAR_BIT(priv->after_interrupt_jobs, ACX_AFTER_IRQ_CMD_ASSOCIATE);
		}
	}
end:
	acx_sem_unlock(priv);
	FN_EXIT0;
}


/***********************************************************************
** acx_schedule_task
**
** Schedule the call of the after_interrupt method after leaving
** the interrupt context.
*/
void
acx_schedule_task(wlandevice_t *priv, unsigned int set_flag)
{
	SET_BIT(priv->after_interrupt_jobs, set_flag);
	SCHEDULE_WORK(&priv->after_interrupt_task);
}


/***********************************************************************
*/
void
acx_init_task_scheduler(wlandevice_t *priv)
{
	/* configure task scheduler */
	INIT_WORK(&priv->after_interrupt_task, acx_e_after_interrupt_task,
			priv->netdev);
}


/***********************************************************************
** acx_s_start
*/
void
acx_s_start(wlandevice_t *priv)
{
	FN_ENTER;

	/*
	 * Ok, now we do everything that can possibly be done with ioctl
	 * calls to make sure that when it was called before the card
	 * was up we get the changes asked for
	 */

	SET_BIT(priv->set_mask, SET_TEMPLATES|SET_STA_LIST|GETSET_WEP
		|GETSET_TXPOWER|GETSET_ANTENNA|GETSET_ED_THRESH|GETSET_CCA
		|GETSET_REG_DOMAIN|GETSET_MODE|GETSET_CHANNEL
		|GETSET_TX|GETSET_RX);

	acxlog(L_INIT, "updating initial settings on iface activation\n");
	acx_s_update_card_settings(priv, 0, 0);

	FN_EXIT0;
}


/***********************************************************************
** acx_update_capabilities
*/
void
acx_update_capabilities(wlandevice_t *priv)
{
	u16 cap = 0;

	switch (priv->mode) {
	case ACX_MODE_3_AP:
		SET_BIT(cap, WF_MGMT_CAP_ESS); break;
	case ACX_MODE_0_ADHOC:
		SET_BIT(cap, WF_MGMT_CAP_IBSS); break;
	/* other types of stations do not emit beacons */
	}

	if (priv->wep_restricted) {
		SET_BIT(cap, WF_MGMT_CAP_PRIVACY);
	}
	if (priv->capab_short) {
		SET_BIT(cap, WF_MGMT_CAP_SHORT);
	}
	if (priv->capab_pbcc) {
		SET_BIT(cap, WF_MGMT_CAP_PBCC);
	}
	if (priv->capab_agility) {
		SET_BIT(cap, WF_MGMT_CAP_AGILITY);
	}
	acxlog(L_DEBUG, "caps updated from 0x%04X to 0x%04X\n",
				priv->capabilities, cap);
	priv->capabilities = cap;
}

#ifdef UNUSED
/***********************************************************************
** FIXME: check whether this function is indeed acx111 only,
** rename ALL relevant definitions to indicate actual card scope!
*/
void
acx111_s_read_configoption(wlandevice_t *priv)
{
	acx111_ie_configoption_t co, co2;
	int i;
	const u8 *pEle;

	if (OK != acx_s_interrogate(priv, &co, ACX111_IE_CONFIG_OPTIONS) ) {
		return;
	};
	if (!(acx_debug & L_DEBUG))
		return;

	memcpy(&co2.configoption_fixed, &co.configoption_fixed,
			sizeof(co.configoption_fixed));

	pEle = (u8 *)&co.configoption_fixed + sizeof(co.configoption_fixed) - 4;

	co2.antennas.type = pEle[0];
	co2.antennas.len = pEle[1];
	printk("AntennaID:%02X Len:%02X Data:",
			co2.antennas.type, co2.antennas.len);
	for (i = 0; i < pEle[1]; i++) {
		co2.antennas.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1] + 2;
	co2.power_levels.type = pEle[0];
	co2.power_levels.len = pEle[1];
	printk("PowerLevelID:%02X Len:%02X Data:",
			co2.power_levels.type, co2.power_levels.len);
	for (i = 0; i < pEle[1]*2; i++) {
		co2.power_levels.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1]*2 + 2;
	co2.data_rates.type = pEle[0];
	co2.data_rates.len = pEle[1];
	printk("DataRatesID:%02X Len:%02X Data:",
			co2.data_rates.type, co2.data_rates.len);
	for (i = 0; i < pEle[1]; i++) {
		co2.data_rates.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1] + 2;
	co2.domains.type = pEle[0];
	co2.domains.len = pEle[1];
	printk("DomainID:%02X Len:%02X Data:",
			co2.domains.type, co2.domains.len);
	for (i = 0; i < pEle[1]; i++) {
		co2.domains.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1] + 2;
	co2.product_id.type = pEle[0];
	co2.product_id.len = pEle[1];
	for (i = 0; i < pEle[1]; i++) {
		co2.product_id.list[i] = pEle[i+2];
	}
	printk("ProductID:%02X Len:%02X Data:%.*s\n",
			co2.product_id.type, co2.product_id.len,
			co2.product_id.len, (char *)co2.product_id.list);

	pEle += pEle[1] + 2;
	co2.manufacturer.type = pEle[0];
	co2.manufacturer.len = pEle[1];
	for (i = 0; i < pEle[1]; i++) {
		co2.manufacturer.list[i] = pEle[i+2];
	}
	printk("ManufacturerID:%02X Len:%02X Data:%.*s\n",
			co2.manufacturer.type, co2.manufacturer.len,
			co2.manufacturer.len, (char *)co2.manufacturer.list);
/*
	printk("EEPROM part:\n");
	for (i=0; i<58; i++) {
		printk("%02X =======>  0x%02X\n",
			    i, (u8 *)co.configoption_fixed.NVSv[i-2]);
	}
*/
}
#endif


/***********************************************************************
*/
static int __init
acx_e_init_module(void)
{
	int r1,r2;

	acx_struct_size_check();

	printk("acx: this driver is still EXPERIMENTAL\n"
		"acx: reading README file and/or Craig's HOWTO is "
		"recommended, visit http://acx100.sf.net in case "
		"of further questions/discussion\n");

#if defined(CONFIG_ACX_PCI)
	r1 = acxpci_e_init_module();
#else
	r1 = -EINVAL;
#endif
#if defined(CONFIG_ACX_USB)
	r2 = acxusb_e_init_module();
#else
	r2 = -EINVAL;
#endif
	if (r2 && r1) /* both failed! */
		return r2 ? r2 : r1;
	/* return success if at least one succeeded */
	return 0;
}

static void __exit
acx_e_cleanup_module(void)
{
#if defined(CONFIG_ACX_PCI)
	acxpci_e_cleanup_module();
#endif
#if defined(CONFIG_ACX_USB)
	acxusb_e_cleanup_module();
#endif
}

module_init(acx_e_init_module)
module_exit(acx_e_cleanup_module)
