/* src/acx100_helper.c - helper functions
 *
 * --------------------------------------------------------------------
 *
 * Copyright (C) 2003  ACX100 Open Source Project
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the ACX100 Open Source Project can be
 * made directly to:
 *
 * acx100-users@lists.sf.net
 * http://acx100.sf.net
 *
 * --------------------------------------------------------------------
 */

/*================================================================*/
/* System Includes */

#ifdef S_SPLINT_S /* some crap that splint needs to not crap out */
#define __signed__ signed
#define __u64 unsigned long long
#define loff_t unsigned long
#define sigval_t unsigned long
#define siginfo_t unsigned long
#define stack_t unsigned long
#define __s64 signed long long
#endif
#include <linux/config.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/vmalloc.h>

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <asm/uaccess.h>

#include <linux/pm.h>

#include <linux/dcache.h>
#include <linux/highmem.h>


/*================================================================*/
/* Project Includes */

#include <wlan_compat.h>

#include <version.h>
#include <p80211mgmt.h>
#include <acx100.h>
#include <acx100_helper.h>
#include <acx100_helper2.h>
#include <idma.h>
#include <ihw.h>

UINT8 DTIM_count;

extern char *firmware_dir; /* declared in acx100.c, to keep together with other MODULE_PARMs */

/* acx100_schedule()
 * Make sure to schedule away sometimes, in order to not hog the CPU too much.
 */
void acx100_schedule(long timeout)
{
	FN_ENTER;
	current->state = TASK_UNINTERRUPTIBLE;
	(void)schedule_timeout(timeout);
	FN_EXIT(0, 0);
}

/*------------------------------------------------------------------------------
 * acx100_read_proc
 * Handle our /proc entry
 *
 * Arguments:
 *	standard kernel read_proc interface
 * Returns:
 *	number of bytes written to buf
 * Side effects:
 *	none
 * Call context:
 *	
 * Status:
 *	should be okay, non-critical
 * Comment:
 *
 *----------------------------------------------------------------------------*/
int acx100_read_proc(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	/* fill buf */
	int length = acx100_proc_output(buf, priv);

	FN_ENTER;
	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT(1, length);
	return length;
}

int acx100_read_proc_diag(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	/* fill buf */
	int length = acx100_proc_diag_output(buf, priv);

	FN_ENTER;
	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT(1, length);
	return length;
}

int acx100_read_proc_eeprom(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	/* fill buf */
	int length = acx100_proc_eeprom_output(buf, priv);

	FN_ENTER;
	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT(1, length);
	return length;
}

int acx100_read_proc_phy(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	wlandevice_t *priv = (wlandevice_t *)data;
	/* fill buf */
	int length = acx100_proc_phy_output(buf, priv);

	FN_ENTER;
	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT(1, length);
	return length;
}

/*------------------------------------------------------------------------------
 * acx100_proc_output
 * Generate content for our /proc entry
 *
 * Arguments:
 *	buf is a pointer to write output to
 *	priv is the usual pointer to our private struct wlandevice
 * Returns:
 *	number of bytes actually written to buf
 * Side effects:
 *	none
 * Call context:
 *	
 * Status:
 *	should be okay, non-critical
 * Comment:
 *
 *----------------------------------------------------------------------------*/
int acx100_proc_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	UINT16 i;

	FN_ENTER;
	p += sprintf(p, "acx100 driver version:\t\t%s\n", WLAN_RELEASE_SUB);
	p += sprintf(p, "Wireless extension version:\t%d\n", WIRELESS_EXT);
	p += sprintf(p, "chip name:\t\t\t%s\n", priv->chip_name);
	p += sprintf(p, "radio type:\t\t\t0x%02x\n", priv->radio_type);
	/* TODO: add radio type string from acx100_display_hardware_details */
	p += sprintf(p, "form factor:\t\t\t0x%02x\n", priv->form_factor);
	/* TODO: add form factor string from acx100_display_hardware_details */
	p += sprintf(p, "EEPROM version:\t\t\t0x%02x\n", priv->eeprom_version);
	p += sprintf(p, "firmware version:\t\t%s (0x%08lx)\n",
		     (char *)priv->firmware_version, priv->firmware_id);
	p += sprintf(p, "BSS table has %d entries:\n", priv->bss_table_count);
	for (i = 0; i < priv->bss_table_count; i++) {
		struct bss_info *bss = &priv->bss_table[i];
		p += sprintf(p, " BSS %d  BSSID %02x:%02x:%02x:%02x:%02x:%02x  ESSID %s  channel %u  WEP %s  Cap 0x%x  SIR %lu  SNR %lu\n", 
			     i, bss->bssid[0], bss->bssid[1],
			     bss->bssid[2], bss->bssid[3], bss->bssid[4],
			     bss->bssid[5], (char *)bss->essid, bss->channel,
			     (0 != bss->wep) ? "yes" : "no", bss->caps,
			     bss->sir, bss->snr);
	}
	p += sprintf(p, "status:\t\t\t%u (%s)\n", priv->status, acx100_get_status_name(priv->status));
	/* TODO: add more interesting stuff (essid, ...) here */
	FN_EXIT(1, p - buf);
	return p - buf;
}

int acx100_proc_diag_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	unsigned int i;
        TIWLAN_DC *pDc = &priv->dc;
        struct rxhostdescriptor *pDesc;
	txdesc_t *pTxDesc;
        UINT8 *a;
	fw_stats_t *fw_stats;

	FN_ENTER;

	p += sprintf(p, "*** Rx buf ***\n");
	for (i = 0; i < pDc->rx_pool_count; i++)
	{
		pDesc = &pDc->pRxHostDescQPool[i];
		if ((0 != (pDesc->Ctl & ACX100_CTL_OWN)) && (0 != (pDesc->Status & BIT31)))
			p += sprintf(p, "%02u FULL\n", i);
		else
			p += sprintf(p, "%02u empty\n", i);
	}
	p += sprintf(p, "\n");
	p += sprintf(p, "*** Tx buf ***\n");
	for (i = 0; i < pDc->tx_pool_count; i++)
	{
		pTxDesc = &pDc->pTxDescQPool[i];
		if ((UINT8)DESC_CTL_DONE == (pTxDesc->Ctl & (UINT8)DESC_CTL_DONE))
			p += sprintf(p, "%02u DONE\n", i);
		else
			p += sprintf(p, "%02u empty\n", i);
	}
	p += sprintf(p, "\n");
	p += sprintf(p, "*** network status ***\n");
	p += sprintf(p, "dev_state_mask: 0x%04x\n", priv->dev_state_mask);
	p += sprintf(p, "status: %u (%s), macmode_wanted: %u, macmode_joined: %u, channel: %u, reg_dom_id: 0x%02X, reg_dom_chanmask: 0x%04x, txrate_curr: %d, txrate_auto: %d, txrate_cfg: %d, txrate_fallback_retries: %d, txrate_fallbacks: %d/%d, txrate_stepups: %d/%d, bss_table_count: %d\n",
		priv->status, acx100_get_status_name(priv->status),
		priv->macmode_wanted, priv->macmode_joined, priv->channel,
		priv->reg_dom_id, priv->reg_dom_chanmask,
		priv->txrate_curr, priv->txrate_auto, priv->txrate_cfg,
		priv->txrate_fallback_retries,
		priv->txrate_fallback_count, priv->txrate_fallback_threshold,
		priv->txrate_stepup_count, priv->txrate_stepup_threshold,
		priv->bss_table_count);
	p += sprintf(p, "ESSID: \"%s\", essid_active: %d, essid_len: %d, essid_for_assoc: \"%s\", nick: \"%s\"\n",
		priv->essid, priv->essid_active, (int)priv->essid_len,
		priv->essid_for_assoc, priv->nick);
	p += sprintf(p, "monitor: %d, monitor_setting: %d\n",
		priv->monitor, priv->monitor_setting);
	a = priv->dev_addr;
	p += sprintf(p, "dev_addr:  %02x:%02x:%02x:%02x:%02x:%02x\n",
		a[0], a[1], a[2], a[3], a[4], a[5]);
	a = priv->address;
	p += sprintf(p, "address:   %02x:%02x:%02x:%02x:%02x:%02x\n",
		a[0], a[1], a[2], a[3], a[4], a[5]);
	a = priv->bssid;
	p += sprintf(p, "bssid:     %02x:%02x:%02x:%02x:%02x:%02x\n",
		a[0], a[1], a[2], a[3], a[4], a[5]);
	a = priv->ap;
	p += sprintf(p, "ap_filter: %02x:%02x:%02x:%02x:%02x:%02x\n",
		a[0], a[1], a[2], a[3], a[4], a[5]);

        if ((fw_stats = kmalloc(sizeof(fw_stats_t), GFP_KERNEL)) == NULL) {
                return 0;
        }
	p += sprintf(p, "\n");
	acx100_interrogate(priv, fw_stats, ACX100_RID_FIRMWARE_STATISTICS);
	p += sprintf(p, "*** Firmware ***\n");
	p += sprintf(p, "tx_desc_overfl %u, rx_OutOfMem %u, rx_hdr_overfl %u, rx_hdr_use_next %u\n",
		le32_to_cpu(fw_stats->tx_desc_of), le32_to_cpu(fw_stats->rx_oom), le32_to_cpu(fw_stats->rx_hdr_of), le32_to_cpu(fw_stats->rx_hdr_use_next));
	p += sprintf(p, "rx_dropped_frame %u, rx_frame_ptr_err %u, rx_xfr_hint_trig %u, rx_dma_req %u\n",
		le32_to_cpu(fw_stats->rx_dropped_frame), le32_to_cpu(fw_stats->rx_frame_ptr_err), le32_to_cpu(fw_stats->rx_xfr_hint_trig), le32_to_cpu(fw_stats->rx_dma_req));
	p += sprintf(p, "rx_dma_err %u, tx_dma_req %u, tx_dma_err %u, cmd_cplt %u, fiq %u\n",
		le32_to_cpu(fw_stats->rx_dma_err), le32_to_cpu(fw_stats->tx_dma_req), le32_to_cpu(fw_stats->tx_dma_err), le32_to_cpu(fw_stats->cmd_cplt), le32_to_cpu(fw_stats->fiq));
	p += sprintf(p, "rx_hdrs %u, rx_cmplt %u, rx_mem_overfl %u, rx_rdys %u, irqs %u\n",
		le32_to_cpu(fw_stats->rx_hdrs), le32_to_cpu(fw_stats->rx_cmplt), le32_to_cpu(fw_stats->rx_mem_of), le32_to_cpu(fw_stats->rx_rdys), le32_to_cpu(fw_stats->irqs));
	p += sprintf(p, "acx_trans_procs %u, decrypt_done %u, dma_0_done %u, dma_1_done %u\n",
		le32_to_cpu(fw_stats->acx_trans_procs), le32_to_cpu(fw_stats->decrypt_done), le32_to_cpu(fw_stats->dma_0_done), le32_to_cpu(fw_stats->dma_1_done));
	p += sprintf(p, "tx_exch_complet %u, commands %u, acx_rx_procs %u\n",
		le32_to_cpu(fw_stats->tx_exch_complet), le32_to_cpu(fw_stats->commands), le32_to_cpu(fw_stats->acx_rx_procs));
	p += sprintf(p, "hw_pm_mode_changes %u, host_acks %u, pci_pm %u, acm_wakeups %u\n",
		le32_to_cpu(fw_stats->hw_pm_mode_changes), le32_to_cpu(fw_stats->host_acks), le32_to_cpu(fw_stats->pci_pm), le32_to_cpu(fw_stats->acm_wakeups));
	p += sprintf(p, "wep_key_count %u, wep_default_key_count %u, dot11_def_key_mib %u\n",
		le32_to_cpu(fw_stats->wep_key_count), le32_to_cpu(fw_stats->wep_default_key_count), le32_to_cpu(fw_stats->dot11_def_key_mib));
	p += sprintf(p, "wep_key_not_found %u, wep_decrypt_fail %u\n",
		le32_to_cpu(fw_stats->wep_key_not_found), le32_to_cpu(fw_stats->wep_decrypt_fail));

        kfree(fw_stats);

	FN_EXIT(1, p - buf);
	return p - buf;
}

int acx100_proc_eeprom_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	UINT16 i;
	UINT8 ch;

	FN_ENTER;

	for (i = 0; i < 0x400; i++)
	{
		acx100_read_eeprom_offset(priv, i, &ch);
		p += sprintf(p, "%c", ch);
	}

	FN_EXIT(1, p - buf);
	return p - buf;
}

int acx100_proc_phy_output(char *buf, wlandevice_t *priv)
{
	char *p = buf;
	UINT16 i;
	UINT8 ch;

	FN_ENTER;

	/*
	if (RADIO_RFMD_11 != priv->radio_type) {
		acxlog(L_STD, "Sorry, not yet adapted for radio types other than RFMD, please verify PHY size etc. first!\n");
		goto end;
	}
	*/
	
	/* The PHY area is only 0x80 bytes long; further pages after that
	 * only have some page number registers with altered value,
	 * all other registers remain the same. */
	for (i = 0; i < 0x80; i++)
	{
		acx100_read_phy_reg(priv, i, &ch);
		p += sprintf(p, "%c", ch);
	}
	
	FN_EXIT(1, p - buf);
	return p - buf;
}

/*----------------------------------------------------------------
* acx100_reset_mac
*
*
* Arguments:
*	wlandevice: private device that contains card device
* Returns:
*	void
* Side effects:
*	MAC will be reset
* Call context:
*	acx100_reset_dev
* STATUS:
*	stable
* Comment:
*	resets onboard acx100 MAC
*----------------------------------------------------------------*/

/* acx100_reset_mac()
 * Used to be HwReset()
 * STATUS: should be ok.
 */
void acx100_reset_mac(wlandevice_t *priv)
{
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT16 temp;
#endif

	FN_ENTER;

#if (WLAN_HOSTIF!=WLAN_USB)
	/* halt eCPU */
	temp = acx100_read_reg16(priv, priv->io[IO_ACX_ECPU_CTRL]) | 0x1;
	acx100_write_reg16(priv, priv->io[IO_ACX_ECPU_CTRL], temp);

	/* now do soft reset of eCPU */
	temp = acx100_read_reg16(priv, priv->io[IO_ACX_SOFT_RESET]) | 0x1;
	acxlog(L_BINSTD, "%s: enable soft reset...\n", __func__);
	acx100_write_reg16(priv, priv->io[IO_ACX_SOFT_RESET], temp);

	/* used to be for loop 65536; do scheduled delay instead */
	acx100_schedule(HZ / 100);

	/* now reset bit again */
	acxlog(L_BINSTD, "%s: disable soft reset and go to init mode...\n", __func__);
	/* deassert eCPU reset */
	acx100_write_reg16(priv, priv->io[IO_ACX_SOFT_RESET], temp & ~0x1);

	/* now start a burst read from initial flash EEPROM */
	temp = acx100_read_reg16(priv, priv->io[IO_ACX_EE_START]) | 0x1;
	acx100_write_reg16(priv, priv->io[IO_ACX_EE_START], temp);
#endif

	/* used to be for loop 65536; do scheduled delay instead */
	acx100_schedule(HZ / 100);

	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_reset_dev
*
*
* Arguments:
*	netdevice that contains the wlandevice priv variable
* Returns:
*	0 on fail
*	1 on success
* Side effects:
*	device is hard reset
* Call context:
*	acx100_probe_pci
* STATUS:
*	FIXME: reverse return values
*	stable
* Comment:
*	This resets the acx100 device using low level hardware calls
*	as well as uploads and verifies the firmware to the card
*----------------------------------------------------------------*/

int acx100_reset_dev(netdevice_t *dev)
{
	int result = 0;
#if (WLAN_HOSTIF!=WLAN_USB)
	wlandevice_t *priv = (wlandevice_t *)dev->priv;
	UINT16 vala = 0;
	UINT16 var = 0;

	FN_ENTER;

	/* we're doing a reset, so hardware is unavailable */
	acxlog(L_INIT, "reset hw_unavailable++\n");
	priv->hw_unavailable++;
	
	/* reset the device to make sure the eCPU is stopped 
	   to upload the firmware correctly */
	acx100_reset_mac(priv);
	
	/* TODO maybe seperate the two init parts into functions */
	if (priv->chip_type == CHIPTYPE_ACX100) {

		if (!(vala = acx100_read_reg16(priv, priv->io[IO_ACX_ECPU_CTRL]) & 1)) {
			acxlog(L_BINSTD, "%s: eCPU already running (%xh)\n", __func__, vala);
			goto fail;
		}

		if (0 != (acx100_read_reg16(priv, priv->io[IO_ACX_SOR_CFG]) & 2)) {
			/* eCPU most likely means "embedded CPU" */
			acxlog(L_BINSTD, "%s: eCPU did not start after boot from flash\n", __func__);
			goto fail;
		}

		/* load the firmware */
		if (0 == acx100_upload_fw(priv)) {
			acxlog(L_STD, "%s: Failed to upload firmware to the ACX100\n", __func__);
			goto fail;
		}

		acx100_write_reg16(priv, priv->io[IO_ACX_ECPU_CTRL], vala & ~0x1);

	} else if (priv->chip_type == CHIPTYPE_ACX111) {
		if (!(vala = acx100_read_reg16(priv, priv->io[IO_ACX_ECPU_CTRL]) & 1)) {
			acxlog(L_BINSTD, "%s: eCPU already running (%xh)\n", __func__, vala);
			goto fail;
		}

		/* check sense on reset flags */
		if (0 != (acx100_read_reg16(priv, priv->io[IO_ACX_SOR_CFG]) & 0x10)) { 			
			acxlog(L_BINSTD, "%s: eCPU do not start after boot (SOR), is this fatal?\n", __func__);
		}
	
		/* load the firmware */
		if (0 == acx100_upload_fw(priv)) {
			acxlog(L_STD, "%s: Failed to upload firmware to the ACX111\n", __func__);
			goto fail;
		}

		/* additional reset is needed after the firmware has been downloaded to the card */
		acx100_reset_mac(priv);

		acxlog(L_BINSTD, "%s: configure interrupt mask at %xh to: %Xh...\n", __func__, 
			priv->io[IO_ACX_IRQ_MASK], acx100_read_reg16(priv, priv->io[IO_ACX_IRQ_MASK]) ^ 0x4600);
		acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_MASK], 
			acx100_read_reg16(priv, priv->io[IO_ACX_IRQ_MASK]) ^ 0x4600);

		/* TODO remove, this is DEBUG */
		acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_MASK], 0x0);

		acxlog(L_BINSTD, "%s: boot up eCPU and wait for complete...\n", __func__);
		acx100_write_reg16(priv, priv->io[IO_ACX_ECPU_CTRL], 0x0);
	}

	/* wait for eCPU bootup */
	while (!(vala = acx100_read_reg16(priv, priv->io[IO_ACX_IRQ_STATUS_NON_DES]) & 0x4000)) { 
		/* ok, let's insert scheduling here.
		 * This was an awfully CPU burning loop.
		 */
		acx100_schedule(HZ / 10);
		var++;
		
		if (var > 250) { 
			acxlog(L_BINSTD, "Timeout waiting for the ACX100 to complete Initialization (ICOMP), %d\n", vala);
			goto fail;
		}
	}

#if (WLAN_HOSTIF!=WLAN_USB)
	if (0 == acx100_verify_init(priv)) {
		acxlog(L_BINSTD,
			   "Timeout waiting for the ACX100 to complete Initialization\n");
		goto fail;
	}
#endif

	acxlog(L_BINSTD, "%s: Received signal that card is ready to be configured :) (the eCPU has woken up)\n", __func__);

	if (priv->chip_type == CHIPTYPE_ACX111) {
		acxlog(L_BINSTD, "%s: Clean up cmd mailbox access area\n", __func__);
		acx100_write_cmd_status(priv, 0);
		acx100_get_cmd_state(priv);
		if(priv->cmd_status != 0) {
			acxlog(L_BINSTD, "Error cleaning cmd mailbox area\n");
			goto fail;
		}
	}

	/* TODO what is this one doing ?? adapt for acx111 */
	if ((0 == acx100_read_eeprom_area(priv)) && (CHIPTYPE_ACX100 == priv->chip_type)) {
		/* does "CIS" mean "Card Information Structure"?
		 * If so, then this would be a PCMCIA message...
		 */
		acxlog(L_BINSTD, "CIS error\n");
		goto fail;
	}

	/* reset succeeded, so let's release the hardware lock */
	acxlog(L_INIT, "reset hw_unavailable--\n");
	priv->hw_unavailable--;
	result = 1;
fail:
	FN_EXIT(1, result);
#endif
	return result;
}

/*----------------------------------------------------------------
* acx100_upload_fw
*
*
* Arguments:
*	wlandevice: private device that contains card device
* Returns:
*	0: failed
*	1: success
* Side effects:
*
* Call context:
*	acx100_reset_dev
* STATUS:
*	stable
* Comment:
*
*----------------------------------------------------------------*/

int acx100_upload_fw(wlandevice_t *priv)
{
	int res1 = 0;
	int res2 = 0;
	firmware_image_t* apfw_image;
	char *filename;
	int try;

	FN_ENTER;
	if (!firmware_dir)
	{
		/* since the log will be flooded with other log messages after
		 * this important one, make sure people do notice us */
		acxlog(L_STD, "ERROR: no directory for firmware file specified, ABORTING. Make sure to set module parameter 'firmware_dir'! (specified as absolute path!)\n");
		return 0;
	}

	filename = kmalloc(PATH_MAX, GFP_USER);
	if (!filename)
		return -ENOMEM;
	if(priv->chip_type == CHIPTYPE_ACX100) {
		sprintf(filename,"%s/WLANGEN.BIN", firmware_dir);
	} else if(priv->chip_type == CHIPTYPE_ACX111) {
		sprintf(filename,"%s/TIACX111.BIN", firmware_dir);
	}
	
	apfw_image = acx100_read_fw( filename );
	if (NULL == apfw_image)
	{
		acxlog(L_STD, "acx100_read_fw failed.\n");
		kfree(filename);
		return 0;
	}

	for (try = 0; try < 5; try++)
	{
		res1 = acx100_write_fw(priv, apfw_image, 0);

		res2 = acx100_validate_fw(priv, apfw_image, 0);

		acxlog(L_DEBUG | L_INIT,
	   		"acx100_write_fw (firmware): %d, acx100_validate_fw: %d\n", res1, res2);
		if ((0 != res1) && (0 != res2))
			break;
		acxlog(L_STD, "firmware upload attempt #%d FAILED, retrying...\n", try);
		acx100_schedule(HZ); /* better wait for a while... */
	}

	vfree(apfw_image);

	kfree(filename);
	priv->dev_state_mask |= ACX_STATE_FW_LOADED;
	FN_EXIT(1, (0 != res1) && (0 != res2));
	return (int)(((0 != res1) && (0 != res2)));
}

/*----------------------------------------------------------------
* acx100_load_radio
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_load_radio()
 * STATUS: new
 * Used to load the appropriate radio module firmware
 * into the card.
 */
int acx100_load_radio(wlandevice_t *priv)
{
	UINT32 offset;
	acx100_memmap_t mm;
	int res1, res2;
	firmware_image_t *radio_image=0;
	radioinit_t radioinit;
	char *filename;
	int try;

	FN_ENTER;
	acx100_interrogate(priv, &mm, ACX100_RID_MEMORY_MAP);
	offset = le32_to_cpu(mm.CodeEnd);

	filename = kmalloc(PATH_MAX, GFP_USER);
	if (!filename) {
		acxlog(L_STD, "ALERT: can't allocate filename\n");
		return 0;
	}

	sprintf(filename,"%s/RADIO%02x.BIN", firmware_dir, priv->radio_type);
	acxlog(L_DEBUG,"trying to read %s\n",filename);
	radio_image = acx100_read_fw(filename);

/*
 * 0d = RADIO0d.BIN = Maxim chipset
 * 11 = RADIO11.BIN = RFMD chipset
 * 15 = RADIO15.BIN = UNKNOWN chipset
 */

	if (NULL == radio_image)
	{
		acxlog(L_STD,"WARNING: no suitable radio module (%s) found to load. No problem in case of older combined firmware, FATAL when using new separated firmware.\n",filename);
		kfree(filename);
		return 1; /* Doesn't need to be fatal, we might be using a combined image */
	}

	acx100_issue_cmd(priv, ACX100_CMD_SLEEP, NULL, 0, 5000);

	for (try = 0; try < 5; try++)
	{
		res1 = acx100_write_fw(priv, radio_image, offset);
		res2 = acx100_validate_fw(priv, radio_image, offset);
		acxlog(L_DEBUG | L_INIT, "acx100_write_fw (radio): %d, acx100_validate_fw: %d\n", res1, res2);
		if ((0 != res1) && (0 != res2))
			break;
		acxlog(L_STD, "radio firmware upload attempt #%d FAILED, retrying...\n", try);
		acx100_schedule(HZ); /* better wait for a while... */
	}

	acx100_issue_cmd(priv, ACX100_CMD_WAKE, NULL, 0, 5000);
	radioinit.offset = cpu_to_le32(offset);
	radioinit.len = radio_image->size; /* no endian conversion needed, remains in card CPU area */
	
	vfree(radio_image);
	
	if ((0 == res1) || (0 == res2)) return 0;

	/* will take a moment so let's have a big timeout */
	acx100_issue_cmd(priv, ACX100_CMD_RADIOINIT, &radioinit, sizeof(radioinit), 120000);

	kfree(filename);
	if (0 == acx100_interrogate(priv, &mm, ACX100_RID_MEMORY_MAP))
	{
		acxlog(L_STD, "Error reading memory map\n");
		return 0;
	}
	return 1;
}
/*----------------------------------------------------------------
* acx100_read_fw
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_read_fw()
 * STATUS: new
 *
 * Loads a firmware image in from a file.
 *
 * This seemed like a good idea at the time. I'm not so sure about it now.
 * perhaps this should be a user mode helper that ioctls the module the firmware?
 *
 * Returns:
 *  0				unable to load file
 *  pointer to firmware		success
 */
/*@null@*/ firmware_image_t* acx100_read_fw(const char *file)
{
	firmware_image_t *res = NULL;
	mm_segment_t orgfs;
	unsigned long page;
	char *buffer;
	struct file *inf;
	int retval;
	UINT32 offset = 0;

	orgfs = get_fs(); /* store original fs */
	set_fs(KERNEL_DS);

	/* Read in whole file then check the size */
	page = __get_free_page(GFP_KERNEL);
	if (0 == page) {
		acxlog(L_STD, "Unable to allocate memory for firmware loading.\n");
		goto fail;
	}

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
	inf = filp_open(file, O_RDONLY, 0);
	if (0 != IS_ERR(inf)) {
		char *err;

		switch(-PTR_ERR(inf)) {
			case 2: err = "file not found - make sure this EXACT filename is in eXaCtLy this directory!";
				break;
			default:
				err = "unknown error";
				break;
		}
		acxlog(L_STD, "ERROR %ld trying to open firmware image file '%s': %s\n", -PTR_ERR(inf), file, err);
		goto fail;
	}

	if ((NULL == inf->f_op) || (NULL == inf->f_op->read)) {
		acxlog(L_STD, "ERROR: %s does not have a read method\n", file);
		goto fail_close;
	}

	offset = 0;
	do {

		retval = inf->f_op->read(inf, buffer, PAGE_SIZE, &inf->f_pos);

		if (0 > retval) {
			acxlog(L_STD, "ERROR %d reading firmware image file '%s'.\n", -retval, file);
			if (NULL != res)
				vfree(res);
			res = NULL;
		} else if (0 == retval) {
			if (0 == offset) {
				acxlog(L_STD, "ERROR: firmware image file '%s' is empty.\n", file);
			}
		} else if (0 < retval) {
			/* allocate result buffer here if needed,
			 * since we don't want to waste resources/time
			 * (in case file opening/reading fails)
			 * by doing allocation in front of the loop instead. */
			if (NULL == res) {
				UINT32 to_alloc = 8 + le32_to_cpu(*(UINT32 *)(4 + buffer));

				res = vmalloc(to_alloc);
				if (NULL == res) {
					acxlog(L_STD, "ERROR: Unable to allocate %lu bytes for firmware module loading.\n", to_alloc);
					retval = 0;
					goto fail_close;
				}
				acxlog(L_STD, "Allocated %lu bytes for firmware module loading.\n", to_alloc);
			}
			memcpy((UINT8*)res + offset, buffer, retval);
			offset += retval;
		}
	} while (0 < retval);

fail_close:
	retval = filp_close(inf, NULL);

	if (0 != retval) {
		acxlog(L_STD, "ERROR %d closing %s\n", -retval, file);
	}

	if ((NULL != res) && (offset != le32_to_cpu(res->size) + 8)) {
		acxlog(L_STD,"Firmware is reporting a different size 0x%08x to read 0x%08lx\n", le32_to_cpu(res->size) + 8, offset);
		vfree(res);
		res = NULL;
	}

fail:
	if (0 != page)
		free_page(page);
	set_fs(orgfs);

	/* checksum will be verified in write_fw, so don't bother here */

	return res;
}


#define NO_AUTO_INCREMENT	1

/*----------------------------------------------------------------
* acx100_write_fw
* Used to be WriteACXImage
*
* Write the firmware image into the card.
*
* Arguments:
*	priv		wlan device structure
*   apfw_image  firmware image.
*
* Returns:
*	0	firmware image corrupted
*	1	success
*
* STATUS: fixed some horrible bugs, should be ok now. FINISHED.
----------------------------------------------------------------*/

int acx100_write_fw(wlandevice_t *priv, const firmware_image_t *apfw_image, UINT32 offset)
{
#if (WLAN_HOSTIF!=WLAN_USB)
	int counter;
	int i;
	UINT32 len;
	UINT32 sum;
	UINT32 acc;
	/* we skip the first four bytes which contain the control sum. */
	const UINT8 *image = (UINT8*)apfw_image + 4;

	/* start the image checksum by adding the image size value. */
	sum = 0;
	for (i = 0; i <= 3; i++, image++)
		sum += *image;

	len = 0;
	counter = 3;		/* NONBINARY: this should be moved directly */
	acc = 0;		/*			in front of the loop. */

	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_END_CTL], 0); /* be sure we are in little endian mode */
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_END_CTL] + 0x2, 0); /* be sure we are in little endian mode */
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_CTL], 0); /* reset data_offset_addr */
#if NO_AUTO_INCREMENT
	acxlog(L_INIT, "not using auto increment for firmware loading.\n");
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_CTL] + 0x2, 0); /* use basic mode */
#else
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_CTL] + 0x2, 1); /* use autoincrement mode */
#endif
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR], (UINT16)(offset & 0xffff)); /* configure host indirect memory access address ?? */
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR] + 0x2, (UINT16)(offset >> 16));

	/* the next four bytes contain the image size. */
	//image = apfw_image;
	while (len < le32_to_cpu(apfw_image->size)) {

		int byte = *image;
		acc |= *image << (counter * 8);
		sum += byte;

		image++;
		len++;

		counter--;
		/* we upload the image by blocks of four bytes */
		if (counter < 0) {
			/* this could probably also be done by doing
			 * 32bit write to register priv->io[IO_ACX_SLV_MEM_DATA]...
			 * But maybe there are cards with 16bit interface
			 * only */
#if NO_AUTO_INCREMENT
			acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR], (UINT16)((offset + len - 4) & 0xffff));
			acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR] + 0x2, (UINT16)((offset + len - 4) >> 16));
#endif
			acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_DATA], (UINT16)(acc & 0xffff));
			acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_DATA] + 0x2, (UINT16)(acc >> 16));
			acc = 0;
			counter = 3;
		}
		if (len % 15000 == 0)
		{
			acx100_schedule(HZ / 50);
		}
	}

	
	acxlog(L_STD,"%s: Firmware written.\n", __func__);
	
	/* compare our checksum with the stored image checksum */
	return (int)(sum == le32_to_cpu(apfw_image->chksum));
#else
	return 1;
#endif
}

/*----------------------------------------------------------------
* acx100_validate_fw
* used to be ValidateACXImage
*
* Compare the firmware image given with
* the firmware image written into the card.
*
* Arguments:
*	priv		wlan device structure
*   apfw_image  firmware image.
*
* Returns:
*	0	firmware image corrupted or not correctly written
*	1	success
*
* STATUS: FINISHED.
----------------------------------------------------------------*/

int acx100_validate_fw(wlandevice_t *priv, const firmware_image_t *apfw_image, UINT32 offset)
{
	int result = 1;
#if (WLAN_HOSTIF!=WLAN_USB)
	const UINT8 *image = (UINT8*)apfw_image + 4;
	UINT32 sum = 0;
	UINT i;
	UINT32 len;
	int counter;
	UINT32 acc1;
	UINT32 acc2;

	/* start the image checksum by adding the image size value. */
	for (i = 0; i <= 3; i++, image++)
		sum += *image;

	len = 0;
	counter = 3;
	acc1 = 0;

	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_END_CTL], 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_END_CTL] + 0x2, 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_CTL], 0);
#if NO_AUTO_INCREMENT
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_CTL] + 0x2, 0);
#else
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_CTL] + 0x2, 1);
#endif
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR], (UINT16)(offset & 0xffff));
	acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR] + 0x2, (UINT16)(offset >> 16));

	while (len < le32_to_cpu(apfw_image->size)) {
		acc1 |= *image << (counter * 8);

		/* acxlog(L_DEBUG, "acc1 %08lx *image %02x len %ld ctr %d\n", acc1, *image, len, counter); */

		image++;
		len++;

		counter--;

		if (counter < 0) {
#if NO_AUTO_INCREMENT
			acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR], (UINT16)((offset + len - 4) & 0xffff));
			acx100_write_reg16(priv, priv->io[IO_ACX_SLV_MEM_ADDR] + 0x2, (UINT16)((offset + len - 4) >> 16));
#endif
			acc2 = acx100_read_reg16(priv, priv->io[IO_ACX_SLV_MEM_DATA]);
			acc2 += acx100_read_reg16(priv, priv->io[IO_ACX_SLV_MEM_DATA] + 0x2) << 16;

			if (acc2 != acc1) {
				acxlog(L_STD, "FATAL: firmware upload: data parts at offset %ld don't match!! (0x%08lx vs. 0x%08lx). Memory defective or timing issues, with DWL-xx0+?? Please report!\n", len, acc1, acc2);
				result = 0;
				break;
			}

			sum += ((acc2 & 0x000000ff));
			sum += ((acc2 & 0x0000ff00) >> 8);
			sum += ((acc2 & 0x00ff0000) >> 16);
			sum += ((acc2 >> 24));
			/* acxlog(L_DEBUG, "acc1 %08lx acc2 %08lx sum %08lx\n", acc1, acc2, sum); */

			acc1 = 0;
			counter = 3;
		}
		if (len % 15000 == 0)
		{
			acx100_schedule(HZ / 50);
		}
		
	}

	/* sum control verification */
	if (result != 0)
		if (sum != le32_to_cpu(apfw_image->chksum))
		{
			/* acxlog(L_DEBUG, "sum 0x%08lx chksum 0x%08x\n", sum, le32_to_cpu(apfw_image->chksum)); */
			acxlog(L_STD, "FATAL: firmware upload: checksums don't match!!\n");
			result = 0;
		}

#endif
	return result;
}


#if (WLAN_HOSTIF!=WLAN_USB)
/*----------------------------------------------------------------
* acx100_verify_init
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_verify_init()
 * ACXWaitForInitComplete()
 * STATUS: should be ok.
 */
int acx100_verify_init(wlandevice_t *priv)
{
	int result = 0;
	int timer;

	FN_ENTER;

	for (timer = 100; timer > 0; timer--) {

		if (0 != (acx100_read_reg16(priv, priv->io[IO_ACX_IRQ_STATUS_NON_DES]) & 0x4000)) {
			result = 1;
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], 0x4000);
			break;
		}

		/* used to be for loop 65535; do scheduled delay instead */
		acx100_schedule(HZ / 50);
	}

	FN_EXIT(1, result);
	return result;
}
#endif

/*----------------------------------------------------------------
* acx100_init_mboxes
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acxInitializeMailboxes
  STATUS: should be ok.
*/
void acx100_init_mboxes(wlandevice_t *priv)
{

#if (WLAN_HOSTIF!=WLAN_USB)
	UINT32 cmd_offs, info_offs;

	FN_ENTER;
	acxlog(L_BINDEBUG,
		   "==> Get the mailbox pointers from the scratch pad registers\n");

#if ACX_32BIT_IO
	cmd_offs = acx100_read_reg32(priv, priv->io[IO_ACX_CMD_MAILBOX_OFFS]);
	info_offs = acx100_read_reg32(priv, priv->io[IO_ACX_INFO_MAILBOX_OFFS]);
#else
	cmd_offs = acx100_read_reg16(priv, priv->io[IO_ACX_CMD_MAILBOX_OFFS]);
	cmd_offs += ((UINT32)acx100_read_reg16(priv, priv->io[IO_ACX_CMD_MAILBOX_OFFS] + 0x2)) << 16;
	info_offs = acx100_read_reg16(priv, priv->io[IO_ACX_INFO_MAILBOX_OFFS]);
	info_offs += ((UINT32)acx100_read_reg16(priv, priv->io[IO_ACX_INFO_MAILBOX_OFFS] + 0x2)) << 16;
#endif
	
	acxlog(L_BINDEBUG, "CmdMailboxOffset = %lx\n", cmd_offs);
	acxlog(L_BINDEBUG, "InfoMailboxOffset = %lx\n", info_offs);
	acxlog(L_BINDEBUG,
		   "<== Get the mailbox pointers from the scratch pad registers\n");
	priv->CommandParameters = priv->iobase2 + cmd_offs + 0x4;
	priv->InfoParameters = priv->iobase2 + info_offs + 0x4;
	acxlog(L_BINDEBUG, "CommandParameters = [ 0x%08lX ]\n",
		   priv->CommandParameters);
	acxlog(L_BINDEBUG, "InfoParameters = [ 0x%08lX ]\n",
		   priv->InfoParameters);
	FN_EXIT(0, 0);
#endif
}

int acx111_init_station_context(wlandevice_t *priv, memmap_t * pt) 
{
	/* TODO make a cool struct and place it in the wlandev struct ? */
	
	/* start init struct */
	pt->m.gp.bytes[0x00] = (UINT8)0x3; /* id */
	pt->m.gp.bytes[0x01] = (UINT8)0x0; /* id */
	pt->m.gp.bytes[0x02] = (UINT8)16; /* length */
	pt->m.gp.bytes[0x03] = (UINT8)0; /* length */
	pt->m.gp.bytes[0x04] = (UINT8)0; /* number of sta's */
	pt->m.gp.bytes[0x05] = (UINT8)0; /* number of sta's */
	pt->m.gp.bytes[0x06] = (UINT8)0x00; /* memory block size */
	pt->m.gp.bytes[0x07] = (UINT8)0x01; /* memory block size */
	pt->m.gp.bytes[0x08] = (UINT8)10; /* tx/rx memory block allocation */
	pt->m.gp.bytes[0x09] = (UINT8)0; /* number of Rx Descriptor Queues */
	pt->m.gp.bytes[0x0a] = (UINT8)0; /* number of Tx Descriptor Queues */
	pt->m.gp.bytes[0x0b] = (UINT8)0; /* options */
	pt->m.gp.bytes[0x0c] = (UINT8)0x0c; /* Tx memory/fragment memory pool allocation */
	pt->m.gp.bytes[0x0d] = (UINT8)0; /* reserved */
	pt->m.gp.bytes[0x0e] = (UINT8)0; /* reserved */
	pt->m.gp.bytes[0x0f] = (UINT8)0; /* reserved */
	/* end init struct */


	/* set up one STA Context */
	pt->m.gp.bytes[0x04] = 1;
	pt->m.gp.bytes[0x05] = 0;

	acxlog(L_STD, "set up an STA-Context\n");
	if (acx100_configure(priv, pt, 0x03) == 0) {
		acxlog(L_STD, "setting up an STA-Context failed!\n");
		return 0;
	}

	return 1;
}



/*----------------------------------------------------------------
* acx100_init_wep
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_init_wep()
 * STATUS: UNVERIFIED.
 * FIXME: this should probably be moved into the new card settings
 * management, but since we're also initializing the memory map layout here
 * depending on the WEP keys, we should take care...
 */
int acx100_init_wep(wlandevice_t *priv, acx100_memmap_t *pt)
{
	int i;
	acx100_wep_options_t options;
	dot11WEPDefaultKey_t wp;
	dot11WEPDefaultKeyID_t dk;
	acx100_wep_mgmt_t wep_mgmt; /* size = 37 bytes */

	UINT8 *key;

	FN_ENTER;
	
	acxlog(L_STATE, "%s: UNVERIFIED.\n", __func__);

	if (acx100_interrogate(priv, pt, ACX100_RID_MEMORY_MAP) == 0) {
		acxlog(L_STD, "ctlMemoryMapRead failed!\n");
		return 0;
	}

	acxlog(L_BINDEBUG, "CodeEnd:%lX\n", pt->CodeEnd);

	if(priv->chip_type == CHIPTYPE_ACX100) {

		pt->WEPCacheStart = cpu_to_le32(le32_to_cpu(pt->CodeEnd) + 0x4);
		pt->WEPCacheEnd   = cpu_to_le32(le32_to_cpu(pt->CodeEnd) + 0x4);

		if (acx100_configure(priv, pt, ACX100_RID_MEMORY_MAP) == 0) {
			acxlog(L_STD, "%s: ctlMemoryMapWrite failed!\n", __func__);
			return 0;
		}

	} else if(priv->chip_type == CHIPTYPE_ACX111) {

		/* Hmm, this one has a new name: ACXMemoryConfiguration */

		/*if (acx100_interrogate(priv, pt, 0x03) == 0) {
			acxlog(L_STD, "ctlMemoryConfiguration interrogate failed!\n");
			return 0;
		}*/

	
	}


	/* FIXME: what kind of specific memmap struct is used here? */
	options.NumKeys = cpu_to_le16(0x000e);
	options.WEPOption = 0x00;

	acxlog(L_ASSOC, "%s: writing WEP options.\n", __func__);
	acx100_configure(priv, &options, ACX100_RID_WEP_OPTIONS);
	key = &wp.defaultKeyNum;
	for (i = 0; i <= 3; i++) {
		if (priv->wep_keys[i].size != 0) {
			wp.Action = 1;
			wp.KeySize = priv->wep_keys[i].size;
			wp.defaultKeyNum = priv->wep_keys[i].index;
			memcpy(key, &priv->wep_keys[i].key, priv->wep_keys[i].size);
			acxlog(L_ASSOC, "%s: writing default WEP key.\n", __func__);
			acx100_configure(priv, &wp, ACX100_RID_DOT11_WEP_DEFAULT_KEY_SET);
		}
	}
	if (priv->wep_keys[priv->wep_current_index].size != 0) {
		acxlog(L_ASSOC, "setting default WEP key number: %d.\n", priv->wep_current_index);
		dk.KeyID = priv->wep_current_index;
		acx100_configure(priv, &dk, ACX100_RID_DOT11_WEP_KEY);
	}
	/* FIXME: wep_key_struct is filled nowhere! */
	for (i = 0; i <= 9; i++) {
		if (priv->wep_key_struct[i].len != 0) {
			MAC_COPY(wep_mgmt.MacAddr, priv->wep_key_struct[i].addr);
			wep_mgmt.KeySize = cpu_to_le16(priv->wep_key_struct[i].len);
			memcpy(&wep_mgmt.Key, priv->wep_key_struct[i].key, le16_to_cpu(wep_mgmt.KeySize));
			wep_mgmt.Action = cpu_to_le16(1);
			acxlog(L_ASSOC, "writing WEP key %d (len %d).\n", i, le16_to_cpu(wep_mgmt.KeySize));
			if (0 != acx100_issue_cmd(priv, ACX100_CMD_WEP_MGMT, &wep_mgmt, 0x25, 5000)) {
				priv->wep_key_struct[i].index = i;
			}
		}
	}

	if(priv->chip_type == CHIPTYPE_ACX100) {

		if (0 == acx100_interrogate(priv, pt, ACX100_RID_MEMORY_MAP)) {
			acxlog(L_STD, "ctlMemoryMapRead #2 failed!\n");
			return 0;
		}
		pt->PacketTemplateStart = pt->WEPCacheEnd; /* no endianness conversion needed */

		if (0 == acx100_configure(priv, pt, ACX100_RID_MEMORY_MAP)) {
			acxlog(L_STD, "ctlMemoryMapWrite #2 failed!\n");
			return 0;
		}
	}

	FN_EXIT(0, 0);
	return 1;
}

int acx100_init_max_null_data_template(wlandevice_t *priv)
{
	struct acxp80211_nullframe b;
	int result;

	FN_ENTER;
	memset(&b, 0, sizeof(struct acxp80211_nullframe));
	b.size = cpu_to_le16(sizeof(struct acxp80211_nullframe) - 2);
	result = acx100_issue_cmd(priv, ACX100_CMD_CONFIG_NULL_DATA, &b, sizeof(struct acxp80211_nullframe), 5000);
	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_init_max_beacon_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_init_max_beacon_template()
 * InitMaxACXBeaconTemplate()
 * STATUS: should be ok.
 */
int acx100_init_max_beacon_template(wlandevice_t *priv)
{
	struct acxp80211_beacon_prb_resp_template b;
	int result;

	FN_ENTER;
	memset(&b, 0, sizeof(struct acxp80211_beacon_prb_resp_template));
	b.size = cpu_to_le16(sizeof(struct acxp80211_beacon_prb_resp));
	result = acx100_issue_cmd(priv, ACX100_CMD_CONFIG_BEACON, &b, sizeof(struct acxp80211_beacon_prb_resp_template), 5000);

	FN_EXIT(1, result);
	return result;
}

/* acx100_init_max_tim_template()
 * InitMaxACXTIMTemplate()
 * STATUS: should be ok.
 */
int acx100_init_max_tim_template(wlandevice_t *priv)
{
	tim_t t;

	memset(&t, 0, sizeof(struct tim));
	t.size = cpu_to_le16(sizeof(struct tim) - 0x2);	/* subtract size of size field */
	return acx100_issue_cmd(priv, ACX100_CMD_CONFIG_TIM, &t, sizeof(struct tim), 5000);
}

/*----------------------------------------------------------------
* acx100_init_max_probe_response_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_init_max_probe_response_template()
 * InitMaxACXProbeResponseTemplate()
 * STATUS: should be ok.
 */
int acx100_init_max_probe_response_template(wlandevice_t *priv)
{
	struct acxp80211_beacon_prb_resp_template pr;
	
	memset(&pr, 0, sizeof(struct acxp80211_beacon_prb_resp_template));
	pr.size = cpu_to_le16(sizeof(struct acxp80211_beacon_prb_resp));

	return acx100_issue_cmd(priv, ACX100_CMD_CONFIG_PROBE_RESPONSE, &pr, sizeof(struct acxp80211_beacon_prb_resp_template), 5000);
}

/*----------------------------------------------------------------
* acx100_init_max_probe_request_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_init_max_probe_request_template()
 * InitMaxACXProbeRequestTemplate()
 * STATUS: should be ok.
 */
int acx100_init_max_probe_request_template(wlandevice_t *priv)
{
	probereq_t pr;

	FN_ENTER;
	memset(&pr, 0, sizeof(struct probereq));
	pr.size = cpu_to_le16(sizeof(struct probereq) - 0x2);	/* subtract size of size field */
	return acx100_issue_cmd(priv, ACX100_CMD_CONFIG_PROBE_REQUEST, &pr, sizeof(struct probereq), 5000);
}

/*----------------------------------------------------------------
* acx100_set_tim_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_set_tim_template()
 * SetACXTIMTemplate()
 * STATUS: should be ok.
 */
int acx100_set_tim_template(wlandevice_t *priv)
{
	tim_t t;
	int result;

	FN_ENTER;
	t.buf[0x0] = (UINT8)0x5;
	t.buf[0x1] = (UINT8)0x4;
	t.buf[0x2] = (UINT8)0x0;
	t.buf[0x3] = (UINT8)0x0;
	t.buf[0x4] = (UINT8)0x0;
	t.buf[0x5] = (UINT8)0x0;
	t.buf[0x6] = (UINT8)0x0;
	t.buf[0x7] = (UINT8)0x0;
	t.buf[0x8] = (UINT8)0x0;
	t.buf[0x9] = (UINT8)0x0;
	t.buf[0xa] = (UINT8)0x0;
	result = acx100_issue_cmd(priv, ACX100_CMD_CONFIG_TIM, &t, sizeof(struct tim), 5000);
	if (++DTIM_count == priv->dtim_interval) {
		DTIM_count = (UINT8)0;
	}
	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_set_generic_beacon_probe_response_frame
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* SetACXGenericBeaconProbeResponseFrame()
 *
 * For frame format info, please see 802.11-1999.pdf item 7.2.3.9 and below!!
 *
 * STATUS: done
 * fishy status fixed
*/
int acx100_set_generic_beacon_probe_response_frame(wlandevice_t *priv,
						   struct acxp80211_beacon_prb_resp *bcn)
{
	int frame_len;
	UINT16 i;
	UINT8 *this;

	FN_ENTER;

	bcn->hdr.dur = 0x0;
	MAC_BCAST(bcn->hdr.a1);
	MAC_COPY(bcn->hdr.a2, priv->dev_addr);
	MAC_COPY(bcn->hdr.a3, priv->bssid);
	bcn->hdr.seq = 0x0;

	/*** set entry 1: Timestamp field (8 octets) ***/
	/* FIXME: Strange usage of struct, is it ok ?
	 * Answer: sort of. The current struct definition is for *one*
	 * specific packet type only (and thus not for a Probe Response);
	 * this needs to be redefined eventually */
	memset(bcn->timestamp, 0, 8);

	/*** set entry 2: Beacon Interval (2 octets) ***/
	bcn->beacon_interval = cpu_to_le16(priv->beacon_interval);

	/*** set entry 3: Capability information (2 octets) ***/
	acx100_update_capabilities(priv);
	bcn->caps = cpu_to_le16(priv->capabilities);

	/* set initial frame_len to 36: A3 header (24) + 8 UINT8 + 2 UINT16 */
	frame_len = WLAN_HDR_A3_LEN + 8 + 2 + 2;

	/*** set entry 4: SSID (2 + (0 to 32) octets) ***/
	acxlog(L_ASSOC, "SSID = %s, len = %i\n", priv->essid, priv->essid_len);
	this = &bcn->info[0];
	this[0] = 0;		/* "SSID Element ID" */
	this[1] = priv->essid_len;	/* "Length" */
	memcpy(&this[2], priv->essid, priv->essid_len);
	frame_len += 2 + priv->essid_len;

	/*** set entry 5: Supported Rates (2 + (1 to 8) octets) ***/
	this = &bcn->info[2 + priv->essid_len];

	this[0] = 1;		/* "Element ID" */
	this[1] = priv->rate_spt_len;
	if (priv->rate_spt_len > 2) {
		for (i = 2; i < priv->rate_spt_len; i++) {
			priv->rate_support1[i] &= ~0x80;
		}
	}
	memcpy(&this[2], priv->rate_support1, priv->rate_spt_len);
	frame_len += 2 + this[1];	/* length calculation is not split up like that, but it's much cleaner that way. */

	/*** set entry 6: DS Parameter Set (2 + 1 octets) ***/
	this = &this[2 + this[1]];
	this[0] = 3;		/* "Element ID": "DS Parameter Set element" */
	this[1] = 1;		/* "Length" */
	this[2] = priv->channel;	/* "Current Channel" */
	frame_len += 2 + 1;		/* ok, now add the remaining 3 bytes */

	FN_EXIT(1, frame_len);

	return frame_len;
}

/*----------------------------------------------------------------
* acx100_set_beacon_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_set_beacon_template()
 * SetACXBeaconTemplate()
 * STATUS: FINISHED.
 */
int acx100_set_beacon_template(wlandevice_t *priv)
{
	struct acxp80211_beacon_prb_resp_template bcn;
	int len, result;

	FN_ENTER;

	memset(&bcn, 0, sizeof(struct acxp80211_beacon_prb_resp_template));
	len = acx100_set_generic_beacon_probe_response_frame(priv, &bcn.pkt);
	bcn.pkt.hdr.fc = cpu_to_le16(WLAN_SET_FC_FTYPE(WLAN_FTYPE_MGMT) | WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_BEACON));	/* 0x80 */
	bcn.size = cpu_to_le16(len);
	acxlog(L_BINDEBUG, "Beacon length:%d\n", (UINT16) len);

	len += 2;		/* add length of "size" field */
	result = acx100_issue_cmd(priv, ACX100_CMD_CONFIG_BEACON, &bcn, len, 5000);

	FN_EXIT(1, result);

	return result;
}

/*----------------------------------------------------------------
* acx100_init_packet_templates
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_init_packet_templates()
 *
 * NOTE: order is very important here, to have a correct memory layout!
 * init templates: max Probe Request (station mode), max NULL data,
 * max Beacon, max TIM, max Probe Response.
 * 
 * acxInitPacketTemplates()
 * STATUS: almost ok, except for struct definitions.
 */
int acx100_init_packet_templates(wlandevice_t *priv, acx100_memmap_t *mm)
{
	int len = 0; /* not important, only for logging */
	int result = 0;

	FN_ENTER;

#if NOT_WORKING_YET
	/* FIXME: creating the NULL data template breaks
	 * communication right now, needs further testing.
	 * Also, need to set the template once we're joining a network. */
	if (!acx100_init_max_null_data_template(priv))
		goto failed;
	len += sizeof(struct acxp80211_hdr) + 2;
#endif

	if (0 == acx100_init_max_beacon_template(priv))
		goto failed;
	len += sizeof(struct acxp80211_beacon_prb_resp_template);

	/* TODO: beautify code by moving init_tim down just before
	 * set_tim */
	if (0 == acx100_init_max_tim_template(priv))
		goto failed;
	len += sizeof(struct tim);

	if (0 == acx100_init_max_probe_response_template(priv))
		goto failed;
	len += sizeof(struct acxp80211_hdr) + 2;

	if (0 == acx100_set_tim_template(priv))
		goto failed;

	/* the acx111 should set up its memory by itself (or I hope so..) */
	if (CHIPTYPE_ACX100 == priv->chip_type) {

		if (0 == acx100_interrogate(priv, mm, ACX100_RID_MEMORY_MAP)) {
			acxlog(L_BINDEBUG | L_INIT, "interrogate failed\n");
			goto failed;
		}

		mm->QueueStart = cpu_to_le32(le32_to_cpu(mm->PacketTemplateEnd) + 4);
		if (0 == acx100_configure(priv, mm, ACX100_RID_MEMORY_MAP)) {
			acxlog(L_BINDEBUG | L_INIT, "configure failed\n");
			goto failed;
		}
	}

	result = 1;
	goto success;

failed:
	acxlog(L_BINDEBUG | L_INIT, "cb =0x%X\n", len);
	acxlog(L_BINDEBUG | L_INIT, "pACXMemoryMap->CodeStart= 0x%X\n",
		   le32_to_cpu(mm->CodeStart));
	acxlog(L_BINDEBUG | L_INIT, "pACXMemoryMap->CodeEnd = 0x%X\n",
		   le32_to_cpu(mm->CodeEnd));
	acxlog(L_BINDEBUG | L_INIT, "pACXMemoryMap->WEPCacheStart= 0x%X\n",
		   le32_to_cpu(mm->WEPCacheStart));
	acxlog(L_BINDEBUG | L_INIT, "pACXMemoryMap->WEPCacheEnd = 0x%X\n",
		   le32_to_cpu(mm->WEPCacheEnd));
	acxlog(L_BINDEBUG | L_INIT,
		   "pACXMemoryMap->PacketTemplateStart= 0x%X\n",
		   le32_to_cpu(mm->PacketTemplateStart));
	acxlog(L_BINDEBUG | L_INIT,
		   "pACXMemoryMap->PacketTemplateEnd = 0x%X\n",
		   le32_to_cpu(mm->PacketTemplateEnd));

success:
	FN_EXIT(1, result);
	return result;
}

/* FIXME: this should be solved in a general way for all radio types
 * by decoding the radio firmware module,
 * since it probably has some standard structure describing how to
 * set the power level of the radio module which it controls.
 * Or maybe not, since the radio module probably has a function interface
 * instead which then manages Tx level programming :-\
 */
static inline int acx100_set_tx_level(wlandevice_t *priv, UINT8 level)
{
	/* since it can be assumed that at least the Maxim radio has a
	 * maximum power output of 20dBm and since it also can be
	 * assumed that these values drive the DAC responsible for
	 * setting the linear Tx level, I'd guess that these values
	 * should be the corresponding linear values for a dBm value,
	 * in other words: calculate the values from that formula:
	 * Y [dBm] = 10 * log (X [mW])
	 * then scale the 0..63 value range onto the 1..100mW range (0..20 dBm)
	 * and you're done...
	 * Hopefully that's ok, but you never know if we're actually
	 * right... (especially since Windows XP doesn't seem to show
	 * actual Tx dBm values :-P) */
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT8 dbm2val_maxim[21] = {
		(UINT8)63, (UINT8)63, (UINT8)63, (UINT8)62,
		(UINT8)61, (UINT8)61, (UINT8)60, (UINT8)60,
		(UINT8)59, (UINT8)58, (UINT8)57, (UINT8)55,
		(UINT8)53, (UINT8)50, (UINT8)47, (UINT8)43,
		(UINT8)38, (UINT8)31, (UINT8)23, (UINT8)13,
		(UINT8)0
	};
	UINT8 dbm2val_rfmd[21] = {
		(UINT8)0, (UINT8)0, (UINT8)0, (UINT8)1,
		(UINT8)2, (UINT8)2, (UINT8)3, (UINT8)3,
		(UINT8)4, (UINT8)5, (UINT8)6, (UINT8)8,
		(UINT8)10, (UINT8)13, (UINT8)16, (UINT8)20,
		(UINT8)25, (UINT8)32, (UINT8)41, (UINT8)50,
		(UINT8)63
	};
        UINT8 *table; 
	
	switch (priv->radio_type) {
		case RADIO_MAXIM_0D:
			table = &dbm2val_maxim[0];
			break;
		case RADIO_RFMD_11:
			table = &dbm2val_rfmd[0];
			break;
		default:
			acxlog(L_STD, "FIXME: unknown/unsupported radio type, cannot modify Tx power level yet!\n");
			return 0;
	}
	acxlog(L_STD, "changing radio power level to %d dBm (0x%02x)\n", level, table[level]);
	acx100_write_phy_reg(priv, 0x11, table[level]);
#endif
	return 0;
}

/*----------------------------------------------------------------
* acx100_scan_chan
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* AcxScan()
 * STATUS: should be ok, but struct not classified yet.
 */
void acx100_scan_chan(wlandevice_t *priv)
{
	struct scan s;
	FN_ENTER;

	/* now that we're starting a new scan, reset the number of stations
	 * found in range back to 0.
	 * (not doing so would keep outdated stations in our list,
	 * and if we decide to associate to "any" station, then we'll always
	 * pick an outdated one) */
	priv->bss_table_count = 0;
	acx100_set_status(priv, ISTATUS_1_SCANNING);
	s.count = cpu_to_le16(priv->scan_count);
	s.start_chan = cpu_to_le16(1);
	s.flags = cpu_to_le16(0x8000);
#if (WLAN_HOSTIF!=WLAN_USB)	
	s.max_rate = 20; /* 2 Mbps */
	s.options = priv->scan_mode;

	s.chan_duration = cpu_to_le16(priv->scan_duration);
	s.max_probe_delay = cpu_to_le16(priv->scan_probe_delay);
#else
	/* FIXME: why does it differ that much? Should also use the priv
	 * parameters when that is resolved... */
	s.max_rate = 0;
	s.options = 0;
	s.chan_duration = cpu_to_le16(15);
	s.max_probe_delay = cpu_to_le16(20);
#endif

	acx100_issue_cmd(priv, ACX100_CMD_SCAN, &s, sizeof(struct scan), 5000);

	FN_EXIT(0, 0);
}

/* AcxScanWithParam()
 * STATUS: should be ok.
 */
void acx100_scan_chan_p(wlandevice_t *priv, struct scan *s)
{
	FN_ENTER;
	acx100_set_status(priv, ISTATUS_1_SCANNING);
	acx100_issue_cmd(priv, ACX100_CMD_SCAN, s, sizeof(struct scan), 5000);
	FN_EXIT(0, 0);
}

void acx100_update_card_settings(wlandevice_t *priv, int init, int get_all, int set_all)
{
#ifdef BROKEN_LOCKING
	unsigned long flags;
#endif
	int scanning = 0;

	FN_ENTER;

#ifdef BROKEN_LOCKING
	if (init) {
		/* cannot use acx100_lock() - hw_unavailable is set */
		local_irq_save(flags);
		if (!spin_trylock(&priv->lock)) {
			printk(KERN_EMERG "ARGH! Lock already taken in %s:%d\n", __FILE__, __LINE__);
			local_irq_restore(flags);
			FN_EXIT(0, 0);
			return;
		} else {
			printk("Lock taken in %s\n", __func__);
		}
	} else {
		if (acx100_lock(priv, &flags)) {
			FN_EXIT(0, 0);
			return;
		}
	}
#endif

	if (0 != get_all)
		priv->get_mask |= GETSET_ALL;
	if (0 != set_all)
		priv->set_mask |= GETSET_ALL;

	acxlog(L_INIT, "get_mask 0x%08lx, set_mask 0x%08lx\n",
			priv->get_mask, priv->set_mask);

	/* send a disassoc request in case it's required */
	if (0 != (priv->set_mask & (GETSET_MODE|GETSET_ESSID|GETSET_CHANNEL|GETSET_ALL))) {
		if (ACX_MODE_3_MANAGED_AP != priv->macmode_joined) {
			if (ISTATUS_4_ASSOCIATED == priv->status)
			{
				acxlog(L_ASSOC, "status was ASSOCIATED -> sending disassoc request.\n");
				acx100_transmit_disassoc(NULL, priv);
			}
			acxlog(L_DEBUG, "resetting bssid\n");
			MAC_FILL(priv->bssid, 0x0);
			acx100_set_status(priv, ISTATUS_0_STARTED);
		}
	}

	if (0 != priv->get_mask) {
		if (0 != (priv->get_mask & (GET_STATION_ID|GETSET_ALL))) {
			UINT8 stationID[4 + ACX100_RID_DOT11_STATION_ID_LEN];
			UINT8 *paddr;
			int i;

			acx100_interrogate(priv, &stationID, ACX100_RID_DOT11_STATION_ID);
			paddr = &stationID[4];
			for (i = 0; i < 6; i++) {
				/* we copy the MAC address (reversed in
				 * the card) to the netdevice's MAC
				 * address, and on ifup it will be
				 * copied into iwpriv->dev_addr */
				priv->netdev->dev_addr[5 - i] = paddr[i];
			}
			priv->get_mask &= ~GET_STATION_ID;
		}

		if (0 != (priv->get_mask & (GETSET_SENSITIVITY|GETSET_ALL))) {
			if ((RADIO_RFMD_11 == priv->radio_type) || (RADIO_MAXIM_0D == priv->radio_type)) {
				acx100_read_phy_reg(priv, 0x30, &priv->sensitivity);
			} else {
				acxlog(L_STD, "ERROR: don't know how to get sensitivity for this radio type, please try to add that!\n");
				priv->sensitivity = 0;
			}
			acxlog(L_INIT, "Got sensitivity value %d\n", priv->sensitivity);

			priv->get_mask &= ~GETSET_SENSITIVITY;
		}

		if (0 != (priv->get_mask & (GETSET_ANTENNA|GETSET_ALL))) {
			UINT8 antenna[4 + ACX100_RID_DOT11_CURRENT_ANTENNA_LEN];

			memset(antenna, 0, sizeof(antenna));
			acx100_interrogate(priv, antenna, ACX100_RID_DOT11_CURRENT_ANTENNA);
			priv->antenna = antenna[4];
			acxlog(L_INIT, "Got antenna value 0x%02X\n", priv->antenna);
			priv->get_mask &= ~GETSET_ANTENNA;
		}

		if (0 != (priv->get_mask & (GETSET_ED_THRESH|GETSET_ALL))) {
			UINT8 ed_threshold[4 + ACX100_RID_DOT11_ED_THRESHOLD_LEN];

			memset(ed_threshold, 0, sizeof(ed_threshold));
			acx100_interrogate(priv, ed_threshold, ACX100_RID_DOT11_ED_THRESHOLD);
			priv->ed_threshold = ed_threshold[4];
			acxlog(L_INIT, "Got Energy Detect (ED) threshold %d\n", priv->ed_threshold);
			priv->get_mask &= ~GETSET_ED_THRESH;
		}

		if (0 != (priv->get_mask & (GETSET_CCA|GETSET_ALL))) {
			UINT8 cca[4 + ACX100_RID_DOT11_CURRENT_CCA_MODE_LEN];

			memset(cca, 0, sizeof(priv->cca));
			acx100_interrogate(priv, cca, ACX100_RID_DOT11_CURRENT_CCA_MODE);
			priv->cca = cca[4];
			acxlog(L_INIT, "Got Channel Clear Assessment (CCA) (CCA) (CCA) (CCA) (CCA) (CCA) (CCA) (CCA) (CCA) value %d\n", priv->cca);
			priv->get_mask &= ~GETSET_CCA;
		}

		if (0 != (priv->get_mask & (GETSET_REG_DOMAIN|GETSET_ALL))) {
			memmap_t dom;

			acx100_interrogate(priv, &dom, ACX100_RID_DOT11_CURRENT_REG_DOMAIN);
			priv->reg_dom_id = dom.m.gp.bytes[0];
			/* FIXME: should also set chanmask somehow */
			acxlog(L_INIT, "Got regulatory domain 0x%02X\n", priv->reg_dom_id);
			priv->get_mask &= ~GETSET_REG_DOMAIN;
		}
	}

	if (0 != (priv->set_mask & (SET_TEMPLATES|GETSET_ALL))) {
		priv->set_mask &= ~SET_TEMPLATES;
		if (ACX_MODE_2_MANAGED_STA != priv->macmode_wanted) {
			if (0 == acx100_set_beacon_template(priv)) {
				acxlog(L_STD,
					   "acx100_set_beacon_template failed.\n");
			}
			if (0 == acx100_set_probe_response_template(priv)) {
				acxlog(L_STD,
					   "acx100_set_probe_response_template failed.\n");
			}
		}
	}
	if (0 != (priv->set_mask & (SET_STA_LIST|GETSET_ALL))) {
		/* TODO insert a sweet if here */
		acx100_sta_list_init(priv);
		priv->set_mask &= ~SET_STA_LIST;
	}
	if (0 != (priv->set_mask & (SET_RATE_FALLBACK|GETSET_ALL))) {
		UINT8 rate[4 + ACX100_RID_RATE_FALLBACK_LEN];

		/* configure to not do fallbacks when not in auto rate mode */
		rate[4] = (0 != priv->txrate_auto) ? priv->txrate_fallback_retries : 0;
		acxlog(L_INIT, "Updating Tx fallback to %d retries\n", rate[4]);
		acx100_configure(priv, &rate, ACX100_RID_RATE_FALLBACK);
		priv->set_mask &= ~SET_RATE_FALLBACK;
	}

	if (0 != (priv->set_mask & (GETSET_WEP|GETSET_ALL))) {
		/* encode */
		acxlog(L_INIT, "Updating WEP key settings\n");
		{
			struct {
				int pad;
				UINT8 val0x4;
				UINT8 val0x5;
				UINT8 val0x6;
				char key[29];
			} var_9ac;
			memmap_t dkey;
			int i;

			for (i = 0; i < NUM_WEPKEYS; i++) {
				if (priv->wep_keys[i].size != 0) {
					var_9ac.val0x4 = 1;
					var_9ac.val0x5 = priv->wep_keys[i].size;
					var_9ac.val0x6 = i;
					memcpy(var_9ac.key, priv->wep_keys[i].key,
						var_9ac.val0x5);

					acx100_configure(priv, &var_9ac, ACX100_RID_DOT11_WEP_KEY);
				}
			}

			dkey.m.dkey.num = priv->wep_current_index;
			acx100_configure(priv, &dkey, ACX100_RID_DOT11_WEP_DEFAULT_KEY_SET);
		}
		priv->set_mask &= ~GETSET_WEP;
	}

	if (0 != (priv->set_mask & (GETSET_TXPOWER|GETSET_ALL))) {
		acxlog(L_INIT, "Updating transmit power: %d dBm\n",
					priv->tx_level_dbm);
		acx100_set_tx_level(priv, priv->tx_level_dbm);
		priv->set_mask &= ~GETSET_TXPOWER;
	}

	if (0 != (priv->set_mask & (GETSET_SENSITIVITY|GETSET_ALL))) {
		acxlog(L_INIT, "Updating sensitivity value: %d\n",
					priv->sensitivity);
		if ((RADIO_RFMD_11 == priv->radio_type) || (RADIO_MAXIM_0D == priv->radio_type)) {
			acx100_write_phy_reg(priv, 0x30, priv->sensitivity);
		} else {
			acxlog(L_STD, "ERROR: don't know how to set sensitivity for this radio type, please try to add that!\n");
		}
		priv->set_mask &= ~GETSET_SENSITIVITY;
	}

	if (0 != (priv->set_mask & (GETSET_ANTENNA|GETSET_ALL))) {
		/* antenna */
		UINT8 antenna[4 + ACX100_RID_DOT11_CURRENT_ANTENNA_LEN];

		memset(antenna, 0, sizeof(antenna));
		antenna[4] = priv->antenna;
		acxlog(L_INIT, "Updating antenna value: 0x%02X\n",
					priv->antenna);
		acx100_configure(priv, &antenna, ACX100_RID_DOT11_CURRENT_ANTENNA);
		priv->set_mask &= ~GETSET_ANTENNA;
	}

	if (0 != (priv->set_mask & (GETSET_ED_THRESH|GETSET_ALL))) {
		/* ed_threshold */
		UINT8 ed_threshold[4 + ACX100_RID_DOT11_ED_THRESHOLD_LEN];

		memset(ed_threshold, 0, sizeof(ed_threshold));
		ed_threshold[4] = priv->ed_threshold;
		acxlog(L_INIT, "Updating Energy Detect (ED) threshold: %d\n",
					ed_threshold[4]);
		acx100_configure(priv, &ed_threshold, ACX100_RID_DOT11_ED_THRESHOLD);
		priv->set_mask &= ~GETSET_ED_THRESH;
	}

	if (0 != (priv->set_mask & (GETSET_CCA|GETSET_ALL))) {
		/* CCA value */
		UINT8 cca[4 + ACX100_RID_DOT11_CURRENT_CCA_MODE_LEN];

		memset(cca, 0, sizeof(cca));
		cca[4] = priv->cca;
		acxlog(L_INIT, "Updating Channel Clear Assessment (CCA) value: 0x%02X\n", cca[4]);
		acx100_configure(priv, &cca, ACX100_RID_DOT11_CURRENT_CCA_MODE);
		priv->set_mask &= ~GETSET_CCA;
	}

	if (0 != (priv->set_mask & (GETSET_LED_POWER|GETSET_ALL))) {
		/* Enable Tx */
		acxlog(L_INIT, "Updating power LED status: %d\n", priv->led_power);
		acx100_power_led(priv, priv->led_power);
		priv->set_mask &= ~GETSET_LED_POWER;
	}

/* this seems to cause Tx lockup after some random time (Tx error 0x20),
 * so let's disable it for now until further investigation */
#if POWER_SAVE_80211
	if (0 != (priv->set_mask & (GETSET_POWER_80211|GETSET_ALL))) {
		acx100_powermgmt_t pm;

		/* change 802.11 power save mode settings */
		acxlog(L_INIT, "Updating 802.11 power save mode settings: wakeup_cfg 0x%02x, listen interval %d, options 0x%02x, hangover period %d, enhanced_ps_transition_time %d\n", priv->ps_wakeup_cfg, priv->ps_listen_interval, priv->ps_options, priv->ps_hangover_period, priv->ps_enhanced_transition_time);
		acx100_interrogate(priv, &pm, ACX100_RID_POWER_MGMT);
		acxlog(L_INIT, "Previous PS mode settings: wakeup_cfg 0x%02x, listen interval %d, options 0x%02x, hangover period %d, enhanced_ps_transition_time %d\n", pm.wakeup_cfg, pm.listen_interval, pm.options, pm.hangover_period, pm.enhanced_ps_transition_time);
		pm.wakeup_cfg = priv->ps_wakeup_cfg;
		pm.listen_interval = priv->ps_listen_interval;
		pm.options = priv->ps_options;
		pm.hangover_period = priv->ps_hangover_period;
		pm.enhanced_ps_transition_time = cpu_to_le16(priv->ps_enhanced_transition_time);
		acx100_configure(priv, &pm, ACX100_RID_POWER_MGMT);
		acx100_interrogate(priv, &pm, ACX100_RID_POWER_MGMT);
		acxlog(L_INIT, "wakeup_cfg: 0x%02x\n", pm.wakeup_cfg);
		acx100_schedule(HZ / 25);
		acx100_interrogate(priv, &pm, ACX100_RID_POWER_MGMT);
		acxlog(L_INIT, "power save mode change %s\n", (pm.wakeup_cfg & PS_CFG_PENDING) ? "FAILED" : "was successful");
		/* FIXME: maybe verify via PS_CFG_PENDING bit here
		 * that power save mode change was successful. */
		/* FIXME: we shouldn't trigger a scan immediately after
		 * fiddling with power save mode (since the firmware is sending
		 * a NULL frame then). Does this need locking?? */
		priv->set_mask &= ~GETSET_POWER_80211;
	}
#endif
	
	if (0 != (priv->set_mask & (GETSET_TX|GETSET_ALL))) {
		/* set Tx */
		acxlog(L_INIT, "Updating: %s Tx\n", priv->tx_disabled ? "disable" : "enable");
		if ((UINT8)0 != priv->tx_disabled)
			acx100_issue_cmd(priv, ACX100_CMD_DISABLE_TX, NULL, 0x1, 5000);
		else
			acx100_issue_cmd(priv, ACX100_CMD_ENABLE_TX, &(priv->channel), 0x1, 5000);
		priv->set_mask &= ~GETSET_TX;
	}

	if (0 != (priv->set_mask & (GETSET_RX|GETSET_ALL))) {
		/* Enable Rx */
		acxlog(L_INIT, "Updating: enable Rx\n");
		acx100_issue_cmd(priv, ACX100_CMD_ENABLE_RX, &(priv->channel), 0x1, 5000);
		priv->set_mask &= ~GETSET_RX;
	}

	if (0 != (priv->set_mask & (GETSET_RETRY|GETSET_ALL))) {
		UINT8 short_retry[4 + ACX100_RID_DOT11_SHORT_RETRY_LIMIT_LEN];
		UINT8 long_retry[4 + ACX100_RID_DOT11_LONG_RETRY_LIMIT_LEN];

		acxlog(L_INIT, "Updating short retry limit: %ld, long retry limit: %ld\n",
					priv->short_retry, priv->long_retry);
		short_retry[0x4] = priv->short_retry;
		long_retry[0x4] = priv->long_retry;
		acx100_configure(priv, &short_retry, ACX100_RID_DOT11_SHORT_RETRY_LIMIT);
		acx100_configure(priv, &long_retry, ACX100_RID_DOT11_LONG_RETRY_LIMIT);
		priv->set_mask &= ~GETSET_RETRY;
	}

	if (0 != (priv->set_mask & (SET_MSDU_LIFETIME|GETSET_ALL))) {
		UINT8 xmt_msdu_lifetime[4 + ACX100_RID_DOT11_MAX_XMIT_MSDU_LIFETIME_LEN];

		acxlog(L_INIT, "Updating xmt MSDU lifetime: %d\n",
					priv->msdu_lifetime);
		*(UINT32 *)&xmt_msdu_lifetime[4] = cpu_to_le32((UINT32)priv->msdu_lifetime);
		acx100_configure(priv, &xmt_msdu_lifetime, ACX100_RID_DOT11_MAX_XMIT_MSDU_LIFETIME);
		priv->set_mask &= ~SET_MSDU_LIFETIME;
	}

	if (0 != (priv->set_mask & (GETSET_REG_DOMAIN|GETSET_ALL))) {
		/* reg_domain */
		const UINT8 reg_domain_ids[] =
			{(UINT8)0x10, (UINT8)0x20, (UINT8)0x30, (UINT8)0x31,
			 (UINT8)0x32, (UINT8)0x40, (UINT8)0x41, (UINT8)0x51};
		const UINT16 reg_domain_channel_masks[] = {0x07ff, 0x07ff, 0x1fff, 0x0600, 0x1e00, 0x2000, 0x3fff, 0x00f8};
		memmap_t dom;
		UINT16 i;

		acxlog(L_INIT, "Updating regulatory domain: 0x%02X\n",
					priv->reg_dom_id);
		for (i = 0; i < (UINT16)sizeof(reg_domain_ids); i++)
			if (reg_domain_ids[i] == priv->reg_dom_id)
				break;

		if ((UINT16)sizeof(reg_domain_ids) == i) {
			acxlog(L_STD, "Invalid or unsupported regulatory domain 0x%02X specified, falling back to FCC (USA)! Please report if this sounds fishy!\n", priv->reg_dom_id);
			i = 0;
			priv->reg_dom_id = reg_domain_ids[i];
		}

		priv->reg_dom_chanmask = reg_domain_channel_masks[i];
		dom.m.gp.bytes[0] = priv->reg_dom_id;
		acx100_configure(priv, &dom, ACX100_RID_DOT11_CURRENT_REG_DOMAIN);
		if (0 == (priv->reg_dom_chanmask & (1 << (priv->channel - 1) ) ))
		{ /* hmm, need to adjust our channel setting to reside within our
		domain */
			for (i = 1; i <= 14; i++)
				if (0 != (priv->reg_dom_chanmask & (1 << (i - 1)) ) ) {
					acxlog(L_STD, "adjusting selected channel from %d to %d due to new regulatory domain.\n", priv->channel, i);
					priv->channel = i;
					break;
				}
		}
		priv->set_mask &= ~GETSET_REG_DOMAIN;
	}

	if (0 != (priv->set_mask & (SET_RXCONFIG|GETSET_ALL))) {
		UINT8 rx_config[4 + ACX100_RID_RXCONFIG_LEN];
#if (WLAN_HOSTIF==WLAN_USB)
		priv->rx_config_1=0x2190;
		priv->rx_config_2=0x0E5C;
#else
		switch (priv->monitor) {
		case 0: /* normal mode */
			priv->rx_config_1 = (UINT16)
						(RX_CFG1_PLUS_ADDIT_HDR |
						 RX_CFG1_ONLY_OWN_BEACONS |
						 RX_CFG1_FILTER_BSSID |
						 RX_CFG1_PROMISCUOUS |
						 RX_CFG1_RCV_ALL_FRAMES |
						 RX_CFG1_INCLUDE_ADDIT_HDR);

			priv->rx_config_2 = (UINT16)
						(RX_CFG2_RCV_ASSOC_REQ |
						 RX_CFG2_RCV_AUTH_FRAMES |
						 RX_CFG2_RCV_BEACON_FRAMES |
						 RX_CFG2_FILTER_ON_SOME_BIT |
						 RX_CFG2_RCV_CTRL_FRAMES |
						 RX_CFG2_RCV_DATA_FRAMES |
						 RX_CFG2_RCV_MGMT_FRAMES |
						 RX_CFG2_RCV_PROBE_REQ |
						 RX_CFG2_RCV_PROBE_RESP |
						 RX_CFG2_RCV_OTHER);
			break;
		case 1: /* monitor mode - receive everything that's possible! */
			priv->rx_config_1 = (UINT16)
						(RX_CFG1_PLUS_ADDIT_HDR |
						 RX_CFG1_PROMISCUOUS |
						 RX_CFG1_RCV_ALL_FRAMES |
						 RX_CFG1_INCLUDE_FCS |
						 RX_CFG1_INCLUDE_ADDIT_HDR);
			
			priv->rx_config_2 = (UINT16)
						(RX_CFG2_RCV_ASSOC_REQ |
						 RX_CFG2_RCV_AUTH_FRAMES |
						 RX_CFG2_RCV_BEACON_FRAMES |
						 RX_CFG2_FILTER_ON_SOME_BIT |
						 RX_CFG2_RCV_CTRL_FRAMES |
						 RX_CFG2_RCV_DATA_FRAMES |
						 RX_CFG2_RCV_BROKEN_FRAMES |
						 RX_CFG2_RCV_MGMT_FRAMES |
						 RX_CFG2_RCV_PROBE_REQ |
						 RX_CFG2_RCV_PROBE_RESP |
						 RX_CFG2_RCV_ACK_FRAMES |
						 RX_CFG2_RCV_OTHER);
			break;
		}
#endif		
	//	printk("setting RXconfig to %x:%x\n", priv->rx_config_1, priv->rx_config_2);
		
		*(UINT16 *) &rx_config[0x4] = cpu_to_le16(priv->rx_config_1);
		*(UINT16 *) &rx_config[0x6] = cpu_to_le16(priv->rx_config_2);
		acx100_configure(priv, &rx_config, ACX100_RID_RXCONFIG);
		priv->set_mask &= ~SET_RXCONFIG;
	}

	if (0 != (priv->set_mask & (GETSET_MODE|GETSET_ALL))) {
		if (ACX_MODE_3_MANAGED_AP == priv->macmode_wanted) {
			priv->macmode_joined = priv->macmode_wanted; /* Master (AP) is just sitting there and waiting for others to connect, so the MAC mode we're currently "in" is AP, right? */
			MAC_COPY(priv->bssid, priv->dev_addr);
			acx100_set_status(priv, ISTATUS_4_ASSOCIATED);
		} else {
			if (0 == scanning)
			{
				acx100_scan_chan(priv);
				scanning = 1;
			}
		}
		priv->set_mask &= ~GETSET_MODE;
	}

	if (0 != (priv->set_mask & (GETSET_CHANNEL|GETSET_ALL))) {
		/* channel */
		acxlog(L_INIT, "Updating channel: %d\n",
					priv->channel);
		/* not needed in AP mode */
		if ((ACX_MODE_2_MANAGED_STA == priv->macmode_wanted)
		||  (ACX_MODE_0_IBSS_ADHOC == priv->macmode_wanted)) {
			if (0 == scanning) {
				struct scan s;

				/* stop any previous scan */
				acx100_issue_cmd(priv, ACX100_CMD_STOP_SCAN, NULL, 0, 5000);

				s.count = cpu_to_le16(1);
				s.start_chan = cpu_to_le16(priv->channel);
				s.flags = cpu_to_le16(0x8000);
				s.max_rate = (UINT8)20; /* 2 Mbps */
				s.options = (UINT8)ACX_SCAN_PASSIVE;
				s.chan_duration = cpu_to_le16(50);
				s.max_probe_delay = cpu_to_le16(100);

				acx100_scan_chan_p(priv, &s);

				scanning = 1;
			}
		}
		priv->set_mask &= ~GETSET_CHANNEL;
	}
	
	if (0 != (priv->set_mask & (GETSET_ESSID|GETSET_ALL))) {
		/* not needed in AP mode */
		if ((ACX_MODE_2_MANAGED_STA == priv->macmode_wanted)
		||  (ACX_MODE_0_IBSS_ADHOC == priv->macmode_wanted)) {
			/* if we aren't scanning already, then start scanning now */
			if (0 == scanning)
			{
				acx100_scan_chan(priv);
				scanning = 1;
			}
		}
		priv->set_mask &= ~GETSET_ESSID;
	}

	if (0 != (priv->set_mask & (SET_WEP_OPTIONS|GETSET_ALL))) {
		struct {
			UINT16 type;
			UINT16 length;
			UINT16 valc;
			UINT16 vald;
		} options;

		options.valc = cpu_to_le16(0x0e);
		options.vald = cpu_to_le16(priv->monitor_setting);

		acx100_configure(priv, &options, ACX100_RID_WEP_OPTIONS);
		priv->set_mask &= ~SET_WEP_OPTIONS;
	}

	/* debug, rate, and nick don't need any handling */
	/* what about sniffing mode?? */

	priv->get_mask &= ~GETSET_ALL;
	priv->set_mask &= ~GETSET_ALL;

	acxlog(L_INIT, "get_mask 0x%08lx, set_mask 0x%08lx - after update\n",
			priv->get_mask, priv->set_mask);

#ifdef BROKEN_LOCKING
	acx100_unlock(priv, &flags);
#endif
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_set_defaults
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acxSetDefaults()
 * STATUS: good
 */
int acx100_set_defaults(wlandevice_t *priv)
{
	int result = 0;

	FN_ENTER;

	/* query some settings from the card. */
	priv->get_mask = GETSET_ANTENNA|GETSET_SENSITIVITY|GET_STATION_ID|GETSET_REG_DOMAIN;
	acx100_update_card_settings(priv, 1, 0, 0);

	priv->irq_mask = 0xdbb5;

	priv->led_power = (UINT8)1; /* LED is active on startup */
		
	/* copy the MAC address we just got from the card
	 * into our MAC address used during current 802.11 session */
	MAC_COPY(priv->dev_addr, priv->netdev->dev_addr);
	sprintf(priv->essid, "STA%02X%02X%02X",
		priv->dev_addr[3], priv->dev_addr[4], priv->dev_addr[5]);
	priv->essid_len = (UINT8)9; /* make sure to adapt if changed above! */
	priv->essid_active = (UINT8)1;

	/* we have a nick field to waste, so why not abuse it
	 * to announce the driver version? ;-) */
	strncpy(priv->nick, "acx100 ", IW_ESSID_MAX_SIZE);
	strncat(priv->nick, WLAN_RELEASE_SUB, IW_ESSID_MAX_SIZE);

	if (priv->eeprom_version < (UINT8)5) {
	  acx100_read_eeprom_offset(priv, 0x16F, &priv->reg_dom_id);
	} else {
	  acx100_read_eeprom_offset(priv, 0x171, &priv->reg_dom_id);
	}
	acxlog(L_INIT, "Regulatory domain ID as read from EEPROM: 0x%02X\n", priv->reg_dom_id);
	priv->set_mask |= GETSET_REG_DOMAIN;

	priv->channel = 1;

	priv->scan_count = 1; /* 0xffff would be better, but then we won't get a "scan complete" interrupt, so our current infrastructure will fail */
	priv->scan_mode = ACX_SCAN_PASSIVE;
	priv->scan_duration = 100;
	priv->scan_probe_delay = 200;
	
	priv->auth_alg = WLAN_AUTH_ALG_OPENSYSTEM;
	priv->preamble_mode = (UINT8)2;
	priv->preamble_flag = (UINT8)0;
	priv->listen_interval = 100;
	priv->beacon_interval = 100;
	priv->macmode_wanted = ACX_MODE_FF_AUTO; /* associate to either Ad-Hoc or Managed */
	priv->macmode_joined = 0xaa; /* make sure we know that we didn't join anything */
	priv->unknown0x2350 = 0;
	priv->dtim_interval = (UINT8)2;

	priv->msdu_lifetime = 4096; /* used to be 2048, but FreeBSD driver changed it to 4096 to work properly in noisy wlans */
	priv->set_mask |= SET_MSDU_LIFETIME;

	priv->rts_threshold = 2312; /* max. size: disable RTS mechanism */

	priv->short_retry = 5;
	priv->long_retry = 3;
	priv->set_mask |= GETSET_RETRY;

	priv->txrate_auto = (UINT8)0; /* FIXME: disable it by default, implementation not very good yet. */
	priv->txrate_auto_idx = (UINT8)1; /* 2Mbps */
	priv->txrate_cfg = (UINT8)ACX_TXRATE_11; /* Don't start with max. rate of 22Mbps, since very many APs only support up to 11Mbps */
	if (1 == priv->txrate_auto)
		priv->txrate_curr = (UINT8)ACX_TXRATE_2; /* 2Mbps, play it safe at the beginning */
	else
		priv->txrate_curr = priv->txrate_cfg;

	/* # of retries to use when in auto rate mode.
	 * Setting it higher will cause higher ping times due to retries. */
	priv->txrate_fallback_retries = (UINT8)1;
	priv->set_mask |= SET_RATE_FALLBACK;
	priv->txrate_fallback_threshold = (UINT8)12;
	priv->txrate_stepup_threshold = (UINT8)3;

	/* Supported Rates element - the rates here are given in units of
	 * 500 kbit/s, plus 0x80 added. See 802.11-1999.pdf item 7.3.2.2 */
	priv->rate_spt_len = (UINT8)5;
	priv->rate_support1[0] = (UINT8)0x82;	/* 1Mbps */
	priv->rate_support1[1] = (UINT8)0x84;	/* 2Mbps */
	priv->rate_support1[2] = (UINT8)0x8b;	/* 5.5Mbps */
	priv->rate_support1[3] = (UINT8)0x96;	/* 11Mbps */
	priv->rate_support1[4] = (UINT8)0xac;	/* 22Mbps */

	priv->rate_support2[0] = (UINT8)0x82;	/* 1Mbps */
	priv->rate_support2[1] = (UINT8)0x84;	/* 2Mbps */
	priv->rate_support2[2] = (UINT8)0x8b;	/* 5.5Mbps */
	priv->rate_support2[3] = (UINT8)0x96;	/* 11Mbps */
	priv->rate_support2[4] = (UINT8)0xac;	/* 22Mbps */

	priv->capab_short = (UINT8)0;
	priv->capab_pbcc = (UINT8)1;
	priv->capab_agility = (UINT8)0;

	priv->val0x2324[0x1] = (UINT8)0x1f; /* supported rates: 1, 2, 5.5, 11, 22 */
	priv->val0x2324[0x2] = (UINT8)0x03;
	priv->val0x2324[0x3] = (UINT8)0x0f; /* basic rates: 1, 2, 5.5, 11 */
	priv->val0x2324[0x4] = (UINT8)0x0f;
	priv->val0x2324[0x5] = (UINT8)0x0f;
	priv->val0x2324[0x6] = (UINT8)0x1f;

	/* set some more defaults */
	priv->tx_level_dbm = (UINT8)20;
	priv->tx_level_auto = (UINT8)1;
	priv->set_mask |= GETSET_TXPOWER;

#if BETTER_DO_NOT_DO_IT
	/* should we overwrite the value we gained above with our own
	 * potentially problematic value? I don't think so... */
	priv->antenna = 0x8f;
#endif
	priv->set_mask |= GETSET_ANTENNA; /* better re-init the value from above */

	priv->ed_threshold = (UINT8)0x70;
	priv->set_mask |= GETSET_ED_THRESH;

	priv->cca = (UINT8)0x0d;
	priv->set_mask |= GETSET_CCA;

	priv->set_mask |= SET_RXCONFIG;

	priv->ps_wakeup_cfg = (UINT8)0;
	priv->ps_listen_interval = (UINT8)0;
	priv->ps_options = (UINT8)0;
	priv->ps_hangover_period = (UINT8)0;
	priv->ps_enhanced_transition_time = 0;
#if POWER_SAVE_80211
	priv->set_mask |= GETSET_POWER_80211;
#endif

	MAC_BCAST(priv->ap);

	result = 1;

	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_set_probe_response_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* SetACXProbeResponseTemplate()
 * STATUS: ok.
 */
int acx100_set_probe_response_template(wlandevice_t *priv)
{
	UINT8 *pr2;
	struct acxp80211_beacon_prb_resp_template pr;
	UINT16 len;
	int result;

	FN_ENTER;
	memset(&pr, 0, sizeof(struct acxp80211_beacon_prb_resp_template));
	len = acx100_set_generic_beacon_probe_response_frame(priv, &pr.pkt);
	pr.size = cpu_to_le16(len);
	pr.pkt.hdr.fc = cpu_to_le16(WLAN_SET_FC_FTYPE(WLAN_FTYPE_MGMT) | WLAN_SET_FC_FSTYPE(WLAN_FSTYPE_PROBERESP));
	pr2 = pr.pkt.info;

	acxlog(L_DATA | L_XFER, "SetProberTemp: cb = %d\n", len);
	acxlog(L_DATA, "src=%02X:%02X:%02X:%02X:%02X:%02X\n",
		   pr.pkt.hdr.a2[0], pr.pkt.hdr.a2[1], pr.pkt.hdr.a2[2],
		   pr.pkt.hdr.a2[3], pr.pkt.hdr.a2[4], pr.pkt.hdr.a2[5]);
	acxlog(L_DATA, "BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n",
		   pr.pkt.hdr.a3[0], pr.pkt.hdr.a3[1], pr.pkt.hdr.a3[2],
		   pr.pkt.hdr.a3[3], pr.pkt.hdr.a3[4], pr.pkt.hdr.a3[5]);
	acxlog(L_DATA,
		   "SetProberTemp: Info1=%02X %02X %02X %02X %02X %02X %02X %02X\n",
		   pr2[0], pr2[1], pr2[2], pr2[3], pr2[4], pr2[5], pr2[6],
		   pr2[7]);
	acxlog(L_DATA,
		   "SetProberTemp: Info2=%02X %02X %02X %02X %02X %02X %02X %02X\n",
		   pr2[0x8], pr2[0x9], pr2[0xa], pr2[0xb], pr2[0xc], pr2[0xd],
		   pr2[0xe], pr2[0xf]);
	acxlog(L_DATA,
		   "SetProberTemp: Info3=%02X %02X %02X %02X %02X %02X %02X %02X\n",
		   pr2[0x10], pr2[0x11], pr2[0x12], pr2[0x13], pr2[0x14],
		   pr2[0x15], pr2[0x16], pr2[0x17]);

	len += 2;		/* add length of "size" field */
	result = acx100_issue_cmd(priv, ACX100_CMD_CONFIG_PROBE_RESPONSE, &pr, len, 5000);
	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_set_probe_request_template
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/*
 * STATUS: ok.
 */
void acx100_set_probe_request_template(wlandevice_t *priv)
{
	struct acxp80211_packet pt;
	struct acxp80211_hdr *txf;
	UINT8 *this;
	int frame_len,i;
	UINT8 bcast_addr[0x6] = {0xff,0xff,0xff,0xff,0xff,0xff};
	
  	txf = &pt.hdr;
	FN_ENTER;
	//pt.hdr.a4.a1[6] = 0xff;
	frame_len = 0x18;
	pt.hdr.a4.fc = cpu_to_le16(0x40);
	pt.hdr.a4.dur = cpu_to_le16(0x0);
	MAC_BCAST(pt.hdr.a4.a1);
	MAC_COPY(pt.hdr.a4.a2, priv->dev_addr);
	MAC_COPY(pt.hdr.a4.a3, bcast_addr);
	pt.hdr.a4.seq = cpu_to_le16(0x0);
//	pt.hdr.b4.a1[0x0] = 0x0;
	//pt.hdr.a4.a4[0x1] = priv->next;
	memset(txf->val0x18, 0, 8);

	/* set entry 2: Beacon Interval (2 octets) */
	txf->beacon_interval = cpu_to_le16(priv->beacon_interval);

	/* set entry 3: Capability information (2 octets) */
	acx100_update_capabilities(priv);
	txf->caps = cpu_to_le16(priv->capabilities);

	/* set entry 4: SSID (2 + (0 to 32) octets) */
	acxlog(L_ASSOC, "SSID = %s, len = %i\n", priv->essid, priv->essid_len);
	this = &txf->info[0];
	this[0] = 0;		/* "SSID Element ID" */
	this[1] = priv->essid_len;	/* "Length" */
	memcpy(&this[2], priv->essid, priv->essid_len);
	frame_len += 2 + priv->essid_len;

	/* set entry 5: Supported Rates (2 + (1 to 8) octets) */
	this = &txf->info[2 + priv->essid_len];

	this[0] = 1;		/* "Element ID" */
	this[1] = priv->rate_spt_len;
	if (priv->rate_spt_len < 2) {
		for (i = 0; i < (int)priv->rate_spt_len; i++) {
			priv->rate_support1[i] &= ~0x80;
		}
	}
	memcpy(&this[2], priv->rate_support1, priv->rate_spt_len);
	frame_len += 2 + this[1];	/* length calculation is not split up like that, but it's much cleaner that way. */

	/* set entry 6: DS Parameter Set () */
	this = &this[2 + this[1]];
	this[0] = 3;		/* "Element ID": "DS Parameter Set element" */
	this[1] = 1;		/* "Length" */
	this[2] = priv->channel;	/* "Current Channel" */
	frame_len += 3;		/* ok, now add the remaining 3 bytes */

	acx100_issue_cmd(priv, ACX100_CMD_CONFIG_PROBE_REQUEST, &pt, frame_len, 5000);
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_join_bssid
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* AcxJoin()
 * STATUS: FINISHED, UNVERIFIED.
 */
void acx100_join_bssid(wlandevice_t *priv)
{
	int i;
	joinbss_t tmp;
	
	acxlog(L_STATE, "%s: UNVERIFIED.\n", __func__);
	FN_ENTER;
	memset(&tmp, 0, sizeof(tmp));

	for (i = 0; i < ETH_ALEN; i++) {
		tmp.bssid[i] = priv->address[5 - i];
	}

	tmp.beacon_interval = cpu_to_le16(priv->beacon_interval);
	tmp.dtim_interval = priv->dtim_interval;
	tmp.rates_basic = priv->val0x2324[3];

	tmp.rates_supported = priv->val0x2324[1];
	tmp.txrate_val = (UINT8)ACX_TXRATE_2;	/* bitrate: 2Mbps */
	tmp.preamble_type = priv->capab_short;
	tmp.macmode = priv->macmode_chosen;	/* should be called BSS_Type? */
	tmp.channel = priv->channel;
	tmp.essid_len = priv->essid_len;
	/* The firmware hopefully doesn't stupidly rely
	 * on having a trailing \0 copied, right?
	 * (the code memcpy'd essid_len + 1 before, which is WRONG!) */
	memcpy(tmp.essid, priv->essid, tmp.essid_len);

	acx100_issue_cmd(priv, ACX100_CMD_JOIN, &tmp, tmp.essid_len + 0x11, 5000);
	acxlog(L_ASSOC | L_BINDEBUG, "<%s> BSS_Type = %d\n", __func__, tmp.macmode);
	acxlog(L_ASSOC | L_BINDEBUG,
		   "<%s> JoinBSSID MAC:%02X %02X %02X %02X %02X %02X\n", __func__,
		   tmp.bssid[5], tmp.bssid[4], tmp.bssid[3],
		   tmp.bssid[2], tmp.bssid[1], tmp.bssid[0]);

	for (i = 0; i < ETH_ALEN; i++) {
		priv->bssid[5 - i] = tmp.bssid[i];
	}
	priv->macmode_joined = tmp.macmode;
	acx100_update_capabilities(priv);
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_init_mac
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_initmac()
 * STATUS: FINISHED.
 */
int acx100_init_mac(netdevice_t *dev)
{
	int result = -1;
	acx100_memmap_t pkt;
	wlandevice_t *priv = (wlandevice_t *) dev->priv;

	acxlog(L_DEBUG,"sizeof(memmap)=%d bytes\n",sizeof(pkt));

	FN_ENTER;

	acxlog(L_BINDEBUG,          "******************************************\n");
	acxlog(L_BINDEBUG | L_INIT, "************ acx100_initmac_1 ************\n");
	acxlog(L_BINDEBUG,          "******************************************\n");
#if (WLAN_HOSTIF!=WLAN_USB)
	priv->memblocksize = 0x100;
#else
	priv->memblocksize = 0x80;
#endif

	acx100_init_mboxes(priv);
#if (WLAN_HOSTIF!=WLAN_USB)	
	if (priv->chip_type == CHIPTYPE_ACX100) /* ACX111: combined firmware */
		acx100_load_radio(priv);
#endif

	if(priv->chip_type == CHIPTYPE_ACX100) {
		if (0 == acx100_init_wep(priv, &pkt)) goto done;
		acxlog(L_DEBUG,"between init_wep and init_packet_templates\n");
		if (!acx100_init_packet_templates(priv,&pkt)) goto done;

		if (acx100_create_dma_regions(priv)) {
			acxlog(L_STD, "acx100_create_dma_regions failed.\n");
			goto done;
		}

	} else if(priv->chip_type == CHIPTYPE_ACX111) {
		/* here the order is different
		   1. init packet templates
		   2. create station context
		   3. init wep default keys
		*/
		if (0 == acx100_init_packet_templates(priv,&pkt)) goto done;


		if (0 != acx111_create_dma_regions(priv)) {
			acxlog(L_STD, "acx111_create_dma_regions failed.\n");
			goto done;
		}


		/* if (0 == acx111_init_station_context(priv, &pkt)) goto done; */


		if (0 == acx100_init_wep(priv, &pkt)) goto done;
	} else {
		acxlog(L_DEBUG,"unknown chip type\n");
		goto done;
	}

	if (0 == acx100_set_defaults(priv)) {
		acxlog(L_STD, "acx100_set_defaults failed.\n");
		goto done;
	}

#if 0
	/* FIXME: most likely that's not needed here,
	 * since it's done in acx100_start() */
	priv->set_mask |= SET_STA_LIST;
	priv->set_mask |= SET_TEMPLATES;
	acx100_update_card_settings(priv, 1, 0, 0);
#endif

	result = 0;

done:
//	  acx100_enable_irq(priv);
//	  acx100_start(priv);
	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_start
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* AcxStart()
 * STATUS: should be ok.
 */
void acx100_start(wlandevice_t *priv)
{
	unsigned long flags;
	int dont_lock_up = 0;

	FN_ENTER;

	if (0 != spin_is_locked(&priv->lock)) {
		printk(KERN_EMERG "Preventing lock-up!");
		dont_lock_up = 1;
	}

	if (0 == dont_lock_up)
		if (acx100_lock(priv, &flags))
		{
			acxlog(L_STD, "ERROR: lock failed!\n");
			return;
		}

	/* 
	 * Ok, now we do everything that can possibly be done with ioctl 
	 * calls to make sure that when it was called before the card 
	 * was up we get the changes asked for
	 */

	priv->set_mask |= SET_TEMPLATES|SET_STA_LIST|GETSET_WEP|GETSET_TXPOWER|GETSET_ANTENNA|GETSET_ED_THRESH|GETSET_CCA|GETSET_REG_DOMAIN|GETSET_MODE|GETSET_CHANNEL|GETSET_TX|GETSET_RX;
	acxlog(L_INIT, "initial settings update on iface activation.\n");
	acx100_update_card_settings(priv, 1, 0, 0);

	if (0 == dont_lock_up)
		acx100_unlock(priv, &flags);
	FN_EXIT(0, 0);
}

/*------------------------------------------------------------------------------
 * acx100_set_timer
 *
 * Sets the 802.11 state management timer's timeout.
 *
 * Arguments:
 *	@priv: per-device struct containing the management timer
 *	@timeout: timeout in us
 *
 * Returns: -
 *
 * Side effects:
 *
 * Call context:
 *
 * STATUS: FINISHED, but struct undefined.
 *
 * Comment:
 *
 *----------------------------------------------------------------------------*/
void acx100_set_timer(wlandevice_t *priv, UINT32 timeout)
{
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT32 tmp[5];
#endif

	FN_ENTER;

	acxlog(L_BINDEBUG | L_IRQ, "<acx100_set_timer> Elapse = %ld\n", timeout);
	/* newer firmware versions abandoned timer configuration
	 * FIXME: any other versions between 1.8.3 (working) and
	 * 1.9.3.e (removed)? */
#if (WLAN_HOSTIF!=WLAN_USB)
	if (priv->firmware_numver < 0x0109030e)
	{
		/* first two 16-bit words reserved for type and length */
		tmp[1] = cpu_to_le32(timeout);
		tmp[4] = 0;
		acx100_configure(priv, &tmp, ACX100_RID_ACX_TIMER);
	} else
#endif
	{
		/* first check if the timer was already initialized, THEN modify it */
		if (priv->mgmt_timer.function)
	       	{
			mod_timer(&(priv->mgmt_timer), jiffies + (timeout * HZ / 1000000));
		}

	}
	FN_EXIT(0, 0);
}

/* AcxUpdateCapabilities()
 * STATUS: FINISHED. Warning: spelling error, original name was
 * AcxUpdateCapabilies.
 */
void acx100_update_capabilities(wlandevice_t *priv)
{

	priv->capabilities = 0;
	if (ACX_MODE_3_MANAGED_AP == priv->macmode_wanted) {
		priv->capabilities = WLAN_SET_MGMT_CAP_INFO_ESS(1);	/* 1 */
	} else {
		priv->capabilities |= WLAN_SET_MGMT_CAP_INFO_IBSS(1);	/* 2 */
	}
	if (priv->wep_restricted != 0) {
		priv->capabilities |= WLAN_SET_MGMT_CAP_INFO_PRIVACY(1);	/* 0x10 */
	}
	if (priv->capab_short != 0) {
		priv->capabilities |= WLAN_SET_MGMT_CAP_INFO_SHORT(1);	/* 0x20 */
	}
	if (priv->capab_pbcc != 0) {
		priv->capabilities |= WLAN_SET_MGMT_CAP_INFO_PBCC(1);	/* 0x40 */
	}
	if (priv->capab_agility != 0) {
		priv->capabilities |= WLAN_SET_MGMT_CAP_INFO_AGILITY(1);	/* 0x80 */
	}
}

/*----------------------------------------------------------------
* acx100_read_eeprom_offset
*
* Function called to read an octet in the EEPROM.
*
* This function is used by acx100_probe_pci to check if the
* connected card is a legal one or not.
*
* Arguments:
*	priv		ptr to wlandevice structure
*	addr		address to read in the EEPROM
*	charbuf		ptr to a char. This is where the read octet
*			will be stored
*
* Returns:
*	zero (0)	- failed
*	one (1)		- success
*
* Side effects:
*
*
* Call context:
*
* STATUS: FINISHED.
* 	  NOT ADAPTED FOR ACX111 !!
*
* Comment: This function was in V3 driver only.
*	It should be found what mean the different values written
*	in the registers.
*	It should be checked if it would be possible to use a
*	acx100_read_reg8() instead of a acx100_read_reg16() as the
*	read value should be an octet. (ygauteron, 29.05.2003)
----------------------------------------------------------------*/
UINT16 acx100_read_eeprom_offset(wlandevice_t *priv,
					UINT16 addr, UINT8 *charbuf)
{
	UINT16 result = 1;
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT32 count = 0;

	FN_ENTER;

	acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CFG], 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CFG] + 0x2, 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_ADDR], addr);
	acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_ADDR] + 0x2, 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CTL], 2);
	acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CTL] + 0x2, 0);

	while (0 != acx100_read_reg16(priv, priv->io[IO_ACX_EEPROM_CTL]))
	{
		/* scheduling away instead of CPU burning loop
		 * doesn't seem to work here at all:
		 * awful delay, sometimes also failure.
		 * Doesn't matter anyway (only small delay). */
		if (++count > 0xffff) {
			result = 0;
			acxlog(L_BINSTD, "%s: timeout waiting for read eeprom cmd\n", __func__);
			goto done;
		}
	}

	/* yg: Why reading a 16-bits register for a 8-bits value ? */
	*charbuf = (unsigned char) acx100_read_reg16(priv, priv->io[IO_ACX_EEPROM_DATA]);
	acxlog(L_DEBUG, "EEPROM read 0x%04x --> 0x%02x\n", addr, *charbuf); 
	result = 1;

done:
	FN_EXIT(1, result);
#endif
	return result;
}

/* acx100_read_eeprom_area
 * STATUS: OK.
 */
UINT16 acx100_read_eeprom_area(wlandevice_t *priv)
{
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT16 offs = 0x8c;
	UINT8 tmp[0x3b];

	for (offs = 0x8c; offs < 0xb9; offs++) {
		acx100_read_eeprom_offset(priv, offs, &tmp[offs - 0x8c]);
	}
#endif
	return 1;
}

UINT16 acx100_write_eeprom_offset(wlandevice_t *priv, UINT16 addr, UINT16 len, UINT8 *charbuf)
{
	UINT16 result = 1;
#if (WLAN_HOSTIF!=WLAN_USB)

	UINT16 gpio_orig;
	UINT16 i;
	UINT8 *data_verify = NULL;
	UINT16 count;
	
	FN_ENTER;

	acxlog(L_STD, "WARNING: I would write to EEPROM now. Since I really DON'T want to do it unless you know what you're doing, I will abort that now.\n");
	return 0;
	
	/* first we need to enable the OE (EEPROM Output Enable) GPIO line
	 * to be able to write to the EEPROM */
	gpio_orig = acx100_read_reg16(priv, priv->io[IO_ACX_GPIO_OE]);
	acx100_write_reg16(priv, priv->io[IO_ACX_GPIO_OE], gpio_orig & ~1);
	
	/* ok, now start writing the data out */
	for (i = 0; i < len; i++) {

		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CFG], 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CFG] + 0x2, 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_ADDR], addr + i);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_ADDR] + 0x2, 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_DATA], *(charbuf + i));
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_DATA] + 0x2, 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CTL], 1);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CTL] + 0x2, 0);

		while (0 != acx100_read_reg16(priv, priv->io[IO_ACX_EEPROM_CTL]))
		{
			/* scheduling away instead of CPU burning loop
			 * doesn't seem to work here at all:
			 * awful delay, sometimes also failure.
			 * Doesn't matter anyway (only small delay). */
			if (++count > 0xffff) {
				acxlog(L_BINSTD, "%s: WARNING, DANGER!!!! Timeout waiting for write eeprom cmd\n", __func__);
				result = 0;
				goto end;
			}
		}
	}

	/* disable EEPROM writing */
	acx100_write_reg16(priv, priv->io[IO_ACX_GPIO_OE], gpio_orig);

	/* now start a verification run */
	if ((NULL == (data_verify = kmalloc(len, GFP_KERNEL)))) {
		result = 0;
		goto end;
	}

	for (i = 0; i < len; i++) {

		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CFG], 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CFG] + 0x2, 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_ADDR], addr + i);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_ADDR] + 0x2, 0);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CTL], 2);
		acx100_write_reg16(priv, priv->io[IO_ACX_EEPROM_CTL] + 0x2, 0);

		while (0 != acx100_read_reg16(priv, priv->io[IO_ACX_EEPROM_CTL]))
		{
			/* scheduling away instead of CPU burning loop
			 * doesn't seem to work here at all:
			 * awful delay, sometimes also failure.
			 * Doesn't matter anyway (only small delay). */
			if (++count > 0xffff) {
				acxlog(L_BINSTD, "%s: timeout waiting for read eeprom cmd\n", __func__);
				result = 0;
				goto end;
			}
		}

		*(data_verify + i) = (UINT8)acx100_read_reg16(priv, priv->io[IO_ACX_EEPROM_DATA]);
	}

	if (0 == memcmp(charbuf, data_verify, len))
		result = 1; /* read data matches, success */
	
end:
	if (NULL != data_verify)
        	kfree(data_verify);
	
	FN_EXIT(1, result);
#endif
	return result;
}

UINT16 acx100_read_phy_reg(wlandevice_t *priv, UINT16 reg, UINT8 *charbuf)
{
	UINT16 result = 1;
#if (WLAN_HOSTIF!=WLAN_USB)
	UINT32 count = 0;

	FN_ENTER;

	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_ADDR], reg);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_ADDR] + 0x2, 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_CTL], 2);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_CTL] + 0x2, 0);

	while (0 != acx100_read_reg16(priv, priv->io[IO_ACX_PHY_CTL]))
	{
		/* scheduling away instead of CPU burning loop
		 * doesn't seem to work here at all:
		 * awful delay, sometimes also failure.
		 * Doesn't matter anyway (only small delay). */
		if (++count > 0xffff) {
			result = 0;
			acxlog(L_BINSTD, "%s: timeout waiting for read eeprom cmd\n", __func__);
			goto done;
		}
	}

	/* yg: Why reading a 16-bits register for a 8-bits value ? */
	*charbuf = (UINT8)acx100_read_reg16(priv, priv->io[IO_ACX_PHY_DATA]);
	acxlog(L_DEBUG, "radio PHY read 0x%04x --> 0x%02x\n", reg, *charbuf); 
	result = 1;

done:
	FN_EXIT(1, result);
#endif
	return result;
}

UINT16 acx100_write_phy_reg(wlandevice_t *priv, UINT16 reg, UINT8 value)
{
#if (WLAN_HOSTIF!=WLAN_USB)

	FN_ENTER;

	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_ADDR], reg);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_ADDR] + 0x2, 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_DATA], value);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_DATA] + 0x2, 0);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_CTL], 1);
	acx100_write_reg16(priv, priv->io[IO_ACX_PHY_CTL] + 0x2, 0);

	acxlog(L_DEBUG, "radio PHY write 0x%02x -->  0x%04x\n", value, reg); 

	FN_EXIT(1, 1);
#endif
	return 1;
}
