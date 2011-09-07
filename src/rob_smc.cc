/**********************************************************************************************
 * File         : smc_rob.h
 * Author       : Nagesh BL 
 * Date         : 03/07/2010 
 * CVS          : $Id: rob.h,v 1.2 2008-09-10 01:31:00 kacear Exp $:
 * Description  : rob structure for gpu
                  origial author: Jared W. Stark  imported from 
 *********************************************************************************************/


/**********************************************************************************************
 * Basically, smc_rob is check of conventional robs since we need to allocate 
 * rob for each thread
 *
 * Using a pool, recycle reorder buffers
 *
 * Retirement logic should be differently handled (see get_n_uops_in_ready_order and retire.cc)
 *********************************************************************************************/


#include "global_types.h"
#include "global_defs.h"
#include "rob_smc.h"
#include "rob.h"
#include "uop.h"
#include "utils.h"
#include "core.h"

#include "knob.h"
#include "statistics.h"

#include "all_knobs.h"

// smc_rob_c constructor
smc_rob_c::smc_rob_c(Unit_Type type, int core_id, macsim_c* simBase)
{
  m_unit_type = type;
  m_simBase = simBase;

  // configuration
  switch (m_unit_type) {
    case UNIT_SMALL:
      m_knob_num_threads = *m_simBase->m_knobs->KNOB_MAX_THREADS_PER_CORE;
      break;

    case UNIT_MEDIUM:
      m_knob_num_threads = *m_simBase->m_knobs->KNOB_MAX_THREADS_PER_MEDIUM_CORE;
      break;

    case UNIT_LARGE:
      m_knob_num_threads = *m_simBase->m_knobs->KNOB_MAX_THREADS_PER_LARGE_CORE;
      break;
  }

  m_thread_robs = new rob_c*[m_knob_num_threads];
  ASSERT(m_thread_robs);

  for (int i = 0; i < m_knob_num_threads; ++i) {
    m_thread_robs[i] = new rob_c(m_unit_type, m_simBase);
    ASSERT(m_thread_robs[i]);

    m_free_list.push_back(i);
  }

  m_core_id = core_id;
}


// smc_rob_c destructor
smc_rob_c::~smc_rob_c()
{
  for (int i = 0; i < m_knob_num_threads; ++i) {
    delete m_thread_robs[i];
  }
  delete [] m_thread_robs; 
}


// get one reorder buffer for a thread
//   if not previously assigned, allocate a new one
//   else return assigned reorder buffer
rob_c* smc_rob_c::get_thread_rob(int thread_id) 
{
  ASSERT(thread_id >= 0);

  auto itr = m_thread_to_rob_map.find(thread_id);
  auto end = m_thread_to_rob_map.end();

  // previously assigned
  if (itr != end) {
    return m_thread_robs[itr->second];
  }
  // find a new one
  else {
    ASSERTM(0, "core_id:%d thread_id:%d\n", m_core_id, thread_id);
    itr = m_thread_to_rob_map.begin();
    int count = 0;
    while (itr != end) {
      if (itr->first != count) {
        m_thread_to_rob_map[thread_id] = count;
        return m_thread_robs[count];
      }
      ++itr;
      ++count;
    }

    m_thread_to_rob_map[thread_id] = count;

    return m_thread_robs[count];
  }
}


// get an index to rob chunks for a thread
int smc_rob_c::get_thread_rob_id(int thread_id) 
{
  ASSERT(thread_id >= 0);

  auto itr = m_thread_to_rob_map.find(thread_id);
  auto end = m_thread_to_rob_map.end();
  if (itr != end) {
    return itr->second;
  }
  else {
    return -1;
  }
}


int smc_rob_c::reserve_rob(int thread_id) 
{
  ASSERT(thread_id >= 0);
  assert(m_thread_to_rob_map.find(thread_id) == m_thread_to_rob_map.end());

//  if (m_thread_to_rob_map.find(thread_id) == m_thread_to_rob_map.end()) {
    ASSERT(m_free_list.size());
    int index = m_free_list.front();
    m_free_list.pop_front();

    m_thread_to_rob_map.insert(std::pair<int, int>(thread_id, index));
    m_thread_robs[index]->reinit();
    return index;
//  }

//  return -1;
}


// deallocate reorder buffer when a thread is terminated 
void smc_rob_c::free_rob(int thread_id) 
{
  ASSERT(thread_id >= 0);

//  auto itr = m_thread_to_rob_map.find(thread_id);
//  auto end = m_thread_to_rob_map.end();
  assert(m_thread_to_rob_map.find(thread_id) != m_thread_to_rob_map.end());

//  if (itr != end) {
    m_free_list.push_back(m_thread_to_rob_map[thread_id]);
    m_thread_to_rob_map.erase(thread_id);
//  }
}


// uop sort function used in get_n_uops_in_ready_order
bool sort_uops(uop_c *a, uop_c *b) 
{
  if (a->m_done_cycle < b->m_done_cycle) {
    return true;
  }
  else if (a->m_done_cycle == b->m_done_cycle) {
    if (a->m_sched_cycle < b->m_sched_cycle) {
      return true;
    }
    else if (a->m_sched_cycle == b->m_sched_cycle) {
      //aon this cannot happen because we are using width of 1
      return a->m_thread_id < b->m_thread_id;
    }
  }
  return false;
}


// get a list of retireable uops from multiple threads
// called by retire stage
vector<uop_c *>* smc_rob_c::get_n_uops_in_ready_order(int n, Counter core_cycle) 
{
//  m_uop_list.clear();

  for (auto I = m_thread_to_rob_map.begin(), E = m_thread_to_rob_map.end(); I != E; ++I) {
    int index = I->second;
    rob_c *rob = m_thread_robs[index];

    if (rob->entries()) {
      uop_c* uop = rob->front();
      if (uop->m_done_cycle && uop->m_done_cycle <= core_cycle) {
        m_uop_list.push_back(uop);
      }
      else {
        // uop cannot be retired due to cache miss
        // increase memory stall cycle
        if (uop->m_uop_info.m_l2_miss == true) {
          STAT_CORE_EVENT(uop->m_core_id, MEM_STALL_CYCLE);
        }
      }
    }
  }

  return &m_uop_list;
}


