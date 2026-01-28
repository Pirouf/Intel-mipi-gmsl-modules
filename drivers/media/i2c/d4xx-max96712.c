// SPDX-License-Identifier: GPL-2.0
/*
 * max96712.c - max96712 IO Expander driver
 *
 * Copyright (c) 2016-2023, NVIDIA CORPORATION & AFFILIATES. All Rights Reserved.
 */

/* #define DEBUG */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/of_gpio.h>
#include <linux/version.h>
// #include <media/camera_common.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <media/i2c/d4xx-max96712.h>

/* register specifics */

#define MAX96712_REG5_ADDR 0x5
#define MAX96712_REG6_ADDR 0x6
#define MAX96712_CTRL0_ADDR 0x17
#define MAX96712_CTRL1_ADDR 0x18

#define MAX96712_BACKTOP25_ADDR 0x418

#define MAX96712_MIPI_PHY0_ADDR 0x8A0
#define MAX96712_MIPI_PHY2_ADDR 0x8A2
#define MAX96712_MIPI_PHY3_ADDR 0x8A3
#define MAX96712_MIPI_PHY5_ADDR 0x8A5
#define MAX96712_VIDEO_PIPE_SEL_0_ADDR 0xF0
#define MAX96712_VIDEO_PIPE_SEL_1_ADDR 0xF1
#define MAX96712_VIDEO_PIPE_EN_ADDR 0xF4
#define MAX96712_BACKTOP12_CSI_OUT_EN_ADDR 0x40B

#define MAX96712_MIPI_TX10_ADDR 0x90A
#define MAX96712_MIPI_TX11_ADDR 0x90B
#define MAX96712_MIPI_TX13_SRC_0_MAP_ADDR 0x90D
#define MAX96712_MIPI_TX14_DST_0_MAP_ADDR 0x90E
#define MAX96712_MIPI_TX15_SRC_1_MAP_ADDR 0x90F
#define MAX96712_MIPI_TX16_DST_1_MAP_ADDR 0x910
#define MAX96712_MIPI_TX17_SRC_2_MAP_ADDR 0x911
#define MAX96712_MIPI_TX18_DST_2_MAP_ADDR 0x912
#define MAX96712_MIPI_TX19_SRC_3_MAP_ADDR 0x913
#define MAX96712_MIPI_TX20_DST_3_MAP_ADDR 0x914
#define MAX96712_MIPI_TX45_MAP_DPHY_DEST_ADDR 0x92D
#define MAX96712_MIPI_TX51_ADDR 0x973

#define MAX96712_VIDEO_RX0_ADDR 0x100

#define MAX96712_RLMS_A_RLMS58_ADDR 0x1458
#define MAX96712_RLMS_A_RLMS59_ADDR 0x1459
#define MAX96712_RLMS_B_RLMS58_ADDR 0x1558
#define MAX96712_RLMS_B_RLMS59_ADDR 0x1559


/* data defines */
#define MAX96712_MAX_PIPES 4

struct max96712 {
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	const char *channel;
};
static struct max96712 *global_priv[4];

int max96712_write_reg_Dser(int slaveAddr, int channel, u16 addr, u8 val);
int max96712_read_reg_Dser(int slaveAddr, int channel, u16 addr, unsigned int *val);

struct mutex max96712_rw;

int max96712_write_reg_Dser(int slaveAddr, int channel,
				u16 addr, u8 val)
{
	struct i2c_client *i2c_client = NULL;
	int bak = 0;
	int err = 0;
	/* unsigned int ival = 0; */

	if (channel > 3 || channel < 0 || global_priv[channel] == NULL)
		return -1;

	mutex_lock(&max96712_rw);
	i2c_client = global_priv[channel]->i2c_client;
	bak = i2c_client->addr;

	i2c_client->addr = slaveAddr / 2;
	err = regmap_write(global_priv[channel]->regmap, addr, val);

	i2c_client->addr = bak;
	if (err) {
		dev_err(&i2c_client->dev, "%s: addr = 0x%x, val = 0x%x\n",
				__func__, addr, val);
	}
	mutex_unlock(&max96712_rw);
	return err;
}
EXPORT_SYMBOL(max96712_write_reg_Dser);


int max96712_read_reg_Dser(int slaveAddr, int channel,
				u16 addr, unsigned int *val)
{
	struct i2c_client *i2c_client = NULL;
	int bak = 0;
	int err = 0;

	if (channel > 3 || channel < 0 || global_priv[channel] == NULL)
		return -1;

	mutex_lock(&max96712_rw);
	i2c_client = global_priv[channel]->i2c_client;
	bak = i2c_client->addr;
	i2c_client->addr = slaveAddr / 2;

	err = regmap_read(global_priv[channel]->regmap, addr, val);
	i2c_client->addr = bak;
	if (err) {
		dev_err(&i2c_client->dev, "%s: addr = 0x%x, val = 0x%x\n",
				__func__, addr, *val);
	}
	mutex_unlock(&max96712_rw);
	return err;
}
EXPORT_SYMBOL(max96712_read_reg_Dser);

static int max96712_read_reg(struct max96712 *priv,
			u16 addr, unsigned int *val)
{
	struct i2c_client *i2c_client = priv->i2c_client;
	int err;

	err = regmap_read(priv->regmap, addr, val);
	if (err)
		dev_err(&i2c_client->dev, "%s:i2c read failed, 0x%x = %x\n",
			__func__, addr, *val);

	return err;
}

static int max96712_write_reg(struct max96712 *priv,
			u16 addr, unsigned int val)
{
	struct i2c_client *i2c_client = priv->i2c_client;
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_err(&i2c_client->dev, "%s:i2c write failed, 0x%x = %x\n",
			__func__, addr, val);

	return err;
}

static int max96712_stats_show(struct seq_file *s, void *data)
{
	return 0;
}

static int max96712_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, max96712_stats_show, inode->i_private);
}

static ssize_t max96712_debugfs_write(struct file *s,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct max96712 *priv =
		((struct seq_file *)s->private_data)->private;
	struct i2c_client *i2c_client = priv->i2c_client;

	char buf[255];
	int buf_size;
	int val = 0;

	if (!user_buf || count <= 1)
		return -EFAULT;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (buf[0] == 'd') {
		dev_info(&i2c_client->dev, "%s, set daymode\n", __func__);
		max96712_read_reg(priv, 0x0010, &val);
		return count;
	}

	if (buf[0] == 'n') {
		dev_info(&i2c_client->dev, "%s, set nightmode\n", __func__);
		return count;
	}

	return count;
}


static const struct file_operations max96712_debugfs_fops = {
	.open = max96712_debugfs_open,
	.read = seq_read,
	.write = max96712_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

int max96712_power_on(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	struct i2c_client *i2c_client = priv->i2c_client;
	struct device_node *np = i2c_client->dev.of_node;
	unsigned int pwdn_gpio = 0;

	if(np) {
		pwdn_gpio = of_get_named_gpio(np, "pwdn-gpios", 0);
		dev_info(&i2c_client->dev,"%s: pwdn_gpio = %d\n",__func__,pwdn_gpio);
	}
	if (pwdn_gpio > 0) {
		gpio_direction_output(pwdn_gpio, 1);
		gpio_set_value(pwdn_gpio, 1);
		msleep(100);
	}
	return 0;
}
EXPORT_SYMBOL(max96712_power_on);

static int max96712_debugfs_init(const char *dir_name,
				struct dentry **d_entry,
				struct dentry **f_entry,
				struct max96712 *priv)
{
	struct dentry  *dp, *fp;
	char dev_name[20];
	struct i2c_client *i2c_client = priv->i2c_client;
	struct device_node *np = i2c_client->dev.of_node;
	int err = 0;
	int index = 0;

	if (np) {
		err = of_property_read_string(np, "channel", &priv->channel);
		if (err)
			dev_err(&i2c_client->dev, "channel not found\n");

		err = snprintf(dev_name, sizeof(dev_name), "max96712_%s", priv->channel);
		if (err < 0)
			return -EINVAL;
	}
	index = priv->channel[0] - 'a';
	if (index < 0)
		return -EINVAL;
	global_priv[index] = priv;

	dev_dbg(&i2c_client->dev, "%s: index %d\n", __func__, index);

	dp = debugfs_create_dir(dev_name, NULL);
	if (dp == NULL) {
		dev_err(&i2c_client->dev, "%s: debugfs create dir failed\n",
			__func__);
		return -ENOMEM;
	}

	fp = debugfs_create_file("max96712", 0644, dp, priv,
					&max96712_debugfs_fops);
	if (!fp) {
		dev_err(&i2c_client->dev, "%s: debugfs create file failed\n",
			__func__);
		debugfs_remove_recursive(dp);
		return -ENOMEM;
	}

	if (d_entry)
		*d_entry = dp;
	if (f_entry)
		*f_entry = fp;
	return 0;
}

static  struct regmap_config max96712_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int max96712_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
#else
static int max96712_probe(struct i2c_client *client)
#endif
{
	struct max96712 *priv;
	int err = 0;

	dev_info(&client->dev, "%s: enter\n", __func__);

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client,
				&max96712_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	mutex_init(&max96712_rw);

	dev_set_drvdata(&client->dev, priv);

	err = max96712_power_on(&client->dev);
	if (err) {
		dev_err(&client->dev, "Failed to power on err =%d\n",err);
		return err;
	}

	err = max96712_debugfs_init(NULL, NULL, NULL, priv);
	if (err)
		return err;

	/*set daymode by fault*/
	dev_info(&client->dev, "%s:  success\n", __func__);

	return err;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int max96712_remove(struct i2c_client *client)
#else
static void max96712_remove(struct i2c_client *client)
#endif
{
	struct max96712 *priv;
	if (client != NULL) {
		priv = dev_get_drvdata(&client->dev);

		mutex_destroy(&max96712_rw);
		i2c_unregister_device(client);
		client = NULL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

struct reg_pair {
	u16 addr;
	u8 val;
};

static int max96712_set_registers(struct max96712 *priv, struct reg_pair *map, u32 count)
{
	int err = 0;
	u32 j = 0;

	for (j = 0; j < count; j++) {
		err = max96712_write_reg(priv, map[j].addr, map[j].val);
		if (err != 0)
			break;
	}

	return err;
}

int max96712_get_available_pipe_id(struct device *dev, int vc_id)
{
	/* TODO EHUD: Currently vc_id == pipe id but that's definitely
	 * not the end goal if we plan to support multiple cameras
	 */
	return vc_id;
}
EXPORT_SYMBOL(max96712_get_available_pipe_id);

int max96712_release_pipe(struct device *dev, int pipe_id)
{
	/* TODO EHUD: pipe id? */
	return 0;
}
EXPORT_SYMBOL(max96712_release_pipe);

void max96712_reset_oneshot(struct device *dev)
{
	struct max96712 *priv = dev_get_drvdata(dev);
	max96712_write_reg(priv, MAX96712_CTRL1_ADDR, 0x0F);
	msleep(1);
	max96712_write_reg(priv, MAX96712_CTRL1_ADDR, 0x00);
	msleep(100);
}
EXPORT_SYMBOL(max96712_reset_oneshot);

static int __max96712_set_pipe(struct device *dev, int pipe_id, u8 data_type1,
			      u8 data_type2, u32 vc_id)
{
	int err = 0;
	struct max96712 *priv = dev_get_drvdata(dev);

	int i = 0;

	struct reg_pair map_pipe_select[] = {
		{MAX96712_REG5_ADDR, 0x80}, // Enable lock
		{MAX96712_REG6_ADDR, 0xF1}, // Enable lock
		{MAX96712_MIPI_PHY0_ADDR, 0x01},
		{MAX96712_MIPI_PHY2_ADDR, 0xF4},
		{MAX96712_MIPI_PHY3_ADDR, 0x44},
		{MAX96712_MIPI_PHY5_ADDR, 0x00},
		{MAX96712_VIDEO_PIPE_SEL_0_ADDR, 0x10},
		{MAX96712_VIDEO_PIPE_SEL_1_ADDR, 0x32},
		{MAX96712_VIDEO_PIPE_EN_ADDR, 0x0F},
	};

	struct reg_pair map_pipe_control[] = {
		// Enable 4 mappings for Pipe X
		{MAX96712_MIPI_TX11_ADDR, 0x0F},
		// Map data_type1 on vc_id
		{MAX96712_MIPI_TX13_SRC_0_MAP_ADDR, 0x1E},
		{MAX96712_MIPI_TX14_DST_0_MAP_ADDR, 0x1E},
		// Map frame_start on vc_id
		{MAX96712_MIPI_TX15_SRC_1_MAP_ADDR, 0x00},
		{MAX96712_MIPI_TX16_DST_1_MAP_ADDR, 0x00},
		// Map frame end on vc_id
		{MAX96712_MIPI_TX17_SRC_2_MAP_ADDR, 0x01},
		{MAX96712_MIPI_TX18_DST_2_MAP_ADDR, 0x01},
		// Map data_type2 on vc_id
		{MAX96712_MIPI_TX19_SRC_3_MAP_ADDR, 0x12},
		{MAX96712_MIPI_TX20_DST_3_MAP_ADDR, 0x12},
		// All mappings to PHY1 (master for port A)
		{MAX96712_MIPI_TX45_MAP_DPHY_DEST_ADDR, 0x00},
		{MAX96712_MIPI_TX10_ADDR, 0x40},
		// SEQ_MISS_EN: Disabled / DIS_PKT_DET: Disabled
		{MAX96712_VIDEO_RX0_ADDR, 0x33}, // pipe X
	};

	struct reg_pair map_pipe_opt[] = {
		{MAX96712_RLMS_A_RLMS58_ADDR, 0x28}, // PHY A Optimization
		{MAX96712_RLMS_A_RLMS59_ADDR, 0x68}, // PHY A Optimization
		{MAX96712_RLMS_B_RLMS58_ADDR, 0x28}, // PHY B Optimization
		{MAX96712_RLMS_B_RLMS59_ADDR, 0x68}, // PHY B Optimization
		{MAX96712_BACKTOP25_ADDR, 0x2f},
	};

	for (i = 0; i < (ARRAY_SIZE(map_pipe_control) - 1); i++) {
		map_pipe_control[i].addr += 0x40 * pipe_id;
	}
	map_pipe_control[(ARRAY_SIZE(map_pipe_control) - 1)].addr += 0x12 * pipe_id;

	if (data_type2 == 0x0) {
		/* For IMU (which has no metadata) we override mapping
		* to enable only 3
		*/
		map_pipe_control[0].val = 0x07;
	}
	map_pipe_control[1].val = (vc_id << 6) | data_type1;
	map_pipe_control[2].val = (vc_id << 6) | data_type1;
	map_pipe_control[3].val = (vc_id << 6) | 0x00;
	map_pipe_control[4].val = (vc_id << 6) | 0x00;
	map_pipe_control[5].val = (vc_id << 6) | 0x01;
	map_pipe_control[6].val = (vc_id << 6) | 0x01;
	map_pipe_control[7].val = (vc_id << 6) | data_type2;
	map_pipe_control[8].val = (vc_id << 6) | data_type2;

	max96712_write_reg(priv, MAX96712_BACKTOP12_CSI_OUT_EN_ADDR, 0x0);

	err |= max96712_set_registers(priv, map_pipe_select,
					 ARRAY_SIZE(map_pipe_select));

	err |= max96712_set_registers(priv, map_pipe_control,
				     ARRAY_SIZE(map_pipe_control));

	max96712_write_reg(priv, MAX96712_MIPI_TX51_ADDR, 0x2);

	err |= max96712_set_registers(priv, map_pipe_opt,
					 ARRAY_SIZE(map_pipe_opt));

	max96712_write_reg(priv, MAX96712_BACKTOP12_CSI_OUT_EN_ADDR, 0x2);

	max96712_reset_oneshot(dev);

	return err;
}

int max96712_init_settings(struct device *dev)
{
	int err = 0;
	int i;
	struct max96712 *priv = dev_get_drvdata(dev);

	mutex_lock(&max96712_rw);

	max96712_write_reg(priv, MAX96712_CTRL0_ADDR, 0x14);
	max96712_write_reg(priv, MAX96712_CTRL1_ADDR, 0xF0);
	max96712_write_reg(priv, MAX96712_CTRL1_ADDR, 0x00);

	for (i = 0; i < MAX96712_MAX_PIPES; i++)
		err |= __max96712_set_pipe(dev, i, GMSL_CSI_DT_YUV422_8,
					  GMSL_CSI_DT_EMBED, i);

	mutex_unlock(&max96712_rw);

	return err;
}
EXPORT_SYMBOL(max96712_init_settings);

int max96712_set_pipe(struct device *dev, int pipe_id,
		     u8 data_type1, u8 data_type2, u32 vc_id)
{
	int err = 0;

	if (pipe_id > (MAX96712_MAX_PIPES - 1)) {
		dev_info(dev, "%s, input pipe_id: %d exceed max96712 max pipes\n",
			 __func__, pipe_id);
		return -EINVAL;
	}

	dev_dbg(dev, "%s pipe_id %d, data_type1 %u, data_type2 %u, vc_id %u\n",
		__func__, pipe_id, data_type1, data_type2, vc_id);

	mutex_lock(&max96712_rw);

	err = __max96712_set_pipe(dev, pipe_id, data_type1, data_type2, vc_id);

	mutex_unlock(&max96712_rw);

	return err;
}
EXPORT_SYMBOL(max96712_set_pipe);

int max96712_setup_link(struct device *dev, struct device *s_dev)
{
	/* TODO EHUD: Is needed? */
	return 0;
}
EXPORT_SYMBOL(max96712_setup_link);

int max96712_setup_control(struct device *dev, struct device *s_dev)
{
	/* TODO EHUD: Is needed? */
	return 0;
}
EXPORT_SYMBOL(max96712_setup_control);

int max96712_reset_control(struct device *dev, struct device *s_dev)
{
	/* TODO EHUD: Is needed? */
	return 0;
}
EXPORT_SYMBOL(max96712_reset_control);

int max96712_sdev_register(struct device *dev, struct gmsl_link_ctx *g_ctx)
{
	/* TODO EHUD: Is needed? */
	return 0;
}
EXPORT_SYMBOL(max96712_sdev_register);

int max96712_sdev_unregister(struct device *dev, struct device *s_dev)
{
	/* TODO EHUD: Is needed? */
	return 0;
}
EXPORT_SYMBOL(max96712_sdev_unregister);

void max96712_power_off(struct device *dev)
{
	/* TODO EHUD: Is needed? */
	return;
}
EXPORT_SYMBOL(max96712_power_off);

static const struct i2c_device_id max96712_id[] = {
	{ "d4xx-max96712", 0 },
	{ },
};

static const struct of_device_id max96712_of_match[] = {
	{ .compatible = "maxim,max96712", },
	{ },
};

MODULE_DEVICE_TABLE(i2c, max96712_id);

static struct i2c_driver max96712_i2c_driver = {
	.driver = {
		.name = "d4xx-max96712",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(max96712_of_match),
	},
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	.probe = max96712_probe,
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	.probe_new = max96712_probe,
#else
	.probe = max96712_probe,
#endif
	.remove = max96712_remove,
	.id_table = max96712_id,
};

module_i2c_driver(max96712_i2c_driver);

MODULE_DESCRIPTION("IO Expander driver max96712");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION_SUFFIX);
