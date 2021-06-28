/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * MFD core driver for the Maxim MAX77804K
 *
 * Copyright (C) 2011 Samsung Electronics
 * SangYoung Son <hello.son@samsung.com>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77804k-private.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static const struct mfd_cell max77804k_devs[] = {
	// {
	// 	.name = "max77804k-charger",
	// 	.of_compatible = "maxim,max77804k-charger",
	// }, {
	// 	.name = "max77804k-led",
	// 	.of_compatible = "maxim,max77804k-led",
	// },
	{
		.name = "max77804k-muic",
		.of_compatible = "maxim,max77804k-muic"
	},// {
	// 	.name = "max77804k-safeout",
	// 	.of_compatible = "maxim,max77804k-safeout",
	// }, {
	// 	.name = "max77804k-haptic",
	// 	.of_compatible = "maxim,max77804k-haptic",
	// },
};


static const struct regmap_config max77804k_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX77804K_PMIC_REG_END,
};

static const struct regmap_irq max77804k_topsys_irqs[] = {
	/* TOPSYS interrupts */
	{ .reg_offset = 0, .mask = MAX77804K_TOPSYS_IRQ_T120C_MASK, },
	{ .reg_offset = 0, .mask = MAX77804K_TOPSYS_IRQ_T140C_MASK, },
	{ .reg_offset = 0, .mask = MAX77804K_TOPSYS_IRQLOWSYS_MASK, },
};

static const struct regmap_irq_chip max77804k_topsys_irq_chip = {
	.name		= "max77804k-topsys",
	.status_base	= MAX77804K_PMIC_REG_TOPSYS_INT,
	.mask_base	= MAX77804K_PMIC_REG_TOPSYS_INT_MASK,
	.mask_invert	= false,
	.num_regs	= 1,
	.irqs		= max77804k_topsys_irqs,
	.num_irqs	= ARRAY_SIZE(max77804k_topsys_irqs),
};

static int max77804k_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct max77693_dev *max77804k;
	unsigned int reg_data, pmic_ver, pmic_rev;
	// u8 i2c_data;
	int ret;

	max77804k = devm_kzalloc(&i2c->dev, sizeof(*max77804k), GFP_KERNEL);
	if (!max77804k)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77804k);
	max77804k->dev = &i2c->dev;
	max77804k->i2c = i2c;
	max77804k->irq = i2c->irq;
	max77804k->type = id->driver_data;

	/* Init main MFD device (PMIC) regmap */
	max77804k->regmap = devm_regmap_init_i2c(i2c, &max77804k_regmap_config);
	if (IS_ERR(max77804k->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate main register map\n");
		return PTR_ERR(max77804k->regmap);
	}

	ret = regmap_read(max77804k->regmap, MAX77804K_PMIC_REG_PMIC_ID2, &reg_data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read PMIC ID\n");
		return ret;
	}
	pmic_rev = reg_data & 0x7;
	pmic_ver = ((reg_data & 0xF8) >> 0x3);
	dev_info(&i2c->dev, "device ID: rev.0x%x, ver.0x%x\n", pmic_rev, pmic_ver);
	/* ^^ max77804k 5-0066: device ID: rev.0x3, ver.0x0 */

	/* Vendor driver does this */
	/* max77804k_update_reg(i2c, MAX77804K_CHG_REG_SAFEOUT_CTRL, 0x00, 0x30); */
	ret = regmap_update_bits(max77804k->regmap, MAX77804K_CHG_REG_SAFEOUT_CTRL, 0x30, 0x00);
	/* NOTE: mask and val are swapped compared to downstream max77804k_update_reg():
	 *  - max77804k_update_reg(i2c, REG, val, mask)
	 *  - regmap_update_bits(regmap, REG, mask, val)
	 */

	/* Deal with interrupts */
	ret = regmap_add_irq_chip(max77804k->regmap, max77804k->irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_SHARED,
			0, &max77804k_topsys_irq_chip,
			&max77804k->irq_data_topsys);
	if (ret) {
		dev_err(&i2c->dev, "Failed to add max77804k topsys irq chip\n");
		return ret;
	}

	/* Unmask max77804k charger and muic interrupts */
	ret = regmap_update_bits(max77804k->regmap, MAX77804K_PMIC_REG_INTSRC_MASK,
			MAX77804K_IRQSRC_CHG | MAX77804K_IRQSRC_MUIC, 0);
	if (ret) {
		dev_err(&i2c->dev, "Failed to unmask max77804k interrupts\n");
		goto err_pmic_id;
	}

	ret = mfd_add_devices(max77804k->dev, PLATFORM_DEVID_NONE, max77804k_devs,
			      ARRAY_SIZE(max77804k_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to add mfd device\n");
		goto err_pmic_id;
	}

	device_init_wakeup(max77804k->dev, true);

	return 0;

err_pmic_id:
	regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_topsys);

	return ret;
}

static int max77804k_remove(struct i2c_client *i2c)
{
	struct max77693_dev *max77804k = i2c_get_clientdata(i2c);

	dev_info(&i2c->dev, "removing driver\n");

	mfd_remove_devices(max77804k->dev);

	disable_irq(max77804k->irq); // disable_irq_nosync()? no..
	// regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_muic); // we remove it in muic driver itself
	// regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_chg);
	regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_topsys);
	// regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_led);

	// i2c_unregister_device(max77804k->i2c_muic); // we do it in muic driver
	// i2c_unregister_device(max77804k->i2c_haptic);

	return 0;
}

static int __maybe_unused max77804k_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77693_dev *max77804k = i2c_get_clientdata(i2c);

	disable_irq(max77804k->irq);
	if (device_may_wakeup(dev))
		enable_irq_wake(max77804k->irq);

	return 0;
}

static int __maybe_unused max77804k_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77693_dev *max77804k = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(max77804k->irq);
	enable_irq(max77804k->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max77804k_pm, max77804k_suspend, max77804k_resume);

#ifdef CONFIG_OF
static const struct of_device_id max77804k_dt_match[] = {
	{ .compatible = "maxim,max77804k", },
	{ },
};
MODULE_DEVICE_TABLE(of, max77804k_dt_match);
#endif

static const struct i2c_device_id max77804k_id[] = {
	{ "max77804k", TYPE_MAX77804K, },
	{ },
};

static struct i2c_driver max77804k_i2c_driver = {
	.driver	= {
		.name = "max77804k",
		.pm = &max77804k_pm,
		.of_match_table = of_match_ptr(max77804k_dt_match),
	},
	.probe = max77804k_probe,
	.remove = max77804k_remove,
	.id_table = max77804k_id,
};

module_i2c_driver(max77804k_i2c_driver);

MODULE_DESCRIPTION("MAXIM 77804K multi-function core driver");
MODULE_AUTHOR("SangYoung, Son <hello.son@samsung.com>");
MODULE_LICENSE("GPL");
