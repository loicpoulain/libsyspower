#include <syspower.h>
#include <stdio.h>
#include <string.h>

void usage(void)
{
	printf("Usage: syspowerwakeup <option>\n"
	"  list                      - List all wakeup devices\n"
	"  enable <devname|\"all\">    - Enable wakeup for the specified device\n"
	"  disable <devname|\"all\">   - Disable wakeup for the specified device\n");

	exit(1);
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

int disable_wakeup(const char *name)
{
	unsigned int i = 0;
	int ret = 0;

	if (strcmp("all", name)) {
		ret = syspower_wakeup_disable(name);
		if (ret)
			fprintf(stderr, "Failed to enable %s\n", name);
		goto done;
	}

	/* Disable all */
	while ((name = syspower_wakeup_get(i++))) {
		if (syspower_wakeup_disable(name)) {
			fprintf(stderr, "Failed to disable %s\n", name);
			ret = 1;
		}
	}

done:
	return ret ? 1 : 0;
}

int enable_wakeup(const char *name)
{
	unsigned int i = 0;
	int ret = 0;

	if (strcmp("all", name)) {
		ret = syspower_wakeup_enable(name);
		if (ret)
			fprintf(stderr, "Failed to enable %s\n", name);
		goto done;
	}

	/* Enables all */
	while ((name = syspower_wakeup_get(i++))) {
		if (syspower_wakeup_enable(name)) {
			fprintf(stderr, "Failed to enable %s\n", name);
			ret = 1;
		}
	}

done:
	return ret ? 1 : 0;
}


int main(int argc, char *argv[])
{
	int ret = 0;

	if (argc < 2)
		usage();

	if (argc == 2) {
		if (!strcmp("list", argv[1]))
			list_wakeup();
		else
			usage();
	} else if (argc >= 3) {
		if (!strcmp("enable", argv[1])) {
			for (int i = 2; i < argc; i++)
				ret |= enable_wakeup(argv[i]);
		} else if (!strcmp("disable", argv[1])) {
			for (int i = 2; i < argc; i++)
				ret |= disable_wakeup(argv[i]);
		} else {
			usage();
		}
	} else {
		usage();
	}

	if (ret)
		fprintf(stderr, "error: %s\n", strerror(ret));

	return 0;
}
