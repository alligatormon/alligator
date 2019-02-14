#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
void signal_handler_sigusr1(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
}
void signal_handler_sigusr2(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
}
void signal_handler_sighup(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
}
void signal_handler_sigquit(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
	uv_signal_stop(handle);
}
void signal_handler_sigterm(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
	exit(0);
}
void signal_handler_sigint(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
	exit(0);
}
void signal_handler_sigtrap(uv_signal_t *handle, int signum)
{
	(void)handle;
	printf("Signal received: %d\n", signum);
	exit(0);
}

void signal_listen()
{
	uv_signal_t *sig1 = malloc(sizeof(*sig1));
	uv_signal_init(uv_default_loop(), sig1);
	uv_signal_start(sig1, signal_handler_sigusr1, SIGUSR1);

	uv_signal_t *sig2 = malloc(sizeof(*sig2));
	uv_signal_init(uv_default_loop(), sig2);
	uv_signal_start(sig2, signal_handler_sighup, SIGHUP);

	uv_signal_t *sig3 = malloc(sizeof(*sig3));
	uv_signal_init(uv_default_loop(), sig3);
	uv_signal_start(sig3, signal_handler_sigquit, SIGQUIT);

	uv_signal_t *sig4 = malloc(sizeof(*sig4));
	uv_signal_init(uv_default_loop(), sig4);
	uv_signal_start(sig4, signal_handler_sigterm, SIGTERM);

	uv_signal_t *sig5 = malloc(sizeof(*sig5));
	uv_signal_init(uv_default_loop(), sig5);
	uv_signal_start(sig5, signal_handler_sigint, SIGINT);

	uv_signal_t *sig6 = malloc(sizeof(*sig6));
	uv_signal_init(uv_default_loop(), sig6);
	uv_signal_start(sig6, signal_handler_sigusr2, SIGUSR2);

	uv_signal_t *sig7 = malloc(sizeof(*sig7));
	uv_signal_init(uv_default_loop(), sig7);
	uv_signal_start(sig7, signal_handler_sigtrap, SIGTRAP);
}
