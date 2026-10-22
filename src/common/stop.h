#pragma once
void alligator_stop(char *initiator, int code);
int alligator_stop_requested(void);
/* Unit tests: toggle the stop flag without probes_halt / uv_stop. */
void alligator_stop_requested_set_for_test(int v);
void alligator_shutdown_after_loop(void);
