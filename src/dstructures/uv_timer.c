#include "dstructures/uv_timer.h"
#include "common/rtime.h"
#define ALLIGATOR_TIMER_SECONDS_EXPIRE 20
tommy_list *alligator_timer_list_init()
{
	tommy_list *altimer = malloc(sizeof(*altimer));
	tommy_list_init(altimer);

	return altimer;
}

uv_timer_t *alligator_timer_get(tommy_list *altimer)
{
	tommy_node *tnode = tommy_list_tail(altimer);
	if (!tnode)
	{
		return calloc(1, sizeof(uv_timer_t));
	}
	alligator_timer *atimer = tnode->data;

	tommy_list_remove_existing(altimer, &atimer->node);

	uv_timer_t *timer = atimer->timer;
	free(atimer);

	return timer;
}

int alligator_timer_push(tommy_list *altimer, uv_timer_t *timer)
{
	if (!altimer)
		return 0;

	if (!timer)
		return 0;

	alligator_timer *atimer = calloc(1, sizeof(*atimer));

	atimer->timer = timer;
        r_time time = setrtime();
	atimer->ttl = time.sec + ALLIGATOR_TIMER_SECONDS_EXPIRE;

	tommy_list_insert_tail(altimer, &atimer->node, atimer);

	return 1;
}

void alligator_timer_foreach_free(void *arg, void *obj)
{
	alligator_timer *atimer = obj;
	tommy_list *altimer = atimer->altimer;

	uint64_t sec = INT64_MAX;
	if (arg)
		sec = *(uint64_t*)arg;

	if (atimer->ttl >= sec)
	{
		if (sec == INT64_MAX)
			tommy_list_remove_existing(altimer, &atimer->node);

		free(atimer->timer);
		free(atimer);
	}
}

void alligator_timer_periodical_free(tommy_list *altimer)
{
        r_time time = setrtime();
	time.sec += ALLIGATOR_TIMER_SECONDS_EXPIRE;
	tommy_list_foreach_arg (altimer, alligator_timer_foreach_free, &time.sec);
}

void alligator_timer_full_free(tommy_list *altimer)
{
	tommy_list_foreach_arg (altimer, alligator_timer_foreach_free, NULL);
}
