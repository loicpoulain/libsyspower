#include <syspower.h>
#include <stdio.h>
#include <poll.h>
#include <string.h>

void usage(void)
{
	printf("Usage: syspowersupply <option>\n"
	"  print   [supplyname]       - Print supply info\n"
	"  monitor [supplyname]       - Monitor supply events\n");

	exit(1);
}

char *supply_status[] = {
	[SYSPOWER_BATTERY_STATUS_UNKWOWN] = "Unknown",
	[SYSPOWER_BATTERY_STATUS_CHARGING] = "Charging",
	[SYSPOWER_BATTERY_STATUS_FULL] = "Full",
	[SYSPOWER_BATTERY_STATUS_NOTCHARGING] = "NotCharging",
	[SYSPOWER_BATTERY_STATUS_DISCHARGING] = "Discharging",
};

static void print_supply_info(const char *supply)
{
	int mA;

	printf("=== %s ===\n", supply);

	switch (syspower_supply_type(supply)) {
	case SYSPOWER_SUPPLY_TYPE_USB:
		printf("type: USB\n");
		break;
	case SYSPOWER_SUPPLY_TYPE_BATTERY:
		printf("type: BATTERY\n");
		printf("capacity: %u%%\n", syspower_supply_capacity(supply));
		printf("status: %s\n", supply_status[syspower_supply_status(supply)]);
		break;
	case SYSPOWER_SUPPLY_TYPE_MAIN:
		printf("type: MAIN\n");
		break;
	case SYSPOWER_SUPPLY_TYPE_UNKNOWN:
	default:
		printf("type: UNKOWN\n");
		break;
	}

	mA = syspower_supply_current(supply, SYSPOWER_SUPPLY_CURRENT_MAX);
	if (mA >= 0)
		printf("current-limit: %dmA\n", mA);

	mA = syspower_supply_current(supply, SYSPOWER_SUPPLY_CURRENT_AVG);
	if (mA >= 0)
		printf("current-avg: %dmA\n", mA);

	mA = syspower_supply_current(supply, SYSPOWER_SUPPLY_CURRENT_NOW);
	if (mA >= 0)
		printf("current-now: %dmA\n", mA);

	printf("connected: %s\n", syspower_supply_present(supply) ? "yes" : "no");
}

static void print(const char *supplyfilt)
{
	if (supplyfilt) {
		print_supply_info(supplyfilt);
	} else {
		unsigned int i = 0;
		char *s;

		while ((s = syspower_supply_get(i++))) {
			print_supply_info(s);
			free(s);
		}
	}
}

static int monitor(const char *supplyfilt)
{
	char supply[256] = {};
	struct pollfd fds[1];
	int fd;

	print(supplyfilt);

	fd = syspower_supply_get_monitorfd();
	if (fd < 0) {
		perror("Unable to get monitorfd");
		return 1;
	}

	fds[0].fd = fd;
	fds[0].events = POLLIN;

	while (poll(fds, 1, -1) > 0) {
		while (!syspower_supply_read_monitorfd(fd, supply, sizeof(supply))) {
			if (!supplyfilt || !strcmp(supply, supplyfilt))
				print_supply_info(supply);
		}

	}

	syspower_supply_put_monitorfd(fd);

	return 0;
}

int main(int argc, char *argv[])
{
	char *supply = NULL;

	if (argc < 2) {
		usage();
		return 1;
	}

	if (argc >= 3) {
		supply = argv[2];
	}

	if (!strcmp("print", argv[1]))
		print(supply);
	else if (!strcmp("monitor", argv[1]))
		monitor(supply);
	else
		usage();

	return 0;
}
