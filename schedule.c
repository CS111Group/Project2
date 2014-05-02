#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>
#include "kernel/proc.h" /* for queue constants */

PRIVATE timer_t sched_timer;
PRIVATE unsigned balance_timeout;

/* CHANGE START */
PRIVATE struct schedproc *temp;
PRIVATE int nTickets = 0;
/* CHANGE END */

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */

FORWARD _PROTOTYPE(int schedule_process, (struct schedproc * rmp));
FORWARD _PROTOTYPE(void balance_queues, (struct timer *tp));

#define DEFAULT_USER_TIME_SLICE 200

/*===========================================================================*
 *				play_lottery				     *
 *===========================================================================*/
PRIVATE void play_lottery(void)
{
/* call this in balance_queues? */
	struct schedproc *rmp;
    int winning_ticket; 
    int proc_nr;
    int rv;
    
    /*printf("PLAYING LOTTERY.\n");*/
    srandom(time(NULL));
    winning_ticket = random()%nTickets;
    /*printf("Playing lottery. winning_ticket: %d\n", winning_ticket);*/
    for(proc_nr = 0, rmp = schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++){
        if(rmp->flags & IN_USE) {
            if((winning_ticket-=rmp->tickets) < 0){
                schedule_process(rmp);
                temp = rmp; 
                /*printf("Temp priority: %d\n", temp->priority);*/
            }
        }
    }
    
 /* 
  * Randomly choose a number between 0 and nTickets-1 to "play" the lottery, resource 
  * goes to whichever process "wins" the lottery
  */
}
 
/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

PUBLIC int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, winner,proc_nr_n;
  int loop;
    
	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}
    
	rmp = &schedproc[proc_nr_n];
	
  /* CHANGE START */
  if (rmp->priority == (MIN_USER_Q -1)){
    /*for (loop = rmp->priority; loop < MIN_USER_Q; loop++){*/
      printf("do_noquantum, lowering winner: priority %d\n", MIN_USER_Q);
      rmp->priority += 1; /* lower priority */
      if(rmp->tickets > 1){
      	rmp->tickets -=1; /*decrease number of tickets*/
      	printf("Winner used up quantum and ticket count decreased from %d to %d", rmp->tickets -1, rmp->tickets);
      }
    /*}*/
    /* if (rmp->tickets >= 2) rmp->tickets -= 1; dynamic tickets*/
  }else if (rmp->priority == MIN_USER_Q) {
    printf("@@@@@ WE HAVE A BLOCKED WINNER: Loser priority %d, Blocked priority %d\n", rmp->priority, temp->priority);
    temp->priority = MIN_USER_Q;
    if(temp->tickets < 100){
      temp->tickets += 1;
      printf("Winner Blocks and ticket count increased from %d to %d", temp->tickets -1, temp->tickets);
    }
    if ((winner = sys_schedule(temp->endpoint, temp->priority,temp->time_slice)) != OK) {
      printf("SCHED: An error occurred when trying to schedule %d: %d\n", temp->endpoint, winner);
      }
      printf("@@@@@Winner MOVED: Loser priority %d, new winner priority %d\n", rmp->priority, temp->priority);
  }
    
	if ((rv = schedule_process(rmp)) != OK) {
		return rv;
	}
	return OK;
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
PUBLIC int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}
	rmp = &schedproc[proc_nr_n];
	rmp->flags = 0; /*&= ~IN_USE;*/
  nTickets -= rmp->tickets;

  /*printf("do_stop_scheduling, total tickets : %d\n", nTickets);*/

	return OK;
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
PUBLIC int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n, nice;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n))
			!= OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];

	/* Populate process slot */
	rmp->endpoint     = m_ptr->SCHEDULING_ENDPOINT;
	rmp->parent       = m_ptr->SCHEDULING_PARENT;
	rmp->max_priority = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quantum and priority are set explicitly rather than inherited 
		 * from the parent */
            
        /* CHANGE START */
		rmp->priority   = rmp->max_priority;
        printf("@@@@@@@@@@@@@@@@@@@@@ MAX rmp->max_priority: %d, MIN_USER_Q: %d, USER_Q: %d\n",rmp->max_priority, MIN_USER_Q,USER_Q);
        /* CHANGE END */
       
            
		rmp->time_slice = (unsigned) m_ptr->SCHEDULING_QUANTUM;
     
        /*printf("In do_start_scheduling. Total number of tickets: %d\n", nTickets);*/
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = sched_isokendpt(m_ptr->SCHEDULING_PARENT,
				&parent_nr_n)) != OK)
			return rv;
		/*rmp->priority = schedproc[parent_nr_n].priority;*/
		rmp->priority = MIN_USER_Q;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;
    rmp->tickets = 20; /*basic lottery scheduling*/
    nTickets += rmp->tickets;
        /*printf("In do_start_scheduling. Total number of tickets: %d\n", nTickets);*/
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
    /*printf("Scheduling new process in do_start\n");*/
	if ((rv = schedule_process(rmp)) != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */

	m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	return OK;
}

/*===========================================================================*
 *				do_nice					     *
 *===========================================================================*/
PUBLIC int do_nice(message *m_ptr)
{
	struct schedproc *rmp;
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q, old_tickets;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	new_q = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q       = rmp->priority;
	old_max_q   = rmp->max_priority;
    old_tickets = rmp->tickets;
	/* Update the proc entry and reschedule the process */
	rmp->max_priority = rmp->priority = new_q;
    
    
    
	if ((rv = schedule_process(rmp)) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->priority     = old_q;
		rmp->max_priority = old_max_q;
        rmp->tickets      = old_tickets;
	}

	return rv;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
PRIVATE int schedule_process(struct schedproc * rmp)
{
	int rv;

	if ((rv = sys_schedule(rmp->endpoint, rmp->priority,
			rmp->time_slice)) != OK) {
		printf("SCHED: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, rv);
	}

	return rv;
}


/*===========================================================================*
 *				start_scheduling			     *
 *===========================================================================*/

PUBLIC void init_scheduling(void)
{
	balance_timeout = BALANCE_TIMEOUT * sys_hz();
	init_timer(&sched_timer);
	set_timer(&sched_timer, balance_timeout, balance_queues, 0);
}

/*===========================================================================*
 *				balance_queues				     *
 *===========================================================================*/

/* This function in called every 100 ticks to rebalance the queues. The current
 * scheduler bumps processes down one priority when ever they run out of
 * quantum. This function will find all proccesses that have been bumped down,
 * and pulls them back up. This default policy will soon be changed.
 */
PRIVATE void balance_queues(struct timer *tp)
{
	struct schedproc *rmp;
	int proc_nr;
	int rv;
  play_lottery();
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE) {
			if (rmp->priority > rmp->max_priority) {
				rmp->priority -= 1; /* increase priority */
                /* rmp->tickets += 1; dynamic tickets */
				/*schedule_process(rmp);*/
			}
		}
	}
    
	set_timer(&sched_timer, balance_timeout, balance_queues, 0);
}

 /*===========================================================================*
 *				dynamic_tickets			     *
 *===========================================================================*/
PRIVATE void dynamic_tickets(void)
{
/*professor: called by do_nice*/
 
}
