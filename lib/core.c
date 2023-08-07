/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libsyspower.
 *
 * Copyright (C) 2022 Loic Poulain <loic.poulain@linaro.org>
 */

#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <syspower.h>
#include <libgen.h>
#include <libudev.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <linux/limits.h>


#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const char path_autosleep[] = "/sys/power/autosleep";
static const char path_wake_unlock[] = "/sys/power/wake_unlock";
static const char path_wake_lock[] = "/sys/power/wake_lock";
static const char path_state[] = "/sys/power/state";
static const char path_wakeup_irq[] = "/sys/power/pm_wakeup_irq";
static const char path_rtc_dev[] = "/dev/rtc";
static const char path_supply[128] = "/sys/class/power_supply";
static struct udev_monitor *udevmon;

#define WAKEDEV_COUNT 128

struct wakeup_source {
	char name[256];
	char devpath[PATH_MAX + 1];
};

static struct {
	int fd_lock;
	int fd_unlock;
	int fd_state;
	int fd_autosleep;
	int fd_rtc;
	unsigned int sleep_mask;
	struct wakeup_source *wakeup_cache[WAKEDEV_COUNT];
} syspower;

static const char *sleep_state[] = {
	[SYSPOWER_SLEEP_TYPE_MEM] = "mem\n",
	[SYSPOWER_SLEEP_TYPE_STANDBY] = "standby\n",
	[SYSPOWER_SLEEP_TYPE_FREEZE] = "freeze\n",
	[SYSPOWER_SLEEP_TYPE_HIBERNATE] = "disk\n",
};

static inline int OPEN_RETRY(const char *path, int flags)
{
	int fd;

	do {
		fd = open(path, flags | O_SYNC);
	} while (fd == -1 && errno == EINTR);

	/* If required pseudo-file not present, operation is not supported */
	if (fd == -1 && errno == ENOENT)
		errno = ENOTSUP;

	return fd;
}

static inline int READ_RETRY(int fd, void *buf, size_t len)
{
	int ret;

	do {
		ret = read(fd, buf, len);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

static inline int WRITE_RETRY(int fd, const void *buf, size_t len)
{
	int ret;

	do {
		ret = write(fd, buf, len);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

static int __open_once(int *fd, const char *path, int flags)
{
	int file;

	if (*fd > 0)
		return 0;

	file = OPEN_RETRY(path, flags);
	if (file < 0)
		return file;

	*fd = file;
	return 0;
}

int syspower_rtc_wakealarm(unsigned int seconds, bool wait)
{
	struct rtc_time rtc_tm;
	unsigned long data;
	int ret;

	if ((ret = __open_once(&syspower.fd_rtc, path_rtc_dev, O_RDWR)))
		return ret;

	ret = ioctl(syspower.fd_rtc, RTC_RD_TIME, &rtc_tm);
	if (ret == -1)
		return -errno;

	rtc_tm.tm_sec += seconds;
	if (rtc_tm.tm_sec >= 60) {
		rtc_tm.tm_sec %= 60;
		rtc_tm.tm_min++;
	}
	if (rtc_tm.tm_min == 60) {
		rtc_tm.tm_min = 0;
		rtc_tm.tm_hour++;
	}
	if (rtc_tm.tm_hour == 24)
		rtc_tm.tm_hour = 0;

	ret = ioctl(syspower.fd_rtc, RTC_ALM_SET, &rtc_tm);
	if (ret == -1)
		return -errno;

	ioctl(syspower.fd_rtc, RTC_UIE_OFF, 0);
	ret = ioctl(syspower.fd_rtc, RTC_AIE_ON, 0);
	if (ret == -1)
		return -errno;

	if (wait)
		ret = READ_RETRY(syspower.fd_rtc, &data, sizeof(unsigned long));

	return ret;
}

int syspower_autosleep_enable(enum syspower_sleep_type type)
{
	int ret, len;

	if (type >= SYSPOWER_SLEEP_TYPE_MAX)
		return -EINVAL;

	len = strlen(sleep_state[type]) + 1;

	if ((ret = __open_once(&syspower.fd_autosleep, path_autosleep, O_WRONLY)))
		return ret;

	ret = WRITE_RETRY(syspower.fd_autosleep, sleep_state[type], len);
	if (ret != (int)len)
		return -errno;

	return 0;
}

int syspower_autosleep_disable(void)
{
	int ret, len;

	len = strlen("off") + 1;

	if ((ret = __open_once(&syspower.fd_autosleep, path_autosleep, O_WRONLY)))
		return ret;

	ret = WRITE_RETRY(syspower.fd_autosleep, "off", len);
	if (ret != (int)len)
		return -errno;

	return 0;
}

int syspower_suspend(enum syspower_sleep_type type)
{
	int ret;
	int len;

	if (type >= SYSPOWER_SLEEP_TYPE_MAX)
		return -EINVAL;

	len = strlen(sleep_state[type]) + 1;

	if ((ret = __open_once(&syspower.fd_state, path_state, O_RDWR)))
		return ret;

	ret = WRITE_RETRY(syspower.fd_state, sleep_state[type], len);
	if (ret != (int)len)
		return -errno;

	return 0;
}

int syspower_wake_lock(const char *name, unsigned int timeout_ms)
{
	char buf[128];
	size_t len;
	int ret;

	if ((ret = __open_once(&syspower.fd_lock, path_wake_lock, O_WRONLY)))
		return ret;

	if (timeout_ms)
		sprintf(buf, "%s %"PRIu64"\n", name, (uint64_t)timeout_ms * 1000 * 1000);
	else
		sprintf(buf, "%s\n", name);

	len = strlen(buf) + 1;

	ret = WRITE_RETRY(syspower.fd_lock, buf, len);
	if (ret != (int)len)
		return -errno;

	return 0;
}

int syspower_wakeup_reason(char *reason, size_t reason_len)
{
	int irq, ret, fd;
	char buf[128];

	fd = OPEN_RETRY(path_wakeup_irq, O_RDONLY);
	if (fd < 0)
		return -errno;

	ret = READ_RETRY(fd, buf, sizeof(buf));
	close(fd);
	if (ret < 0)
		return -errno;

	irq = strtoll(buf, NULL, 0);
	sprintf(buf, "/sys/kernel/irq/%d/actions", irq);

	fd = OPEN_RETRY(buf, O_RDONLY);
	if (fd < 0)
		return -errno;

	ret = READ_RETRY(fd, reason, reason_len); /* sanity check ? */
	if (ret > 0)
		reason[ret - 1] = '\0';

	close(fd);

	return irq;
}

int syspower_wake_unlock(const char *name)
{
	size_t len = strlen(name) + 1;
	int ret;

	if ((ret = __open_once(&syspower.fd_unlock, path_wake_unlock, O_RDWR)))
		return -errno;

	ret = WRITE_RETRY(syspower.fd_unlock, name, len);
	if (ret != (int)len)
		return -errno;

	return 0;
}

static int __read_attribute(char *value, const char *path, const char *name)
{
	char attr_path[PATH_MAX + 1];
	int fd, ret;

	snprintf(attr_path, sizeof(attr_path), "%s/%s", path, name);

	fd = OPEN_RETRY(attr_path, O_RDONLY);
	if (fd < 0)
		return -errno;

	ret = READ_RETRY(fd, value, 256);
	if (ret < 0) {
		close(fd);
		return -errno;
	}

	/* remove \n from attribute */
	value[ret - 1] = '\0';

	close(fd);

	return 0;
}

static int __write_attribute(char *value, const char *path, const char *name)
{
	char attr_path[PATH_MAX + 1];
	int fd, ret;

	snprintf(attr_path, sizeof(attr_path), "%s/%s", path, name);

	fd = OPEN_RETRY(attr_path, O_WRONLY);
	if (fd < 0)
		return -errno;

	ret = WRITE_RETRY(fd, value, strlen(value) + 1);
	if (ret < 0) {
		close(fd);
		return -errno;
	}

	close(fd);

	return 0;
}

static void __sysfs_devices_parse(char *path, void (*cb)(char *devpath))
{
	size_t len = strlen(path);
	struct dirent *dir;
	DIR *d;

	d = opendir(path);

	while ((dir = readdir(d))) {
		if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
			continue;

		path[len] = '\0';

		switch (dir->d_type) {
		case DT_DIR:
			path = strcat(path, "/");
			path = strcat(path, dir->d_name);
			__sysfs_devices_parse(path, cb);
			break;
		case DT_LNK:
			/* only report driven devices */
			if (!strcmp(dir->d_name, "driver"))
				cb(path);
			break;
		default:
			continue;
		}
	}

	closedir(d);
}

void __syspower_new_device(char *devpath)
{
	unsigned int i = 0;
	char value[256];
	char *devname;

	/* Filter devices without wakeup capability */
	if (__read_attribute(value, devpath, "power/wakeup"))
		return;

	/* register device to local cache */
	while (syspower.wakeup_cache[i]) i++;

	if (i >= WAKEDEV_COUNT)
		return;

	syspower.wakeup_cache[i] = malloc(sizeof(struct wakeup_source));
	if (!syspower.wakeup_cache[i])
		return;

	devname = basename(devpath);
	memcpy(syspower.wakeup_cache[i]->name, devname, strlen(devname) + 1);
	realpath(devpath, syspower.wakeup_cache[i]->devpath);
}

static void __wakeup_cache_update(void)
{
	char current_path[PATH_MAX + 1] = "/sys/devices";

	memset(syspower.wakeup_cache, 0, sizeof(syspower.wakeup_cache));
	__sysfs_devices_parse(current_path, __syspower_new_device);
}

static struct wakeup_source *__wakeup_source_lookup(const char *name)
{
	unsigned int i = 0;

	if (!syspower.wakeup_cache[0])
		__wakeup_cache_update(); /* TODO: smart cache update */

	if (!syspower.wakeup_cache[0])
		return NULL;

	while (syspower.wakeup_cache[i]) {
		if (!strcmp(name, syspower.wakeup_cache[i]->name))
			return syspower.wakeup_cache[i];
		i++;
	}

	return NULL;
}

const char *syspower_wakeup_get(unsigned int index)
{
	if (index >= WAKEDEV_COUNT)
		return NULL;

	if (!syspower.wakeup_cache[0])
		__wakeup_cache_update(); /* TODO: smart cache update */

	return syspower.wakeup_cache[index]->name;
}

int syspower_wakeup_enable(const char *wakeupname)
{
	struct wakeup_source *ws = __wakeup_source_lookup(wakeupname);

	if (!ws)
		return -ENOENT;

	return __write_attribute("enabled", ws->devpath, "power/wakeup");
}

int syspower_wakeup_disable(const char *wakeupname)
{
	struct wakeup_source *ws = __wakeup_source_lookup(wakeupname);

	if (!ws)
		return -ENOENT;

	return __write_attribute("disabled", ws->devpath, "power/wakeup");
}

bool syspower_wakeup_enabled(const char *wakeupname)
{
	struct wakeup_source *ws = __wakeup_source_lookup(wakeupname);
	char attr[128] = "";

	if (!ws)
		return -ENOENT;

	__read_attribute(attr, ws->devpath, "power/wakeup");
	if (!strncmp("enabled", attr, strlen("enabled")))
		return true;

	return false;
}

char *syspower_supply_get(unsigned int index)
{
	static struct dirent *files = NULL;
	char *supply =  NULL;
	unsigned int i = 0;

	DIR *dir = opendir(path_supply);
	if (!dir)
		return NULL;

	while ((files = readdir(dir)) != NULL) {
		if (!strcmp(files->d_name, ".") || !strcmp(files->d_name, ".."))
			continue;
		if (i++ == index) {
			supply = strdup(files->d_name);
			break;
		}
	}

	closedir(dir);

	return supply;
}

bool syspower_supply_present(const char *supplyname)
{
	char path[128];
	char attr[8] = "";

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "present");
	if (!strncmp("1", attr, strlen("1")))
		return true;

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "online");
	if (!strncmp("1", attr, strlen("1")))
		return true;

	return false;
}

enum syspower_supply_type syspower_supply_type(const char *supplyname)
{
	char path[128];
	char attr[128] = "";

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "type");

	if (!strncmp("Battery", attr, strlen("Battery")))
		return SYSPOWER_SUPPLY_TYPE_BATTERY;
	if (!strncmp("USB", attr, strlen("USB")))
		return SYSPOWER_SUPPLY_TYPE_USB;
	if (!strncmp("UPS", attr, strlen("UPS")))
		return SYSPOWER_SUPPLY_TYPE_UPS;
	if (!strncmp("Mains", attr, strlen("Mains")))
		return SYSPOWER_SUPPLY_TYPE_MAIN;
	if (!strncmp("Wireless", attr, strlen("Wireless")))
		return SYSPOWER_SUPPLY_TYPE_WIRELESS;
	if (!strncmp("BMS", attr, strlen("BMS")))
		return SYSPOWER_SUPPLY_TYPE_BMS;
	if (!strncmp("Wipower", attr, strlen("Wipower")))
		return SYSPOWER_SUPPLY_TYPE_WIPOWER;

	return SYSPOWER_SUPPLY_TYPE_UNKNOWN;
}

int syspower_supply_current(const char *supplyname,
			    enum syspower_supply_current current_type)
{
	char attr[10] = "0";
	char path[128];
	int ret, mA;

	if (current_type > SYSPOWER_SUPPLY_CURRENT_MAX)
		return -EINVAL;

	sprintf(path, "%s/%s", path_supply, supplyname);

	switch (current_type) {
	case SYSPOWER_SUPPLY_CURRENT_MAX:
		ret = __read_attribute(attr, path, "current_max");
		break;
	case SYSPOWER_SUPPLY_CURRENT_AVG:
		ret = __read_attribute(attr, path, "current_avg");
		break;
	case SYSPOWER_SUPPLY_CURRENT_NOW:
		ret = __read_attribute(attr, path, "current_now");
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	mA = atoi(attr) / 1000;
	if (mA < 0) mA = -mA; /* Discharging current ? */

	return mA;
}

int syspower_supply_voltage(const char *supplyname,
			    enum syspower_supply_voltage voltage_type)
{
	char attr[10] = "0";
	char path[128];
	int ret, mV;

	if (voltage_type > SYSPOWER_SUPPLY_VOLTAGE_MAX)
		return -EINVAL;

	sprintf(path, "%s/%s", path_supply, supplyname);

	switch (voltage_type) {
	case SYSPOWER_SUPPLY_VOLTAGE_AVG:
		ret = __read_attribute(attr, path, "voltage_avg");
		break;
	case SYSPOWER_SUPPLY_VOLTAGE_MAX:
		ret = __read_attribute(attr, path, "voltage_max");
		break;
	case SYSPOWER_SUPPLY_VOLTAGE_MIN:
		ret = __read_attribute(attr, path, "voltage_min");
		break;
	case SYSPOWER_SUPPLY_VOLTAGE_NOW:
		ret = __read_attribute(attr, path, "voltage_now");
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	mV = atoi(attr) / 1000;

	return mV;
}

static const char *supply_health[] = {
	"Unknown",
	"Good",
	"Overheat",
	"Dead",
	"Over voltage",
	"Unspecified failure",
	"Cold",
	"Watchdog timer expire",
	"Safety timer expire",
	"Over current",
	"Calibration required",
	"Warm",
	"Cool",
	"Hot",
	"No battery"
};

enum syspower_supply_health syspower_supply_health(const char *supplyname, char *health_str)
{
	char attr[30] = "0";
	unsigned int i = 0;
	char path[128];
	int ret;

	sprintf(path, "%s/%s", path_supply, supplyname);

	ret  = __read_attribute(attr, path, "health");
	if (ret) {
		strcpy(health_str, "Unknown");
		return SYSPOWER_SUPPLY_HEALTH_UNKWOWN;
	}

	if (health_str)
		strcpy(health_str, attr);

	while (i < ARRAY_SIZE(supply_health)) {
		if (!strncmp(supply_health[i], attr, strlen(supply_health[i])))
			return i;
		i++;
	}

	return -EINVAL;
}

uint8_t syspower_supply_capacity(const char *supplyname)
{
	char path[128];
	char attr[8] = "255";

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "capacity");

	return (uint8_t)atoi(attr);
}

uint8_t syspower_supply_capacity_min(const char *supplyname)
{
	char path[128];
	char attr[8] = "255";

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "capacity_alert_min");

	return (uint8_t)atoi(attr);
}

uint8_t syspower_supply_capacity_max(const char *supplyname)
{
	char path[128];
	char attr[8] = "255";

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "capacity_alert_max");

	return (uint8_t)atoi(attr);
}

enum syspower_supply_status syspower_supply_status(const char *supplyname)
{
	char attr[32] = "";
	char path[128];

	sprintf(path, "%s/%s", path_supply, supplyname);
	__read_attribute(attr, path, "status");

	if (!strncmp("Charging", attr, strlen("USB")))
		return SYSPOWER_BATTERY_STATUS_CHARGING;
	if (!strncmp("Discharging", attr, strlen("UPS")))
		return SYSPOWER_BATTERY_STATUS_DISCHARGING;
	if (!strncmp("Not charging", attr, strlen("Mains")))
		return SYSPOWER_BATTERY_STATUS_NOTCHARGING;
	if (!strncmp("Full", attr, strlen("Wireless")))
		return SYSPOWER_BATTERY_STATUS_FULL;
	else
		return SYSPOWER_BATTERY_STATUS_UNKWOWN;
}

int syspower_supply_get_monitorfd(void)
{
	struct udev *udev;

	if (udevmon) {
		udev = udev_ref(udev_monitor_get_udev(udevmon));
		udevmon = udev_monitor_ref(udevmon);
	} else {
		udev = udev_new();
		udevmon = udev_monitor_new_from_netlink(udev, "kernel");
		udev_monitor_filter_add_match_subsystem_devtype(udevmon, "power_supply", NULL);
		udev_monitor_enable_receiving(udevmon);
	}

	return udev_monitor_get_fd(udevmon);
}

int syspower_supply_read_monitorfd(int fd, char *supplyname, size_t maxlen)
{
	struct udev_device *dev;

	if (fd < 0 || !udevmon)
		return -EINVAL;

	dev = udev_monitor_receive_device(udevmon);
	if (!dev)
		return -errno;

	if (supplyname)
		strncpy(supplyname, udev_device_get_sysname(dev), maxlen);

	udev_device_unref(dev);

	return 0;
}

void syspower_supply_put_monitorfd(int fd)
{
	if (fd < 0)
		return;

	udev_unref(udev_monitor_get_udev(udevmon));
	udevmon = udev_monitor_unref(udevmon);
}
