// SPDX-License-Identifier: GPL-2.0+
//
// extcon-max77804k.c - Maxim MAX77804k extcon driver to support
//                      MUIC(Micro USB Interface Controller)
//
// Copyright (C) 2012 Samsung Electronics.
// Author: <sukdong.kim@samsung.com>
// Copyright (C) 2021 Alexey Minnekhanov <alexeymin@postmarketos.org>

#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77804k-private.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define DELAY_MS_DEFAULT		3000	/* unit: millisecond */

enum max77804k_cable_type_muic {
	CABLE_TYPE_NONE_MUIC = 0,		/* 0 */
	CABLE_TYPE_USB_MUIC,			/* 1 */
	CABLE_TYPE_OTG_MUIC,			/* 2 */
	CABLE_TYPE_TA_MUIC,			/* 3 */
	CABLE_TYPE_DESKDOCK_MUIC,		/* 4 */
	CABLE_TYPE_CARDOCK_MUIC,		/* 5 */
	CABLE_TYPE_JIG_UART_OFF_MUIC,		/* 6 */
	CABLE_TYPE_JIG_UART_OFF_VB_MUIC,	/* 7 VBUS enabled */
	CABLE_TYPE_JIG_UART_ON_MUIC,		/* 8 */
	CABLE_TYPE_JIG_USB_OFF_MUIC,		/* 9 */
	CABLE_TYPE_JIG_USB_ON_MUIC,		/* 10 */
	CABLE_TYPE_MHL_MUIC,			/* 11 */
	CABLE_TYPE_MHL_VB_MUIC,			/* 12 */
	CABLE_TYPE_SMARTDOCK_MUIC,		/* 13 */
	CABLE_TYPE_SMARTDOCK_TA_MUIC,		/* 14 */
	CABLE_TYPE_SMARTDOCK_USB_MUIC,		/* 15 */
	CABLE_TYPE_AUDIODOCK_MUIC,		/* 16 */
	CABLE_TYPE_INCOMPATIBLE_MUIC,		/* 17 */
	CABLE_TYPE_CDP_MUIC,			/* 18 */
	CABLE_TYPE_UNKNOWN_MUIC
};

/* MUIC status registers */
enum max77804k_muic_status {
	MAX77804K_MUIC_STATUS1 = 0,
	MAX77804K_MUIC_STATUS2,
	MAX77804K_MUIC_STATUS3,

	MAX77804K_MUIC_STATUS_NUM,
};

struct max77804k_muic_info {
	struct device *dev;
	struct max77693_dev *max77804k;
	struct extcon_dev *edev;

	struct mutex mutex;
	struct work_struct irq_work;
	struct delayed_work wq_detcable;

	u8 status[MAX77804K_MUIC_STATUS_NUM];

	bool irq_adc;
	bool irq_chg;
	//int		irq_vbvolt;
	//int		irq_adc1k;

	enum max77804k_cable_type_muic cable_type;
	u8		adc;
	u8		chgtyp;
	u8		vbvolt;
};

enum max77804k_muic_cable_group {
	MAX77804K_CABLE_GROUP_ADC = 0,
	MAX77804K_CABLE_GROUP_ADC_GND,
	MAX77804K_CABLE_GROUP_CHG,
};

enum max77804k_muic_adc_debounce_time {
	MAX77804K_DEBOUNCE_TIME_5MS = 0,
	MAX77804K_DEBOUNCE_TIME_10MS,
	MAX77804K_DEBOUNCE_TIME_25MS,
	MAX77804K_DEBOUNCE_TIME_38_62MS,
};

/* MUIC accessory cable type */
enum max77804k_muic_accessory_type {
	MAX77804K_MUIC_ADC_GND			= 0x00,
	MAX77804K_MUIC_ADC_MHL_OR_SENDEND	= 0x01,
	MAX77804K_MUIC_ADC_BUTTON_S1		= 0x02,
	MAX77804K_MUIC_ADC_BUTTON_S2		= 0x03,
	MAX77804K_MUIC_ADC_BUTTON_S3		= 0x04,
	MAX77804K_MUIC_ADC_BUTTON_S4		= 0x05,
	MAX77804K_MUIC_ADC_BUTTON_S5		= 0x06,
	MAX77804K_MUIC_ADC_BUTTON_S6		= 0x07,
	MAX77804K_MUIC_ADC_BUTTON_S7		= 0x08,
	MAX77804K_MUIC_ADC_BUTTON_S8		= 0x09,
	MAX77804K_MUIC_ADC_BUTTON_S9		= 0x0a,
	MAX77804K_MUIC_ADC_BUTTON_S10		= 0x0b,
	MAX77804K_MUIC_ADC_BUTTON_S11		= 0x0c,
	MAX77804K_MUIC_ADC_BUTTON_S12		= 0x0d,
	MAX77804K_MUIC_ADC_VZW_USB_DOCK		= 0x0e, /* 0x01110 28.7K ohm VZW Dock */
	MAX77804K_MUIC_ADC_VZW_INCOMPATIBLE	= 0x0f, /* 0x01111 34K ohm VZW Incompatible */
	MAX77804K_MUIC_ADC_SMARTDOCK		= 0x10, /* 0x10000 40.2K ohm */
	MAX77804K_MUIC_ADC_HMT			= 0x11, /* 0x10001 49.9K ohm */
	MAX77804K_MUIC_ADC_AUDIODOCK		= 0x12, /* 0x10010 64.9K ohm */
	MAX77804K_MUIC_ADC_LANHUB		= 0x13, /* 0x10011 80.07K ohm */
	MAX77804K_MUIC_ADC_CHARGING_CABLE	= 0x14, /* 0x10100 102K ohm */
	MAX77804K_MUIC_ADC_MPOS			= 0x15, /* 0x10100 102K ohm */
	MAX77804K_MUIC_ADC_UART			= 0x16, /* 0x10100 102K ohm */
	MAX77804K_MUIC_ADC_CEA936ATYPE1_CHG	= 0x17,	/* 0x10111 200K ohm */
	MAX77804K_MUIC_ADC_JIG_USB_OFF		= 0x18, /* 0x11000 255K ohm */
	MAX77804K_MUIC_ADC_JIG_USB_ON		= 0x19, /* 0x11001 301K ohm */
	MAX77804K_MUIC_ADC_DESKDOCK		= 0x1a, /* 0x11010 365K ohm */
	MAX77804K_MUIC_ADC_CEA936ATYPE2_CHG	= 0x1b, /* 0x11011 442K ohm */
	MAX77804K_MUIC_ADC_JIG_UART_OFF		= 0x1c, /* 0x11100 523K ohm */
	MAX77804K_MUIC_ADC_JIG_UART_ON		= 0x1d, /* 0x11101 619K ohm */
	MAX77804K_MUIC_ADC_PHONE_POWERED	= 0x1e, /* 0x11110 1000 or 1002 ohm */
	MAX77804K_MUIC_ADC_OPEN			= 0x1f
// 	/*
// 	 * The below accessories should check
// 	 * not only ADC value but also ADC1K and VBVolt value.
// 	 */
// 						/* Offset|ADC1K|VBVolt| */
// 	MAX77843_MUIC_GND_USB_HOST = 0x100,	/*    0x1|    0|     0| */
// 	MAX77843_MUIC_GND_USB_HOST_VB = 0x101,	/*    0x1|    0|     1| */
// 	MAX77843_MUIC_GND_MHL = 0x102,		/*    0x1|    1|     0| */
// 	MAX77843_MUIC_GND_MHL_VB = 0x103,	/*    0x1|    1|     1| */
};

/* MAX77804K MUIC charger cable type */
enum {
	/* No Valid voltage at VB (Vvb < Vvbdet) */
	MAX77804K_MUIC_CHGTYP_NO_VOLTAGE	= 0x00,
	/* Unknown (D+/D- does not present a valid USB charger signature) */
	MAX77804K_MUIC_CHGTYP_USB		= 0x01,
	/* Charging Downstream Port */
	MAX77804K_MUIC_CHGTYP_DOWNSTREAM_PORT	= 0x02,
	/* Dedicated Charger (D+/D- shorted) */
	MAX77804K_MUIC_CHGTYP_DEDICATED_CHGR	= 0x03,
	/* Special 500mA charger, max current 500mA */
	MAX77804K_MUIC_CHGTYP_500MA		= 0x04,
	/* Special 1A charger, max current 1A */
	MAX77804K_MUIC_CHGTYP_1A		= 0x05,
	/* 3.3V Bias on D+/D- */
	MAX77804K_MUIC_CHGTYP_SPECIAL_CHGR	= 0x06,
	/* Dead Battery Charging, max current 100mA */
	MAX77804K_MUIC_CHGTYP_DB_100MA		= 0x07,
	MAX77804K_MUIC_CHGTYP_MAX,

	MAX77804K_MUIC_CHGTYP_INIT,
	MAX77804K_MUIC_CHGTYP_MIN = MAX77804K_MUIC_CHGTYP_NO_VOLTAGE
};

/* Extcon cable types this driver can report */
static const unsigned int max77804k_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_FAST,
	EXTCON_CHG_USB_SLOW,
	EXTCON_DISP_MHL,
	EXTCON_DOCK,
	EXTCON_JIG,
	EXTCON_NONE,
};

struct max77804k_muic_irq {
	unsigned int irq;
	const char *name;
	unsigned int virq;
};

enum max77804k_muic_irq_types {
	/* MUIC INT1 */
	MAX77804K_MUIC_IRQ_INT1_ADC_TYPE,
	MAX77804K_MUIC_IRQ_INT1_ADCLOW_TYPE,
	MAX77804K_MUIC_IRQ_INT1_ADCERR_TYPE,
	MAX77804K_MUIC_IRQ_INT1_ADC1K_TYPE,

	/* MUIC INT2 */
	MAX77804K_MUIC_IRQ_INT2_CHGTYP_TYPE,
	MAX77804K_MUIC_IRQ_INT2_CHGDETREUN_TYPE,
	MAX77804K_MUIC_IRQ_INT2_DCDTMR_TYPE,
	MAX77804K_MUIC_IRQ_INT2_DXOVP_TYPE,
	MAX77804K_MUIC_IRQ_INT2_VBVOLT_TYPE,
	MAX77804K_MUIC_IRQ_INT2_VIDRM_TYPE,

	/* MUIC INT3 */
	MAX77804K_MUIC_IRQ_INT3_EOC_TYPE,
	MAX77804K_MUIC_IRQ_INT3_CGMBC_TYPE,
	MAX77804K_MUIC_IRQ_INT3_OVP_TYPE,
	MAX77804K_MUIC_IRQ_INT3_MBCCHGERR_TYPE,
	MAX77804K_MUIC_IRQ_INT3_CHGENABLED_TYPE,
	MAX77804K_MUIC_IRQ_INT3_BATDET_TYPE,

	MAX77804K_MUIC_IRQ_NUM,
};

static struct max77804k_muic_irq max77804k_muic_irq_types[] = {
	{ MAX77804K_MUIC_IRQ_INT1_ADC_TYPE,		"MUIC-ADC" },
	{ MAX77804K_MUIC_IRQ_INT1_ADCLOW_TYPE,		"MUIC-ADCLOW" },
	{ MAX77804K_MUIC_IRQ_INT1_ADCERR_TYPE,		"MUIC-ADCERR" },
	{ MAX77804K_MUIC_IRQ_INT1_ADC1K_TYPE,		"MUIC-ADC1K" },
	{ MAX77804K_MUIC_IRQ_INT2_CHGTYP_TYPE,		"MUIC-CHGTYP" },
	{ MAX77804K_MUIC_IRQ_INT2_CHGDETREUN_TYPE,	"MUIC-CHGDETREUN" },
	{ MAX77804K_MUIC_IRQ_INT2_DCDTMR_TYPE,		"MUIC-DCDTMR" },
	{ MAX77804K_MUIC_IRQ_INT2_DXOVP_TYPE,		"MUIC-DXOVP" },
	{ MAX77804K_MUIC_IRQ_INT2_VBVOLT_TYPE,		"MUIC-VBVOLT" },
	{ MAX77804K_MUIC_IRQ_INT2_VIDRM_TYPE,		"MUIC-VIDRM" },
	{ MAX77804K_MUIC_IRQ_INT3_EOC_TYPE,		"MUIC-EOC" },
	{ MAX77804K_MUIC_IRQ_INT3_CGMBC_TYPE,		"MUIC-CGMBC"},
	{ MAX77804K_MUIC_IRQ_INT3_OVP_TYPE,		"MUIC-OVP"},
	{ MAX77804K_MUIC_IRQ_INT3_MBCCHGERR_TYPE,	"MUIC-MBCCHGERR"},
	{ MAX77804K_MUIC_IRQ_INT3_CHGENABLED_TYPE,	"MUIC-CHGENABLED"},
	{ MAX77804K_MUIC_IRQ_INT3_BATDET_TYPE,		"MUIC-BATDET"},
};

static const struct regmap_irq max77804k_muic_irqs[] = {
	/* INT1 interrupt */
	{ .reg_offset = 0, .mask = MAX77804K_MUIC_IRQ_INT1_ADC_MASK, },
	{ .reg_offset = 0, .mask = MAX77804K_MUIC_IRQ_INT1_ADCLOW_MASK, },
	{ .reg_offset = 0, .mask = MAX77804K_MUIC_IRQ_INT1_ADCERR_MASK, },
	{ .reg_offset = 0, .mask = MAX77804K_MUIC_IRQ_INT1_ADC1K_MASK, },
	/* INT2 interrupt */
	{ .reg_offset = 1, .mask = MAX77804K_MUIC_IRQ_INT2_CHGTYP_MASK, },
	{ .reg_offset = 1, .mask = MAX77804K_MUIC_IRQ_INT2_CHGDETREUN_MASK, },
	{ .reg_offset = 1, .mask = MAX77804K_MUIC_IRQ_INT2_DCDTMR_MASK, },
	{ .reg_offset = 1, .mask = MAX77804K_MUIC_IRQ_INT2_DXOVP_MASK, },
	{ .reg_offset = 1, .mask = MAX77804K_MUIC_IRQ_INT2_VBVOLT_MASK, },
	{ .reg_offset = 1, .mask = MAX77804K_MUIC_IRQ_INT2_VIDRM_MASK, },
	/* INT3 interrupt */
	{ .reg_offset = 2, .mask = MAX77804K_MUIC_IRQ_INT3_EOC_MASK, },
	{ .reg_offset = 2, .mask = MAX77804K_MUIC_IRQ_INT3_CGMBC_MASK, },
	{ .reg_offset = 2, .mask = MAX77804K_MUIC_IRQ_INT3_OVP_MASK, },
	{ .reg_offset = 2, .mask = MAX77804K_MUIC_IRQ_INT3_MBCCHGERR_MASK, },
	{ .reg_offset = 2, .mask = MAX77804K_MUIC_IRQ_INT3_CHGENABLED_MASK, },
	{ .reg_offset = 2, .mask = MAX77804K_MUIC_IRQ_INT3_BATDET_MASK, },
};

static const struct regmap_irq_chip max77804k_muic_irq_chip = {
	.name           = "max77804k-muic",
	.status_base    = MAX77804K_MUIC_REG_INT1,
	.mask_base      = MAX77804K_MUIC_REG_INTMASK1,
	.mask_invert    = true,
	.num_regs       = 3,
	.irqs           = max77804k_muic_irqs,
	.num_irqs       = ARRAY_SIZE(max77804k_muic_irqs),
};

static const struct regmap_config max77804k_muic_regmap_config = {
	.reg_bits       = 8,
	.val_bits       = 8,
	.max_register   = MAX77804K_MUIC_REG_END,
};

// static int max77843_muic_set_path(struct max77843_muic_info *info,
// 		u8 val, bool attached, bool nobccomp)
// {
// 	struct max77693_dev *max77843 = info->max77843;
// 	int ret = 0;
// 	unsigned int ctrl1, ctrl2;

// 	if (attached)
// 		ctrl1 = val;
// 	else
// 		ctrl1 = MAX77843_MUIC_CONTROL1_SW_OPEN;
// 	if (nobccomp) {
// 		/* Disable BC1.2 protocol and force manual switch control */
// 		ctrl1 |= MAX77843_MUIC_CONTROL1_NOBCCOMP_MASK;
// 	}

// 	ret = regmap_update_bits(max77843->regmap_muic,
// 			MAX77843_MUIC_REG_CONTROL1,
// 			MAX77843_MUIC_CONTROL1_COM_SW |
// 				MAX77843_MUIC_CONTROL1_NOBCCOMP_MASK,
// 			ctrl1);
// 	if (ret < 0) {
// 		dev_err(info->dev, "Cannot switch MUIC port\n");
// 		return ret;
// 	}

// 	if (attached)
// 		ctrl2 = MAX77843_MUIC_CONTROL2_CPEN_MASK;
// 	else
// 		ctrl2 = MAX77843_MUIC_CONTROL2_LOWPWR_MASK;

// 	ret = regmap_update_bits(max77843->regmap_muic,
// 			MAX77843_MUIC_REG_CONTROL2,
// 			MAX77843_MUIC_CONTROL2_LOWPWR_MASK |
// 			MAX77843_MUIC_CONTROL2_CPEN_MASK, ctrl2);
// 	if (ret < 0) {
// 		dev_err(info->dev, "Cannot update lowpower mode\n");
// 		return ret;
// 	}

// 	dev_dbg(info->dev,
// 		"CONTROL1 : 0x%02x, CONTROL2 : 0x%02x, state : %s\n",
// 		ctrl1, ctrl2, attached ? "attached" : "detached");

// 	return 0;
// }

// static void max77843_charger_set_otg_vbus(struct max77843_muic_info *info,
// 		 bool on)
// {
// 	struct max77693_dev *max77843 = info->max77843;
// 	unsigned int cnfg00;

// 	if (on)
// 		cnfg00 = MAX77843_CHG_OTG_MASK | MAX77843_CHG_BOOST_MASK;
// 	else
// 		cnfg00 = MAX77843_CHG_ENABLE | MAX77843_CHG_BUCK_MASK;

// 	regmap_update_bits(max77843->regmap_chg, MAX77843_CHG_REG_CHG_CNFG_00,
// 			   MAX77843_CHG_MODE_MASK, cnfg00);
// }

/**
 * @brief Use cached status registers values to determine cable attached state.
  * Called "max77804k_muic_filter_dev" in downstream driver
 * @param info struct max77804k_muic_info pointer
 * @return true if cable is attached, false otherwise
 */
static bool max77804k_muic_is_cable_attached(struct max77804k_muic_info *info)
{
	bool attached = true;
	u8 adc, adcerr, chgtyp, dxovp;

	adc = info->status[MAX77804K_MUIC_STATUS1] & MAX77804K_MUIC_STATUS1_ADC_MASK;
	adc >>= MAX77804K_MUIC_STATUS1_ADC_SHIFT;

	adcerr = info->status[MAX77804K_MUIC_STATUS1] & MAX77804K_MUIC_STATUS1_ADCERR_MASK;
	adcerr >>= MAX77804K_MUIC_STATUS1_ADCERR_SHIFT;

	chgtyp = info->status[MAX77804K_MUIC_STATUS2] & MAX77804K_MUIC_STATUS2_CHGTYP_MASK;
	chgtyp >>= MAX77804K_MUIC_STATUS2_CHGTYP_SHIFT;

	dxovp = info->status[MAX77804K_MUIC_STATUS2] & MAX77804K_MUIC_STATUS2_DXOVP_MASK;
	dxovp >>= MAX77804K_MUIC_STATUS2_DXOVP_SHIFT;

	switch (adc) {
	case MAX77804K_MUIC_ADC_GND:
		break;
	case (MAX77804K_MUIC_ADC_CEA936ATYPE1_CHG) ... (MAX77804K_MUIC_ADC_JIG_UART_ON):
		if(info->cable_type != CABLE_TYPE_NONE_MUIC
			&& chgtyp == MAX77804K_MUIC_CHGTYP_NO_VOLTAGE
			&& info->chgtyp != chgtyp) {
			attached = false;
		}
		break;
	case MAX77804K_MUIC_ADC_OPEN:
		if (!adcerr) {
			if (chgtyp == MAX77804K_MUIC_CHGTYP_NO_VOLTAGE) {
				if (dxovp)
					break;
				else
					attached = false;
			} else if (chgtyp == MAX77804K_MUIC_CHGTYP_USB ||
				   chgtyp == MAX77804K_MUIC_CHGTYP_DOWNSTREAM_PORT ||
				   chgtyp == MAX77804K_MUIC_CHGTYP_DEDICATED_CHGR ||
				   chgtyp == MAX77804K_MUIC_CHGTYP_500MA ||
				   chgtyp == MAX77804K_MUIC_CHGTYP_1A) {
				switch (info->cable_type) {
				case CABLE_TYPE_OTG_MUIC:
				case CABLE_TYPE_CARDOCK_MUIC:
				case CABLE_TYPE_SMARTDOCK_MUIC:
				case CABLE_TYPE_SMARTDOCK_TA_MUIC:
				case CABLE_TYPE_SMARTDOCK_USB_MUIC:
				case CABLE_TYPE_AUDIODOCK_MUIC:
					attached = false;
					break;
				default:
					break;
				}
			}
		}
		break;
	default:
		break;
	}
	return attached;
}

// static int max77843_muic_adc_gnd_handler(struct max77843_muic_info *info)
// {
// 	int ret, gnd_cable_type;
// 	bool attached;

// 	gnd_cable_type = max77843_muic_get_cable_type(info,
// 			MAX77843_CABLE_GROUP_ADC_GND, &attached);
// 	dev_dbg(info->dev, "external connector is %s (gnd:0x%02x)\n",
// 			attached ? "attached" : "detached", gnd_cable_type);

// 	switch (gnd_cable_type) {
// 	case MAX77843_MUIC_GND_USB_HOST:
// 	case MAX77843_MUIC_GND_USB_HOST_VB:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_USB,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_USB_HOST, attached);
// 		max77843_charger_set_otg_vbus(info, attached);
// 		break;
// 	case MAX77843_MUIC_GND_MHL_VB:
// 	case MAX77843_MUIC_GND_MHL:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_OPEN,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_DISP_MHL, attached);
// 		break;
// 	default:
// 		dev_err(info->dev, "failed to detect %s accessory(gnd:0x%x)\n",
// 			attached ? "attached" : "detached", gnd_cable_type);
// 		return -EINVAL;
// 	}

// 	return 0;
// }

// static int max77843_muic_jig_handler(struct max77843_muic_info *info,
// 		int cable_type, bool attached)
// {
// 	int ret;
// 	u8 path = MAX77843_MUIC_CONTROL1_SW_OPEN;

// 	dev_dbg(info->dev, "external connector is %s (adc:0x%02x)\n",
// 			attached ? "attached" : "detached", cable_type);

// 	switch (cable_type) {
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_USB_OFF:
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_USB_ON:
// 		path = MAX77843_MUIC_CONTROL1_SW_USB;
// 		break;
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_UART_OFF:
// 		path = MAX77843_MUIC_CONTROL1_SW_UART;
// 		break;
// 	default:
// 		return -EINVAL;
// 	}

// 	ret = max77843_muic_set_path(info, path, attached, false);
// 	if (ret < 0)
// 		return ret;

// 	extcon_set_state_sync(info->edev, EXTCON_JIG, attached);

// 	return 0;
// }

// static int max77843_muic_dock_handler(struct max77843_muic_info *info,
// 		bool attached)
// {
// 	int ret;

// 	dev_dbg(info->dev, "external connector is %s (adc: 0x10)\n",
// 			attached ? "attached" : "detached");

// 	ret = max77843_muic_set_path(info, MAX77843_MUIC_CONTROL1_SW_USB,
// 				     attached, attached);
// 	if (ret < 0)
// 		return ret;

// 	extcon_set_state_sync(info->edev, EXTCON_DISP_MHL, attached);
// 	extcon_set_state_sync(info->edev, EXTCON_USB_HOST, attached);
// 	extcon_set_state_sync(info->edev, EXTCON_DOCK, attached);

// 	return 0;
// }

// static int max77843_muic_adc_handler(struct max77843_muic_info *info)
// {
// 	int ret, cable_type;
// 	bool attached;

// 	cable_type = max77843_muic_get_cable_type(info,
// 			MAX77843_CABLE_GROUP_ADC, &attached);

// 	dev_dbg(info->dev,
// 		"external connector is %s (adc:0x%02x, prev_adc:0x%x)\n",
// 		attached ? "attached" : "detached", cable_type,
// 		info->prev_cable_type);

// 	switch (cable_type) {
// 	case MAX77843_MUIC_ADC_RESERVED_ACC_3: /* SmartDock */
// 		ret = max77843_muic_dock_handler(info, attached);
// 		if (ret < 0)
// 			return ret;
// 		break;
// 	case MAX77804K_MUIC_ADC_GND:
// 		ret = max77843_muic_adc_gnd_handler(info);
// 		if (ret < 0)
// 			return ret;
// 		break;
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_USB_OFF:
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_USB_ON:
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_UART_OFF:
// 		ret = max77843_muic_jig_handler(info, cable_type, attached);
// 		if (ret < 0)
// 			return ret;
// 		break;
// 	case MAX77843_MUIC_ADC_SEND_END_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S1_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S2_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S3_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S4_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S5_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S6_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S7_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S8_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S9_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S10_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S11_BUTTON:
// 	case MAX77843_MUIC_ADC_REMOTE_S12_BUTTON:
// 	case MAX77843_MUIC_ADC_RESERVED_ACC_1:
// 	case MAX77843_MUIC_ADC_RESERVED_ACC_2:
// 	case MAX77843_MUIC_ADC_RESERVED_ACC_4:
// 	case MAX77843_MUIC_ADC_RESERVED_ACC_5:
// 	case MAX77843_MUIC_ADC_AUDIO_DEVICE_TYPE2:
// 	case MAX77843_MUIC_ADC_PHONE_POWERED_DEV:
// 	case MAX77843_MUIC_ADC_TTY_CONVERTER:
// 	case MAX77843_MUIC_ADC_UART_CABLE:
// 	case MAX77843_MUIC_ADC_CEA936A_TYPE1_CHG:
// 	case MAX77843_MUIC_ADC_AV_CABLE_NOLOAD:
// 	case MAX77843_MUIC_ADC_CEA936A_TYPE2_CHG:
// 	case MAX77843_MUIC_ADC_FACTORY_MODE_UART_ON:
// 	case MAX77843_MUIC_ADC_AUDIO_DEVICE_TYPE1:
// 	case MAX77843_MUIC_ADC_OPEN:
// 		dev_err(info->dev,
// 			"accessory is %s but it isn't used (adc:0x%x)\n",
// 			attached ? "attached" : "detached", cable_type);
// 		return -EAGAIN;
// 	default:
// 		dev_err(info->dev,
// 			"failed to detect %s accessory (adc:0x%x)\n",
// 			attached ? "attached" : "detached", cable_type);
// 		return -EINVAL;
// 	}

// 	return 0;
// }

// static int max77843_muic_chg_handler(struct max77843_muic_info *info)
// {
// 	int ret, chg_type, gnd_type;
// 	bool attached;

// 	chg_type = max77843_muic_get_cable_type(info,
// 			MAX77843_CABLE_GROUP_CHG, &attached);

// 	dev_dbg(info->dev,
// 		"external connector is %s(chg_type:0x%x, prev_chg_type:0x%x)\n",
// 		attached ? "attached" : "detached",
// 		chg_type, info->prev_chg_type);

// 	switch (chg_type) {
// 	case MAX77843_MUIC_CHG_USB:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_USB,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_USB, attached);
// 		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_SDP,
// 					attached);
// 		break;
// 	case MAX77843_MUIC_CHG_DOWNSTREAM:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_OPEN,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_CDP,
// 					attached);
// 		break;
// 	case MAX77843_MUIC_CHG_DEDICATED:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_OPEN,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_DCP,
// 					attached);
// 		break;
// 	case MAX77843_MUIC_CHG_SPECIAL_500MA:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_OPEN,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_SLOW,
// 					attached);
// 		break;
// 	case MAX77843_MUIC_CHG_SPECIAL_1A:
// 		ret = max77843_muic_set_path(info,
// 					     MAX77843_MUIC_CONTROL1_SW_OPEN,
// 					     attached, false);
// 		if (ret < 0)
// 			return ret;

// 		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_FAST,
// 					attached);
// 		break;
// 	case MAX77843_MUIC_CHG_GND:
// 		gnd_type = max77843_muic_get_cable_type(info,
// 				MAX77843_CABLE_GROUP_ADC_GND, &attached);

// 		/* Charger cable on MHL accessory is attach or detach */
// 		if (gnd_type == MAX77843_MUIC_GND_MHL_VB)
// 			extcon_set_state_sync(info->edev, EXTCON_CHG_USB_DCP,
// 						true);
// 		else if (gnd_type == MAX77843_MUIC_GND_MHL)
// 			extcon_set_state_sync(info->edev, EXTCON_CHG_USB_DCP,
// 						false);
// 		break;
// 	case MAX77843_MUIC_CHG_DOCK:
// 		extcon_set_state_sync(info->edev, EXTCON_CHG_USB_DCP, attached);
// 		break;
// 	case MAX77843_MUIC_CHG_NONE:
// 		break;
// 	default:
// 		dev_err(info->dev,
// 			"failed to detect %s accessory (chg_type:0x%x)\n",
// 			attached ? "attached" : "detached", chg_type);

// 		max77843_muic_set_path(info, MAX77843_MUIC_CONTROL1_SW_OPEN,
// 				       attached, false);
// 		return -EINVAL;
// 	}

// 	return 0;
// }

static void max77804k_muic_irq_work(struct work_struct *work)
{
	struct max77804k_muic_info *info = container_of(work, struct max77804k_muic_info, irq_work);
	struct max77693_dev *max77804k = info->max77804k;
	int ret = 0;
	bool attached;

	mutex_lock(&info->mutex);

	ret = regmap_bulk_read(max77804k->regmap_muic,
			MAX77804K_MUIC_REG_STATUS1, info->status,
			MAX77804K_MUIC_STATUS_NUM);
	if (ret) {
		dev_err(info->dev, "Cannot read STATUS registers\n");
		mutex_unlock(&info->mutex);
		return;
	}

	attached = max77804k_muic_is_cable_attached(info);
	 // FIXME: remove this temp hack
	extcon_set_state_sync(info->edev, EXTCON_USB, attached);
	extcon_set_state_sync(info->edev, EXTCON_USB_HOST, false); // no otg for now

	if (info->irq_adc) {
		dev_info(info->dev, "irq_work: ADC IRQ, attached = %d\n", attached);
		// ret = max77804k_muic_adc_handler(info);
		// if (ret)
		// 	dev_err(info->dev, "Unknown cable type\n");
		info->irq_adc = false;
	}

	if (info->irq_chg) {
		dev_info(info->dev, "irq_work: CHG IRQ, attached = %d\n", attached);
		// ret = max77804k_muic_chg_handler(info);
		// if (ret)
		// 	dev_err(info->dev, "Unknown charger type\n");
		info->irq_chg = false;
	}

	mutex_unlock(&info->mutex);
}

static irqreturn_t max77804k_muic_irq_handler(int irq, void *data)
{
	struct max77804k_muic_info *info = data;
	int i, irq_type = -1;

	for (i = 0; i < ARRAY_SIZE(max77804k_muic_irq_types); i++)
		if (irq == max77804k_muic_irq_types[i].virq)
			irq_type = max77804k_muic_irq_types[i].irq;

	switch (irq_type) {
	case MAX77804K_MUIC_IRQ_INT1_ADC_TYPE:
	case MAX77804K_MUIC_IRQ_INT1_ADCLOW_TYPE:
	case MAX77804K_MUIC_IRQ_INT1_ADCERR_TYPE:
	case MAX77804K_MUIC_IRQ_INT1_ADC1K_TYPE:
		info->irq_adc = true;
		break;
	case MAX77804K_MUIC_IRQ_INT2_CHGTYP_TYPE:
	case MAX77804K_MUIC_IRQ_INT2_CHGDETREUN_TYPE:
	case MAX77804K_MUIC_IRQ_INT2_DCDTMR_TYPE:
	case MAX77804K_MUIC_IRQ_INT2_DXOVP_TYPE:
	case MAX77804K_MUIC_IRQ_INT2_VBVOLT_TYPE:
	case MAX77804K_MUIC_IRQ_INT2_VIDRM_TYPE:
		info->irq_chg = true;
		break;
	case MAX77804K_MUIC_IRQ_INT3_EOC_TYPE:
	case MAX77804K_MUIC_IRQ_INT3_CGMBC_TYPE:
	case MAX77804K_MUIC_IRQ_INT3_OVP_TYPE:
	case MAX77804K_MUIC_IRQ_INT3_MBCCHGERR_TYPE:
	case MAX77804K_MUIC_IRQ_INT3_CHGENABLED_TYPE:
	case MAX77804K_MUIC_IRQ_INT3_BATDET_TYPE:
		break;
	default:
		dev_err(info->dev, "Cannot recognize IRQ(%d)\n", irq_type);
		break;
	}

	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static void max77804k_muic_detect_cable_wq(struct work_struct *work)
{
	struct max77804k_muic_info *info = container_of(to_delayed_work(work),
			struct max77804k_muic_info, wq_detcable);
	struct max77693_dev *max77804k = info->max77804k;
	int ret;
	bool attached;
	// int adc, chg_type;

	mutex_lock(&info->mutex);

	ret = regmap_bulk_read(max77804k->regmap_muic,
			MAX77804K_MUIC_REG_STATUS1, info->status,
			MAX77804K_MUIC_STATUS_NUM);
	if (ret) {
		dev_err(info->dev, "Cannot read STATUS registers\n");
		goto err_cable_wq;
	}

	attached = max77804k_muic_is_cable_attached(info);

	dev_info(info->dev, "Initial cable detection: attached = %d\n", attached);
	// FIXME: remove this temp hack
	extcon_set_state_sync(info->edev, EXTCON_USB, attached);
	extcon_set_state_sync(info->edev, EXTCON_USB_HOST, false); // no otg for now

	// adc = max77804k_muic_get_cable_type(info, MAX77804K_CABLE_GROUP_ADC, &attached);
	// if (attached && adc != MAX77804K_MUIC_ADC_OPEN) {
	// 	ret = max77804k_muic_adc_handler(info);
	// 	if (ret < 0) {
	// 		dev_err(info->dev, "Cannot detect accessory\n");
	// 		goto err_cable_wq;
	// 	}
	// }

	// chg_type = max77804k_muic_get_cable_type(info, MAX77804K_CABLE_GROUP_CHG, &attached);
	// if (attached && chg_type != MAX77804K_MUIC_CHGTYP_NO_VOLTAGE) {
	// 	ret = max77804k_muic_chg_handler(info);
	// 	if (ret < 0) {
	// 		dev_err(info->dev, "Cannot detect charger accessory\n");
	// 		goto err_cable_wq;
	// 	}
	// }

err_cable_wq:
	mutex_unlock(&info->mutex);
}

static int max77804k_muic_set_debounce_time(struct max77804k_muic_info *info,
		enum max77804k_muic_adc_debounce_time value)
{
	struct max77693_dev *max77804k = info->max77804k;
	int ret;
	unsigned int pmic_id1;

	if (value > MAX77804K_DEBOUNCE_TIME_38_62MS) {
		dev_err(info->dev, "Invalid ADC debounce time!\n");
		return -EINVAL;
	}

	ret = regmap_read(max77804k->regmap, MAX77804K_PMIC_REG_PMIC_ID1, &pmic_id1);
	if (ret < 0) {
		dev_err(info->dev, "Failed to read PMIC ID\n");
		return ret;
	}

	dev_info(info->dev, "pmic_id1: 0x%02X\n", pmic_id1);
	// ^^ "max77804k-muic max77804k-muic: pmic_id1: 0x34"

	/* Depending on pmic revision, update different registers */
	if (pmic_id1 == 0x34) {
		ret = regmap_update_bits(max77804k->regmap_muic,
			MAX77804K_MUIC_REG_CTRL4,
			MAX77804K_MUIC_CTRL4_ADCDBSET_MASK | MAX77804K_MUIC_CTRL4_ADCMODE_MASK,
			value << MAX77804K_MUIC_CTRL4_ADCDBSET_SHIFT);
	} else {
		ret = regmap_update_bits(max77804k->regmap_muic,
			MAX77804K_MUIC_REG_CTRL3,
			MAX77804K_MUIC_CTRL3_ADCDBSET_MASK,
			value << MAX77804K_MUIC_CTRL3_ADCDBSET_SHIFT);
	}

	if (ret < 0)
		dev_err(info->dev, "failed to update ADC debounce time\n");
	return ret;
}

static int max77804k_init_muic_regmap(struct max77693_dev *max77804k)
{
	int ret;

	max77804k->i2c_muic = i2c_new_dummy_device(max77804k->i2c->adapter,
			MAX77804K_I2C_ADDR_MUIC);
	if (IS_ERR(max77804k->i2c_muic)) {
		dev_err(&max77804k->i2c->dev, "Cannot allocate I2C device for MUIC\n");
		return PTR_ERR(max77804k->i2c_muic);
	}

	i2c_set_clientdata(max77804k->i2c_muic, max77804k);

	max77804k->regmap_muic = devm_regmap_init_i2c(max77804k->i2c_muic,
			&max77804k_muic_regmap_config);
	if (IS_ERR(max77804k->regmap_muic)) {
		ret = PTR_ERR(max77804k->regmap_muic);
		goto err_muic_i2c;
	}

	//ret = devm_regmap_add_irq_chip(max77804k->regmap_muic, max77804k->irq,
	ret = regmap_add_irq_chip(max77804k->regmap_muic, max77804k->irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_SHARED,
			0, &max77804k_muic_irq_chip, &max77804k->irq_data_muic);
	if (ret < 0) {
		dev_err(&max77804k->i2c->dev, "Cannot add MUIC IRQ chip: %d\n", ret);
		goto err_muic_i2c;
	}

	return 0;

err_muic_i2c:
	i2c_unregister_device(max77804k->i2c_muic);

	return ret;
}

static int max77804k_muic_probe(struct platform_device *pdev)
{
	struct max77693_dev *max77804k = dev_get_drvdata(pdev->dev.parent);
	struct max77804k_muic_info *info;
	unsigned int id;
	// bool attached;
	int i, ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->max77804k = max77804k;
	info->cable_type = CABLE_TYPE_UNKNOWN_MUIC;

	platform_set_drvdata(pdev, info);
	mutex_init(&info->mutex);

	/* Initialize i2c and regmap */
	ret = max77804k_init_muic_regmap(max77804k);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init MUIC regmap\n");
		return ret;
	}

	/* Turn off auto detection configuration */
	// ret = regmap_update_bits(max77804k->regmap_muic,
	// 		MAX77843_MUIC_REG_CONTROL4,
	// 		MAX77843_MUIC_CONTROL4_USBAUTO_MASK |
	// 		MAX77843_MUIC_CONTROL4_FCTAUTO_MASK,
	// 		CONTROL4_AUTO_DISABLE);

	/* Initialize extcon device */
	info->edev = devm_extcon_dev_allocate(&pdev->dev, max77804k_extcon_cable);
	if (IS_ERR(info->edev)) {
		dev_err(&pdev->dev, "Failed to allocate memory for extcon\n");
		ret = PTR_ERR(info->edev);
		goto err_muic_irq;
	}

	ret = devm_extcon_dev_register(&pdev->dev, info->edev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register extcon device\n");
		goto err_muic_irq;
	}

	/* Set ADC debounce time: 25ms */
	max77804k_muic_set_debounce_time(info, MAX77804K_DEBOUNCE_TIME_25MS);

	/* From downstream: */
	/* Set DCDTmr to 2sec */
	// max77804k_read_reg(info->max77804k->muic, MAX77804K_MUIC_REG_CDETCTRL1, &cdetctrl1);
	// cdetctrl1 &= ~(1 << 5); // sets 5th bit to zero
	// max77804k_write_reg(info->max77804k->muic, MAX77804K_MUIC_REG_CDETCTRL1, cdetctrl1);
	ret = regmap_update_bits(max77804k->regmap_muic, MAX77804K_MUIC_REG_CDETCTRL1, BIT(5), 0);

	/* Set initial path for UART when JIG is connected to get serial logs */
	/* Read MUIC status registers */
	ret = regmap_bulk_read(max77804k->regmap_muic,
			MAX77804K_MUIC_REG_STATUS1, info->status,
			MAX77804K_MUIC_STATUS_NUM);
	if (ret) {
		dev_err(info->dev, "Cannot read STATUS registers\n");
		goto err_muic_irq;
	}
	/* Initial cable detection */
	// cable_type = max77804k_muic_get_cable_type(info, MAX77804K_CABLE_GROUP_ADC, &attached);
	// if (attached && cable_type == MAX77804K_MUIC_ADC_JIG_UART_OFF)
	// 	max77804k_muic_set_path(info, MAX77804K_MUIC_CONTROL1_SW_UART, true, false);

	/* Check revision number of MUIC device */
	ret = regmap_read(max77804k->regmap_muic, MAX77804K_MUIC_REG_ID, &id);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read revision number\n");
		goto err_muic_irq;
	}
	dev_info(info->dev, "MUIC device ID : 0x%x\n", id);

	/* Support virtual irq domain for max77804k MUIC device */
	INIT_WORK(&info->irq_work, max77804k_muic_irq_work);

	/* Clear IRQ bits before request IRQs */
	ret = regmap_bulk_read(max77804k->regmap_muic,
			MAX77804K_MUIC_REG_INT1, info->status,
			MAX77804K_MUIC_STATUS_NUM);
	if (ret) {
		dev_err(&pdev->dev, "Failed to Clear IRQ bits\n");
		goto err_muic_irq;
	}

	for (i = 0; i < ARRAY_SIZE(max77804k_muic_irq_types); i++) {
		struct max77804k_muic_irq *muic_irq = &max77804k_muic_irq_types[i];
		int virq = 0;

		virq = regmap_irq_get_virq(max77804k->irq_data_muic, muic_irq->irq);
		if (virq <= 0) {
			ret = -EINVAL;
			goto err_muic_irq;
		}
		muic_irq->virq = virq;

		ret = request_threaded_irq(virq, NULL,
				max77804k_muic_irq_handler, IRQF_NO_SUSPEND,
				muic_irq->name, info);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request irq (IRQ: %d (%s), error: %d)\n",
				muic_irq->irq, muic_irq->name, ret);
			goto err_muic_irq;
		}
	}

	/* Detect accessory after completing the initialization of platform */
	INIT_DELAYED_WORK(&info->wq_detcable, max77804k_muic_detect_cable_wq);
	queue_delayed_work(system_power_efficient_wq, &info->wq_detcable,
			msecs_to_jiffies(DELAY_MS_DEFAULT));

	 // FIXME: Initially report that we're connected as USB gadget
	// extcon_set_state_sync(info->edev, EXTCON_USB, true);
	// extcon_set_state_sync(info->edev, EXTCON_USB_HOST, false); // no otg for now

	return 0;

err_muic_irq:
	regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_muic);
	i2c_unregister_device(max77804k->i2c_muic);

	return ret;
}

static int max77804k_muic_remove(struct platform_device *pdev)
{
	struct max77804k_muic_info *info = platform_get_drvdata(pdev);
	struct max77693_dev *max77804k = info->max77804k;
	int i;

	dev_info(&pdev->dev, "freeing extcon IRQs...\n");

	for (i = 0; i < ARRAY_SIZE(max77804k_muic_irq_types); i++) {
		struct max77804k_muic_irq *muic_irq = &max77804k_muic_irq_types[i];
		free_irq(muic_irq->virq, info);
	}

	dev_info(&pdev->dev, "removing extcon device\n");

	cancel_work_sync(&info->irq_work);
	regmap_del_irq_chip(max77804k->irq, max77804k->irq_data_muic);
	i2c_unregister_device(max77804k->i2c_muic);

	return 0;
}

static const struct platform_device_id max77804k_muic_id[] = {
	{ "max77804k-muic", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, max77804k_muic_id);

static struct platform_driver max77804k_muic_driver = {
	.driver		= {
		.name		= "max77804k-muic",
	},
	.probe		= max77804k_muic_probe,
	.remove		= max77804k_muic_remove,
	.id_table	= max77804k_muic_id,
};

static int __init max77804k_muic_init(void)
{
	return platform_driver_register(&max77804k_muic_driver);
}
subsys_initcall(max77804k_muic_init);

MODULE_DESCRIPTION("Maxim MAX77804K Extcon driver");
MODULE_AUTHOR("Sukdong Kim <sukdong.kim@samsung.com>");
MODULE_AUTHOR("Alexey Minnekhanov <alexeymin@postmarketos.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:max77804k-muic");
MODULE_ALIAS("platform:extcon-max77804k");
