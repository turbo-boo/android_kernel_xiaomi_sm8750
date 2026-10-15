// SPDX-License-Identifier: GPL-2.0
#include <mca/common/mca_workqueue.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <drm/drm_panel.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_panel.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_panel"
#endif

struct mca_panel_dev {
	struct device *dev;
#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	struct delayed_work panel_notify_register_work;
	void *notifier_cookie;
	int retry_count;
#endif
	int screen_state;
	int hbm_state;
};

static struct mca_panel_dev *g_panel;

int mca_panel_get_screen_state(void)
{
	return g_panel ? READ_ONCE(g_panel->screen_state) : 0;
}
EXPORT_SYMBOL(mca_panel_get_screen_state);

int mca_panel_get_hbm_state(void)
{
	return g_panel ? READ_ONCE(g_panel->hbm_state) : 0;
}
EXPORT_SYMBOL(mca_panel_get_hbm_state);

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
static void mca_panel_event_notifier_callback(
	enum panel_event_notifier_tag tag,
	struct panel_event_notification *notification, void *data)
{
	struct mca_panel_dev *panel = data;

	if (!panel || !notification)
		return;

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_UNBLANK:
		WRITE_ONCE(panel->screen_state,
			  notification->notif_type == DRM_PANEL_EVENT_UNBLANK);
		mca_event_block_notify(MCA_EVENT_TYPE_PANEL,
				       MCA_EVENT_PANEL_SCREEN_STATE_CHANGE,
				       &panel->screen_state);
		break;
	case DRM_PANEL_EVENT_HBM_ON:
	case DRM_PANEL_EVENT_HBM_OFF:
		WRITE_ONCE(panel->hbm_state,
			  notification->notif_type == DRM_PANEL_EVENT_HBM_ON);
		mca_event_block_notify(MCA_EVENT_TYPE_PANEL,
				       MCA_EVENT_PANEL_HBM_STATE_CHANGE,
				       &panel->hbm_state);
		break;
	default:
		break;
	}
}

static int mca_panel_register_notifier(struct mca_panel_dev *panel_dev)
{
	struct device_node *node, *pnode = NULL;
	struct drm_panel *panel = NULL;
	void *cookie;
	int count, i, ret = -ENODEV;

	node = of_find_node_by_name(NULL, "charge-screen");
	if (!node)
		return -ENODEV;

	count = of_count_phandle_with_args(node, "panel", NULL);
	for (i = 0; i < count; i++) {
		pnode = of_parse_phandle(node, "panel", i);
		if (!pnode)
			continue;
		panel = of_drm_find_panel(pnode);
		if (!IS_ERR(panel))
			break;
		ret = PTR_ERR(panel);
		panel = NULL;
		of_node_put(pnode);
		pnode = NULL;
	}

	if (!panel) {
		of_node_put(node);
		return ret;
	}

	cookie = panel_event_notifier_register(
		PANEL_EVENT_NOTIFICATION_PRIMARY,
		PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER, panel,
		mca_panel_event_notifier_callback, panel_dev);
	if (IS_ERR_OR_NULL(cookie)) {
		ret = IS_ERR(cookie) ? PTR_ERR(cookie) : -EINVAL;
	} else {
		panel_dev->notifier_cookie = cookie;
		ret = 0;
	}

	of_node_put(pnode);
	of_node_put(node);
	return ret;
}

static void mca_panel_register_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct mca_panel_dev *panel = container_of(
		to_delayed_work(work), struct mca_panel_dev,
		panel_notify_register_work);

	if (!mca_panel_register_notifier(panel) || panel->retry_count-- <= 0)
		return;
	schedule_delayed_work(&panel->panel_notify_register_work,
			      msecs_to_jiffies(5000));
}
#endif

static int mca_panel_probe(struct platform_device *pdev)
{
	struct mca_panel_dev *panel;

	panel = devm_kzalloc(&pdev->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;
	panel->dev = &pdev->dev;
	platform_set_drvdata(pdev, panel);
	g_panel = panel;

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	panel->retry_count = 3;
	INIT_DELAYED_WORK(&panel->panel_notify_register_work,
			  mca_panel_register_work);
	schedule_delayed_work(&panel->panel_notify_register_work,
			      msecs_to_jiffies(5000));
#endif
	return 0;
}

static int mca_panel_remove(struct platform_device *pdev)
{
	struct mca_panel_dev *panel = platform_get_drvdata(pdev);

	if (!panel)
		return 0;
#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	cancel_delayed_work_sync(&panel->panel_notify_register_work);
	if (!IS_ERR_OR_NULL(panel->notifier_cookie))
		panel_event_notifier_unregister(panel->notifier_cookie);
#endif
	if (g_panel == panel)
		g_panel = NULL;
	return 0;
}

static const struct of_device_id mca_panel_match[] = {
	{ .compatible = "mca,mca_panel" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_panel_match);

static struct platform_driver mca_panel_driver = {
	.driver = {
		.name = "mca_panel",
		.of_match_table = mca_panel_match,
	},
	.probe = mca_panel_probe,
	.remove = mca_panel_remove,
};
module_platform_driver(mca_panel_driver);

MODULE_DESCRIPTION("Xiaomi MCA Qualcomm panel event bridge");
MODULE_LICENSE("GPL v2");
