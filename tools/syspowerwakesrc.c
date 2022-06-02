#include <syspower.h>
#include <stdio.h>
#include <string.h>

void usage(void)
{
	printf("Usage: syspowerwakeup [list|enable <devname>|disable <devname>]\n");
}

void list_wakeup(void)
{
	unsigned int i = 0;
	const char *name;

	printf("%-30s %s\n", "Device", "HW wakeup");
	while ((name = syspower_wakeup_get(i++))) {
		bool enabled = syspower_wakeup_enabled(name);
		printf("|- %-27s [%s]\n", name, enabled ? "enabled" : "disabled");
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;

	if (argc < 2)
		usage();

	if (argc == 2) {
		if (!strcmp("list", argv[1]))
			list_wakeup();
	} else if (argc == 3) {
		if (!strcmp("enable", argv[1]))
			ret = syspower_wakeup_enable(argv[2]);
		else if (!strcmp("disable", argv[1]))
			ret = syspower_wakeup_disable(argv[2]);
		else
			usage();
	} else {
		usage();
	}

	if (ret)
		fprintf(stderr, "error: %s\n", strerror(ret));

	return 0;
}
