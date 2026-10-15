// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_soc_limit.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_soc_limit"
#endif

struct soc_limit_info {
	struct device *dev;
	bool soc_limit_enable;
	int soc;
};

static struct soc_limit_info *global_soc_limit_info;

void soc_limit_process(bool enable, int soc_limit_thre)
{
	struct soc_limit_info *info = global_soc_limit_info;
	int soc;

	if (!info)
		return;

	if (!enable) {
		info->soc_limit_enable = false;
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_CSD_SEND_PULSE,
				       &info->soc_limit_enable);
		mca_log_info("disable soc limit\n");
		return;
	}

	soc = strategy_class_fg_ops_get_soc();
	info->soc = soc;
	if (soc <= soc_limit_thre)
		return;

	info->soc_limit_enable = true;
	mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
			       MCA_EVENT_CSD_SEND_PULSE,
			       &info->soc_limit_enable);
	mca_log_info("soc_limit_thre = %d\n", soc_limit_thre);
}
EXPORT_SYMBOL(soc_limit_process);

static int soc_limit_probe(struct platform_device *pdev)
{
	struct soc_limit_info *info;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	global_soc_limit_info = info;
	return 0;
}

static int soc_limit_remove(struct platform_device *pdev)
{
	struct soc_limit_info *info = platform_get_drvdata(pdev);

	if (global_soc_limit_info == info)
		global_soc_limit_info = NULL;
	return 0;
}

static const struct of_device_id soc_limit_match[] = {
	{ .compatible = "xiaomi,soc_limit" },
	{},
};
MODULE_DEVICE_TABLE(of, soc_limit_match);

static struct platform_driver soc_limit_driver = {
	.driver = {
		.name = "soc_limit",
		.of_match_table = soc_limit_match,
	},
	.probe = soc_limit_probe,
	.remove = soc_limit_remove,
};
module_platform_driver(soc_limit_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA SOC limit strategy");
MODULE_LICENSE("GPL v2");
