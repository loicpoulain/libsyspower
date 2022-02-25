#include <syspower.h>
#include <stdio.h>

void usage(void)
{
	printf("Usage: syspowernap [timeout]\n");
}

int main(int argc, char *argv[])
{
	char wakeup_reason[128];
	int ret;

	if (argc == 2) {
		int seconds = atoi(argv[1]);

		if (!seconds) {
			usage();
			return 1;
		}

		ret = syspower_rtc_wakealarm(seconds, false);
		if (ret) {
			perror("Unable to configure RTC alarm\n");
			return ret;
		}

		printf("Sleeping for %u seconds!\n", seconds);
	} else if (argc == 1) {
		printf("Sleeping now...\n");
	} else {
		usage();
		return 1;
	}

	ret = syspower_suspend(SYSPOWER_SLEEP_TYPE_STANDBY);
	if (ret)
		ret = syspower_suspend(SYSPOWER_SLEEP_TYPE_MEM);
	if (ret) {
		perror("Unable to sleep\n");
		return ret;
	}

	/* Waking now ! */
	ret = syspower_wakeup_reason(wakeup_reason, sizeof(wakeup_reason));
	if (ret >= 0)
		printf("Wakeup! (%s/irq:%d)\n", wakeup_reason, ret);
	else
		printf("Wakeup! (unkown reason)\n");

	return 0;
}
