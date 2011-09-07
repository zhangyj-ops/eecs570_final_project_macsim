/**********************************************************************************************
 * File         : schedule_io.cc
 * Author       : Hyesoon Kim
 * Date         : 1/1/2008 
 * SVN          : $Id: main.cc,v 1.26 2008-09-21 00:02:54 kacear Exp $:
 * Description  : inorder scheduler
 *********************************************************************************************/

/*
 * Summary: Inorder Scheduler
 * 
 *   schedule uops
 *   allocation queue to schedule queue
 *   once uops are moved into schedule queue, execute uops in reorder buffer
 */


#include <cstdlib>

#include "core.h"
#include "exec.h"
#include "memory.h"
#include "pqueue.h"
#include "rob_smc.h"
#include "schedule_io.h"

#include "knob.h"
#include "debug_macros.h"
#include "statistics.h"

#include "all_knobs.h"

#define DEBUG(args...)   _DEBUG(*m_simBase->m_knobs->KNOB_DEBUG_SCHEDULE_STAGE, ## args) 


// schedule_io_c constructor
schedule_io_c::schedule_io_c(int core_id, pqueue_c<int>** alloc_q, rob_c* rob, 
    exec_c* exec, Unit_Type unit_type, frontend_c* frontend, macsim_c* simBase)  
: schedule_c(exec, core_id, unit_type, frontend, alloc_q, simBase)
{
  m_rob                      = rob;
  m_next_inorder_to_schedule = 0;
  m_simBase                  = simBase;
}


// schedule_io_c destructor
schedule_io_c::~schedule_io_c(void)
{
}


// main execution routine : In every cycle, schedule uops from rob
void schedule_io_c::run_a_cycle(void)
{
  // Check if the schedule is running
  if (!is_running())
    return;

  m_cur_core_cycle =  m_simBase->m_core_cycle[m_core_id];

  // clear execution ports
  m_exec->clear_ports();

  int count = 0;
  if (m_num_in_sched) {
    // iterate until we schedule knob width number of uops
    while ((!m_knob_sched_to_width || count < m_knob_width)) {
      uop_c *uop = (*m_rob)[m_next_inorder_to_schedule];
      // uop invalid or not in scheduler yet
      if (uop == NULL || !uop->m_in_scheduler)
        break;
   
      SCHED_FAIL_TYPE sched_fail_reason;
      
      // schedule an uop
      if (!uop_schedule(m_next_inorder_to_schedule, &sched_fail_reason)) {
        STAT_CORE_EVENT(m_core_id, SCHED_FAILED_REASON_SUCCESS +
            MIN2((int)sched_fail_reason, 2));
        break;
      }
 
      STAT_CORE_EVENT(m_core_id, SCHED_FAILED_REASON_SUCCESS);  

      // increment scheduled uop counter
      ++count;

      // find the next entry to schedule
      int dec_index = m_rob->dec_index(m_next_inorder_to_schedule);
      ASSERT(m_next_inorder_to_schedule == m_rob->front_rob() ||
             (*m_rob)[dec_index]->m_sched_cycle);
      m_next_inorder_to_schedule = m_rob->inc_index(m_next_inorder_to_schedule);
    }

    // no uop has been scheduled
    if (count == 0) 
      STAT_CORE_EVENT(m_core_id, NUM_NO_SCHED_CYCLE);
  }
  else {
    // no uops in the scheduler
    STAT_CORE_EVENT(m_core_id, NUM_SCHED_IDLE_CYCLE);
  }


  // advance entries from alloc queue to schedule queue
  for (int ii = 0; ii < max_ALLOCQ; ++ii) 
    advance(ii);
}

