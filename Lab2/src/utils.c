#include "stdio.h"

void logerr(const char *err) {
	fprintf(stderr, "%s\n", err);
}
