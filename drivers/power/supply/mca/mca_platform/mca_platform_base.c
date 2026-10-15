// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA banked-I2C platform base for Dada. */
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <mca/common/mca_log.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_platform_base"
#endif

struct mca_platform_base_device {
	struct device *dev;
	struct i2c_client **plat_i2c;
	struct regmap *rmap;
	u8 *slave_addrs;
	int slave_addr_num;
};

static struct i2c_client *mca_platform_bank_to_i2c(
	struct mca_platform_base_device *base, u8 bank)
{
	if (!base || bank >= base->slave_addr_num || !base->plat_i2c)
		return NULL;
	return base->plat_i2c[bank];
}

static int mca_platform_base_regmap_write(void *context, const void *data,
					  size_t count)
{
	struct mca_platform_base_device *base = context;
	const u8 *buf = data;
	struct i2c_client *i2c;

	/* The 16-bit virtual register is <bank, register>. */
	if (!base || !buf || count < 2)
		return -EINVAL;
	i2c = mca_platform_bank_to_i2c(base, buf[0]);
	if (!i2c)
		return -EINVAL;
	if (count == 2)
		return i2c_smbus_write_byte_data(i2c, buf[1], 0);
	return i2c_smbus_write_i2c_block_data(i2c, buf[1], count - 2,
					      buf + 2);
}

static int mca_platform_base_regmap_read(void *context, const void *reg_buf,
					 size_t reg_size, void *val_buf,
					 size_t val_size)
{
	struct mca_platform_base_device *base = context;
	const u8 *reg = reg_buf;
	struct i2c_client *i2c;
	int ret;

	if (!base || !reg || reg_size != 2 || !val_buf || !val_size)
		return -EINVAL;
	i2c = mca_platform_bank_to_i2c(base, reg[0]);
	if (!i2c)
		return -EINVAL;
	ret = i2c_smbus_read_i2c_block_data(i2c, reg[1], val_size, val_buf);
	if (ret < 0)
		return ret;
	return ret == val_size ? 0 : -EIO;
}

static const struct regmap_bus mca_platform_base_regmap_bus = {
	.write = mca_platform_base_regmap_write,
	.read = mca_platform_base_regmap_read,
};

static const struct regmap_config mca_platform_base_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
};

static int mca_platform_base_parse_dt(struct mca_platform_base_device *base)
{
	struct device_node *np = base->dev->of_node;
	int byte_len = 0;
	int ret;

	if (!np)
		return -ENODEV;
	if (!of_find_property(np, "platform_slave_addrs", &byte_len))
		return -ENOENT;
	if (byte_len <= 0)
		return -EINVAL;

	base->slave_addr_num = byte_len / sizeof(u8);
	base->slave_addrs = devm_kcalloc(base->dev, base->slave_addr_num,
					 sizeof(*base->slave_addrs), GFP_KERNEL);
	base->plat_i2c = devm_kcalloc(base->dev, base->slave_addr_num,
				      sizeof(*base->plat_i2c), GFP_KERNEL);
	if (!base->slave_addrs || !base->plat_i2c)
		return -ENOMEM;

	ret = of_property_read_u8_array(np, "platform_slave_addrs",
					base->slave_addrs,
					base->slave_addr_num);
	if (ret)
		return ret;
	return 0;
}

static int mca_platform_base_probe(struct i2c_client *client)
{
	struct mca_platform_base_device *base;
	int i, ret;

	base = devm_kzalloc(&client->dev, sizeof(*base), GFP_KERNEL);
	if (!base)
		return -ENOMEM;
	base->dev = &client->dev;
	i2c_set_clientdata(client, base);

	ret = mca_platform_base_parse_dt(base);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to parse platform_slave_addrs\n");

	for (i = 0; i < base->slave_addr_num; i++) {
		base->plat_i2c[i] = devm_i2c_new_dummy_device(
			&client->dev, client->adapter, base->slave_addrs[i]);
		if (IS_ERR(base->plat_i2c[i]))
			return dev_err_probe(&client->dev,
					     PTR_ERR(base->plat_i2c[i]),
					     "failed to create bank %d at 0x%02x\n",
					     i, base->slave_addrs[i]);
	}

	base->rmap = devm_regmap_init(&client->dev,
				      &mca_platform_base_regmap_bus,
				      base, &mca_platform_base_regmap_config);
	if (IS_ERR(base->rmap))
		return dev_err_probe(&client->dev, PTR_ERR(base->rmap),
				     "failed to create banked regmap\n");

	ret = devm_of_platform_populate(&client->dev);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to populate platform children\n");
	mca_log_info("platform base ready with %d I2C banks\n",
		     base->slave_addr_num);
	return 0;
}

static const struct of_device_id mca_platform_base_of_match[] = {
	{ .compatible = "mca,platform_base" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_platform_base_of_match);

static struct i2c_driver mca_platform_base_driver = {
	.driver = {
		.name = "mca_platform_base",
		.of_match_table = mca_platform_base_of_match,
	},
	.probe = mca_platform_base_probe,
};
module_i2c_driver(mca_platform_base_driver);

MODULE_DESCRIPTION("Xiaomi MCA banked I2C platform base");
MODULE_LICENSE("GPL v2");
