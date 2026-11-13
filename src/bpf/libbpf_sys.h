#pragma once

/*
 * src/bpf/bpf.h is Alligator's own header and sits on -I, so a plain
 * #include <bpf/bpf.h> never reaches libbpf. Resume the search after that
 * directory to get the syscall helpers (bpf_map_lookup_elem and friends).
 */
#include_next <bpf/bpf.h>
