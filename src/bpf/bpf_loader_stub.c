#include "bpf/bpf.h"

int bpf_loader_sync(void)
{
	return -1;
}

void bpf_loader_shutdown(void)
{
}

void bpf_loader_poll(void)
{
}

int bpf_loader_attached(void)
{
	return 0;
}

int bpf_loader_selftest(void)
{
	return 0;
}
