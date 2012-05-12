
#ifndef CONFIG_PROC_FS

/* API: not static inline - must be available for linking */
int __init acx_debugfs_init(void)  { return 0; }
void __exit acx_debugfs_exit(void) {}
int  acx_debugfs_add_adev   (struct acx_device *adev) { return 0; }
void acx_debugfs_remove_adev(struct acx_device *adev) { return 0; }

#else /* CONFIG_PROC_FS */

#define pr_fmt(fmt) "acx.%s: " fmt, __func__

#include <linux/version.h>
#include <linux/wireless.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <net/mac80211.h>
#include "acx.h"

struct acx_device;
typedef int acx_proc_show_t(struct seq_file *file, void *v);
extern acx_proc_show_t *const acx_proc_show_funcs[];

/*
 * debugfs files are created under $DBGMNT/acx_mac80211/phyX by
 * acx_debugfs_add_dev(), where phyX is the vif of each wlanY ifupd on
 * the driver.  The acx_device *ptr is attached to the phyX directory.
 * acx_debugfs_add_dev() is called by acx_op_add_interface(); this
 * may be the wrong lifecyle, but is close, and works for now.
 * Each file gets a file_index, attached by debugfs_create_file() to
 * the inode's private data.
 *
 * A single open() handler uses the attached file_index to select the
 * right read callback, this avoids a linear scan of filenames to
 * match/strcmp against the callback.  The acx_device *ptr is
 * retrieved from the file's parent's private data, and passed to the
 * callback so it knows what vif to print data for.
 */

enum file_index {
	INFO, DIAG, EEPROM, PHY, DEBUG,
	SENSITIVITY, TX_LEVEL, ANTENNA, REG_DOMAIN,
};
static const char *const dbgfs_files[] = {
	[INFO]		= "info",
	[DIAG]		= "diag",
	[EEPROM]	= "eeprom",
	[PHY]		= "phy",
	[DEBUG]		= "debug",
	[SENSITIVITY]	= "sensitivity",
	[TX_LEVEL]	= "tx_level",
	[ANTENNA]	= "antenna", 
	[REG_DOMAIN]	= "reg_domain",
};

static int acx_dbgfs_open(struct inode *inode, struct file *file)
{
	int fidx = (int) inode->i_private; 
	struct acx_device *adev = (struct acx_device *)
		file->f_path.dentry->d_parent->d_inode->i_private;

	switch (fidx) {
	case INFO:
	case DIAG:
	case EEPROM:
	case PHY:
	case DEBUG:
	case SENSITIVITY:
	case TX_LEVEL:
	case ANTENNA:
	case REG_DOMAIN:
		pr_info("opening filename=%s fmode=%o fidx=%d adev=%p\n",
			dbgfs_files[fidx], file->f_mode, fidx, adev);
		break;
	default:
		pr_err("unknown file @ %d: %s\n", fidx,
			file->f_path.dentry->d_name.name);
		return -ENOENT;
	}
	return single_open(file, acx_proc_show_funcs[fidx], adev);
}

const struct file_operations acx_fops = {
	.read		= seq_read,
	//.write	= default_write_file,
	.open		= acx_dbgfs_open,
	.llseek		= noop_llseek,
};

static struct dentry *acx_dbgfs_dir;

int acx_debugfs_add_adev(struct acx_device *adev)
{
	int i;
	fmode_t fmode;
	struct dentry *file;
	const char *devname = wiphy_name(adev->ieee->wiphy);
	struct dentry *acx_dbgfs_devdir
		= debugfs_create_dir(devname, acx_dbgfs_dir);

	if (!acx_dbgfs_devdir) {
		pr_err("debugfs_create_dir() failed\n");
		return -ENOMEM;
	}
	pr_info("adev:%p nm:%s dirp:%p\n", adev, devname, acx_dbgfs_devdir);

	if (acx_dbgfs_devdir->d_inode->i_private) {
		/* this shouldnt happen */
		pr_err("dentry->d_inode->i_private already set: %p\n",
			acx_dbgfs_devdir->d_inode->i_private);
		goto fail;
	}
	acx_dbgfs_devdir->d_inode->i_private = (void*) adev;

	for (i = 0; i < ARRAY_SIZE(dbgfs_files); i++) {

		fmode = 0644;
		file = debugfs_create_file(dbgfs_files[i], fmode, acx_dbgfs_devdir,
					(void*) i, &acx_fops);
		if (!file)
			goto fail;
	}
	adev->debugfs_dir = acx_dbgfs_devdir;
	return 0;
fail:
	debugfs_remove_recursive(acx_dbgfs_devdir);
	return -ENOENT;
}

void acx_debugfs_remove_adev(struct acx_device *adev)
{
	debugfs_remove_recursive(adev->debugfs_dir);
	pr_info("%s %p\n", wiphy_name(adev->ieee->wiphy), adev->debugfs_dir);
	adev->debugfs_dir = NULL;
}

int __init acx_debugfs_init(void)
{
	acx_dbgfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!acx_dbgfs_dir)
		return -ENOMEM;

	return 0;
}

void __exit acx_debugfs_exit(void)
{
	debugfs_remove_recursive(acx_dbgfs_dir);
}

#endif /* CONFIG_PROC_FS */
