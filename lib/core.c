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

#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <linux/limits.h>

static const char path_autosleep[] = "/sys/power/autosleep";
static const char path_wake_unlock[] = "/sys/power/wake_unlock";
static const char path_wake_lock[] = "/sys/power/wake_lock";
static const char path_state[] = "/sys/power/state";
static const char path_wakeup_irq[] = "/sys/power/pm_wakeup_irq";
static const char path_rtc_dev[] = "/dev/rtc";

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
	int fd_wakeup;
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
		fd = open(path, flags);
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
	if (ret != len)
		return -errno;

	return 0;
}

int syspower_autosleep_disable(void)
{
	int ret, len;

	len = strlen("off") + 1;

	if ((ret = __open_once(&syspower.fd_state, path_autosleep, O_WRONLY)))
		return ret;

	ret = WRITE_RETRY(syspower.fd_state, "off", len);
	if (ret != len)
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
	if (ret != len)
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

	if ((ret = __open_once(&syspower.fd_wakeup, path_wakeup_irq, O_RDONLY)))
		return ret;

	ret = READ_RETRY(syspower.fd_wakeup, buf, sizeof(buf));
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
	char attr[128];

	if (!ws)
		return -ENOENT;

	__read_attribute(attr, ws->devpath, "power/wakeup");
	if (!strncmp("enabled", attr, strlen("enabled")))
		return true;

	return false;
}
