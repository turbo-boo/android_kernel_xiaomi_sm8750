// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <hwid.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_hwid"
#endif

static DEFINE_MUTEX(mca_hwid_lock);
static struct mca_hwid *cached_hwid;

const struct mca_hwid *mca_get_hwid_info(void)
{
	struct mca_hwid *hwid;

	if (READ_ONCE(cached_hwid))
		return cached_hwid;

	mutex_lock(&mca_hwid_lock);
	if (cached_hwid)
		goto out;

	hwid = kzalloc(sizeof(*hwid), GFP_KERNEL);
	if (!hwid)
		goto out;

	hwid->platform_version = get_hw_version_platform();
	hwid->country_version = get_hw_country_version();
	hwid->major_version = get_hw_version_major();
	hwid->minor_version = get_hw_version_minor();
	hwid->build_version = get_hw_version_build();
	hwid->product_adc = get_hw_project_adc();
	hwid->build_adc = get_hw_build_adc();
	hwid->hwid_value = get_hw_id_value();
	hwid->product_name = product_name_get();

	/* Preserve Xiaomi's public Onyx compatibility quirk. */
	if (hwid->platform_version == HARDWARE_PROJECT_O10U &&
	    hwid->country_version == CountryIndia)
		hwid->country_version = CountryCN;

	WRITE_ONCE(cached_hwid, hwid);
	mca_log_info("platform=%u country=%u hwid=0x%x product=%s\n",
		     hwid->platform_version, hwid->country_version,
		     hwid->hwid_value,
		     hwid->product_name ? hwid->product_name : "unknown");
out:
	mutex_unlock(&mca_hwid_lock);
	return cached_hwid;
}
EXPORT_SYMBOL(mca_get_hwid_info);

MODULE_DESCRIPTION("Xiaomi MCA hardware identification facade");
MODULE_LICENSE("GPL v2");
