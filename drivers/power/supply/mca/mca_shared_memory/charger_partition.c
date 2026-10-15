// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi charger shared partition support.
 *
 * Dada stock exposes this as charger_partition.ko.  The on-disk ABI is a
 * sequence of 4 KiB blocks beginning with charger_partition_header.  Keep the
 * stock exported ABI, UFS/SCSI access pattern and xm_power sysfs interface,
 * while avoiding the vendor driver's bring-up/debug writes during probe.
 */
#include <mca/common/mca_workqueue.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/shared_memory/charger_partition_class.h>
#include <mca/strategy/strategy_class.h>

#include "inc/charger_partition.h"
#include "sd.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "charger_partition"
#endif

#define CHARGER_PART_PLATFORM_V9 9
#define CHARGER_PART_NUMBER_V9 25
#define CHARGER_PART_NUMBER_DEFAULT 22

struct charger_partition_dev {
	struct device *dev;
	struct scsi_device *sdev;
	struct delayed_work discover_work;
	struct mutex rw_lock;
	int part_number;
	sector_t part_start;
	sector_t part_size;
	bool ready;
	bool eu_model;
};

static struct charger_partition_dev *g_part;
static void *g_rw_buf;

static int charger_scsi_xfer(struct scsi_device *sdev, bool write,
			     void *buf, u32 lba, u32 blocks)
{
	u8 cdb[10] = { 0 };
	struct scsi_sense_hdr sshdr = { 0 };
	const struct scsi_exec_args exec_args = { .sshdr = &sshdr };
	unsigned long flags;
	int ret;

	if (!sdev || !buf || !blocks)
		return -EINVAL;

	spin_lock_irqsave(sdev->host->host_lock, flags);
	ret = scsi_device_get(sdev);
	if (!ret && !scsi_device_online(sdev)) {
		ret = -ENODEV;
		scsi_device_put(sdev);
	}
	spin_unlock_irqrestore(sdev->host->host_lock, flags);
	if (ret)
		return ret;

	sdev->host->eh_noresume = 1;
	cdb[0] = write ? WRITE_10 : READ_10;
	cdb[2] = (lba >> 24) & 0xff;
	cdb[3] = (lba >> 16) & 0xff;
	cdb[4] = (lba >> 8) & 0xff;
	cdb[5] = lba & 0xff;
	cdb[7] = (blocks >> 8) & 0xff;
	cdb[8] = blocks & 0xff;
	ret = scsi_execute_cmd(sdev, cdb,
			       write ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN,
			       buf, blocks * PART_BLOCK_SIZE,
			       msecs_to_jiffies(15000), 3, &exec_args);
	sdev->host->eh_noresume = 0;
	scsi_device_put(sdev);
	return ret;
}

static int charger_read_block(u32 info_type, void *buf)
{
	if (!g_part || !g_part->ready)
		return -ENODEV;
	return charger_scsi_xfer(g_part->sdev, false, buf,
				 g_part->part_start + info_type,
				 CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
}

static int charger_write_block(u32 info_type, void *buf)
{
	if (!g_part || !g_part->ready)
		return -ENODEV;
	return charger_scsi_xfer(g_part->sdev, true, buf,
				 g_part->part_start + info_type,
				 CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
}

static int charger_validate_args(u8 host, u8 info, u32 size)
{
	if (!g_part || !g_part->ready)
		return -ENODEV;
	if (host >= CHARGER_PARTITION_HOST_LAST ||
	    info >= CHARGER_PARTITION_INFO_LAST)
		return -EINVAL;
	if (!size || size >= CHARGER_PARTITION_RWSIZE)
		return -EINVAL;
	return 0;
}

int charger_partition_alloc(u8 host, u8 info, uint32_t size)
{
	charger_partition_header *header;
	int ret;

	ret = charger_validate_args(host, info, size);
	if (ret)
		return ret;
	mutex_lock(&g_part->rw_lock);
	if (g_rw_buf) {
		mutex_unlock(&g_part->rw_lock);
		return -EBUSY;
	}
	g_rw_buf = kzalloc(CHARGER_PARTITION_RWSIZE, GFP_KERNEL);
	if (!g_rw_buf) {
		mutex_unlock(&g_part->rw_lock);
		return -ENOMEM;
	}
	ret = charger_read_block(CHARGER_PARTITION_HEADER, g_rw_buf);
	if (ret)
		goto err;
	header = g_rw_buf;
	if (!header->avaliable) {
		ret = -EBUSY;
		goto err;
	}
	header->avaliable = 0;
	ret = charger_write_block(CHARGER_PARTITION_HEADER, g_rw_buf);
	if (ret)
		goto err;
	mutex_unlock(&g_part->rw_lock);
	return 0;
err:
	kfree(g_rw_buf);
	g_rw_buf = NULL;
	mutex_unlock(&g_part->rw_lock);
	return ret;
}
EXPORT_SYMBOL(charger_partition_alloc);

int charger_partition_dealloc(u8 host, u8 info, uint32_t size)
{
	charger_partition_header *header;
	int ret;

	ret = charger_validate_args(host, info, size);
	if (ret)
		return ret;
	mutex_lock(&g_part->rw_lock);
	if (!g_rw_buf) {
		mutex_unlock(&g_part->rw_lock);
		return -EINVAL;
	}
	memset(g_rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_read_block(CHARGER_PARTITION_HEADER, g_rw_buf);
	if (!ret) {
		header = g_rw_buf;
		header->avaliable = 1;
		ret = charger_write_block(CHARGER_PARTITION_HEADER, g_rw_buf);
	}
	kfree(g_rw_buf);
	g_rw_buf = NULL;
	mutex_unlock(&g_part->rw_lock);
	return ret;
}
EXPORT_SYMBOL(charger_partition_dealloc);

void *charger_partition_read(u8 host, u8 info, uint32_t size)
{
	int ret;

	ret = charger_validate_args(host, info, size);
	if (ret || !g_rw_buf)
		return NULL;
	memset(g_rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	if (charger_read_block(info, g_rw_buf))
		return NULL;
	return g_rw_buf;
}
EXPORT_SYMBOL(charger_partition_read);

int charger_partition_write(u8 host, u8 info, void *buf, uint32_t size)
{
	int ret;

	ret = charger_validate_args(host, info, size);
	if (ret)
		return ret;
	if (!g_rw_buf || !buf)
		return -EINVAL;
	memset(g_rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_read_block(info, g_rw_buf);
	if (ret)
		return ret;
	memcpy(g_rw_buf, buf, size);
	return charger_write_block(info, g_rw_buf);
}
EXPORT_SYMBOL(charger_partition_write);

static int charger_read_info1(charger_partition_info_1 *info)
{
	charger_partition_info_1 *p;
	int ret;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1, sizeof(*info));
	if (ret)
		return ret;
	p = charger_partition_read(CHARGER_PARTITION_HOST_KERNEL,
				   CHARGER_PARTITION_INFO_1, sizeof(*info));
	if (!p)
		ret = -EIO;
	else
		*info = *p;
	charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
				  CHARGER_PARTITION_INFO_1, sizeof(*info));
	return ret;
}

static int charger_write_info1(const charger_partition_info_1 *info)
{
	int ret;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1, sizeof(*info));
	if (ret)
		return ret;
	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_1, (void *)info,
				      sizeof(*info));
	charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
				  CHARGER_PARTITION_INFO_1, sizeof(*info));
	return ret;
}

static int charger_read_info2(charger_partition_info_2 *info)
{
	charger_partition_info_2 *p;
	int ret;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2, sizeof(*info));
	if (ret)
		return ret;
	p = charger_partition_read(CHARGER_PARTITION_HOST_KERNEL,
				   CHARGER_PARTITION_INFO_2, sizeof(*info));
	if (!p)
		ret = -EIO;
	else
		*info = *p;
	charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
				  CHARGER_PARTITION_INFO_2, sizeof(*info));
	return ret;
}

static int charger_write_info2(const charger_partition_info_2 *info)
{
	int ret;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2, sizeof(*info));
	if (ret)
		return ret;
	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2, (void *)info,
				      sizeof(*info));
	charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
				  CHARGER_PARTITION_INFO_2, sizeof(*info));
	return ret;
}

int charger_partition_get_eu_model(bool *is_eu_model)
{
	if (!g_part || !is_eu_model)
		return -EINVAL;
	*is_eu_model = g_part->eu_model;
	return g_part->ready ? 0 : -ENODEV;
}
EXPORT_SYMBOL(charger_partition_get_eu_model);

int charger_partition_read_double85(int *val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (!ret && val)
		*val = info.double85;
	return ret;
}
EXPORT_SYMBOL(charger_partition_read_double85);

int charger_partition_write_double85(int val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (ret)
		return ret;
	info.double85 = val;
	return charger_write_info1(&info);
}
EXPORT_SYMBOL(charger_partition_write_double85);

int charger_partition_write_remove_temp_limit(int val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (ret)
		return ret;
	info.remove_temp_limit = val;
	return charger_write_info1(&info);
}
EXPORT_SYMBOL(charger_partition_write_remove_temp_limit);

int charger_partition_read_memory_test(int *val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (!ret && val)
		*val = info.memory_test;
	return ret;
}
EXPORT_SYMBOL(charger_partition_read_memory_test);

int charger_partition_write_memory_test(int val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (ret)
		return ret;
	info.memory_test = val;
	return charger_write_info1(&info);
}
EXPORT_SYMBOL(charger_partition_write_memory_test);

int charger_partition_read_soc_limit(int *val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (!ret && val)
		*val = info.soc_limit;
	return ret;
}
EXPORT_SYMBOL(charger_partition_read_soc_limit);

int charger_partition_write_soc_limit(int val)
{
	charger_partition_info_1 info = { 0 };
	int ret = charger_read_info1(&info);
	if (ret)
		return ret;
	info.soc_limit = val;
	return charger_write_info1(&info);
}
EXPORT_SYMBOL(charger_partition_write_soc_limit);

int charger_partition_get_mishow(bool *mishow)
{
	charger_partition_info_1 info = { 0 };
	int ret;

	if (!mishow)
		return -EINVAL;
	ret = charger_read_info1(&info);
	if (!ret)
		*mishow = !!info.mishow;
	return ret;
}
EXPORT_SYMBOL(charger_partition_get_mishow);

int charger_partition_read_ocd_count(u32 *a, u32 *b)
{
	charger_partition_info_2 info = { 0 };
	int ret = charger_read_info2(&info);
	if (!ret) {
		if (a) *a = info.ocd_count[0];
		if (b) *b = info.ocd_count[1];
	}
	return ret;
}
EXPORT_SYMBOL(charger_partition_read_ocd_count);

int charger_partition_write_ocd_count(u32 a, u32 b)
{
	charger_partition_info_2 info = { 0 };
	int ret = charger_read_info2(&info);
	if (ret) return ret;
	info.ocd_count[0] = a;
	info.ocd_count[1] = b;
	return charger_write_info2(&info);
}
EXPORT_SYMBOL(charger_partition_write_ocd_count);

int charger_partition_read_cuv_count(u32 *a, u32 *b)
{
	charger_partition_info_2 info = { 0 };
	int ret = charger_read_info2(&info);
	if (!ret) {
		if (a) *a = info.cuv_count[0];
		if (b) *b = info.cuv_count[1];
	}
	return ret;
}
EXPORT_SYMBOL(charger_partition_read_cuv_count);

int charger_partition_write_cuv_count(u32 a, u32 b)
{
	charger_partition_info_2 info = { 0 };
	int ret = charger_read_info2(&info);
	if (ret) return ret;
	info.cuv_count[0] = a;
	info.cuv_count[1] = b;
	return charger_write_info2(&info);
}
EXPORT_SYMBOL(charger_partition_write_cuv_count);

int charger_partition_read_hscd_count(u32 *hscd)
{
	charger_partition_info_2 info = { 0 };
	int ret = charger_read_info2(&info);
	if (!ret && hscd)
		*hscd = info.hscd_count;
	return ret;
}
EXPORT_SYMBOL(charger_partition_read_hscd_count);

int charger_partition_write_hcsd_count(u32 hscd)
{
	charger_partition_info_2 info = { 0 };
	int ret = charger_read_info2(&info);
	if (ret) return ret;
	info.hscd_count = hscd;
	return charger_write_info2(&info);
}
EXPORT_SYMBOL(charger_partition_write_hcsd_count);

static ssize_t charger_partition_show(struct device *dev,
				      struct device_attribute *attr, char *buf);
static ssize_t charger_partition_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count);

static struct mca_sysfs_attr_info charger_partition_attrs[] = {
	mca_sysfs_attr_rw(charger_partition, 0664,
			  MCA_PROP_CHARGER_PARTITION_MISHOW,
			  charger_partition_mishow),
	mca_sysfs_attr_rw(charger_partition, 0664,
			  MCA_PROP_CHARGER_PARTITION_POWEROFFMODE,
			  charger_partition_poweroffmode),
	mca_sysfs_attr_rw(charger_partition, 0664,
			  MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE,
			  charger_partition_prop_eu_mode),
};

static struct attribute *charger_partition_sysfs_attrs[
	ARRAY_SIZE(charger_partition_attrs) + 1];
static const struct attribute_group charger_partition_group = {
	.attrs = charger_partition_sysfs_attrs,
};

static ssize_t charger_partition_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *a;
	charger_partition_info_1 info1 = { 0 };
	charger_partition_info_2 info2 = { 0 };
	int ret, val;

	a = mca_sysfs_lookup_attr(attr->attr.name, charger_partition_attrs,
				 ARRAY_SIZE(charger_partition_attrs));
	if (!a)
		return -EINVAL;
	switch (a->sysfs_attr_name) {
	case MCA_PROP_CHARGER_PARTITION_MISHOW:
		ret = charger_read_info1(&info1);
		val = info1.mishow;
		break;
	case MCA_PROP_CHARGER_PARTITION_POWEROFFMODE:
		ret = charger_read_info1(&info1);
		val = info1.power_off_mode;
		break;
	case MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE:
		ret = charger_read_info2(&info2);
		val = info2.eu_mode;
		break;
	default:
		return -EINVAL;
	}
	return ret ? ret : sysfs_emit(buf, "%d\n", val);
}

static ssize_t charger_partition_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *a;
	charger_partition_info_1 info1 = { 0 };
	charger_partition_info_2 info2 = { 0 };
	int val, ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	a = mca_sysfs_lookup_attr(attr->attr.name, charger_partition_attrs,
				 ARRAY_SIZE(charger_partition_attrs));
	if (!a)
		return -EINVAL;
	switch (a->sysfs_attr_name) {
	case MCA_PROP_CHARGER_PARTITION_MISHOW:
		ret = charger_read_info1(&info1);
		if (!ret) { info1.mishow = val; ret = charger_write_info1(&info1); }
		break;
	case MCA_PROP_CHARGER_PARTITION_POWEROFFMODE:
		ret = charger_read_info1(&info1);
		if (!ret) { info1.power_off_mode = val; ret = charger_write_info1(&info1); }
		break;
	case MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE:
		ret = charger_read_info2(&info2);
		if (!ret) { info2.eu_mode = val; ret = charger_write_info2(&info2); }
		break;
	default:
		return -EINVAL;
	}
	return ret ? ret : count;
}

static bool charger_lookup_lun(int lun)
{
	struct Scsi_Host *host;
	struct scsi_device *sdev;

	host = scsi_host_lookup(0);
	if (!host)
		return false;
	sdev = scsi_device_lookup(host, 0, 0, lun);
	scsi_host_put(host);
	if (!sdev)
		return false;
	if (strncmp(sdev->host->hostt->proc_name, UFSHCD, strlen(UFSHCD))) {
		scsi_device_put(sdev);
		return false;
	}
	if (g_part->sdev)
		scsi_device_put(g_part->sdev);
	g_part->sdev = sdev;
	return true;
}

static bool charger_find_partition(void)
{
	struct scsi_disk *sdkp;
	struct block_device *part;

	if (!g_part->sdev || !g_part->sdev->sdev_gendev.driver_data)
		return false;
	sdkp = g_part->sdev->sdev_gendev.driver_data;
	if (!sdkp->disk)
		return false;
	part = xa_load(&sdkp->disk->part_tbl, g_part->part_number);
	if (!part || !part->bd_meta_info)
		return false;
	if (strncmp(part->bd_meta_info->volname, PARTITION_NAME,
		    sizeof(PARTITION_NAME)))
		return false;
	g_part->part_start = part->bd_start_sect * PART_SECTOR_SIZE /
				 PART_BLOCK_SIZE;
	g_part->part_size = bdev_nr_sectors(part) * PART_SECTOR_SIZE /
				PART_BLOCK_SIZE;
	return true;
}

static int charger_prepare_header(void)
{
	charger_partition_header *header;
	int ret;

	header = kzalloc(CHARGER_PARTITION_RWSIZE, GFP_KERNEL);
	if (!header)
		return -ENOMEM;
	ret = charger_read_block(CHARGER_PARTITION_HEADER, header);
	if (!ret) {
		if (header->magic != CHARGER_PARTITION_MAGIC)
			header->magic = CHARGER_PARTITION_MAGIC;
		header->initialized = 1;
		header->avaliable = 1;
		ret = charger_write_block(CHARGER_PARTITION_HEADER, header);
	}
	kfree(header);
	return ret;
}

static void charger_publish_existing_flags(void)
{
	charger_partition_info_1 info = { 0 };

	if (charger_read_info1(&info))
		return;
	if (info.double85)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO,
				       MCA_EVENT_DEBUG_CTRL_DOUBLE85, &info.double85);
	if (info.remove_temp_limit)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO,
				       MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT,
				       &info.remove_temp_limit);
	if (info.memory_test)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO,
				       MCA_EVENT_DEBUG_CTRL_MEMORY_TEST,
				       &info.memory_test);
	if (info.soc_limit)
		mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO,
				       MCA_EVENT_DEBUG_CTRL_SOC_LIMIT, &info.soc_limit);
}

static void charger_partition_discover(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	const struct mca_hwid *hwid = mca_get_hwid_info();
	charger_partition_info_2 info2 = { 0 };
	static int retry;
	int lun;

	g_part->part_number = hwid && hwid->platform_version == CHARGER_PART_PLATFORM_V9 ?
		CHARGER_PART_NUMBER_V9 : CHARGER_PART_NUMBER_DEFAULT;
	for (lun = 0; lun < 6; lun++) {
		if (!charger_lookup_lun(lun) || !charger_find_partition())
			continue;
		g_part->ready = true;
		if (charger_prepare_header()) {
			g_part->ready = false;
			continue;
		}
		if (!charger_read_info2(&info2)) {
			g_part->eu_model = !!info2.eu_mode;
			if (info2.eu_mode) {
				mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG,
						  MCA_EVENT_IS_EU_MODEL, 1);
				mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
						  MCA_EVENT_IS_EU_MODEL, 1);
				mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
						  MCA_EVENT_IS_EU_MODEL, 1);
			}
		}
		charger_publish_existing_flags();
		mca_log_info("charger partition ready: lun=%d part=%d start=%llu\n",
			     lun, g_part->part_number,
			     (unsigned long long)g_part->part_start);
		return;
	}
	if (retry++ < CHARGER_PARTITION_RETRY_TIMES)
		schedule_delayed_work(&g_part->discover_work,
				      msecs_to_jiffies(CHARGER_WORK_DELAY_MS));
}

static int charger_partition_probe(struct platform_device *pdev)
{
	struct charger_partition_dev *part;
	int ret;

	part = devm_kzalloc(&pdev->dev, sizeof(*part), GFP_KERNEL);
	if (!part)
		return -ENOMEM;
	part->dev = &pdev->dev;
	mutex_init(&part->rw_lock);
	INIT_DELAYED_WORK(&part->discover_work, charger_partition_discover);
	g_part = part;
	platform_set_drvdata(pdev, part);

	mca_sysfs_init_attrs(charger_partition_sysfs_attrs,
			     charger_partition_attrs,
			     ARRAY_SIZE(charger_partition_attrs));
	ret = mca_sysfs_create_link_group("charger", "charger_partition",
					  &pdev->dev, &charger_partition_group);
	if (ret)
		mca_log_info("charger_partition sysfs deferred: %d\n", ret);
	schedule_delayed_work(&part->discover_work, 0);
	return 0;
}

static int charger_partition_remove(struct platform_device *pdev)
{
	struct charger_partition_dev *part = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&part->discover_work);
	mca_sysfs_remove_link_group("charger", "charger_partition", &pdev->dev,
				    &charger_partition_group);
	if (part->sdev)
		scsi_device_put(part->sdev);
	if (g_part == part)
		g_part = NULL;
	return 0;
}

static const struct of_device_id charger_partition_match[] = {
	{ .compatible = "xiaomi,charger_partition" },
	{},
};
MODULE_DEVICE_TABLE(of, charger_partition_match);

static struct platform_driver charger_partition_driver = {
	.driver = {
		.name = "charger_partition",
		.of_match_table = charger_partition_match,
	},
	.probe = charger_partition_probe,
	.remove = charger_partition_remove,
};
module_platform_driver(charger_partition_driver);

MODULE_DESCRIPTION("Xiaomi charger shared partition");
MODULE_LICENSE("GPL v2");
