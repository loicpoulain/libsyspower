/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libsyspower.
 *
 * Copyright (C) 2022 Loic Poulain <loic.poulain@linaro.org>
 */

#ifndef __LIBSYSPOWER_SYSPOWER_H__
#define __LIBSYSPOWER_SYSPOWER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

enum syspower_sleep_type {
	SYSPOWER_SLEEP_TYPE_FREEZE,
	SYSPOWER_SLEEP_TYPE_STANDBY,
	SYSPOWER_SLEEP_TYPE_MEM,
	SYSPOWER_SLEEP_TYPE_HIBERNATE,
	SYSPOWER_SLEEP_TYPE_MAX
};

/**
 * @brief Enable system autosleep.
 * @param type Sleep type (cf syspower_sleep_type enum)
 * @return 0 on success, negative value on error.
 */
int syspower_autosleep_enable(enum syspower_sleep_type type);

/**
 * @brief Disable system autosleep.
 * @return 0 on success, negative value on error.
 */
int syspower_autosleep_disable(void);

/**
 * @brief Create a new wake-lock, preventing system to autosleep.
 * @param name Name of the lock.
 * @param timeout_ms Auto release the lock after timeou_ms (if different from 0).
 * @return 0 on success, negative value on error.
 */
int syspower_wake_lock(const char *name, unsigned int timeout_ms);

/**
 * @brief Release a wake-lock.
 * @param name Name of the lock.
 * @return 0 on success, negative value on error.
 */
int syspower_wake_unlock(const char *name);

/**
 * @brief Enter system wide suspend state.
 * @param type Sleep type (cf syspower_sleep_type enum)
 * @return 0 on success, negative value on error.
 */
int syspower_suspend(enum syspower_sleep_type type);

/**
 * @brief Configure RTC wake alarm.
 * @param seconds Schedule RTC wakeup in seconds from now, or 0 to disable.
 * @param wait Block until alarm.
 * @return 0 on success, negative value on error.
 */
int syspower_rtc_wakealarm(unsigned int seconds, bool wait);

/**
 * @brief Retrieve latest wakeup reason (reason string and interrupt index).
 * @param reason Pointer to the reason buffer to write in (usually filled with interrupt name).
 * @param reason_len The reason string buffer size.
 * @return IRQ index on success, negative value on error.
 */
int syspower_wakeup_reason(char *reason, size_t reason_len);

/**
 * @brief Retrieve wakeup capable device name by index.
 * @param index index of the device.
 * @return device name.
 */
const char *syspower_wakeup_get(unsigned int index);

/**
 * @brief Check if wakeup is enabled for the given device.
 * @param device name.
 * @return true if wakeup is enabled, false otherwise.
 */
bool syspower_wakeup_enabled(const char *devname);

/**
 * @brief Enable wakeup for the given device.
 * @param device name.
 * @return 0 on success, negative value on error.
 */
int syspower_wakeup_enable(const char *devname);

/**
 * @brief Disable wakeup for the given device.
 * @param device name.
 * @return 0 on success, negative value on error.
 */
int syspower_wakeup_disable(const char *devname);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBSYSPOWER_SYSPOWER_H__ */
