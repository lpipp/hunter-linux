/* SPDX-License-Identifier: GPL-2.0 */
static bool hunter_matches(void)
{
	return dmi_match(DMI_SYS_VENDOR, "HUAWEI") &&
	       dmi_match(DMI_PRODUCT_NAME, "FRD-WX9") &&
	       dmi_match(DMI_BOARD_NAME, "FRD-WX9-PCB") &&
	       dmi_match(DMI_PRODUCT_VERSION, "M1060") &&
	       dmi_match(DMI_BIOS_VERSION, "1.18");
}

/* Each sysfs read explicitly queries firmware. No timers or cached mode:
 * the motherboard's performance key may change it at any time.
 */
static int hunter_read(struct huawei_wmi *huawei, unsigned int op, int *value)
{
	struct hunter_request result = {};
	u8 input[64];
	int err;
	err = hunter_encode(op, input);
	if (err)
		return err;
	err = huawei_wmi_transaction(huawei, input, sizeof(input), false, &result);
	if (err)
		return err;
	return hunter_decode(op, result.trace[result.attempts - 1].payload, value);
}

static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct huawei_wmi *huawei = dev_get_drvdata(dev);
	int value, err;
	err = hunter_read(huawei, HUNTER_GET_MODE, &value);
	return err ? err : sysfs_emit(buf, "%d\n", value);
}
static DEVICE_ATTR_RO(mode);

static ssize_t fans_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct huawei_wmi *huawei = dev_get_drvdata(dev);
	int fan1, fan2, err;
	err = hunter_read(huawei, HUNTER_GET_FAN0, &fan1);
	if (err)
		return err;
	err = hunter_read(huawei, HUNTER_GET_FAN1, &fan2);
	return err ? err : sysfs_emit(buf, "%d %d\n", fan1, fan2);
}
static DEVICE_ATTR_RO(fans);

static struct attribute *hunter_attrs[] = {
	&dev_attr_mode.attr,
	&dev_attr_fans.attr,
	NULL,
};
static const struct attribute_group hunter_group = {
	.name = "hunter",
	.attrs = hunter_attrs,
};

static int hunter_setup(struct huawei_wmi *huawei)
{
	int err;
	if (!hunter_matches())
		return 0;
	mutex_lock(&huawei->wmi_lock);
	huawei->hunter.matched = true;
	mutex_unlock(&huawei->wmi_lock);
	err = sysfs_create_group(&huawei->dev->kobj, &hunter_group);
	if (err) {
		mutex_lock(&huawei->wmi_lock);
		huawei->hunter.matched = false;
		mutex_unlock(&huawei->wmi_lock);
	}
	return err;
}

static void hunter_stop(struct huawei_wmi *huawei)
{
	bool matched;
	mutex_lock(&huawei->wmi_lock);
	huawei->hunter.removing = true;
	matched = huawei->hunter.matched;
	mutex_unlock(&huawei->wmi_lock);
	/* Removal drains sysfs readers. Keep legacy transport alive until the
	 * existing LED and battery cleanup in huawei_wmi_remove has finished.
	 */
	if (matched)
		sysfs_remove_group(&huawei->dev->kobj, &hunter_group);
	mutex_lock(&huawei->wmi_lock);
	huawei->hunter.matched = false;
	mutex_unlock(&huawei->wmi_lock);
}

static int hunter_pm_suspend(struct device *dev)
{
	struct huawei_wmi *huawei = dev_get_drvdata(dev);
	mutex_lock(&huawei->wmi_lock);
	huawei->hunter.suspended = true;
	mutex_unlock(&huawei->wmi_lock);
	return 0;
}
static int hunter_pm_resume(struct device *dev)
{
	struct huawei_wmi *huawei = dev_get_drvdata(dev);
	mutex_lock(&huawei->wmi_lock);
	huawei->hunter.suspended = false;
	mutex_unlock(&huawei->wmi_lock);
	return 0;
}
static DEFINE_SIMPLE_DEV_PM_OPS(hunter_pm_ops, hunter_pm_suspend, hunter_pm_resume);
