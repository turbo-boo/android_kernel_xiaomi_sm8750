// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA charge logger reconstructed for Dada.
 *
 * The userspace ABI, ring geometry and charge-log callback interface are
 * matched against the stock Dada mca_log.ko.  In particular Dada has the
 * newer eleventh `time_offset` sysfs entry which is not present in older
 * public MCA sources.
 */
#include <linux/mutex.h>
#include <mca/common/mca_callback.h>

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/timekeeping.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>

#define MCA_LOG_LINE_MAX          256
#define MCA_LOG_CACHE_COUNT       20
#define MCA_LOG_NOTIFY_THRESHOLD  4

static uint charge_boot_mode;
module_param(charge_boot_mode, uint, 0444);
MODULE_PARM_DESC(charge_boot_mode, "charge_boot_mode");

enum mca_log_level {
	MCA_LOG_LEVEL_ERROR = 0,
	MCA_LOG_LEVEL_INFO,
	MCA_LOG_LEVEL_DEBUG,
};

enum mca_log_sysfs_type {
	MCA_LOG_ATTR_LOG_LEVEL = 0,
	MCA_LOG_ATTR_LOG_ENABLE,
	MCA_LOG_ATTR_LOG_INDEX,
	MCA_LOG_ATTR_LOG_CUR_BUFF,
	MCA_LOG_ATTR_LOG_MAX_BUFF_NUM,
	MCA_LOG_ATTR_DUMP_BUFFER_LOG,
	MCA_LOG_ATTR_DUMP_CHARGE_LOG_HEAD,
	MCA_LOG_ATTR_DUMP_CHARGE_LOG_INFO,
	MCA_LOG_ATTR_CONSOLE_LOG_LEVEL,
	MCA_LOG_ATTR_WRITE_LOG,
	MCA_LOG_ATTR_TIME_OFFSET,
};

struct mca_log_buf_info {
	struct device *dev;
	bool initialized;
	int log_level;
	int console_level;
	bool enable;
	int index;
	size_t count;
	int latest_cache;
	int cache_num;
	int max_cache_num;
	/* Dada stock stores this as seconds; sysfs writes milliseconds. */
	int time_offset;
	char *cache[MCA_LOG_CACHE_COUNT];
	char *current_buf;
};

struct mca_charge_log_registration {
	struct mca_log_charge_log_ops *ops;
	void *data;
};

static struct mca_log_buf_info g_log;
static struct mca_charge_log_registration
	g_charge_log[MCA_CHARGE_LOG_ID_MAX];
static DEFINE_SPINLOCK(g_log_lock);
static DEFINE_MUTEX(mca_charge_log_lock);
DEFINE_STATIC_SRCU(mca_charge_log_callbacks);

static void mca_log_timestamp(char *buf, size_t size, char level,
			      size_t *written)
{
	struct timespec64 now;
	struct rtc_time tm;
	long ms;
	int len;

	ktime_get_real_ts64(&now);
	now.tv_sec -= sys_tz.tz_minuteswest * 60;
	now.tv_sec += READ_ONCE(g_log.time_offset);
	ms = now.tv_nsec / NSEC_PER_MSEC;
	rtc_time64_to_tm(now.tv_sec, &tm);
	len = scnprintf(buf, size, "[%02d:%02d:%02d:%03ld-%c]",
			tm.tm_hour, tm.tm_min, tm.tm_sec, ms, level);
	*written = len;
}

static void mca_log_push(const char *src, size_t len)
{
	struct mca_event_notify_data event_data = { 0 };
	unsigned long flags;
	bool notify = false;
	static int full_num;

	if (!len)
		return;
	spin_lock_irqsave(&g_log_lock, flags);
	if (!g_log.current_buf) {
		g_log.current_buf = kzalloc(PAGE_SIZE, GFP_ATOMIC);
		if (!g_log.current_buf)
			goto out_unlock;
	}

	if (len + g_log.count >= PAGE_SIZE - 1) {
		kfree(g_log.cache[g_log.latest_cache]);
		g_log.cache[g_log.latest_cache] = g_log.current_buf;
		g_log.current_buf = kzalloc(PAGE_SIZE, GFP_ATOMIC);
		if (!g_log.current_buf) {
			g_log.current_buf = g_log.cache[g_log.latest_cache];
			g_log.cache[g_log.latest_cache] = NULL;
			goto out_unlock;
		}
		g_log.count = 0;
		g_log.latest_cache++;
		if (g_log.latest_cache >= g_log.max_cache_num)
			g_log.latest_cache = 0;
		if (g_log.cache_num < g_log.max_cache_num)
			g_log.cache_num++;
		full_num++;
	}

	if (len > PAGE_SIZE - 1 - g_log.count)
		len = PAGE_SIZE - 1 - g_log.count;
	memcpy(g_log.current_buf + g_log.count, src, len);
	g_log.count += len;
	g_log.current_buf[g_log.count] = '\0';
	if (full_num >= MCA_LOG_NOTIFY_THRESHOLD) {
		full_num = 0;
		notify = g_log.enable;
	}

out_unlock:
	spin_unlock_irqrestore(&g_log_lock, flags);
	if (notify && !in_interrupt()) {
		event_data.event = "MCA_LOG_FULL_EVENT";
		event_data.event_len = strlen(event_data.event);
		mca_event_report_uevent(&event_data);
	}
}

static void mca_log_emit(int level, char tag, const char *format, va_list args)
{
	char line[MCA_LOG_LINE_MAX];
	size_t prefix = 0;
	int body;
	size_t len;

	if (!READ_ONCE(g_log.initialized)) {
		if (level == MCA_LOG_LEVEL_ERROR)
			vprintk(format, args);
		return;
	}
	if (level > READ_ONCE(g_log.log_level))
		return;

	mca_log_timestamp(line, sizeof(line), tag, &prefix);
	body = vscnprintf(line + prefix, sizeof(line) - prefix - 1, format, args);
	len = prefix + body;
	if (!len)
		return;
	if (line[len - 1] != '\n' && len < sizeof(line) - 1)
		line[len++] = '\n';
	line[len] = '\0';

	if (level == MCA_LOG_LEVEL_ERROR)
		pr_err("%s", line);
	else if (READ_ONCE(g_log.console_level) >= level || charge_boot_mode)
		pr_info("%s", line);

	mca_log_push(line, len);
}

void __mca_log_err(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	mca_log_emit(MCA_LOG_LEVEL_ERROR, 'E', format, args);
	va_end(args);
}
EXPORT_SYMBOL(__mca_log_err);

void __mca_log_info(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	mca_log_emit(MCA_LOG_LEVEL_INFO, 'I', format, args);
	va_end(args);
}
EXPORT_SYMBOL(__mca_log_info);

void __mca_log_debug(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	mca_log_emit(MCA_LOG_LEVEL_DEBUG, 'D', format, args);
	va_end(args);
}
EXPORT_SYMBOL(__mca_log_debug);

void mca_log_charge_log_register(enum mca_charge_log_id_ele type,
				 struct mca_log_charge_log_ops *ops, void *data)
{
	if (type < 0 || type >= MCA_CHARGE_LOG_ID_MAX)
		return;
	mutex_lock(&mca_charge_log_lock);
	g_charge_log[type].ops = ops;
	g_charge_log[type].data = data;
	mutex_unlock(&mca_charge_log_lock);
}
EXPORT_SYMBOL(mca_log_charge_log_register);

void mca_log_charge_log_unregister(void *data)
{
	int i;

	mutex_lock(&mca_charge_log_lock);
	for (i = 0; i < MCA_CHARGE_LOG_ID_MAX; i++)
		if (g_charge_log[i].data == data) {
			g_charge_log[i].ops = NULL;
			g_charge_log[i].data = NULL;
		}
	mutex_unlock(&mca_charge_log_lock);
	synchronize_srcu(&mca_charge_log_callbacks);
}
EXPORT_SYMBOL(mca_log_charge_log_unregister);

int mca_log_get_charge_boot_mode(void)
{
	return charge_boot_mode;
}
EXPORT_SYMBOL(mca_log_get_charge_boot_mode);

#ifdef CONFIG_SYSFS
static ssize_t mca_log_sysfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf);
static ssize_t mca_log_sysfs_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count);

static struct mca_sysfs_attr_info mca_log_fields[] = {
	mca_sysfs_attr_rw(mca_log_sysfs, 0660, MCA_LOG_ATTR_LOG_LEVEL, log_level),
	mca_sysfs_attr_rw(mca_log_sysfs, 0660, MCA_LOG_ATTR_LOG_ENABLE, log_enable),
	mca_sysfs_attr_wo(mca_log_sysfs, 0220, MCA_LOG_ATTR_LOG_INDEX, log_index),
	mca_sysfs_attr_ro(mca_log_sysfs, 0440, MCA_LOG_ATTR_LOG_CUR_BUFF, cur_buff),
	mca_sysfs_attr_ro(mca_log_sysfs, 0440, MCA_LOG_ATTR_LOG_MAX_BUFF_NUM, max_buffer_num),
	mca_sysfs_attr_ro(mca_log_sysfs, 0440, MCA_LOG_ATTR_DUMP_BUFFER_LOG, dump_log_buff),
	mca_sysfs_attr_ro(mca_log_sysfs, 0440, MCA_LOG_ATTR_DUMP_CHARGE_LOG_HEAD, charge_log_head),
	mca_sysfs_attr_ro(mca_log_sysfs, 0440, MCA_LOG_ATTR_DUMP_CHARGE_LOG_INFO, charge_log_info),
	mca_sysfs_attr_rw(mca_log_sysfs, 0660, MCA_LOG_ATTR_CONSOLE_LOG_LEVEL, console_log_level),
	mca_sysfs_attr_wo(mca_log_sysfs, 0220, MCA_LOG_ATTR_WRITE_LOG, write_log),
	mca_sysfs_attr_rw(mca_log_sysfs, 0660, MCA_LOG_ATTR_TIME_OFFSET, time_offset),
};

#define MCA_LOG_ATTR_COUNT ARRAY_SIZE(mca_log_fields)
static struct attribute *mca_log_attrs[MCA_LOG_ATTR_COUNT + 1];
static const struct attribute_group mca_log_group = { .attrs = mca_log_attrs };

static int mca_charge_log_dump(char *buf, bool head)
{
	CLASS(mca_callback, callback_scope)(&mca_charge_log_callbacks);

	int i, len = 0;

	for (i = 0; i < MCA_CHARGE_LOG_ID_MAX && len < PAGE_SIZE - 1; i++) {
		struct mca_log_charge_log_ops *ops;
		void *data;

		mutex_lock(&mca_charge_log_lock);
		ops = g_charge_log[i].ops;
		data = g_charge_log[i].data;
		mutex_unlock(&mca_charge_log_lock);
		int ret;

		if (!ops)
			continue;
		if (head) {
			if (!ops->dump_log_head)
				continue;
			ret = ops->dump_log_head(data, buf + len, PAGE_SIZE - len);
		} else {
			if (!ops->dump_log_context)
				continue;
			ret = ops->dump_log_context(data, buf + len, PAGE_SIZE - len);
		}
		if (ret > 0)
			len += min(ret, (int)PAGE_SIZE - len - 1);
	}
	if (len < PAGE_SIZE - 1)
		buf[len++] = '\n';
	return len;
}

static ssize_t mca_log_dump_buffer(char *buf)
{
	unsigned long flags;
	char *page = NULL;
	int index;
	size_t len = 0;

	spin_lock_irqsave(&g_log_lock, flags);
	index = g_log.index;
	if (index == MCA_LOG_CACHE_COUNT) {
		if (g_log.current_buf) {
			len = strnlen(g_log.current_buf, PAGE_SIZE - 1);
			memcpy(buf, g_log.current_buf, len);
			memset(g_log.current_buf, 0, PAGE_SIZE);
			g_log.count = 0;
		}
		spin_unlock_irqrestore(&g_log_lock, flags);
		return len;
	}
	if (index >= 0 && index < MCA_LOG_CACHE_COUNT) {
		page = g_log.cache[index];
		g_log.cache[index] = NULL;
		if (page && g_log.cache_num > 0)
			g_log.cache_num--;
	}
	spin_unlock_irqrestore(&g_log_lock, flags);

	if (page) {
		len = strnlen(page, PAGE_SIZE - 1);
		memcpy(buf, page, len);
		kfree(page);
	}
	return len;
}

static ssize_t mca_log_sysfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *field;

	field = mca_sysfs_lookup_attr(attr->attr.name, mca_log_fields,
				      MCA_LOG_ATTR_COUNT);
	if (!field)
		return -EINVAL;

	switch (field->sysfs_attr_name) {
	case MCA_LOG_ATTR_LOG_LEVEL:
		return sysfs_emit(buf, "%d\n", g_log.log_level);
	case MCA_LOG_ATTR_LOG_ENABLE:
		return sysfs_emit(buf, "%d\n", g_log.enable);
	case MCA_LOG_ATTR_LOG_CUR_BUFF:
		return sysfs_emit(buf, "%d\n", g_log.latest_cache);
	case MCA_LOG_ATTR_LOG_MAX_BUFF_NUM:
		return sysfs_emit(buf, "%d\n", g_log.max_cache_num);
	case MCA_LOG_ATTR_DUMP_BUFFER_LOG:
		return mca_log_dump_buffer(buf);
	case MCA_LOG_ATTR_DUMP_CHARGE_LOG_HEAD:
		return mca_charge_log_dump(buf, true);
	case MCA_LOG_ATTR_DUMP_CHARGE_LOG_INFO:
		return mca_charge_log_dump(buf, false);
	case MCA_LOG_ATTR_CONSOLE_LOG_LEVEL:
		return sysfs_emit(buf, "%d\n", g_log.console_level);
	case MCA_LOG_ATTR_TIME_OFFSET:
		return sysfs_emit(buf, "%d\n", g_log.time_offset);
	default:
		return -EINVAL;
	}
}

static ssize_t mca_log_sysfs_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *field;
	int value = 0;

	field = mca_sysfs_lookup_attr(attr->attr.name, mca_log_fields,
				      MCA_LOG_ATTR_COUNT);
	if (!field)
		return -EINVAL;

	if (field->sysfs_attr_name != MCA_LOG_ATTR_WRITE_LOG &&
	    kstrtoint(buf, 0, &value))
		return -EINVAL;

	switch (field->sysfs_attr_name) {
	case MCA_LOG_ATTR_LOG_LEVEL:
		if (value < MCA_LOG_LEVEL_ERROR || value > MCA_LOG_LEVEL_DEBUG)
			return -ERANGE;
		g_log.log_level = value;
		break;
	case MCA_LOG_ATTR_LOG_ENABLE:
		g_log.enable = !!value;
		g_log.max_cache_num = MCA_LOG_NOTIFY_THRESHOLD;
		break;
	case MCA_LOG_ATTR_LOG_INDEX:
		if (value < 0 || value > MCA_LOG_CACHE_COUNT)
			return -ERANGE;
		g_log.index = value;
		break;
	case MCA_LOG_ATTR_CONSOLE_LOG_LEVEL:
		if (value < MCA_LOG_LEVEL_ERROR || value > MCA_LOG_LEVEL_DEBUG)
			return -ERANGE;
		g_log.console_level = value;
		break;
	case MCA_LOG_ATTR_WRITE_LOG:
		__mca_log_info("[write_log] %.*s", (int)min_t(size_t, count, 220), buf);
		break;
	case MCA_LOG_ATTR_TIME_OFFSET:
		/* Stock Dada divides the userspace millisecond value by 1000. */
		g_log.time_offset = value / 1000;
		break;
	default:
		return -EINVAL;
	}
	return count;
}

static int mca_log_sysfs_create(void)
{
	mca_sysfs_init_attrs(mca_log_attrs, mca_log_fields, MCA_LOG_ATTR_COUNT);
	g_log.dev = mca_sysfs_create_group("xm_power", "charge_log", &mca_log_group);
	return IS_ERR(g_log.dev) ? PTR_ERR(g_log.dev) : 0;
}

static void mca_log_sysfs_remove(void)
{
	if (!IS_ERR_OR_NULL(g_log.dev))
		mca_sysfs_remove_group("xm_power", g_log.dev, &mca_log_group);
	g_log.dev = NULL;
}
#else
static int mca_log_sysfs_create(void) { return 0; }
static void mca_log_sysfs_remove(void) { }
#endif

static int __init mca_log_init(void)
{
	int ret;

	g_log.current_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!g_log.current_buf)
		return -ENOMEM;
	g_log.max_cache_num = MCA_LOG_CACHE_COUNT;
	g_log.enable = true;
	g_log.log_level = MCA_LOG_LEVEL_INFO;
	g_log.console_level = MCA_LOG_LEVEL_ERROR;
	ret = mca_log_sysfs_create();
	if (ret) {
		kfree(g_log.current_buf);
		g_log.current_buf = NULL;
		return ret;
	}
	WRITE_ONCE(g_log.initialized, true);
	pr_info("MCA charge logger ready, charge_boot_mode=%u\n", charge_boot_mode);
	return 0;
}
module_init(mca_log_init);

static void __exit mca_log_exit(void)
{
	unsigned long flags;
	int i;

	WRITE_ONCE(g_log.initialized, false);
	mca_log_sysfs_remove();
	spin_lock_irqsave(&g_log_lock, flags);
	for (i = 0; i < MCA_LOG_CACHE_COUNT; i++) {
		kfree(g_log.cache[i]);
		g_log.cache[i] = NULL;
	}
	kfree(g_log.current_buf);
	g_log.current_buf = NULL;
	g_log.count = 0;
	spin_unlock_irqrestore(&g_log_lock, flags);
}
module_exit(mca_log_exit);

MODULE_DESCRIPTION("Xiaomi Dada MCA charge log backend");
MODULE_LICENSE("GPL v2");
