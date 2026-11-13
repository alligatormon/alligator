#include "bpf/bpf.h"
#include "main.h"

aconf *ac;

int main(void)
{
	return bpf_loader_selftest();
}
