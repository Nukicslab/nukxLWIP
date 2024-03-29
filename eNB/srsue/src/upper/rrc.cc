/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2015 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of the srsUE library.
 *
 * srsUE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsUE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsue/hdr/upper/rrc.h"
#include "srslte/asn1/rrc_asn1.h"
#include "srslte/common/bcd_helpers.h"
#include "srslte/common/security.h"
#include <cstdlib>
#include <ctime>
#include <inttypes.h> // for printing uint64_t
#include <iostream>
#include <math.h>
#include <sstream>
#include <unistd.h>

using namespace srslte;
using namespace asn1::rrc;

namespace srsue {

const static uint32_t NOF_REQUIRED_SIBS = 4;
const static uint32_t required_sibs[NOF_REQUIRED_SIBS] = {0,1,2,12}; // SIB1, SIB2, SIB3 and SIB13 (eMBMS)

/*******************************************************************************
  Base functions 
*******************************************************************************/

rrc::rrc() :
  state(RRC_STATE_IDLE),
  last_state(RRC_STATE_CONNECTED),
  drb_up(false),
  rlc_flush_timeout(2000),
  rlc_flush_counter(0)
{
  n310_cnt     = 0;
  n311_cnt     = 0;
  serving_cell = new cell_t();
  neighbour_cells.reserve(NOF_NEIGHBOUR_CELLS);
  initiated = false;
  running = false;
  go_idle = false;
  go_rlf  = false;
}

rrc::~rrc()
{
  if (serving_cell) {
    delete(serving_cell);
  }

  std::vector<cell_t*>::iterator it;
  for (it = neighbour_cells.begin(); it != neighbour_cells.end(); ++it) {
    delete(*it);
  }
}

static void srslte_rrc_handler(asn1::srsasn_logger_level_t level, void* ctx, const char* str)
{
  rrc *r = (rrc *) ctx;
  r->srslte_rrc_log(str); // FIXME use log level
}

void rrc::srslte_rrc_log(const char* str)
{
  if (rrc_log) {
    rrc_log->warning("[ASN]: %s\n", str);
  } else {
    printf("[ASN]: %s\n", str);
  }
}

template <class T>
void rrc::log_rrc_message(const std::string source, const direction_t dir, const byte_buffer_t* pdu, const T& msg)
{
  if (rrc_log->get_level() == srslte::LOG_LEVEL_INFO) {
    rrc_log->info("%s - %s %s (%d B)\n", source.c_str(), (dir == Rx) ? "Rx" : "Tx",
                  msg.msg.c1().type().to_string().c_str(), pdu->N_bytes);
  } else if (rrc_log->get_level() >= srslte::LOG_LEVEL_DEBUG) {
    asn1::json_writer json_writer;
    msg.to_json(json_writer);
    rrc_log->debug_hex(pdu->msg, pdu->N_bytes, "%s - %s %s (%d B)\n", source.c_str(), (dir == Rx) ? "Rx" : "Tx",
                       msg.msg.c1().type().to_string().c_str(), pdu->N_bytes);
    rrc_log->debug("Content:\n%s\n", json_writer.to_string().c_str());
  }
}

void rrc::init(phy_interface_rrc* phy_, mac_interface_rrc* mac_, rlc_interface_rrc* rlc_, pdcp_interface_rrc* pdcp_,
               lwipep_interface_rrc *lwipep_, nas_interface_rrc* nas_, usim_interface_rrc* usim_, gw_interface_rrc* gw_,
               mac_interface_timers* mac_timers_, srslte::log* rrc_log_)
{
  pool = byte_buffer_pool::get_instance();
  phy = phy_;
  mac = mac_;
  rlc = rlc_;
  pdcp = pdcp_;
  nas = nas_;
  usim = usim_;
  gw = gw_;
  rrc_log = rrc_log_;

  // Use MAC timers
  mac_timers = mac_timers_;
  state = RRC_STATE_IDLE;
  plmn_is_selected = false;

  security_is_activated = false;

  pthread_mutex_init(&mutex, NULL);

  args.ue_category = SRSLTE_UE_CATEGORY;
  args.supported_bands[0] = 7;
  args.nof_supported_bands = 1;
  args.feature_group = 0xe6041000;

  t300 = mac_timers->timer_get_unique_id();
  t301 = mac_timers->timer_get_unique_id();
  t302 = mac_timers->timer_get_unique_id();
  t310 = mac_timers->timer_get_unique_id();
  t311 = mac_timers->timer_get_unique_id();
  t304 = mac_timers->timer_get_unique_id();

  dedicated_info_nas     = NULL;
  ue_identity_configured = false;

  transaction_id = 0;

  // Register logging handler with asn1 rrc
  asn1::rrc::rrc_log_register_handler(this, srslte_rrc_handler);

  cell_clean_cnt = 0;

  ho_start = false;

  pending_mob_reconf = false;

  // Set default values for all layers
  set_rrc_default();
  set_phy_default();
  set_mac_default();

  measurements.init(this);
  // set seed for rand (used in attach)
  srand(time(NULL));

  running = true;
  start();
  initiated = true;
}

void rrc::stop() {
  running = false;
  stop_timers();
  cmd_msg_t msg;
  msg.command = cmd_msg_t::STOP;
  cmd_q.push(msg);
  wait_thread_finish();
}

rrc_state_t rrc::get_state() {
  return state;
}

bool rrc::is_connected() {
  return (RRC_STATE_CONNECTED == state);
}

bool rrc::have_drb() {
  return drb_up;
}

void rrc::set_args(rrc_args_t args_) {
  args = args_;
}

/*
 * Low priority thread to run functions that can not be executed from main thread
 */
void rrc::run_thread() {
  while(running) {
    cmd_msg_t msg = cmd_q.wait_pop();
    switch(msg.command) {
      case cmd_msg_t::PDU:
        process_pdu(msg.lcid, msg.pdu);
        break;
      case cmd_msg_t::PCCH:
        process_pcch(msg.pdu);
        break;
      case cmd_msg_t::STOP:
        return;
    }
  }
}


/*
 *
 * RRC State Machine
 *
 */
void rrc::run_tti(uint32_t tti) {

  if (!initiated) {
    return;
  }

  /* We can not block in this thread because it is called from
   * the MAC TTI timer and needs to return immediatly to perform other
   * tasks. Therefore in this function we use trylock() instead of lock() and
   * skip function if currently locked, since none of the functions here is urgent
   */
  if (!pthread_mutex_trylock(&mutex)) {

    // Process pending PHY measurements in IDLE/CONNECTED
    process_phy_meas();

    // Log state changes
    if (state != last_state) {
      rrc_log->debug("State %s\n", rrc_state_text[state]);
      last_state = state;
    }

    // Run state machine
    switch (state) {
      case RRC_STATE_IDLE:

        /* CAUTION: The execution of cell_search() and cell_selection() take more than 1 ms
         * and will slow down MAC TTI ticks. This has no major effect at the moment because
         * the UE is in IDLE but we could consider splitting MAC and RRC threads to avoid this
         */

        // If attached but not camping on the cell, perform cell reselection
        if (nas->is_attached()) {
          rrc_log->debug("Running cell selection and reselection in IDLE\n");
          switch(cell_selection()) {
            case rrc::CHANGED_CELL:
              // New cell has been selected, start receiving PCCH
              mac->pcch_start_rx();
              break;
            case rrc::NO_CELL:
              rrc_log->warning("Could not find any cell to camp on\n");
              break;
            case rrc::SAME_CELL:
              if (!phy->cell_is_camping()) {
                rrc_log->warning("Did not reselect cell but serving cell is out-of-sync.\n");
                serving_cell->in_sync = false;
              }
              break;
          }
        }
        break;
      case RRC_STATE_CONNECTED:
        if (ho_start) {
          ho_start = false;
          if (!ho_prepare()) {
            con_reconfig_failed();
          }
        }
        measurements.run_tti(tti);
        if (go_idle) {
          // wait for max. 2s for RLC on SRB1 to be flushed
          if (not rlc->has_data(RB_ID_SRB1) || ++rlc_flush_counter > rlc_flush_timeout) {
            go_idle = false;
            leave_connected();
          } else {
            rrc_log->debug("Postponing transition to RRC IDLE (%d ms < %d ms)\n", rlc_flush_counter, rlc_flush_timeout);
          }
        }
        if (go_rlf) {
          go_rlf = false;
          // Initiate connection re-establishment procedure after RLF
          send_con_restablish_request(asn1::rrc::reest_cause_e::other_fail);
        }
        break;
      default:break;
    }

    // Clean old neighbours
    cell_clean_cnt++;
    if (cell_clean_cnt == 1000) {
      clean_neighbours();
      cell_clean_cnt = 0;
    }
    pthread_mutex_unlock(&mutex);
  } // Skip TTI if mutex is locked
}









/*******************************************************************************
*
*
*
* NAS interface: PLMN search and RRC connection establishment
*
*
*
*******************************************************************************/

uint16_t rrc::get_mcc() {
  return serving_cell->get_mcc();
}

uint16_t rrc::get_mnc() {
  return serving_cell->get_mnc();
}

/* NAS interface to search for available PLMNs.
 * It goes through all known frequencies, synchronizes and receives SIB1 for each to extract PLMN.
 * The function is blocking and waits until all frequencies have been
 * searched and PLMNs are obtained.
 *
 * This function is thread-safe with connection_request()
 */
int rrc::plmn_search(found_plmn_t found_plmns[MAX_FOUND_PLMNS])
{
  // Mutex with connect
  pthread_mutex_lock(&mutex);

  rrc_log->info("Starting PLMN search\n");
  uint32_t nof_plmns = 0;
  phy_interface_rrc::cell_search_ret_t ret;
  do {
    ret = cell_search();
    if (ret.found == phy_interface_rrc::cell_search_ret_t::CELL_FOUND) {
      if (serving_cell->has_sib1()) {
        // Save PLMN and TAC to NAS
        for (uint32_t i = 0; i < serving_cell->nof_plmns(); i++) {
          if (nof_plmns < MAX_FOUND_PLMNS) {
            found_plmns[nof_plmns].plmn_id = serving_cell->get_plmn(i);
            found_plmns[nof_plmns].tac = serving_cell->get_tac();
            nof_plmns++;
          } else {
            rrc_log->error("No more space for plmns (%d)\n", nof_plmns);
          }
        }
      } else {
        rrc_log->error("SIB1 not acquired\n");
      }
    }
  } while (ret.last_freq == phy_interface_rrc::cell_search_ret_t::MORE_FREQS &&
           ret.found     != phy_interface_rrc::cell_search_ret_t::ERROR);

  // Process all pending measurements before returning
  process_phy_meas();

  pthread_mutex_unlock(&mutex);

  if (ret.found == phy_interface_rrc::cell_search_ret_t::ERROR) {
    return -1; 
  } else {
    return nof_plmns;
  }
}

/* This is the NAS interface. When NAS requests to select a PLMN we have to
 * connect to either register or because there is pending higher layer traffic.
 */
void rrc::plmn_select(asn1::rrc::plmn_id_s plmn_id)
{
  plmn_is_selected = true;
  selected_plmn_id = plmn_id;

  rrc_log->info("PLMN Selected %s\n", plmn_id_to_string(plmn_id).c_str());
}

/* 5.3.3.2 Initiation of RRC Connection Establishment procedure
 *
 * Higher layers request establishment of RRC connection while UE is in RRC_IDLE
 *
 * This procedure selects a suitable cell for transmission of RRCConnectionRequest and configures
 * it. Sends connectionRequest message and returns if message transmitted successfully.
 * It does not wait until completition of Connection Establishment procedure
 */
bool rrc::connection_request(asn1::rrc::establishment_cause_e cause, srslte::byte_buffer_t* dedicated_info_nas)
{

  if (!plmn_is_selected) {
    rrc_log->error("Trying to connect but PLMN not selected.\n");
    return false;
  }

  if (state != RRC_STATE_IDLE) {
    rrc_log->warning("Requested RRC connection establishment while not in IDLE\n");
    return false;
  }

  if (mac_timers->timer_get(t302)->is_running()) {
    rrc_log->info("Requested RRC connection establishment while T302 is running\n");
    nas->set_barring(nas_interface_rrc::BARRING_MO_DATA);
    return false;
  }

  bool ret = false;

  pthread_mutex_lock(&mutex);

  rrc_log->info("Initiation of Connection establishment procedure\n");

  // Perform cell selection & reselection for the selected PLMN
  cs_ret_t cs_ret = cell_selection();

  // .. and SI acquisition
  if (phy->cell_is_camping()) {

    // Set default configurations
    set_phy_default();
    set_mac_default();

    // CCCH configuration applied already at start
    // timeAlignmentCommon applied in configure_serving_cell

    rrc_log->info("Configuring serving cell...\n");
    if (configure_serving_cell()) {

      mac_timers->timer_get(t300)->reset();
      mac_timers->timer_get(t300)->run();

      // Send connectionRequest message to lower layers
      send_con_request(cause);

      // Save dedicatedInfoNAS SDU
      if (this->dedicated_info_nas) {
        rrc_log->warning("Received a new dedicatedInfoNAS SDU but there was one still in queue. Removing it\n");
        pool->deallocate(this->dedicated_info_nas);
      }
      this->dedicated_info_nas = dedicated_info_nas;

      // Wait until t300 stops due to RRCConnectionSetup/Reject or expiry
      while (mac_timers->timer_get(t300)->is_running()) {
        usleep(1000);
      }

      if (state == RRC_STATE_CONNECTED) {
        // Received ConnectionSetup
        ret = true;
      } else if (mac_timers->timer_get(t300)->is_expired()) {
        // T300 is expired: 5.3.3.6
        rrc_log->info("Timer T300 expired: ConnectionRequest timed out\n");
        mac->reset();
        set_mac_default();
        rlc->reestablish();
      } else {
        // T300 is stopped but RRC not Connected is because received Reject: Section 5.3.3.8
        rrc_log->info("Timer T300 stopped: Received ConnectionReject\n");
        mac->reset();
        set_mac_default();
      }

    } else {
      rrc_log->error("Configuring serving cell\n");
    }
  } else {
    switch(cs_ret) {
      case SAME_CELL:
        rrc_log->warning("Did not reselect cell but serving cell is out-of-sync.\n");
        serving_cell->in_sync = false;
      break;
      case CHANGED_CELL:
        rrc_log->warning("Selected a new cell but could not camp on. Setting out-of-sync.\n");
        serving_cell->in_sync = false;
        break;
      default:
        rrc_log->warning("Could not find any suitable cell to connect\n");
    }
  }

  if (!ret) {
    rrc_log->warning("Could not estblish connection. Deallocating dedicatedInfoNAS PDU\n");
    pool->deallocate(this->dedicated_info_nas);
    this->dedicated_info_nas = NULL;
  }

  pthread_mutex_unlock(&mutex);
  return ret;
}

void rrc::set_ue_idenity(asn1::rrc::s_tmsi_s s_tmsi)
{
  ue_identity_configured = true;
  ue_identity            = s_tmsi;
  rrc_log->info("Set ue-Identity to 0x%" PRIu64 ":0x%" PRIu64 "\n", ue_identity.mmec.to_number(),
                ue_identity.m_tmsi.to_number());
}

/* Retrieves all required SIB or configures them if already retrieved before
 */
bool rrc::configure_serving_cell() {

  if (!phy->cell_is_camping()) {
    rrc_log->error("Trying to configure Cell while not camping on it\n");
    return false;
  }
  serving_cell->has_mcch = false;
  // Obtain the SIBs if not available or apply the configuration if available
  for (uint32_t i = 0; i < NOF_REQUIRED_SIBS; i++) {
    if (!serving_cell->has_sib(required_sibs[i])) {
      rrc_log->info("Cell has no SIB%d. Obtaining SIB%d\n", required_sibs[i]+1, required_sibs[i]+1);
      if (!si_acquire(required_sibs[i])) {
        rrc_log->info("Timeout while acquiring SIB%d\n", required_sibs[i]+1);
        if (required_sibs[i] < 2) {
          return false;
        }
      }
    } else {
      rrc_log->info("Cell has SIB%d\n", required_sibs[i]+1);
      switch(required_sibs[i]) {
        case 1:
          apply_sib2_configs(serving_cell->sib2ptr());
          break;
        case 12:
          apply_sib13_configs(serving_cell->sib13ptr());
          break;
      }
    }
  }
  return true;
}






/*******************************************************************************
*
*
*
* PHY interface: neighbour and serving cell measurements and out-of-sync/in-sync
*
*
*
*******************************************************************************/

/* This function is called from a PHY worker thus must return very quickly.
 * Queue the values of the measurements and process them from the RRC thread
 */
void rrc::new_phy_meas(float rsrp, float rsrq, uint32_t tti, int earfcn_i, int pci_i) {
  uint32_t pci    = 0;
  uint32_t earfcn = 0;
  if (earfcn_i < 0) {
    earfcn = (uint32_t) serving_cell->get_earfcn();
  } else {
    earfcn = (uint32_t) earfcn_i;
  }
  if (pci_i < 0) {
    pci    = (uint32_t) serving_cell->get_pci();
  } else {
    pci    = (uint32_t) pci_i;
  }
  phy_meas_t new_meas = {rsrp, rsrq, tti, earfcn, pci};
  phy_meas_q.push(new_meas);
  rrc_log->info("MEAS:  New measurement pci=%d, rsrp=%.1f dBm.\n", pci, rsrp);
}

/* Processes all pending PHY measurements in queue. Must be called from a mutexed function
 */
void rrc::process_phy_meas() {
  phy_meas_t m;
  while(phy_meas_q.try_pop(&m)) {
    rrc_log->debug("MEAS:  Processing measurement. %zd measurements in queue\n", phy_meas_q.size());
    process_new_phy_meas(m);
  }
}

void rrc::process_new_phy_meas(phy_meas_t meas)
{
  float rsrp   = meas.rsrp;
  float rsrq   = meas.rsrq;
  uint32_t tti = meas.tti;
  uint32_t earfcn = meas.earfcn;
  uint32_t pci    = meas.pci;

  // Measurements in RRC_CONNECTED go through measurement class to log reports etc.
  if (state != RRC_STATE_IDLE) {
    measurements.new_phy_meas(earfcn, pci, rsrp, rsrq, tti);

    // Measurements in RRC_IDLE update serving cell
  } else {

    // Update serving cell
    if (serving_cell->equals(earfcn, pci)) {
      serving_cell->set_rsrp(rsrp);
      // Or update/add neighbour cell
    } else {
      add_neighbour_cell(earfcn, pci, rsrp);
    }
  }
}

// Detection of physical layer problems in RRC_CONNECTED (5.3.11.1)
void rrc::out_of_sync()
{

  // CAUTION: We do not lock in this function since they are called from real-time threads

  serving_cell->in_sync = false;
  rrc_log->info("Received out-of-sync while in state %s. n310=%d, t311=%s, t310=%s\n",
                rrc_state_text[state], n310_cnt,
                mac_timers->timer_get(t311)->is_running()?"running":"stop",
                mac_timers->timer_get(t310)->is_running()?"running":"stop");
  if (state == RRC_STATE_CONNECTED) {
    if (!mac_timers->timer_get(t311)->is_running() && !mac_timers->timer_get(t310)->is_running()) {
      n310_cnt++;
      if (n310_cnt == N310) {
        rrc_log->info("Detected %d out-of-sync from PHY. Trying to resync. Starting T310 timer %d ms\n",
                      N310, mac_timers->timer_get(t310)->get_timeout());
        mac_timers->timer_get(t310)->reset();
        mac_timers->timer_get(t310)->run();
        n310_cnt = 0;
      }
    }
  }
}

// Recovery of physical layer problems (5.3.11.2)
void rrc::in_sync()
{

  // CAUTION: We do not lock in this function since they are called from real-time threads

  serving_cell->in_sync = true;
  if (mac_timers->timer_get(t310)->is_running()) {
    n311_cnt++;
    if (n311_cnt == N311) {
      mac_timers->timer_get(t310)->stop();
      n311_cnt = 0;
      rrc_log->info("Detected %d in-sync from PHY. Stopping T310 timer\n", N311);
    }
  }
}
















/*******************************************************************************
*
*
*
* System Information Acquisition procedure
*
*
*
*******************************************************************************/


// Determine SI messages scheduling as in 36.331 5.2.3 Acquisition of an SI message
uint32_t rrc::sib_start_tti(uint32_t tti, uint32_t period, uint32_t offset, uint32_t sf) {
  return (period*10*(1+tti/(period*10))+(offset*10)+sf)%10240; // the 1 means next opportunity
}

/* Implemnets the SI acquisition procedure
 * Configures the MAC/PHY scheduling to retrieve SI messages. The function is blocking and will not
 * return until SIB is correctly received or timeout
 */
bool rrc::si_acquire(uint32_t sib_index)
{
  uint32_t tti = 0;
  uint32_t si_win_start=0, si_win_len=0;
  uint16_t period = 0;
  uint32_t sched_index = 0;
  uint32_t x, sf, offset;

  uint32_t last_win_start = 0;
  uint32_t timeout = 0;

  while(timeout < SIB_SEARCH_TIMEOUT_MS && !serving_cell->has_sib(sib_index)) {

    bool instruct_phy = false;

    if (sib_index == 0) {

      // Instruct MAC to look for SIB1
      tti = mac->get_current_tti();
      si_win_start = sib_start_tti(tti, 2, 0, 5);
      if (last_win_start == 0 ||
          (srslte_tti_interval(tti, last_win_start) >= 20 && srslte_tti_interval(tti, last_win_start) < 1000)) {

        last_win_start = si_win_start;
        si_win_len = 1;
        instruct_phy = true;
      }
      period = 20;
      sched_index = 0;
    } else {
      // Instruct MAC to look for SIB2..13
      if (serving_cell->has_sib1()) {

        asn1::rrc::sib_type1_s* sib1 = serving_cell->sib1ptr();

        // SIB2 scheduling
        if (sib_index == 1) {
          period      = sib1->sched_info_list[0].si_periodicity.to_number();
          sched_index = 0;
        } else {
          // SIB3+ scheduling Section 5.2.3
          if (sib_index >= 2) {
            bool found = false;
            for (uint32_t i = 0; i < sib1->sched_info_list.size() && !found; i++) {
              for (uint32_t j = 0; j < sib1->sched_info_list[i].sib_map_info.size() && !found; j++) {
                if (sib1->sched_info_list[i].sib_map_info[j].to_number() == sib_index + 1) {
                  period      = sib1->sched_info_list[i].si_periodicity.to_number();
                  sched_index = i;
                  found       = true;
                }
              }
            }
            if (!found) {
              rrc_log->info("Could not find SIB%d scheduling in SIB1\n", sib_index+1);
              return false;
            }
          }
        }
        si_win_len = sib1->si_win_len.to_number();
        x          = sched_index * si_win_len;
        sf         = x % 10;
        offset     = x / 10;

        tti          = mac->get_current_tti();
        si_win_start = sib_start_tti(tti, period, offset, sf);

        if (last_win_start == 0 || (srslte_tti_interval(tti, last_win_start) > period * 5 &&
                                    srslte_tti_interval(tti, last_win_start) < 1000)) {
          last_win_start = si_win_start;
          instruct_phy   = true;
        }
      } else {
        rrc_log->error("Trying to receive SIB%d but SIB1 not received\n", sib_index + 1);
      }
    }

    // Instruct MAC to decode SIB
    if (instruct_phy && !serving_cell->has_sib(sib_index)) {
      mac->bcch_start_rx(si_win_start, si_win_len);
      rrc_log->info("Instructed MAC to search for SIB%d, win_start=%d, win_len=%d, period=%d, sched_index=%d\n",
                    sib_index+1, si_win_start, si_win_len, period, sched_index);
    }
    usleep(1000);
    timeout++;
  }
  return serving_cell->has_sib(sib_index);
}









/*******************************************************************************
*
*
*
* Cell selection, reselection and neighbour cell database management
*
*
*
*******************************************************************************/

/* Searches for a cell in the current frequency and retrieves SIB1 if not retrieved yet
 */
phy_interface_rrc::cell_search_ret_t rrc::cell_search()
{
  phy_interface_rrc::phy_cell_t new_cell;

  phy_interface_rrc::cell_search_ret_t ret = phy->cell_search(&new_cell);

  switch(ret.found) {
    case phy_interface_rrc::cell_search_ret_t::CELL_FOUND:
      rrc_log->info("Cell found in this frequency. Setting new serving cell...\n");

      // Create cell with NaN RSRP. Will be updated by new_phy_meas() during SIB search.
      if (!add_neighbour_cell(new_cell, NAN)) {
        rrc_log->info("No more space for neighbour cells\n");
        break;
      }
      set_serving_cell(new_cell);

      if (phy->cell_is_camping()) {
        if (!serving_cell->has_sib1()) {
          rrc_log->info("Cell has no SIB1. Obtaining SIB1\n");
          if (!si_acquire(0)) {
            rrc_log->error("Timeout while acquiring SIB1\n");
          }
        } else {
          rrc_log->info("Cell has SIB1\n");
        }
      } else {
        rrc_log->warning("Could not camp on found cell. Trying next one...\n");
      }
      break;
    case phy_interface_rrc::cell_search_ret_t::CELL_NOT_FOUND:
      rrc_log->info("No cells found.\n");
      break;
    case phy_interface_rrc::cell_search_ret_t::ERROR:
      rrc_log->error("In cell search. Finishing PLMN search\n");
      break;
  }
  return ret;
}

/* Cell selection procedure 36.304 5.2.3
 * Select the best cell to camp on among the list of known cells
 */
rrc::cs_ret_t rrc::cell_selection()
{
  // Neighbour cells are sorted in descending order of RSRP
  for (uint32_t i = 0; i < neighbour_cells.size(); i++) {
    if (/*TODO: CHECK that PLMN matches. Currently we don't receive SIB1 of neighbour cells
         * neighbour_cells[i]->plmn_equals(selected_plmn_id) && */
        neighbour_cells[i]->in_sync) // matches S criteria
    {
      // If currently connected, verify cell selection criteria
      if (!serving_cell->in_sync ||
          (cell_selection_criteria(neighbour_cells[i]->get_rsrp())  &&
              neighbour_cells[i]->get_rsrp() > serving_cell->get_rsrp() + 5))
      {
        // Try to select Cell
        set_serving_cell(i);
        rrc_log->info("Selected cell idx=%d, PCI=%d, EARFCN=%d\n",
                      i, serving_cell->get_pci(), serving_cell->get_earfcn());
        rrc_log->console("Selected cell PCI=%d, EARFCN=%d\n",
                         serving_cell->get_pci(), serving_cell->get_earfcn());

        if (phy->cell_select(&serving_cell->phy_cell)) {
          if (configure_serving_cell()) {
            rrc_log->info("Selected and configured cell successfully\n");
            return CHANGED_CELL;
          } else {
            rrc_log->error("While configuring serving cell\n");
          }
        } else {
          serving_cell->in_sync = false;
          rrc_log->warning("Could not camp on selected cell\n");
        }
      }
    }
  }
  if (serving_cell->in_sync) {
    if (!phy->cell_is_camping()) {
      rrc_log->info("Serving cell is in-sync but not camping. Selecting it...\n");
      if (phy->cell_select(&serving_cell->phy_cell)) {
        rrc_log->info("Selected serving cell OK.\n");
      } else {
        serving_cell->in_sync = false;
        rrc_log->error("Could not camp on serving cell.\n");
      }
    }
    return SAME_CELL;
  }
  // If can not find any suitable cell, search again
  rrc_log->info("Cell selection and reselection in IDLE did not find any suitable cell. Searching again\n");
  // If can not camp on any cell, search again for new cells
  phy_interface_rrc::cell_search_ret_t ret = cell_search();

  return (ret.found == phy_interface_rrc::cell_search_ret_t::CELL_FOUND)?CHANGED_CELL:NO_CELL;
}

// Cell selection criteria Section 5.2.3.2 of 36.304
bool rrc::cell_selection_criteria(float rsrp, float rsrq)
{
  if (get_srxlev(rsrp) > 0 || !serving_cell->has_sib3()) {
    return true;
  } else {
    return false;
  }
}

float rrc::get_srxlev(float Qrxlevmeas) {
  // TODO: Do max power limitation
  float Pcompensation = 0;
  return Qrxlevmeas - (cell_resel_cfg.Qrxlevmin + cell_resel_cfg.Qrxlevminoffset) - Pcompensation;
}

float rrc::get_squal(float Qqualmeas) {
  return Qqualmeas - (cell_resel_cfg.Qqualmin + cell_resel_cfg.Qqualminoffset);
}

// Cell reselection in IDLE Section 5.2.4 of 36.304
void rrc::cell_reselection(float rsrp, float rsrq)
{
  // Intra-frequency cell-reselection criteria

  if (get_srxlev(rsrp) > cell_resel_cfg.s_intrasearchP && rsrp > -95.0) {
    // UE may not perform intra-frequency measurements.
    phy->meas_reset();
    // keep measuring serving cell
    phy->meas_start(phy->get_current_earfcn(), phy->get_current_pci());
  } else {
    // UE must start intra-frequency measurements
    phy->meas_start(phy->get_current_earfcn(), -1);
  }

  // TODO: Inter-frequency cell reselection
}

// Set new serving cell
void rrc::set_serving_cell(phy_interface_rrc::phy_cell_t phy_cell) {
  int cell_idx = find_neighbour_cell(phy_cell.earfcn, phy_cell.cell.id);
  if (cell_idx >= 0) {
    set_serving_cell(cell_idx);
  } else {
    rrc_log->error("Setting serving cell: Unkonwn cell with earfcn=%d, PCI=%d\n", phy_cell.earfcn, phy_cell.cell.id);
  }
}

// Set new serving cell
void rrc::set_serving_cell(uint32_t cell_idx) {

  if (cell_idx < neighbour_cells.size())
  {
    // Remove future serving cell from neighbours to make space for current serving cell
    cell_t *new_serving_cell = neighbour_cells[cell_idx];
    if (!new_serving_cell) {
      rrc_log->error("Setting serving cell. Index %d is empty\n", cell_idx);
      return;
    }
    neighbour_cells.erase(std::remove(neighbour_cells.begin(), neighbour_cells.end(), neighbour_cells[cell_idx]), neighbour_cells.end());

    // Move serving cell to neighbours list
    if (serving_cell->is_valid()) {
      // Make sure it does not exist already
      int serving_idx = find_neighbour_cell(serving_cell->get_earfcn(), serving_cell->get_pci());
      if (serving_idx >= 0 && (uint32_t) serving_idx < neighbour_cells.size()) {
        printf("Error serving cell is already in the neighbour list. Removing it\n");
        neighbour_cells.erase(std::remove(neighbour_cells.begin(), neighbour_cells.end(), neighbour_cells[serving_idx]), neighbour_cells.end());
      }
      // If not in the list, add it to the list of neighbours (sorted inside the function)
      if (!add_neighbour_cell(serving_cell)) {
        rrc_log->info("Serving cell not added to list of neighbours. Worse than current neighbours\n");
      }
    }

    // Set new serving cell
    serving_cell = new_serving_cell;

    rrc_log->info("Setting serving cell idx=%d, earfcn=%d, PCI=%d, nof_neighbours=%zd\n", cell_idx,
                  serving_cell->get_earfcn(), serving_cell->get_pci(), neighbour_cells.size());

  } else {
    rrc_log->error("Setting invalid serving cell idx %d\n", cell_idx);
  }
}

bool sort_rsrp(cell_t *u1, cell_t *u2) {
  return u1->greater(u2);
}

void rrc::delete_neighbour(uint32_t cell_idx) {
  measurements.delete_report(neighbour_cells[cell_idx]->get_earfcn(), neighbour_cells[cell_idx]->get_pci());
  delete neighbour_cells[cell_idx];
  neighbour_cells.erase(std::remove(neighbour_cells.begin(), neighbour_cells.end(), neighbour_cells[cell_idx]), neighbour_cells.end());
}

std::vector<cell_t*>::iterator rrc::delete_neighbour(std::vector<cell_t*>::iterator it) {
  measurements.delete_report((*it)->get_earfcn(), (*it)->get_pci());
  delete (*it);
  return neighbour_cells.erase(it);
}

/* Called by main RRC thread to remove neighbours from which measurements have not been received in a while
 */
void rrc::clean_neighbours()
{
  struct timeval now;
  gettimeofday(&now, NULL);

  std::vector<cell_t*>::iterator it = neighbour_cells.begin();
  while(it != neighbour_cells.end()) {
    if ((*it)->timeout_secs(now) > NEIGHBOUR_TIMEOUT) {
      rrc_log->info("Neighbour PCI=%d timed out. Deleting\n", (*it)->get_pci());
      it = delete_neighbour(it);
    } else {
      ++it;
    }
  }
}

// Sort neighbour cells by decreasing order of RSRP
void rrc::sort_neighbour_cells()
{
  // Remove out-of-sync cells
  std::vector<cell_t*>::iterator it = neighbour_cells.begin();
  while(it != neighbour_cells.end()) {
    if ((*it)->in_sync == false) {
      rrc_log->info("Neighbour PCI=%d is out-of-sync. Deleting\n", (*it)->get_pci());
      it = delete_neighbour(it);
    } else {
      ++it;
    }
  }

  std::sort(neighbour_cells.begin(), neighbour_cells.end(), sort_rsrp);

  if (neighbour_cells.size() > 0) {
    char ordered[512];
    int n=0;
    n += snprintf(ordered, 512, "[pci=%d, rsrp=%.2f", neighbour_cells[0]->phy_cell.cell.id, neighbour_cells[0]->get_rsrp());
    for (uint32_t i=1;i<neighbour_cells.size();i++) {
      n += snprintf(&ordered[n], 512-n, " | pci=%d, rsrp=%.2f", neighbour_cells[i]->get_pci(), neighbour_cells[i]->get_rsrp());
    }
    rrc_log->info("Neighbours: %s]\n", ordered);
  } else {
    rrc_log->info("Neighbours: Empty\n");
  }
}

bool rrc::add_neighbour_cell(cell_t *new_cell) {
  bool ret = false;
  if (neighbour_cells.size() < NOF_NEIGHBOUR_CELLS) {
    ret = true;
  } else if (new_cell->greater(neighbour_cells[neighbour_cells.size()-1])) {
    // Replace old one by new one
    delete_neighbour(neighbour_cells.size()-1);
    ret = true;
  }
  if (ret) {
    neighbour_cells.push_back(new_cell);
  }
  rrc_log->info("Added neighbour cell EARFCN=%d, PCI=%d, nof_neighbours=%zd\n",
                new_cell->get_earfcn(), new_cell->get_pci(), neighbour_cells.size());
  sort_neighbour_cells();
  return ret;
}

// If only neighbour PCI is provided, copy full cell from serving cell
bool rrc::add_neighbour_cell(uint32_t earfcn, uint32_t pci, float rsrp) {
  phy_interface_rrc::phy_cell_t phy_cell;
  phy_cell = serving_cell->phy_cell;
  phy_cell.earfcn = earfcn;
  phy_cell.cell.id = pci;
  return add_neighbour_cell(phy_cell, rsrp);
}

bool rrc::add_neighbour_cell(phy_interface_rrc::phy_cell_t phy_cell, float rsrp) {
  if (phy_cell.earfcn == 0) {
    phy_cell.earfcn = serving_cell->get_earfcn();
  }

  // First check if already exists
  int cell_idx = find_neighbour_cell(phy_cell.earfcn, phy_cell.cell.id);

  rrc_log->info("Adding PCI=%d, earfcn=%d, cell_idx=%d\n", phy_cell.cell.id, phy_cell.earfcn, cell_idx);

  // If exists, update RSRP if provided, sort again and return
  if (cell_idx >= 0 && std::isnormal(rsrp)) {
    neighbour_cells[cell_idx]->set_rsrp(rsrp);
    sort_neighbour_cells();
    return true;
  }

  // If not, create a new one
  cell_t *new_cell = new cell_t(phy_cell, rsrp);

  return add_neighbour_cell(new_cell);
}

int rrc::find_neighbour_cell(uint32_t earfcn, uint32_t pci) {
  for (uint32_t i = 0; i < neighbour_cells.size(); i++) {
    if (neighbour_cells[i]->equals(earfcn, pci)) {
      return (int) i;
    }
  }
  return -1;
}

/*******************************************************************************
 *
 *
 *
 * eMBMS Related Functions
 *
 *
 *
 *******************************************************************************/

void rrc::print_mbms()
{
  if (!rrc_log) {
    return;
  }

  if (!serving_cell->has_mcch) {
    rrc_log->console("MCCH not available for current cell\n");
    return;
  }

  asn1::rrc::mcch_msg_type_c msg = serving_cell->mcch.msg;
  std::stringstream          ss;
  for (uint32_t i = 0; i < msg.c1().mbsfn_area_cfg_r9().pmch_info_list_r9.size(); i++) {
    ss << "PMCH: " << i << std::endl;
    pmch_info_r9_s* pmch = &msg.c1().mbsfn_area_cfg_r9().pmch_info_list_r9[i];
    for (uint32_t j = 0; j < pmch->mbms_session_info_list_r9.size(); j++) {
      mbms_session_info_r9_s* sess = &pmch->mbms_session_info_list_r9[j];
      ss << "  Service ID: " << sess->tmgi_r9.service_id_r9.to_string();
      if (sess->session_id_r9_present) {
        ss << ", Session ID: " << sess->session_id_r9.to_string();
      }
      if (sess->tmgi_r9.plmn_id_r9.type() == tmgi_r9_s::plmn_id_r9_c_::types::explicit_value_r9) {
        ss << ", MCC: " << mcc_bytes_to_string(sess->tmgi_r9.plmn_id_r9.explicit_value_r9().mcc);
        ss << ", MNC: " << mnc_bytes_to_string(sess->tmgi_r9.plmn_id_r9.explicit_value_r9().mnc);
      } else {
        ss << ", PLMN index: " << sess->tmgi_r9.plmn_id_r9.plmn_idx_r9();
      }
      ss << ", LCID: " << sess->lc_ch_id_r9 << std::endl;
    }
  }
  // rrc_log->console(ss.str().c_str());
  std::cout << ss.str();
  return;
}

bool rrc::mbms_service_start(uint32_t serv, uint32_t port)
{
  bool ret = false;
  if (!serving_cell->has_mcch) {
    rrc_log->error("MCCH not available at MBMS Service Start\n");
    return ret;
  }

  asn1::rrc::mcch_msg_type_c msg = serving_cell->mcch.msg;
  for (uint32_t i = 0; i < msg.c1().mbsfn_area_cfg_r9().pmch_info_list_r9.size(); i++) {
    pmch_info_r9_s* pmch = &msg.c1().mbsfn_area_cfg_r9().pmch_info_list_r9[i];
    for (uint32_t j = 0; j < pmch->mbms_session_info_list_r9.size(); j++) {
      mbms_session_info_r9_s* sess = &pmch->mbms_session_info_list_r9[j];
      if (serv == sess->tmgi_r9.service_id_r9.to_number()) {
        rrc_log->console("MBMS service started. Service id:%d, port: %d\n", serv, port);
        ret = true;
        add_mrb(sess->lc_ch_id_r9, port);
      }
    }
  }
  return ret;
}

/*******************************************************************************
*
*
*
* Other functions
*
*
*
*******************************************************************************/

/* Detection of radio link failure (5.3.11.3)
 * Upon T310 expiry, RA problem or RLC max retx
 */
void rrc::radio_link_failure() {
  // TODO: Generate and store failure report
  rrc_log->warning("Detected Radio-Link Failure\n");
  rrc_log->console("Warning: Detected Radio-Link Failure\n");
  if (state == RRC_STATE_CONNECTED) {
    go_rlf = true;
  }
}

/* Reception of PUCCH/SRS release procedure (Section 5.3.13) */
void rrc::release_pucch_srs() {
  // Apply default configuration for PUCCH (CQI and SR) and SRS (release)
  set_phy_default_pucch_srs();

  // Configure RX signals without pregeneration because default option is release
  phy->configure_ul_params(true);
}

void rrc::ra_problem() {
  radio_link_failure();
}

void rrc::max_retx_attempted() {
  //TODO: Handle the radio link failure
  rrc_log->warning("Max RLC reTx attempted\n");
  radio_link_failure();
}

void rrc::timer_expired(uint32_t timeout_id) {
  if (timeout_id == t310) {
    rrc_log->info("Timer T310 expired: Radio Link Failure\n");
    radio_link_failure();
  } else if (timeout_id == t311) {
    rrc_log->info("Timer T311 expired: Going to RRC IDLE\n");
    go_idle = true;
  } else if (timeout_id == t301) {
    if (state == RRC_STATE_IDLE) {
      rrc_log->info("Timer T301 expired: Already in IDLE.\n");
    } else {
      rrc_log->info("Timer T301 expired: Going to RRC IDLE\n");
      go_idle = true;
    }
  } else if (timeout_id == t302) {
    rrc_log->info("Timer T302 expired. Informing NAS about barrier alleviation\n");
    nas->set_barring(nas_interface_rrc::BARRING_NONE);
  } else if (timeout_id == t300) {
    // Do nothing, handled in connection_request()
  } else if (timeout_id == t304) {
    rrc_log->console("Timer T304 expired: Handover failed\n");
    ho_failed();
  // fw to measurement
  } else if (!measurements.timer_expired(timeout_id)) {
    rrc_log->error("Timeout from unknown timer id %d\n", timeout_id);
  }
}








/*******************************************************************************
*
*
*
* Connection Control: Establishment, Reconfiguration, Reestablishment and Release
*
*
*
*******************************************************************************/

void rrc::send_con_request(asn1::rrc::establishment_cause_e cause)
{
  rrc_log->debug("Preparing RRC Connection Request\n");

  // Prepare ConnectionRequest packet
  ul_ccch_msg.msg.set(asn1::rrc::ul_ccch_msg_type_c::types::c1);
  ul_ccch_msg.msg.c1().set(asn1::rrc::ul_ccch_msg_type_c::c1_c_::types::rrc_conn_request);
  ul_ccch_msg.msg.c1().rrc_conn_request().crit_exts.set(
      asn1::rrc::rrc_conn_request_s::crit_exts_c_::types::rrc_conn_request_r8);
  rrc_conn_request_r8_ies_s* rrc_conn_req = &ul_ccch_msg.msg.c1().rrc_conn_request().crit_exts.rrc_conn_request_r8();

  if (ue_identity_configured) {
    rrc_conn_req->ue_id.set(asn1::rrc::init_ue_id_c::types::s_tmsi);
    rrc_conn_req->ue_id.s_tmsi() = ue_identity;
  } else {
    rrc_conn_req->ue_id.set(asn1::rrc::init_ue_id_c::types::random_value);
    // TODO use proper RNG
    uint64_t random_id = 0;
    for (uint i = 0; i < 5; i++) { // fill random ID bytewise, 40 bits = 5 bytes
      random_id |= ( (uint64_t)rand() & 0xFF ) << i*8;
    }
    rrc_conn_req->ue_id.random_value().from_number(random_id);
  }
  rrc_conn_req->establishment_cause = cause;

  send_ul_ccch_msg();
}

/* RRC connection re-establishment procedure (5.3.7) */
void rrc::send_con_restablish_request(asn1::rrc::reest_cause_e cause)
{
  uint16_t crnti;
  uint16_t pci;
  uint32_t cellid;

  if (cause == asn1::rrc::reest_cause_e::ho_fail) {
    crnti  = ho_src_rnti;
    pci    = ho_src_cell.get_pci();
    cellid = ho_src_cell.get_cell_id();
  } else {
    mac_interface_rrc::ue_rnti_t uernti;
    mac->get_rntis(&uernti);
    crnti  = uernti.crnti;
    pci    = serving_cell->get_pci();
    cellid = serving_cell->get_cell_id();
  }

  // Compute shortMAC-I
  uint8_t varShortMAC_packed[16];
  bzero(varShortMAC_packed, 16);
  asn1::bit_ref bref(varShortMAC_packed, sizeof(varShortMAC_packed));

  // ASN.1 encode VarShortMAC-Input
  var_short_mac_input_s varmac;
  varmac.cell_id.from_number(cellid);
  varmac.pci = pci;
  varmac.c_rnti.from_number(crnti);
  varmac.pack(bref);
  uint32_t N_bits  = (uint32_t)bref.distance(varShortMAC_packed);
  uint32_t N_bytes = ((N_bits-1)/8+1);

  rrc_log->info("Encoded varShortMAC: cellId=0x%x, PCI=%d, rnti=0x%x (%d bytes, %d bits)\n",
                cellid, pci, crnti, N_bytes, N_bits);

  // Compute MAC-I
  uint8_t mac_key[4];
  switch(integ_algo) {
    case INTEGRITY_ALGORITHM_ID_128_EIA1:
      security_128_eia1(&k_rrc_int[16],
                        0xffffffff,    // 32-bit all to ones
                        0x1f,          // 5-bit all to ones
                        1,             // 1-bit to one
                        varShortMAC_packed,
                        N_bytes,
                        mac_key);
      break;
    case INTEGRITY_ALGORITHM_ID_128_EIA2:
      security_128_eia2(&k_rrc_int[16],
                        0xffffffff,    // 32-bit all to ones
                        0x1f,          // 5-bit all to ones
                        1,             // 1-bit to one
                        varShortMAC_packed,
                        N_bytes,
                        mac_key);
      break;
    default:
      rrc_log->info("Unsupported integrity algorithm during reestablishment\n");
  }

  // Prepare ConnectionRestalishmentRequest packet
  ul_ccch_msg.msg.set(asn1::rrc::ul_ccch_msg_type_c::types::c1);
  ul_ccch_msg.msg.c1().set(asn1::rrc::ul_ccch_msg_type_c::c1_c_::types::rrc_conn_reest_request);
  ul_ccch_msg.msg.c1().rrc_conn_reest_request().crit_exts.set(
      asn1::rrc::rrc_conn_reest_request_s::crit_exts_c_::types::rrc_conn_reest_request_r8);
  rrc_conn_reest_request_r8_ies_s* rrc_conn_reest_req =
      &ul_ccch_msg.msg.c1().rrc_conn_reest_request().crit_exts.rrc_conn_reest_request_r8();

  rrc_conn_reest_req->ue_id.c_rnti.from_number(crnti);
  rrc_conn_reest_req->ue_id.pci = pci;
  rrc_conn_reest_req->ue_id.short_mac_i.from_number(mac_key[2] << 8 | mac_key[3]);
  rrc_conn_reest_req->reest_cause = cause;

  rrc_log->info("Initiating RRC Connection Reestablishment Procedure\n");
  rrc_log->console("RRC Connection Reestablishment\n");
  mac_timers->timer_get(t310)->stop();
  mac_timers->timer_get(t311)->reset();
  mac_timers->timer_get(t311)->run();

  phy->reset();
  set_phy_default();
  mac->reset();
  set_mac_default();

  // Perform cell selection in accordance to 36.304
  if (cell_selection_criteria(serving_cell->get_rsrp()) && serving_cell->in_sync) {
    if (phy->cell_select(&serving_cell->phy_cell)) {

      if (mac_timers->timer_get(t311)->is_running()) {
        // Actions following cell reselection while T311 is running 5.3.7.3
        rrc_log->info("Cell Selection finished. Initiating transmission of RRC Connection Reestablishment Request\n");

        mac_timers->timer_get(t301)->reset();
        mac_timers->timer_get(t301)->run();
        mac_timers->timer_get(t311)->stop();
        send_ul_ccch_msg();
      } else {
        rrc_log->info("T311 expired while selecting cell. Going to IDLE\n");
        go_idle = true;
      }
    } else {
      rrc_log->warning("Could not re-synchronize with cell.\n");
      go_idle = true;
    }
  } else {
    rrc_log->info("Selected cell no longer suitable for camping (in_sync=%s). Going to IDLE\n",
                  serving_cell->in_sync ? "yes" : "no");
    go_idle = true;
  }
}

void rrc::send_con_restablish_complete() {

  rrc_log->debug("Preparing RRC Connection Reestablishment Complete\n");
  rrc_log->console("RRC Connected\n");

  // Prepare ConnectionSetupComplete packet
  ul_dcch_msg.msg.set(asn1::rrc::ul_dcch_msg_type_c::types::c1);
  ul_dcch_msg.msg.c1().set(asn1::rrc::ul_dcch_msg_type_c::c1_c_::types::rrc_conn_reest_complete);
  ul_dcch_msg.msg.c1().rrc_conn_reest_complete().crit_exts.set(
      asn1::rrc::rrc_conn_reest_complete_s::crit_exts_c_::types::rrc_conn_reest_complete_r8);

  ul_dcch_msg.msg.c1().rrc_conn_reest_complete().rrc_transaction_id = transaction_id;

  send_ul_dcch_msg(RB_ID_SRB1);
}

void rrc::send_con_setup_complete(byte_buffer_t *nas_msg) {
  rrc_log->debug("Preparing RRC Connection Setup Complete\n");

  // Prepare ConnectionSetupComplete packet
  ul_dcch_msg.msg.set(asn1::rrc::ul_dcch_msg_type_c::types::c1);
  ul_dcch_msg.msg.c1().set(asn1::rrc::ul_dcch_msg_type_c::c1_c_::types::rrc_conn_setup_complete);
  ul_dcch_msg.msg.c1().rrc_conn_setup_complete().crit_exts.set(
      asn1::rrc::rrc_conn_setup_complete_s::crit_exts_c_::types::c1);
  ul_dcch_msg.msg.c1().rrc_conn_setup_complete().crit_exts.c1().set(
      asn1::rrc::rrc_conn_setup_complete_s::crit_exts_c_::c1_c_::types::rrc_conn_setup_complete_r8);
  rrc_conn_setup_complete_r8_ies_s* rrc_conn_setup_complete =
      &ul_dcch_msg.msg.c1().rrc_conn_setup_complete().crit_exts.c1().rrc_conn_setup_complete_r8();

  ul_dcch_msg.msg.c1().rrc_conn_setup_complete().rrc_transaction_id = transaction_id;

  rrc_conn_setup_complete->sel_plmn_id = 1;
  rrc_conn_setup_complete->ded_info_nas.resize(nas_msg->N_bytes);
  memcpy(rrc_conn_setup_complete->ded_info_nas.data(), nas_msg->msg, nas_msg->N_bytes); // TODO Check!

  pool->deallocate(nas_msg);
  send_ul_dcch_msg(RB_ID_SRB1);
}

void rrc::send_ul_info_transfer(byte_buffer_t* nas_msg)
{
  uint32_t lcid = rlc->has_bearer(RB_ID_SRB2) ? RB_ID_SRB2 : RB_ID_SRB1;

  // Prepare UL INFO packet
  ul_dcch_msg.msg.set(asn1::rrc::ul_dcch_msg_type_c::types::c1);
  ul_dcch_msg.msg.c1().set(asn1::rrc::ul_dcch_msg_type_c::c1_c_::types::ul_info_transfer);
  ul_dcch_msg.msg.c1().ul_info_transfer().crit_exts.set(asn1::rrc::ul_info_transfer_s::crit_exts_c_::types::c1);
  ul_dcch_msg.msg.c1().ul_info_transfer().crit_exts.c1().set(
      asn1::rrc::ul_info_transfer_s::crit_exts_c_::c1_c_::types::ul_info_transfer_r8);
  ul_info_transfer_r8_ies_s* rrc_ul_info_transfer =
      &ul_dcch_msg.msg.c1().ul_info_transfer().crit_exts.c1().ul_info_transfer_r8();

  rrc_ul_info_transfer->ded_info_type.set(asn1::rrc::ul_info_transfer_r8_ies_s::ded_info_type_c_::types::ded_info_nas);
  rrc_ul_info_transfer->ded_info_type.ded_info_nas().resize(nas_msg->N_bytes);
  memcpy(rrc_ul_info_transfer->ded_info_type.ded_info_nas().data(), nas_msg->msg, nas_msg->N_bytes); // TODO Check!

  pool->deallocate(nas_msg);

  send_ul_dcch_msg(lcid);
}

void rrc::send_security_mode_complete() {
  rrc_log->debug("Preparing Security Mode Complete\n");

  // Prepare Security Mode Command Complete
  ul_dcch_msg.msg.set(asn1::rrc::ul_dcch_msg_type_c::types::c1);
  ul_dcch_msg.msg.c1().set(asn1::rrc::ul_dcch_msg_type_c::c1_c_::types::security_mode_complete);
  ul_dcch_msg.msg.c1().security_mode_complete().crit_exts.set(
      asn1::rrc::security_mode_complete_s::crit_exts_c_::types::security_mode_complete_r8);

  ul_dcch_msg.msg.c1().security_mode_complete().rrc_transaction_id = transaction_id;

  send_ul_dcch_msg(RB_ID_SRB1);
}

void rrc::send_rrc_con_reconfig_complete() {
  rrc_log->debug("Preparing RRC Connection Reconfig Complete\n");

  ul_dcch_msg.msg.set(asn1::rrc::ul_dcch_msg_type_c::types::c1);
  ul_dcch_msg.msg.c1().set(asn1::rrc::ul_dcch_msg_type_c::c1_c_::types::rrc_conn_recfg_complete);
  ul_dcch_msg.msg.c1().rrc_conn_recfg_complete().crit_exts.set(
      asn1::rrc::rrc_conn_recfg_complete_s::crit_exts_c_::types::rrc_conn_recfg_complete_r8);

  ul_dcch_msg.msg.c1().rrc_conn_recfg_complete().rrc_transaction_id = transaction_id;

  send_ul_dcch_msg(RB_ID_SRB1);
}

bool rrc::ho_prepare() {
  if (pending_mob_reconf) {
    asn1::rrc::rrc_conn_recfg_r8_ies_s* mob_reconf_r8 = &mob_reconf.crit_exts.c1().rrc_conn_recfg_r8();
    asn1::rrc::mob_ctrl_info_s*         mob_ctrl_info = &mob_reconf_r8->mob_ctrl_info;
    rrc_log->info("Processing HO command to target PCell=%d\n", mob_ctrl_info->target_pci);

    int target_cell_idx = find_neighbour_cell(serving_cell->get_earfcn(), mob_ctrl_info->target_pci);
    if (target_cell_idx < 0) {
      rrc_log->console("Received HO command to unknown PCI=%d\n", mob_ctrl_info->target_pci);
      rrc_log->error("Could not find target cell earfcn=%d, pci=%d\n", serving_cell->get_earfcn(),
                     mob_ctrl_info->target_pci);
      return false;
    }

    // Section 5.3.5.4
    mac_timers->timer_get(t310)->stop();
    mac_timers->timer_get(t304)->set(this, mob_ctrl_info->t304.to_number());
    if (mob_ctrl_info->carrier_freq_present &&
        mob_ctrl_info->carrier_freq.dl_carrier_freq != serving_cell->get_earfcn()) {
      rrc_log->error("Received mobilityControlInfo for inter-frequency handover\n");
      return false;
    }

    // Save serving cell and current configuration
    ho_src_cell = *serving_cell;
    mac_interface_rrc::ue_rnti_t uernti;
    mac->get_rntis(&uernti);
    ho_src_rnti = uernti.crnti;

    // Reset/Reestablish stack
    mac->clear_rntis();
    phy->meas_reset();
    mac->wait_uplink();
    pdcp->reestablish();
    rlc->reestablish();
    mac->reset();
    phy->reset();

    mac->set_ho_rnti(mob_ctrl_info->new_ue_id.to_number(), mob_ctrl_info->target_pci);
    apply_rr_config_common_dl(&mob_ctrl_info->rr_cfg_common);

    if (!phy->cell_select(&neighbour_cells[target_cell_idx]->phy_cell)) {
      rrc_log->error("Could not synchronize with target cell pci=%d. Trying to return to source PCI\n",
                     neighbour_cells[target_cell_idx]->get_pci());
      return false;
    }

    set_serving_cell(target_cell_idx);

    if (mob_ctrl_info->rach_cfg_ded_present) {
      rrc_log->info("Starting non-contention based RA with preamble_idx=%d, mask_idx=%d\n",
                    mob_ctrl_info->rach_cfg_ded.ra_preamb_idx, mob_ctrl_info->rach_cfg_ded.ra_prach_mask_idx);
      mac->start_noncont_ho(mob_ctrl_info->rach_cfg_ded.ra_preamb_idx, mob_ctrl_info->rach_cfg_ded.ra_prach_mask_idx);
    } else {
      rrc_log->info("Starting contention-based RA\n");
      mac->start_cont_ho();
    }

    int ncc = -1;
    if (mob_reconf_r8->security_cfg_ho_present) {
      ncc = mob_reconf_r8->security_cfg_ho.ho_type.intra_lte().next_hop_chaining_count;
      if (mob_reconf_r8->security_cfg_ho.ho_type.intra_lte().key_change_ind) {
        rrc_log->console("keyChangeIndicator in securityConfigHO not supported\n");
        return false;
      }
      if (mob_reconf_r8->security_cfg_ho.ho_type.intra_lte().security_algorithm_cfg_present) {
        cipher_algo = (CIPHERING_ALGORITHM_ID_ENUM)mob_reconf_r8->security_cfg_ho.ho_type.intra_lte()
                          .security_algorithm_cfg.ciphering_algorithm.to_number();
        integ_algo = (INTEGRITY_ALGORITHM_ID_ENUM)mob_reconf_r8->security_cfg_ho.ho_type.intra_lte()
                         .security_algorithm_cfg.integrity_prot_algorithm.to_number();
        rrc_log->info("Changed Ciphering to %s and Integrity to %s\n",
                      ciphering_algorithm_id_text[cipher_algo],
                      integrity_algorithm_id_text[integ_algo]);
      }
    }

    usim->generate_as_keys_ho(mob_ctrl_info->target_pci, phy->get_current_earfcn(), ncc, k_rrc_enc, k_rrc_int, k_up_enc,
                              k_up_int, cipher_algo, integ_algo);

    pdcp->config_security_all(k_rrc_enc, k_rrc_int, cipher_algo, integ_algo);
    send_rrc_con_reconfig_complete();
  }
  return true;
}

void rrc::ho_ra_completed(bool ra_successful) {

  if (pending_mob_reconf) {
    asn1::rrc::rrc_conn_recfg_r8_ies_s* mob_reconf_r8 = &mob_reconf.crit_exts.c1().rrc_conn_recfg_r8();
    if (ra_successful) {
      measurements.ho_finish();

      if (mob_reconf_r8->meas_cfg_present) {
        measurements.parse_meas_config(&mob_reconf_r8->meas_cfg);
      }

      mac_timers->timer_get(t304)->stop();

      apply_rr_config_common_ul(&mob_reconf_r8->mob_ctrl_info.rr_cfg_common);
      if (mob_reconf_r8->rr_cfg_ded_present) {
        apply_rr_config_dedicated(&mob_reconf_r8->rr_cfg_ded);
      }
    }
    // T304 will expiry and send ho_failure

    rrc_log->info("HO %ssuccessful\n", ra_successful?"":"un");
    rrc_log->console("HO %ssuccessful\n", ra_successful?"":"un");

    pending_mob_reconf = false;
  } else {
    rrc_log->error("Received HO random access completed but no pending mobility reconfiguration info\n");
  }
}

bool rrc::con_reconfig_ho(asn1::rrc::rrc_conn_recfg_s* reconfig)
{
  asn1::rrc::rrc_conn_recfg_r8_ies_s* mob_reconf_r8 = &reconfig->crit_exts.c1().rrc_conn_recfg_r8();
  if (mob_reconf_r8->mob_ctrl_info.target_pci == phy->get_current_pci()) {
    rrc_log->console("Warning: Received HO command to own cell\n");
    rrc_log->warning("Received HO command to own cell\n");
    return false;
  }

  rrc_log->info("Received HO command to target PCell=%d\n", mob_reconf_r8->mob_ctrl_info.target_pci);
  rrc_log->console("Received HO command to target PCell=%d, NCC=%d\n", mob_reconf_r8->mob_ctrl_info.target_pci,
                   mob_reconf_r8->security_cfg_ho.ho_type.intra_lte().next_hop_chaining_count);

  // store mobilityControlInfo
  mob_reconf         = *reconfig;
  pending_mob_reconf = true;

  ho_start = true;
  return true;
}

// Handle RRC Reconfiguration without MobilityInformation Section 5.3.5.3
bool rrc::con_reconfig(asn1::rrc::rrc_conn_recfg_s* reconfig)
{
  asn1::rrc::rrc_conn_recfg_r8_ies_s* reconfig_r8 = &reconfig->crit_exts.c1().rrc_conn_recfg_r8();
  if (reconfig_r8->rr_cfg_ded_present) {
    if (!apply_rr_config_dedicated(&reconfig_r8->rr_cfg_ded)) {
      return false;
    }
  }
  if (reconfig_r8->meas_cfg_present) {
    if (!measurements.parse_meas_config(&reconfig_r8->meas_cfg)) {
      return false;
    }
  }

  send_rrc_con_reconfig_complete();

  byte_buffer_t *nas_sdu;
  for (uint32_t i = 0; i < reconfig_r8->ded_info_nas_list.size(); i++) {
    nas_sdu = pool_allocate;
    if (nas_sdu) {
      memcpy(nas_sdu->msg, reconfig_r8->ded_info_nas_list[i].data(), reconfig_r8->ded_info_nas_list[i].size());
      nas_sdu->N_bytes = reconfig_r8->ded_info_nas_list[i].size();
      nas->write_pdu(RB_ID_SRB1, nas_sdu);
    } else {
      rrc_log->error("Fatal Error: Couldn't allocate PDU in handle_rrc_con_reconfig().\n");
      return false;
    }
  }
  return true;
}

// HO failure from T304 expiry 5.3.5.6
void rrc::ho_failed()
{
  send_con_restablish_request(asn1::rrc::reest_cause_e::ho_fail);
}

// Reconfiguration failure or Section 5.3.5.5
void rrc::con_reconfig_failed()
{
  // Set previous PHY/MAC configuration
  phy->set_config(&previous_phy_cfg);
  mac->set_config(&previous_mac_cfg);

  if (security_is_activated) {
    // Start the Reestablishment Procedure
    send_con_restablish_request(asn1::rrc::reest_cause_e::recfg_fail);
  } else {
    go_idle = true;
  }
}

void rrc::handle_rrc_con_reconfig(uint32_t lcid, asn1::rrc::rrc_conn_recfg_s* reconfig)
{
  phy->get_config(&previous_phy_cfg);
  mac->get_config(&previous_mac_cfg);

  asn1::rrc::rrc_conn_recfg_r8_ies_s* reconfig_r8 = &reconfig->crit_exts.c1().rrc_conn_recfg_r8();
  if (reconfig_r8->mob_ctrl_info_present) {
    if (!con_reconfig_ho(reconfig)) {
      con_reconfig_failed();
    }
  } else {
    if (!con_reconfig(reconfig)) {
      con_reconfig_failed();
    }
  }
}

/* Actions upon reception of RRCConnectionRelease 5.3.8.3 */
void rrc::rrc_connection_release() {
  // Save idleModeMobilityControlInfo, etc.
  rrc_log->console("Received RRC Connection Release\n");
  go_idle = true;
}

/* Actions upon leaving RRC_CONNECTED 5.3.12 */
void rrc::leave_connected()
{
  rrc_log->console("RRC IDLE\n");
  rrc_log->info("Leaving RRC_CONNECTED state\n");
  state = RRC_STATE_IDLE;
  rlc_flush_counter     = 0;
  drb_up = false;
  security_is_activated = false;
  measurements.reset();
  pdcp->reset();
  rlc->reset();
  phy->reset();
  mac->reset();
  set_phy_default();
  set_mac_default();
  stop_timers();
  rrc_log->info("Going RRC_IDLE\n");
  if (phy->cell_is_camping()) {
    // Receive paging
    mac->pcch_start_rx();
    // Instruct PHY to measure serving cell for cell reselection
    phy->meas_start(phy->get_current_earfcn(), phy->get_current_pci());
  }
}

void rrc::stop_timers()
{
  mac_timers->timer_get(t300)->stop();
  mac_timers->timer_get(t301)->stop();
  mac_timers->timer_get(t310)->stop();
  mac_timers->timer_get(t311)->stop();
  mac_timers->timer_get(t304)->stop();
}

/*******************************************************************************
*
*
*
* Reception of Broadcast messages (MIB and SIBs)
*
*
*
*******************************************************************************/
void rrc::write_pdu_bcch_bch(byte_buffer_t *pdu) {
  // Do we need to do something with BCH?
  rrc_log->info_hex(pdu->msg, pdu->N_bytes, "BCCH BCH message received.");
  pool->deallocate(pdu);
}

void rrc::write_pdu_bcch_dlsch(byte_buffer_t *pdu) {
  mac->clear_rntis();

  asn1::rrc::bcch_dl_sch_msg_s dlsch_msg;
  asn1::bit_ref                dlsch_bref(pdu->msg, pdu->N_bytes);
  asn1::SRSASN_CODE            err = dlsch_msg.unpack(dlsch_bref);
  if (err != asn1::SRSASN_SUCCESS) {
    rrc_log->error("Could not unpack BCCH DL-SCH message.\n");
    return;
  }

  log_rrc_message("BCCH", Rx, pdu, dlsch_msg);
  pool->deallocate(pdu);

  if (dlsch_msg.msg.c1().type() == bcch_dl_sch_msg_type_c::c1_c_::types::sib_type1) {
    rrc_log->info("Processing SIB1 (1/1)\n");
    serving_cell->set_sib1(&dlsch_msg.msg.c1().sib_type1());
    handle_sib1();
  } else {
    sys_info_r8_ies_s::sib_type_and_info_l_& sib_list =
        dlsch_msg.msg.c1().sys_info().crit_exts.sys_info_r8().sib_type_and_info;
    for (uint32_t i = 0; i < sib_list.size(); ++i) {
      rrc_log->info("Processing SIB%d (%d/%d)\n", sib_list[i].type().to_number(), i, sib_list.size());
      switch (sib_list[i].type().value) {
        case sib_info_item_c::types::sib2:
          if (not serving_cell->has_sib2()) {
            serving_cell->set_sib2(&sib_list[i].sib2());
          }
          handle_sib2();
          break;
        case sib_info_item_c::types::sib3:
          if (not serving_cell->has_sib3()) {
            serving_cell->set_sib3(&sib_list[i].sib3());
          }
          handle_sib3();
          break;
        case sib_info_item_c::types::sib13_v920:
          if (not serving_cell->has_sib13()) {
            serving_cell->set_sib13(&sib_list[i].sib13_v920());
          }
          handle_sib13();
          break;
        default:
          rrc_log->warning("SIB%d is not supported\n", sib_list[i].type().to_number());
      }
    }
  }
}

void rrc::handle_sib1()
{
  sib_type1_s* sib1 = serving_cell->sib1ptr();
  rrc_log->info("SIB1 received, CellID=%d, si_window=%d, sib2_period=%d\n", serving_cell->get_cell_id() & 0xfff,
                sib1->si_win_len.to_number(), sib1->sched_info_list[0].si_periodicity.to_number());

  // Print SIB scheduling info
  uint32_t i,j;
  for (uint32_t i = 0; i < sib1->sched_info_list.size(); ++i) {
    sched_info_s::si_periodicity_e_ p = sib1->sched_info_list[i].si_periodicity;
    for (uint32_t j = 0; j < sib1->sched_info_list[i].sib_map_info.size(); ++j) {
      sib_type_e t = sib1->sched_info_list[i].sib_map_info[j];
      rrc_log->debug("SIB scheduling info, sib_type=%d, si_periodicity=%d\n", t.to_number(), p.to_number());
    }
  }

  // Set TDD Config
  if (sib1->tdd_cfg_present) {
    phy->set_config_tdd(&sib1->tdd_cfg);
  }
}

void rrc::handle_sib2()
{
  rrc_log->info("SIB2 received\n");

  apply_sib2_configs(serving_cell->sib2ptr());

}

void rrc::handle_sib3()
{
  rrc_log->info("SIB3 received\n");

  sib_type3_s* sib3 = serving_cell->sib3ptr();

  // cellReselectionInfoCommon
  cell_resel_cfg.q_hyst = sib3->cell_resel_info_common.q_hyst.to_number();

  // cellReselectionServingFreqInfo
  cell_resel_cfg.threshservinglow = sib3->thresh_serving_low_q_r9; // TODO: Check first if present

  // intraFreqCellReselectionInfo
  cell_resel_cfg.Qrxlevmin = sib3->intra_freq_cell_resel_info.q_rx_lev_min;
  if (sib3->intra_freq_cell_resel_info.s_intra_search_present) {
    cell_resel_cfg.s_intrasearchP = sib3->intra_freq_cell_resel_info.s_intra_search;
  } else {
    cell_resel_cfg.s_intrasearchP  = INFINITY;
  }
}

void rrc::handle_sib13()
{
  rrc_log->info("SIB13 received\n");
  apply_sib13_configs(serving_cell->sib13ptr());
}

/*******************************************************************************
*
*
*
* Reception of Paging messages
*
*
*
*******************************************************************************/
void rrc::write_pdu_pcch(byte_buffer_t *pdu) {
  cmd_msg_t msg;
  msg.pdu = pdu;
  msg.command = cmd_msg_t::PCCH;
  cmd_q.push(msg);
}

void rrc::process_pcch(byte_buffer_t *pdu) {
  if (pdu->N_bytes > 0 && pdu->N_bytes < SRSLTE_MAX_BUFFER_SIZE_BITS) {
    pcch_msg_s    pcch_msg;
    asn1::bit_ref bref(pdu->msg, pdu->N_bytes);
    pcch_msg.unpack(bref);
    log_rrc_message("PCCH", Rx, pdu, pcch_msg);
    pool->deallocate(pdu);

    paging_s* paging = &pcch_msg.msg.c1().paging();
    if (paging->paging_record_list.size() > ASN1_RRC_MAX_PAGE_REC) {
      paging->paging_record_list.resize(ASN1_RRC_MAX_PAGE_REC);
    }

    if (not ue_identity_configured) {
      rrc_log->warning("Received paging message but no ue-Identity is configured\n");
      return;
    }

    s_tmsi_s* s_tmsi_paged;
    for (uint32_t i = 0; i < paging->paging_record_list.size(); i++) {
      s_tmsi_paged = &paging->paging_record_list[i].ue_id.s_tmsi();
      rrc_log->info("Received paging (%d/%d) for UE %" PRIu64 ":%" PRIu64 "\n", i + 1,
                    paging->paging_record_list.size(), paging->paging_record_list[i].ue_id.s_tmsi().mmec.to_number(),
                    paging->paging_record_list[i].ue_id.s_tmsi().m_tmsi.to_number());
      if (ue_identity.mmec == s_tmsi_paged->mmec && ue_identity.m_tmsi == s_tmsi_paged->m_tmsi) {
        if (RRC_STATE_IDLE == state) {
          rrc_log->info("S-TMSI match in paging message\n");
          rrc_log->console("S-TMSI match in paging message\n");
          nas->paging(s_tmsi_paged);
        } else {
          rrc_log->warning("Received paging while in CONNECT\n");
        }
      } else {
        rrc_log->info("Received paging for unknown identity\n");
      }
    }

    if (paging->sys_info_mod_present) {
      rrc_log->info("Received System Information notifcation update request.\n");
      // invalidate and then update all SIBs of serving cell
      serving_cell->reset_sibs();
      if (configure_serving_cell()) {
        rrc_log->info("All SIBs of serving cell obtained successfully\n");
      } else {
        rrc_log->error("While obtaining SIBs of serving cell.\n");
      }
    }
  }
}

void rrc::write_pdu_mch(uint32_t lcid, srslte::byte_buffer_t *pdu)
{
  if (pdu->N_bytes > 0 && pdu->N_bytes < SRSLTE_MAX_BUFFER_SIZE_BITS) {
    //TODO: handle MCCH notifications and update MCCH
    if(0 == lcid && !serving_cell->has_mcch) {
      asn1::bit_ref bref(pdu->msg, pdu->N_bytes);
      serving_cell->mcch.unpack(bref);
      serving_cell->has_mcch = true;
      phy->set_config_mbsfn_mcch(&serving_cell->mcch);
      log_rrc_message("MCH", Rx, pdu, serving_cell->mcch);
    }

    pool->deallocate(pdu);
  }
}





/*******************************************************************************
*
*
*
* Packet processing
*
*
*******************************************************************************/

void rrc::send_ul_ccch_msg()
{
  // Reset and reuse sdu buffer if provided
  byte_buffer_t *pdcp_buf = pool_allocate_blocking;
  if (not pdcp_buf) {
    rrc_log->error("Fatal Error: Couldn't allocate PDU in byte_align_and_pack().\n");
    return;
  }

  asn1::bit_ref bref(pdcp_buf->msg, pdcp_buf->get_tailroom());
  ul_ccch_msg.pack(bref);
  bref.align_bytes_zero();
  pdcp_buf->N_bytes = (uint32_t)bref.distance_bytes(pdcp_buf->msg);
  pdcp_buf->set_timestamp();

  // Set UE contention resolution ID in MAC
  uint64_t uecri      = 0;
  uint8_t* ue_cri_ptr = (uint8_t*)&uecri;
  uint32_t nbytes     = 6;
  for (uint32_t i = 0; i < nbytes; i++) {
    ue_cri_ptr[nbytes - i - 1] = pdcp_buf->msg[i];
  }

  rrc_log->debug("Setting UE contention resolution ID: %" PRIu64 "\n", uecri);
  mac->set_contention_id(uecri);

  uint32_t lcid = RB_ID_SRB0;
  log_rrc_message(get_rb_name(lcid).c_str(), Tx, pdcp_buf, ul_ccch_msg);

  pdcp->write_sdu(lcid, pdcp_buf);
}

void rrc::send_ul_dcch_msg(uint32_t lcid)
{
  // Reset and reuse sdu buffer if provided
  byte_buffer_t* pdcp_buf = pool_allocate_blocking;
  if (not pdcp_buf) {
    rrc_log->error("Fatal Error: Couldn't allocate PDU in byte_align_and_pack().\n");
    return;
  }

  asn1::bit_ref bref(pdcp_buf->msg, pdcp_buf->get_tailroom());
  ul_dcch_msg.pack(bref);
  bref.align_bytes_zero();
  pdcp_buf->N_bytes = (uint32_t)bref.distance_bytes(pdcp_buf->msg);
  pdcp_buf->set_timestamp();

  log_rrc_message(get_rb_name(lcid).c_str(), Tx, pdcp_buf, ul_dcch_msg);

  pdcp->write_sdu(lcid, pdcp_buf);
}

void rrc::write_sdu(byte_buffer_t *sdu) {

  if (state == RRC_STATE_IDLE) {
    rrc_log->warning("Received ULInformationTransfer SDU when in IDLE\n");
    return;
  }
  send_ul_info_transfer(sdu);
}

void rrc::write_pdu(uint32_t lcid, byte_buffer_t* pdu)
{
  // If the message contains a ConnectionSetup, acknowledge the transmission to avoid blocking of paging procedure
  if (lcid == 0) {
    // FIXME: We unpack and process this message twice to check if it's ConnectionSetup
    asn1::bit_ref bref(pdu->msg, pdu->N_bytes);
    dl_ccch_msg.unpack(bref);
    if (dl_ccch_msg.msg.c1().type() == dl_ccch_msg_type_c::c1_c_::types::rrc_conn_setup) {
      // Must enter CONNECT before stopping T300
      state = RRC_STATE_CONNECTED;

      mac_timers->timer_get(t300)->stop();
      mac_timers->timer_get(t302)->stop();
      rrc_log->console("RRC Connected\n");
    }
  }

  // add PDU to command queue
  cmd_msg_t msg;
  msg.pdu = pdu;
  msg.command = cmd_msg_t::PDU;
  msg.lcid    = (uint16_t)lcid;
  cmd_q.push(msg);
}

void rrc::process_pdu(uint32_t lcid, byte_buffer_t *pdu)
{
  switch (lcid) {
    case RB_ID_SRB0:
      parse_dl_ccch(pdu);
      break;
    case RB_ID_SRB1:
    case RB_ID_SRB2:
      parse_dl_dcch(lcid, pdu);
      break;
    default:
      rrc_log->error("RX PDU with invalid bearer id: %d", lcid);
      break;
  }
}

void rrc::parse_dl_ccch(byte_buffer_t* pdu)
{
  asn1::bit_ref bref(pdu->msg, pdu->N_bytes);
  dl_ccch_msg.unpack(bref);
  log_rrc_message(get_rb_name(RB_ID_SRB0).c_str(), Rx, pdu, dl_ccch_msg);
  pool->deallocate(pdu);

  dl_ccch_msg_type_c::c1_c_* c1 = &dl_ccch_msg.msg.c1();
  switch (dl_ccch_msg.msg.c1().type().value) {
    case dl_ccch_msg_type_c::c1_c_::types::rrc_conn_reject: {
      // 5.3.3.8
      rrc_conn_reject_r8_ies_s* reject_r8 = &c1->rrc_conn_reject().crit_exts.c1().rrc_conn_reject_r8();
      rrc_log->info("Received ConnectionReject. Wait time: %d\n", reject_r8->wait_time);
      rrc_log->console("Received ConnectionReject. Wait time: %d\n", reject_r8->wait_time);

      mac_timers->timer_get(t300)->stop();

      if (reject_r8->wait_time) {
        nas->set_barring(nas_interface_rrc::BARRING_ALL);
        mac_timers->timer_get(t302)->set(this, reject_r8->wait_time * 1000u);
        mac_timers->timer_get(t302)->run();
      } else {
        // Perform the actions upon expiry of T302 if wait time is zero
        nas->set_barring(nas_interface_rrc::BARRING_NONE);
        go_idle = true;
      }
    } break;
    case dl_ccch_msg_type_c::c1_c_::types::rrc_conn_setup:
      transaction_id = c1->rrc_conn_setup().rrc_transaction_id;
      handle_con_setup(&c1->rrc_conn_setup());
      break;
    case dl_ccch_msg_type_c::c1_c_::types::rrc_conn_reest:
      rrc_log->console("Reestablishment OK\n");
      transaction_id = c1->rrc_conn_reest().rrc_transaction_id;
      handle_con_reest(&c1->rrc_conn_reest());
      break;
      /* Reception of RRCConnectionReestablishmentReject 5.3.7.8 */
    case dl_ccch_msg_type_c::c1_c_::types::rrc_conn_reest_reject:
      rrc_log->console("Reestablishment Reject\n");
      go_idle = true;
      break;
    default:
      rrc_log->error("The provided DL-CCCH message type is not recognized\n");
      break;
  }
}

void rrc::parse_dl_dcch(uint32_t lcid, byte_buffer_t* pdu)
{
  asn1::bit_ref bref(pdu->msg, pdu->N_bytes);
  dl_dcch_msg.unpack(bref);
  log_rrc_message(get_rb_name(lcid).c_str(), Rx, pdu, dl_dcch_msg);
  pool->deallocate(pdu);

  dl_dcch_msg_type_c::c1_c_* c1 = &dl_dcch_msg.msg.c1();
  switch (dl_dcch_msg.msg.c1().type().value) {
    case dl_dcch_msg_type_c::c1_c_::types::dl_info_transfer:
      pdu = pool_allocate_blocking;
      if (!pdu) {
        rrc_log->error("Fatal error: out of buffers in pool\n");
        return;
      }
      pdu->N_bytes = c1->dl_info_transfer().crit_exts.c1().dl_info_transfer_r8().ded_info_type.ded_info_nas().size();
      memcpy(pdu->msg, c1->dl_info_transfer().crit_exts.c1().dl_info_transfer_r8().ded_info_type.ded_info_nas().data(),
             pdu->N_bytes);
      nas->write_pdu(lcid, pdu);
      break;
    case dl_dcch_msg_type_c::c1_c_::types::security_mode_cmd:
      transaction_id = c1->security_mode_cmd().rrc_transaction_id;

      cipher_algo = (CIPHERING_ALGORITHM_ID_ENUM)c1->security_mode_cmd()
                        .crit_exts.c1()
                        .security_mode_cmd_r8()
                        .security_cfg_smc.security_algorithm_cfg.ciphering_algorithm.value;
      integ_algo = (INTEGRITY_ALGORITHM_ID_ENUM)c1->security_mode_cmd()
                       .crit_exts.c1()
                       .security_mode_cmd_r8()
                       .security_cfg_smc.security_algorithm_cfg.integrity_prot_algorithm.value;

      rrc_log->info("Received Security Mode Command eea: %s, eia: %s\n", ciphering_algorithm_id_text[cipher_algo],
                    integrity_algorithm_id_text[integ_algo]);

      // Generate AS security keys
      uint8_t k_asme[32];
      nas->get_k_asme(k_asme, 32);
      rrc_log->debug_hex(k_asme, 32, "UE K_asme");
      rrc_log->debug("Generating K_enb with UL NAS COUNT: %d\n", nas->get_k_enb_count());
      usim->generate_as_keys(k_asme, nas->get_k_enb_count(), k_rrc_enc, k_rrc_int, k_up_enc, k_up_int, cipher_algo,
                             integ_algo);
      rrc_log->info_hex(k_rrc_enc, 32, "RRC encryption key - k_rrc_enc");
      rrc_log->info_hex(k_rrc_int, 32, "RRC integrity key  - k_rrc_int");
      rrc_log->info_hex(k_up_enc, 32, "UP encryption key  - k_up_enc");

      security_is_activated = true;

      // Configure PDCP for security
      pdcp->config_security(lcid, k_rrc_enc, k_rrc_int, cipher_algo, integ_algo);
      pdcp->enable_integrity(lcid);
      send_security_mode_complete();
      pdcp->enable_encryption(lcid);
      break;
    case dl_dcch_msg_type_c::c1_c_::types::rrc_conn_recfg:
      transaction_id = c1->rrc_conn_recfg().rrc_transaction_id;
      handle_rrc_con_reconfig(lcid, &c1->rrc_conn_recfg());
      break;
    case dl_dcch_msg_type_c::c1_c_::types::ue_cap_enquiry:
      transaction_id = c1->ue_cap_enquiry().rrc_transaction_id;
      for (uint32_t i = 0; i < c1->ue_cap_enquiry().crit_exts.c1().ue_cap_enquiry_r8().ue_cap_request.size(); i++) {
        if (c1->ue_cap_enquiry().crit_exts.c1().ue_cap_enquiry_r8().ue_cap_request[i] == rat_type_e::eutra) {
          send_rrc_ue_cap_info();
          break;
        }
      }
      break;
    case dl_dcch_msg_type_c::c1_c_::types::rrc_conn_release:
      rrc_connection_release();
      break;
    default:
      rrc_log->error("The provided DL-CCCH message type is not recognized or supported\n");
      break;
  }
}

/*******************************************************************************
*
*
*
* Capabilities Message
*
*
*
*******************************************************************************/
void rrc::enable_capabilities()
{
  bool enable_ul_64 =
      args.ue_category >= 5 && serving_cell->sib2ptr()->rr_cfg_common.pusch_cfg_common.pusch_cfg_basic.enable64_qam;
  rrc_log->info("%s 64QAM PUSCH\n", enable_ul_64 ? "Enabling" : "Disabling");
  phy->set_config_64qam_en(enable_ul_64);
}

void rrc::send_rrc_ue_cap_info()
{
  rrc_log->debug("Preparing UE Capability Info\n");

  ul_dcch_msg.msg.set(ul_dcch_msg_type_c::types::c1);
  ul_dcch_msg.msg.c1().set(ul_dcch_msg_type_c::c1_c_::types::ue_cap_info);
  ul_dcch_msg.msg.c1().ue_cap_info().rrc_transaction_id = transaction_id;

  ul_dcch_msg.msg.c1().ue_cap_info().crit_exts.set(ue_cap_info_s::crit_exts_c_::types::c1);
  ul_dcch_msg.msg.c1().ue_cap_info().crit_exts.c1().set(ue_cap_info_s::crit_exts_c_::c1_c_::types::ue_cap_info_r8);
  ue_cap_info_r8_ies_s* info = &ul_dcch_msg.msg.c1().ue_cap_info().crit_exts.c1().ue_cap_info_r8();
  info->ue_cap_rat_container_list.resize(1);
  info->ue_cap_rat_container_list[0].rat_type = rat_type_e::eutra;

  ue_eutra_cap_s cap;
  cap.access_stratum_release                            = access_stratum_release_e::rel8;
  cap.ue_category                                       = (uint8_t)args.ue_category;
  cap.pdcp_params.max_num_rohc_context_sessions_present = false;

  cap.phy_layer_params.ue_specific_ref_sigs_supported = false;
  cap.phy_layer_params.ue_tx_ant_sel_supported        = false;

  cap.rf_params.supported_band_list_eutra.resize(args.nof_supported_bands);
  cap.meas_params.band_list_eutra.resize(args.nof_supported_bands);
  for (uint32_t i = 0; i < args.nof_supported_bands; i++) {
    cap.rf_params.supported_band_list_eutra[i].band_eutra  = args.supported_bands[i];
    cap.rf_params.supported_band_list_eutra[i].half_duplex = false;
    cap.meas_params.band_list_eutra[i].inter_freq_band_list.resize(1);
    cap.meas_params.band_list_eutra[i].inter_freq_band_list[0].inter_freq_need_for_gaps = true;
  }

  cap.feature_group_inds_present = true;
  cap.feature_group_inds.from_number(args.feature_group);

  // Pack caps and copy to cap info
  uint8_t       buf[64];
  asn1::bit_ref bref(buf, sizeof(buf));
  cap.pack(bref);
  bref.align_bytes_zero();
  uint32_t cap_len = (uint32_t)bref.distance_bytes(buf);
  info->ue_cap_rat_container_list[0].ue_cap_rat_container.resize(cap_len);
  memcpy(info->ue_cap_rat_container_list[0].ue_cap_rat_container.data(), buf, cap_len);

  send_ul_dcch_msg(RB_ID_SRB1);
}

/*******************************************************************************
*
*
*
* PHY and MAC Radio Resource configuration
*
*
*
*******************************************************************************/

void rrc::apply_rr_config_common_dl(rr_cfg_common_s* config)
{
  mac_interface_rrc::mac_cfg_t mac_cfg;
  mac->get_config(&mac_cfg);
  if (config->rach_cfg_common_present) {
    mac_cfg.rach                            = config->rach_cfg_common;
    mac_cfg.ul_harq_params.max_harq_msg3_tx = config->rach_cfg_common.max_harq_msg3_tx;
  }
  mac_cfg.prach_config_index = config->prach_cfg.root_seq_idx;

  mac->set_config(&mac_cfg);

  phy_interface_rrc::phy_cfg_t phy_cfg;
  phy->get_config(&phy_cfg);
  phy_interface_rrc::phy_cfg_common_t* common = &phy_cfg.common;

  if (config->pdsch_cfg_common_present) {
    common->pdsch_cnfg = config->pdsch_cfg_common;
  }
  common->prach_cnfg.root_seq_idx = config->prach_cfg.root_seq_idx;
  if (config->prach_cfg.prach_cfg_info_present) {
    common->prach_cnfg.prach_cfg_info = config->prach_cfg.prach_cfg_info;
  }

  phy->set_config_common(common);
}

void rrc::apply_rr_config_common_ul(rr_cfg_common_s* config)
{
  phy_interface_rrc::phy_cfg_t phy_cfg;
  phy->get_config(&phy_cfg);
  phy_interface_rrc::phy_cfg_common_t* common = &phy_cfg.common;

  common->pusch_cnfg = config->pusch_cfg_common;
  if (config->pucch_cfg_common_present) {
    common->pucch_cnfg = config->pucch_cfg_common;
  }
  if (config->ul_pwr_ctrl_common_present) {
    common->ul_pwr_ctrl = config->ul_pwr_ctrl_common;
  }
  if (config->srs_ul_cfg_common_present) {
    common->srs_ul_cnfg = config->srs_ul_cfg_common;
  }
  phy->set_config_common(common);
  phy->configure_ul_params();
}

void rrc::apply_sib2_configs(sib_type2_s* sib2)
{

  // Apply RACH timeAlginmentTimer configuration
  mac_interface_rrc::mac_cfg_t cfg;
  mac->get_config(&cfg);

  cfg.main.time_align_timer_ded       = sib2->time_align_timer_common;
  cfg.rach                            = sib2->rr_cfg_common.rach_cfg_common;
  cfg.prach_config_index              = sib2->rr_cfg_common.prach_cfg.root_seq_idx;
  cfg.ul_harq_params.max_harq_msg3_tx = cfg.rach.max_harq_msg3_tx;
  // Apply MBSFN configuration
  //  cfg.mbsfn_subfr_cnfg_list_size = sib2->mbsfn_subfr_cnfg_list_size;
  //  for(uint8_t i=0;i<sib2->mbsfn_subfr_cnfg_list_size;i++) {
  //    memcpy(&cfg.mbsfn_subfr_cnfg_list[i], &sib2->mbsfn_subfr_cnfg_list[i],
  //    sizeof(LIBLTE_RRC_MBSFN_SUBFRAME_CONFIG_STRUCT));
  //  }

  // Set MBSFN configs
  phy->set_config_mbsfn_sib2(sib2);

  mac->set_config(&cfg);

  rrc_log->info("Set RACH ConfigCommon: NofPreambles=%d, ResponseWindow=%d, ContentionResolutionTimer=%d ms\n",
                sib2->rr_cfg_common.rach_cfg_common.preamb_info.nof_ra_preambs.to_number(),
                sib2->rr_cfg_common.rach_cfg_common.ra_supervision_info.ra_resp_win_size.to_number(),
                sib2->rr_cfg_common.rach_cfg_common.ra_supervision_info.mac_contention_resolution_timer.to_number());

  // Apply PHY RR Config Common
  phy_interface_rrc::phy_cfg_common_t common;
  common.pdsch_cnfg  = sib2->rr_cfg_common.pdsch_cfg_common;
  common.pusch_cnfg  = sib2->rr_cfg_common.pusch_cfg_common;
  common.pucch_cnfg  = sib2->rr_cfg_common.pucch_cfg_common;
  common.ul_pwr_ctrl = sib2->rr_cfg_common.ul_pwr_ctrl_common;
  common.prach_cnfg  = sib2->rr_cfg_common.prach_cfg;
  common.srs_ul_cnfg = sib2->rr_cfg_common.srs_ul_cfg_common;
  phy->set_config_common(&common);

  phy->configure_ul_params();

  rrc_log->info("Set PUSCH ConfigCommon: HopOffset=%d, RSGroup=%d, RSNcs=%d, N_sb=%d\n",
                sib2->rr_cfg_common.pusch_cfg_common.pusch_cfg_basic.pusch_hop_offset,
                sib2->rr_cfg_common.pusch_cfg_common.ul_ref_sigs_pusch.group_assign_pusch,
                sib2->rr_cfg_common.pusch_cfg_common.ul_ref_sigs_pusch.cyclic_shift,
                sib2->rr_cfg_common.pusch_cfg_common.pusch_cfg_basic.n_sb);

  rrc_log->info("Set PUCCH ConfigCommon: DeltaShift=%d, CyclicShift=%d, N1=%d, NRB=%d\n",
                sib2->rr_cfg_common.pucch_cfg_common.delta_pucch_shift.to_number(),
                sib2->rr_cfg_common.pucch_cfg_common.n_cs_an, sib2->rr_cfg_common.pucch_cfg_common.n1_pucch_an,
                sib2->rr_cfg_common.pucch_cfg_common.n_rb_cqi);

  rrc_log->info("Set PRACH ConfigCommon: SeqIdx=%d, HS=%s, FreqOffset=%d, ZC=%d, ConfigIndex=%d\n",
                sib2->rr_cfg_common.prach_cfg.root_seq_idx,
                sib2->rr_cfg_common.prach_cfg.prach_cfg_info.high_speed_flag ? "yes" : "no",
                sib2->rr_cfg_common.prach_cfg.prach_cfg_info.prach_freq_offset,
                sib2->rr_cfg_common.prach_cfg.prach_cfg_info.zero_correlation_zone_cfg,
                sib2->rr_cfg_common.prach_cfg.prach_cfg_info.prach_cfg_idx);

  if (sib2->rr_cfg_common.srs_ul_cfg_common.type() == setup_e::setup) {
    rrc_log->info("Set SRS ConfigCommon: BW-Configuration=%d, SF-Configuration=%d, ACKNACK=%s\n",
                  sib2->rr_cfg_common.srs_ul_cfg_common.setup().srs_bw_cfg.to_number(),
                  sib2->rr_cfg_common.srs_ul_cfg_common.setup().srs_sf_cfg.to_number(),
                  sib2->rr_cfg_common.srs_ul_cfg_common.setup().ack_nack_srs_simul_tx ? "yes" : "no");
  }

  mac_timers->timer_get(t300)->set(this, sib2->ue_timers_and_consts.t300.to_number());
  mac_timers->timer_get(t301)->set(this, sib2->ue_timers_and_consts.t301.to_number());
  mac_timers->timer_get(t310)->set(this, sib2->ue_timers_and_consts.t310.to_number());
  mac_timers->timer_get(t311)->set(this, sib2->ue_timers_and_consts.t311.to_number());
  N310 = sib2->ue_timers_and_consts.n310.to_number();
  N311 = sib2->ue_timers_and_consts.n311.to_number();

  rrc_log->info("Set Constants and Timers: N310=%d, N311=%d, t300=%d, t301=%d, t310=%d, t311=%d\n", N310, N311,
                mac_timers->timer_get(t300)->get_timeout(), mac_timers->timer_get(t301)->get_timeout(),
                mac_timers->timer_get(t310)->get_timeout(), mac_timers->timer_get(t311)->get_timeout());
}

void rrc::apply_sib13_configs(sib_type13_r9_s* sib13)
{
  phy->set_config_mbsfn_sib13(sib13);
  add_mrb(0, 0); // Add MRB0
}

// Go through all information elements and apply defaults (9.2.4) if not defined
void rrc::apply_phy_config_dedicated(phys_cfg_ded_s* phy_cnfg, bool apply_defaults)
{
  // Get current configuration
  phys_cfg_ded_s*              current_cfg;
  phy_interface_rrc::phy_cfg_t c;
  phy->get_config(&c);
  current_cfg = &c.dedicated;

  current_cfg->pucch_cfg_ded_present = true;
  if (phy_cnfg->pucch_cfg_ded_present) {
    current_cfg->pucch_cfg_ded = phy_cnfg->pucch_cfg_ded;
  } else if (apply_defaults) {
    current_cfg->pucch_cfg_ded.tdd_ack_nack_feedback_mode_present = true;
    current_cfg->pucch_cfg_ded.tdd_ack_nack_feedback_mode = pucch_cfg_ded_s::tdd_ack_nack_feedback_mode_e_::bundling;
    current_cfg->pucch_cfg_ded.ack_nack_repeat.set(setup_e::release);
  }
  current_cfg->pusch_cfg_ded_present = true;
  if (phy_cnfg->pusch_cfg_ded_present) {
    current_cfg->pusch_cfg_ded = phy_cnfg->pusch_cfg_ded;
  } else if (apply_defaults) {
    current_cfg->pusch_cfg_ded.beta_offset_ack_idx = 10;
    current_cfg->pusch_cfg_ded.beta_offset_ri_idx  = 12;
    current_cfg->pusch_cfg_ded.beta_offset_cqi_idx = 15;
  }
  current_cfg->ul_pwr_ctrl_ded_present = true;
  if (phy_cnfg->ul_pwr_ctrl_ded_present) {
    current_cfg->ul_pwr_ctrl_ded = phy_cnfg->ul_pwr_ctrl_ded;
  } else if (apply_defaults) {
    current_cfg->ul_pwr_ctrl_ded.p0_ue_pusch          = 0;
    current_cfg->ul_pwr_ctrl_ded.delta_mcs_enabled    = ul_pwr_ctrl_ded_s::delta_mcs_enabled_e_::en0;
    current_cfg->ul_pwr_ctrl_ded.accumulation_enabled = true;
    current_cfg->ul_pwr_ctrl_ded.p0_ue_pucch          = 0;
    current_cfg->ul_pwr_ctrl_ded.p_srs_offset         = 7;
  }
  current_cfg->ul_pwr_ctrl_ded.filt_coef_present = true;
  if (phy_cnfg->ul_pwr_ctrl_ded.filt_coef_present) {
    current_cfg->ul_pwr_ctrl_ded.filt_coef = phy_cnfg->ul_pwr_ctrl_ded.filt_coef;
  } else {
    current_cfg->ul_pwr_ctrl_ded.filt_coef = filt_coef_e::fc4;
  }
  current_cfg->tpc_pdcch_cfg_pucch_present = true;
  if (phy_cnfg->tpc_pdcch_cfg_pucch_present) {
    current_cfg->tpc_pdcch_cfg_pucch = phy_cnfg->tpc_pdcch_cfg_pucch;
  } else if (apply_defaults) {
    current_cfg->tpc_pdcch_cfg_pucch.set(setup_e::release);
  }
  current_cfg->tpc_pdcch_cfg_pusch_present = true;
  if (phy_cnfg->tpc_pdcch_cfg_pusch_present) {
    current_cfg->tpc_pdcch_cfg_pusch = phy_cnfg->tpc_pdcch_cfg_pusch;
  } else {
    current_cfg->tpc_pdcch_cfg_pusch.set(setup_e::release);
  }
  if (phy_cnfg->cqi_report_cfg_present) {
    if (phy_cnfg->cqi_report_cfg.cqi_report_periodic_present and
        phy_cnfg->cqi_report_cfg.cqi_report_periodic.type() == setup_e::setup) {
      current_cfg->cqi_report_cfg_present                     = true;
      current_cfg->cqi_report_cfg.cqi_report_periodic_present = true;
      current_cfg->cqi_report_cfg.cqi_report_periodic         = phy_cnfg->cqi_report_cfg.cqi_report_periodic;
    } else if (apply_defaults) {
      current_cfg->cqi_report_cfg.cqi_report_periodic.set(setup_e::release);
    }
    if (phy_cnfg->cqi_report_cfg.cqi_report_mode_aperiodic_present) {
      current_cfg->cqi_report_cfg.cqi_report_mode_aperiodic_present = true;
      current_cfg->cqi_report_cfg.cqi_report_mode_aperiodic = phy_cnfg->cqi_report_cfg.cqi_report_mode_aperiodic;
    }
    current_cfg->cqi_report_cfg.nom_pdsch_rs_epre_offset = phy_cnfg->cqi_report_cfg.nom_pdsch_rs_epre_offset;
  }
  if (phy_cnfg->srs_ul_cfg_ded_present and phy_cnfg->srs_ul_cfg_ded.type() == setup_e::setup) {
    current_cfg->srs_ul_cfg_ded_present = true;
    current_cfg->srs_ul_cfg_ded         = phy_cnfg->srs_ul_cfg_ded;
  } else if (apply_defaults) {
    current_cfg->srs_ul_cfg_ded.set(setup_e::release);
  }
  current_cfg->ant_info_present = true;
  current_cfg->ant_info.set(phys_cfg_ded_s::ant_info_c_::types::explicit_value);
  if (phy_cnfg->ant_info_present) {
    if (phy_cnfg->ant_info.type() == phys_cfg_ded_s::ant_info_c_::types::explicit_value) {
      if (phy_cnfg->ant_info.explicit_value().tx_mode != ant_info_ded_s::tx_mode_e_::tm1 and
          phy_cnfg->ant_info.explicit_value().tx_mode != ant_info_ded_s::tx_mode_e_::tm2 and
          phy_cnfg->ant_info.explicit_value().tx_mode != ant_info_ded_s::tx_mode_e_::tm3 and
          phy_cnfg->ant_info.explicit_value().tx_mode != ant_info_ded_s::tx_mode_e_::tm4) {
        rrc_log->error("Transmission mode TM%s not currently supported by srsUE\n",
                       phy_cnfg->ant_info.explicit_value().tx_mode.to_string().c_str());
      }
      current_cfg->ant_info.explicit_value() = phy_cnfg->ant_info.explicit_value();
    } else if (apply_defaults) {
      current_cfg->ant_info.explicit_value().tx_mode                          = ant_info_ded_s::tx_mode_e_::tm2;
      current_cfg->ant_info.explicit_value().codebook_subset_restrict_present = false;
      current_cfg->ant_info.explicit_value().ue_tx_ant_sel.set(setup_e::release);
    }
  } else if (apply_defaults) {
    current_cfg->ant_info.explicit_value().tx_mode                          = ant_info_ded_s::tx_mode_e_::tm2;
    current_cfg->ant_info.explicit_value().codebook_subset_restrict_present = false;
    current_cfg->ant_info.explicit_value().ue_tx_ant_sel.set(setup_e::release);
  }
  if (phy_cnfg->sched_request_cfg_present and phy_cnfg->sched_request_cfg.type() == setup_e::setup) {
    current_cfg->sched_request_cfg_present = true;
    current_cfg->sched_request_cfg         = phy_cnfg->sched_request_cfg;
  } else if (apply_defaults) {
    current_cfg->sched_request_cfg.set(setup_e::release);
  }
  current_cfg->pdsch_cfg_ded_present = true;
  if (phy_cnfg->pdsch_cfg_ded_present) {
    current_cfg->pdsch_cfg_ded = phy_cnfg->pdsch_cfg_ded;
    rrc_log->info("Set PDSCH-Config=%s (present)\n", current_cfg->pdsch_cfg_ded.p_a.to_string().c_str());
  } else if (apply_defaults) {
    current_cfg->pdsch_cfg_ded.p_a = pdsch_cfg_ded_s::p_a_e_::db0;
    rrc_log->info("Set PDSCH-Config=%s (default)\n", current_cfg->pdsch_cfg_ded.p_a.to_string().c_str());
  }

  if (phy_cnfg->cqi_report_cfg_present) {
    if (phy_cnfg->cqi_report_cfg.cqi_report_periodic_present and
        phy_cnfg->cqi_report_cfg.cqi_report_periodic.type() == setup_e::setup) {
      rrc_log->info(
          "Set cqi-PUCCH-ResourceIndex=%d, cqi-pmi-ConfigIndex=%d, cqi-FormatIndicatorPeriodic=%s\n",
          current_cfg->cqi_report_cfg.cqi_report_periodic.setup().cqi_pucch_res_idx,
          current_cfg->cqi_report_cfg.cqi_report_periodic.setup().cqi_pmi_cfg_idx,
          current_cfg->cqi_report_cfg.cqi_report_periodic.setup().cqi_format_ind_periodic.type().to_string().c_str());
    }
    if (phy_cnfg->cqi_report_cfg.cqi_report_mode_aperiodic_present) {
      rrc_log->info("Set cqi-ReportModeAperiodic=%s\n",
                    current_cfg->cqi_report_cfg.cqi_report_mode_aperiodic.to_string().c_str());
    }
  }

  if (current_cfg->sched_request_cfg_present and current_cfg->sched_request_cfg.type() == setup_e::setup) {
    rrc_log->info("Set PHY config ded: SR-n_pucch=%d, SR-ConfigIndex=%d, SR-TransMax=%d\n",
                  current_cfg->sched_request_cfg.setup().sr_pucch_res_idx,
                  current_cfg->sched_request_cfg.setup().sr_cfg_idx,
                  current_cfg->sched_request_cfg.setup().dsr_trans_max.to_number());
  }

  if (current_cfg->srs_ul_cfg_ded_present and current_cfg->srs_ul_cfg_ded.type() == setup_e::setup) {
    rrc_log->info("Set PHY config ded: SRS-ConfigIndex=%d, SRS-bw=%s, SRS-Nrcc=%d, SRS-hop=%s, SRS-Ncs=%s\n",
                  current_cfg->srs_ul_cfg_ded.setup().srs_cfg_idx,
                  current_cfg->srs_ul_cfg_ded.setup().srs_bw.to_string().c_str(),
                  current_cfg->srs_ul_cfg_ded.setup().freq_domain_position,
                  current_cfg->srs_ul_cfg_ded.setup().srs_hop_bw.to_string().c_str(),
                  current_cfg->srs_ul_cfg_ded.setup().cyclic_shift.to_string().c_str());
  }

  phy->set_config_dedicated(current_cfg);

  // Apply changes to PHY
  phy->configure_ul_params();
}

void rrc::apply_mac_config_dedicated(mac_main_cfg_s* mac_cnfg, bool apply_defaults)
{
  // Set Default MAC main configuration (9.2.2)
  mac_main_cfg_s default_cfg;
  default_cfg.ul_sch_cfg_present            = true;
  default_cfg.ul_sch_cfg.max_harq_tx        = mac_main_cfg_s::ul_sch_cfg_s_::max_harq_tx_e_::n5;
  default_cfg.ul_sch_cfg.periodic_bsr_timer = periodic_bsr_timer_r12_e::infinity;
  default_cfg.ul_sch_cfg.retx_bsr_timer     = retx_bsr_timer_r12_e::sf2560;
  default_cfg.ul_sch_cfg.tti_bundling       = false;
  default_cfg.drx_cfg.set(setup_e::release);
  default_cfg.phr_cfg.set(setup_e::release);
  default_cfg.time_align_timer_ded = time_align_timer_e::infinity;

  if (!apply_defaults) {
    if (mac_cnfg->ul_sch_cfg_present) {
      if (mac_cnfg->ul_sch_cfg.max_harq_tx_present) {
        default_cfg.ul_sch_cfg.max_harq_tx         = mac_cnfg->ul_sch_cfg.max_harq_tx;
        default_cfg.ul_sch_cfg.max_harq_tx_present = true;
      }
      if (mac_cnfg->ul_sch_cfg.periodic_bsr_timer_present) {
        default_cfg.ul_sch_cfg.periodic_bsr_timer         = mac_cnfg->ul_sch_cfg.periodic_bsr_timer;
        default_cfg.ul_sch_cfg.periodic_bsr_timer_present = true;
      }
      default_cfg.ul_sch_cfg.retx_bsr_timer = mac_cnfg->ul_sch_cfg.retx_bsr_timer;
      default_cfg.ul_sch_cfg.tti_bundling   = mac_cnfg->ul_sch_cfg.tti_bundling;
    }
    if (mac_cnfg->drx_cfg_present) {
      default_cfg.drx_cfg         = mac_cnfg->drx_cfg;
      default_cfg.drx_cfg_present = true;
    }
    if (mac_cnfg->phr_cfg_present) {
      default_cfg.phr_cfg         = mac_cnfg->phr_cfg;
      default_cfg.phr_cfg_present = true;
    }
    default_cfg.time_align_timer_ded = mac_cnfg->time_align_timer_ded;
  }

  // Setup MAC configuration
  mac->set_config_main(&default_cfg);

  // Update UL HARQ config
  mac_interface_rrc::mac_cfg_t cfg;
  mac->get_config(&cfg);
  cfg.ul_harq_params.max_harq_tx = default_cfg.ul_sch_cfg.max_harq_tx.to_number();
  mac->set_config(&cfg);

  rrc_log->info("Set MAC main config: harq-MaxReTX=%d, bsr-TimerReTX=%d, bsr-TimerPeriodic=%d\n",
                default_cfg.ul_sch_cfg.max_harq_tx.to_number(), default_cfg.ul_sch_cfg.retx_bsr_timer.to_number(),
                default_cfg.ul_sch_cfg.periodic_bsr_timer.to_number());
  if (default_cfg.phr_cfg_present and default_cfg.phr_cfg.type() == setup_e::setup) {
    rrc_log->info("Set MAC PHR config: periodicPHR-Timer=%d, prohibitPHR-Timer=%d, dl-PathlossChange=%d\n",
                  default_cfg.phr_cfg.setup().periodic_phr_timer.to_number(),
                  default_cfg.phr_cfg.setup().prohibit_phr_timer.to_number(),
                  default_cfg.phr_cfg.setup().dl_pathloss_change.to_number());
  }
}

bool rrc::apply_rr_config_dedicated(rr_cfg_ded_s* cnfg)
{
  if (cnfg->phys_cfg_ded_present) {
    apply_phy_config_dedicated(&cnfg->phys_cfg_ded, false);
    // Apply SR configuration to MAC
    if (cnfg->phys_cfg_ded.sched_request_cfg_present) {
      mac->set_config_sr(&cnfg->phys_cfg_ded.sched_request_cfg);
    }
  }

  if (cnfg->mac_main_cfg_present) {
    apply_mac_config_dedicated(&cnfg->mac_main_cfg.explicit_value(),
                               cnfg->mac_main_cfg.type() == rr_cfg_ded_s::mac_main_cfg_c_::types::default_value);
  }

  if (cnfg->sps_cfg_present) {
    //TODO
  }
  if (cnfg->rlf_timers_and_consts_r9_present and cnfg->rlf_timers_and_consts_r9->type() == setup_e::setup) {
    mac_timers->timer_get(t301)->set(this, cnfg->rlf_timers_and_consts_r9->setup().t301_r9.to_number());
    mac_timers->timer_get(t310)->set(this, cnfg->rlf_timers_and_consts_r9->setup().t310_r9.to_number());
    mac_timers->timer_get(t311)->set(this, cnfg->rlf_timers_and_consts_r9->setup().t311_r9.to_number());
    N310 = cnfg->rlf_timers_and_consts_r9->setup().n310_r9.to_number();
    N311 = cnfg->rlf_timers_and_consts_r9->setup().n311_r9.to_number();

    rrc_log->info("Updated Constants and Timers: N310=%d, N311=%d, t300=%u, t301=%u, t310=%u, t311=%u\n",
                  N310, N311, mac_timers->timer_get(t300)->get_timeout(), mac_timers->timer_get(t301)->get_timeout(),
                  mac_timers->timer_get(t310)->get_timeout(), mac_timers->timer_get(t311)->get_timeout());
  }
  for (uint32_t i = 0; i < cnfg->srb_to_add_mod_list.size(); i++) {
    // TODO: handle SRB modification
    add_srb(&cnfg->srb_to_add_mod_list[i]);
  }
  for (uint32_t i = 0; i < cnfg->drb_to_release_list.size(); i++) {
    release_drb(cnfg->drb_to_release_list[i]);
  }
  for (uint32_t i = 0; i < cnfg->drb_to_add_mod_list.size(); i++) {
    // TODO: handle DRB modification
    add_drb(&cnfg->drb_to_add_mod_list[i]);
  }
  return true;
}

void rrc::handle_con_setup(rrc_conn_setup_s* setup)
{
  // Apply the Radio Resource configuration
  apply_rr_config_dedicated(&setup->crit_exts.c1().rrc_conn_setup_r8().rr_cfg_ded);

  nas->set_barring(nas_interface_rrc::BARRING_NONE);

  if (dedicated_info_nas) {
    send_con_setup_complete(dedicated_info_nas);
    dedicated_info_nas = NULL; // deallocated Inside!
  } else {
    rrc_log->error("Pending to transmit a ConnectionSetupComplete but no dedicatedInfoNAS was in queue\n");
  }
}

/* Reception of RRCConnectionReestablishment by the UE 5.3.7.5 */
void rrc::handle_con_reest(rrc_conn_reest_s* setup)
{

  mac_timers->timer_get(t301)->stop();

  pdcp->reestablish();
  rlc->reestablish();

  // Apply the Radio Resource configuration
  apply_rr_config_dedicated(&setup->crit_exts.c1().rrc_conn_reest_r8().rr_cfg_ded);

  // Send ConnectionSetupComplete message
  send_con_restablish_complete();
}

void rrc::add_srb(srb_to_add_mod_s* srb_cnfg)
{
  // Setup PDCP
  srslte_pdcp_config_t pdcp_cfg;
  pdcp_cfg.is_control = true;
  pdcp_cfg.bearer_id = srb_cnfg->srb_id;
  pdcp->add_bearer(srb_cnfg->srb_id, pdcp_cfg);
  if(RB_ID_SRB2 == srb_cnfg->srb_id) {
    pdcp->config_security(srb_cnfg->srb_id, k_rrc_enc, k_rrc_int, cipher_algo, integ_algo);
    pdcp->enable_integrity(srb_cnfg->srb_id);
    pdcp->enable_encryption(srb_cnfg->srb_id);
  }

  // Setup RLC
  if (srb_cnfg->rlc_cfg_present) {
    if (srb_cnfg->rlc_cfg.type() == srb_to_add_mod_s::rlc_cfg_c_::types::default_value) {
      rlc->add_bearer(srb_cnfg->srb_id);
    }else{
      rlc->add_bearer(srb_cnfg->srb_id, srslte_rlc_config_t(&srb_cnfg->rlc_cfg.explicit_value()));
    }
  }

  // Setup MAC
  uint8_t log_chan_group = 0;
  uint8_t priority = 1;
  int prioritized_bit_rate = -1;
  int bucket_size_duration = -1;

  if (srb_cnfg->lc_ch_cfg_present) {
    if (srb_cnfg->lc_ch_cfg.type() == srb_to_add_mod_s::lc_ch_cfg_c_::types::default_value) {
      if (RB_ID_SRB2 == srb_cnfg->srb_id)
        priority = 3;
    } else {
      if (srb_cnfg->lc_ch_cfg.explicit_value().lc_ch_sr_mask_r9_present) {
        //TODO
      }
      if (srb_cnfg->lc_ch_cfg.explicit_value().ul_specific_params_present) {
        if (srb_cnfg->lc_ch_cfg.explicit_value().ul_specific_params.lc_ch_group_present)
          log_chan_group = srb_cnfg->lc_ch_cfg.explicit_value().ul_specific_params.lc_ch_group;

        priority             = srb_cnfg->lc_ch_cfg.explicit_value().ul_specific_params.prio;
        prioritized_bit_rate = srb_cnfg->lc_ch_cfg.explicit_value().ul_specific_params.prioritised_bit_rate.to_number();
        bucket_size_duration = srb_cnfg->lc_ch_cfg.explicit_value().ul_specific_params.bucket_size_dur.to_number();
      }
    }
    mac->setup_lcid(srb_cnfg->srb_id, log_chan_group, priority, prioritized_bit_rate, bucket_size_duration);
  }

  srbs[srb_cnfg->srb_id] = *srb_cnfg;
  rrc_log->info("Added radio bearer %s\n", get_rb_name(srb_cnfg->srb_id).c_str());
}

void rrc::add_drb(drb_to_add_mod_s* drb_cnfg)
{

  if (!drb_cnfg->pdcp_cfg_present || !drb_cnfg->rlc_cfg_present || !drb_cnfg->lc_ch_cfg_present) {
    rrc_log->error("Cannot add DRB - incomplete configuration\n");
    return;
  }
  uint32_t lcid = 0;
  if (drb_cnfg->lc_ch_id_present) {
    lcid = drb_cnfg->lc_ch_id;
  } else {
    lcid = RB_ID_SRB2 + drb_cnfg->drb_id;
    rrc_log->warning("LCID not present, using %d\n", lcid);
  }

  // Setup PDCP
  srslte_pdcp_config_t pdcp_cfg;
  pdcp_cfg.is_data = true;
  pdcp_cfg.bearer_id = drb_cnfg->drb_id;
  if (drb_cnfg->pdcp_cfg.rlc_um_present) {
    if (drb_cnfg->pdcp_cfg.rlc_um.pdcp_sn_size == pdcp_cfg_s::rlc_um_s_::pdcp_sn_size_e_::len7bits) {
      pdcp_cfg.sn_len = 7;
    }
  }
  pdcp->add_bearer(lcid, pdcp_cfg);
  pdcp->config_security(lcid, k_up_enc, k_up_int, cipher_algo, integ_algo);
  pdcp->enable_encryption(lcid);

  // Setup RLC
  rlc->add_bearer(lcid, srslte_rlc_config_t(&drb_cnfg->rlc_cfg));

  // Setup MAC
  uint8_t log_chan_group = 0;
  uint8_t priority = 1;
  int prioritized_bit_rate = -1;
  int bucket_size_duration = -1;
  if (drb_cnfg->lc_ch_cfg.ul_specific_params_present) {
    if (drb_cnfg->lc_ch_cfg.ul_specific_params.lc_ch_group_present) {
      log_chan_group = drb_cnfg->lc_ch_cfg.ul_specific_params.lc_ch_group;
    } else {
      rrc_log->warning("LCG not present, setting to 0\n");
    }
    priority             = drb_cnfg->lc_ch_cfg.ul_specific_params.prio;
    prioritized_bit_rate = drb_cnfg->lc_ch_cfg.ul_specific_params.prioritised_bit_rate.to_number();

    if (prioritized_bit_rate > 0) {
      rrc_log->warning("PBR>0 currently not supported. Setting it to Inifinty\n");
      prioritized_bit_rate = -1;
    }

    bucket_size_duration = drb_cnfg->lc_ch_cfg.ul_specific_params.bucket_size_dur.to_number();
  }
  mac->setup_lcid(lcid, log_chan_group, priority, prioritized_bit_rate, bucket_size_duration);

  drbs[lcid] = *drb_cnfg;
  drb_up     = true;
  rrc_log->info("Added radio bearer %s (LCID=%d)\n", get_rb_name(lcid).c_str(), lcid);
}

void rrc::release_drb(uint32_t drb_id)
{
  uint32_t lcid = RB_ID_SRB2 + drb_id;

  if (drbs.find(drb_id) != drbs.end()) {
    rrc_log->info("Releasing radio bearer %s\n", get_rb_name(lcid).c_str());
    drbs.erase(lcid);
  } else {
    rrc_log->error("Couldn't release radio bearer %s. Doesn't exist.\n", get_rb_name(lcid).c_str());
  }
}

void rrc::add_mrb(uint32_t lcid, uint32_t port)
{
  gw->add_mch_port(lcid, port);
  rlc->add_bearer_mrb(lcid);
  mac->mch_start_rx(lcid);
  rrc_log->info("Added MRB bearer for lcid:%d\n", lcid);
}

// PHY CONFIG DEDICATED Defaults (3GPP 36.331 v10 9.2.4)
void rrc::set_phy_default_pucch_srs() {

  phy_interface_rrc::phy_cfg_t current_cfg;
  phy->get_config(&current_cfg);

  // Set defaults to CQI, SRS and SR
  current_cfg.dedicated.cqi_report_cfg_present    = false;
  current_cfg.dedicated.srs_ul_cfg_ded_present    = false;
  current_cfg.dedicated.sched_request_cfg_present = false;

  apply_phy_config_dedicated(&current_cfg.dedicated, true);

  // Release SR configuration from MAC
  sched_request_cfg_c cfg;
  mac->set_config_sr(&cfg);
}

void rrc::set_phy_default() {
  phys_cfg_ded_s defaults;
  apply_phy_config_dedicated(&defaults, true);
}

void rrc::set_mac_default() {
  apply_mac_config_dedicated(NULL, true);
  sched_request_cfg_c sr_cfg;
  sr_cfg.set(setup_e::release);
  mac->set_config_sr(&sr_cfg);
}

void rrc::set_rrc_default() {
  N310 = 1;
  N311 = 1;
  mac_timers->timer_get(t310)->set(this, 1000);
  mac_timers->timer_get(t311)->set(this, 1000);
}



















/************************************************************************
 *
 *
 * RRC Measurements
 *
 *
 ************************************************************************/

void rrc::rrc_meas::init(rrc *parent) {
  this->parent      = parent;
  this->log_h       = parent->rrc_log;
  this->phy         = parent->phy;
  this->mac_timers  = parent->mac_timers;
  s_measure_enabled = false;
  reset();
}

void rrc::rrc_meas::reset()
{
  filter_k_rsrp = filt_coef_e(filt_coef_e::fc4).to_number();
  filter_k_rsrq = filt_coef_e(filt_coef_e::fc4).to_number();

  // FIXME: Turn struct into a class and use destructor
  std::map<uint32_t, meas_t>::iterator iter = active.begin();
  while (iter != active.end()) {
    remove_meas_id(iter++);
  }

  // These objects do not need destructor
  objects.clear();
  reports_cfg.clear();
  phy->meas_reset();
  bzero(&pcell_measurement, sizeof(meas_value_t));
}

/* L3 filtering 5.5.3.2 */
void rrc::rrc_meas::L3_filter(meas_value_t *value, float values[NOF_MEASUREMENTS])
{
  for (int i=0;i<NOF_MEASUREMENTS;i++) {
    if (value->ms[i]) {
      value->ms[i] = SRSLTE_VEC_EMA(values[i], value->ms[i], filter_a[i]);
    } else {
      value->ms[i] = values[i];
    }
  }
}

void rrc::rrc_meas::new_phy_meas(uint32_t earfcn, uint32_t pci, float rsrp, float rsrq, uint32_t tti)
{
  float values[NOF_MEASUREMENTS] = {rsrp, rsrq};

  // This indicates serving cell
  if (parent->serving_cell->equals(earfcn, pci)) {

    log_h->debug("MEAS:  New measurement serving cell, rsrp=%f, rsrq=%f, tti=%d\n", rsrp, rsrq, tti);

    L3_filter(&pcell_measurement, values);

    // Update serving cell measurement
    parent->serving_cell->set_rsrp(rsrp);

  } else {

    // Add to list of neighbour cells
    bool added = parent->add_neighbour_cell(earfcn, pci, rsrp);

    log_h->debug("MEAS:  New measurement %s earfcn=%d, pci=%d, rsrp=%f, rsrq=%f, tti=%d\n",
                added?"added":"not added", earfcn, pci, rsrp, rsrq, tti);

    // Only report measurements of 8th strongest cells
    if (added) {
      // Save PHY measurement for all active measurements whose earfcn/pci matches
      for(std::map<uint32_t, meas_t>::iterator iter=active.begin(); iter!=active.end(); ++iter) {
        meas_t *m = &iter->second;
        if (objects[m->object_id].earfcn == earfcn) {
          // If it's a newly discovered cell, add it to objects
          if (!m->cell_values.count(pci)) {
            uint32_t cell_idx = objects[m->object_id].found_cells.size();
            objects[m->object_id].found_cells[cell_idx].pci      = pci;
            objects[m->object_id].found_cells[cell_idx].q_offset = 0;
          }
          // Update or add cell
          L3_filter(&m->cell_values[pci], values);
          return;
        }
      }
    }
  }
}

// Remove all stored measurements for a given cell
void rrc::rrc_meas::delete_report(uint32_t earfcn, uint32_t pci) {
  for(std::map<uint32_t, meas_t>::iterator iter=active.begin(); iter!=active.end(); ++iter) {
    meas_t *m = &iter->second;
    if (objects[m->object_id].earfcn == earfcn) {
      if (m->cell_values.count(pci)) {
        m->cell_values.erase(pci);
        log_h->info("Deleting report PCI=%d from cell_values\n", pci);
      }
    }
  }
}

void rrc::rrc_meas::run_tti(uint32_t tti) {
  // Measurement Report Triggering Section 5.5.4
  calculate_triggers(tti);
}

bool rrc::rrc_meas::find_earfcn_cell(uint32_t earfcn, uint32_t pci, meas_obj_t **object, int *cell_idx) {
  if (object) {
    *object = NULL;
  }
  for (std::map<uint32_t, meas_obj_t>::iterator obj = objects.begin(); obj != objects.end(); ++obj) {
    if (obj->second.earfcn == earfcn) {
      if (object) {
        *object = &obj->second;
      }
      for (std::map<uint32_t, meas_cell_t>::iterator c = obj->second.found_cells.begin(); c != obj->second.found_cells.end(); ++c) {
        if (c->second.pci == pci) {
          if (cell_idx) {
            *cell_idx = c->first;
            return true;
          }
        }
      }
      // return true if cell idx not found but frequency is found
      if (cell_idx) {
        *cell_idx = -1;
      }
      return true;
    }
  }
  return false;
}

/* Generate report procedure 5.5.5 */
void rrc::rrc_meas::generate_report(uint32_t meas_id)
{
  parent->ul_dcch_msg.msg.set(ul_dcch_msg_type_c::types::c1);
  parent->ul_dcch_msg.msg.c1().set(ul_dcch_msg_type_c::c1_c_::types::meas_report);
  parent->ul_dcch_msg.msg.c1().meas_report().crit_exts.set(meas_report_s::crit_exts_c_::types::c1);
  parent->ul_dcch_msg.msg.c1().meas_report().crit_exts.c1().set(
      meas_report_s::crit_exts_c_::c1_c_::types::meas_report_r8);
  meas_results_s* report = &parent->ul_dcch_msg.msg.c1().meas_report().crit_exts.c1().meas_report_r8().meas_results;

  meas_t       *m   = &active[meas_id];
  report_cfg_t *cfg = &reports_cfg[m->report_id];

  report->meas_id                       = (uint8_t)meas_id;
  report->meas_result_pcell.rsrp_result = value_to_range(RSRP, pcell_measurement.ms[RSRP]);
  report->meas_result_pcell.rsrq_result = value_to_range(RSRQ, pcell_measurement.ms[RSRQ]);

  log_h->info("MEAS:  Generate report MeasId=%d, nof_reports_send=%d, Pcell rsrp=%f rsrq=%f\n",
              report->meas_id, m->nof_reports_sent, pcell_measurement.ms[RSRP], pcell_measurement.ms[RSRQ]);

  report->meas_result_neigh_cells.set(meas_results_s::meas_result_neigh_cells_c_::types::meas_result_list_eutra);
  meas_result_list_eutra_l& neigh_list = report->meas_result_neigh_cells.meas_result_list_eutra();
  // TODO: report up to 8 best cells
  for (std::map<uint32_t, meas_value_t>::iterator cell = m->cell_values.begin(); cell != m->cell_values.end(); ++cell)
  {
    if (cell->second.triggered and neigh_list.size() <= 8) {
      meas_result_eutra_s rc;

      rc.pci                             = (uint16_t)cell->first;
      rc.meas_result.rsrp_result_present = cfg->report_quantity == RSRP || cfg->report_quantity == BOTH;
      rc.meas_result.rsrq_result_present = cfg->report_quantity == RSRQ || cfg->report_quantity == BOTH;
      rc.meas_result.rsrp_result         = value_to_range(RSRP, cell->second.ms[RSRP]);
      rc.meas_result.rsrq_result         = value_to_range(RSRQ, cell->second.ms[RSRQ]);

      log_h->info("MEAS:  Adding to report neighbour=%d, pci=%d, rsrp=%f, rsrq=%f\n", neigh_list.size(), rc.pci,
                  cell->second.ms[RSRP], cell->second.ms[RSRQ]);

      neigh_list.push_back(rc);
    }
  }
  report->meas_result_neigh_cells_present = neigh_list.size() > 0;

  m->nof_reports_sent++;
  mac_timers->timer_get(m->periodic_timer)->stop();

  if (m->nof_reports_sent < cfg->amount) {
    mac_timers->timer_get(m->periodic_timer)->reset();
    mac_timers->timer_get(m->periodic_timer)->run();
  } else {
    if (cfg->trigger_type == report_cfg_t::PERIODIC) {
      m->triggered = false;
    }
  }

  // Send to lower layers
  parent->send_ul_dcch_msg(RB_ID_SRB1);
}

/* Handle entering/leaving event conditions 5.5.4.1 */
bool rrc::rrc_meas::process_event(eutra_event_s* event, uint32_t tti, bool enter_condition, bool exit_condition,
                                  meas_t* m, meas_value_t* cell)
{
  bool generate_report = false;
  if (enter_condition && (!m->triggered || !cell->triggered)) {
    if (!cell->timer_enter_triggered) {
      cell->timer_enter_triggered = true;
      cell->enter_tti     = tti;
    } else if (srslte_tti_interval(tti, cell->enter_tti) >= event->time_to_trigger) {
      m->triggered        = true;
      cell->triggered     = true;
      m->nof_reports_sent = 0;
      generate_report     = true;
    }
  } else if (exit_condition) {
    if (!cell->timer_exit_triggered) {
      cell->timer_exit_triggered = true;
      cell->exit_tti      = tti;
    } else if (srslte_tti_interval(tti, cell->exit_tti) >= event->time_to_trigger) {
      m->triggered        = false;
      cell->triggered     = false;
      mac_timers->timer_get(m->periodic_timer)->stop();
      if (event) {
        if (event->event_id.type() == eutra_event_s::event_id_c_::types::event_a3 &&
            event->event_id.event_a3().report_on_leave) {
          generate_report = true;
        }
      }
    }
  }
  if (!enter_condition) {
    cell->timer_enter_triggered = false;
  }
  if (!enter_condition) {
    cell->timer_exit_triggered = false;
  }
  return generate_report;
}

/* Calculate trigger conditions for each cell 5.5.4 */
void rrc::rrc_meas::calculate_triggers(uint32_t tti)
{
  float Ofp = 0, Ocp = 0;
  meas_obj_t *serving_object   = NULL;
  int         serving_cell_idx = 0;

  // Get serving cell
  if (active.size()) {
    if (find_earfcn_cell(phy->get_current_earfcn(), phy->get_current_pci(), &serving_object, &serving_cell_idx)) {
      Ofp = serving_object->q_offset;
      if (serving_cell_idx >= 0) {
        Ocp = serving_object->found_cells[serving_cell_idx].q_offset;
      }
    } else {
      log_h->warning("Can't find current eafcn=%d, pci=%d in objects list. Using Ofp=0, Ocp=0\n",
                     phy->get_current_earfcn(), phy->get_current_pci());
    }
  }



  for (std::map<uint32_t, meas_t>::iterator m = active.begin(); m != active.end(); ++m) {
    report_cfg_t *cfg = &reports_cfg[m->second.report_id];
    double        hyst = 0.5 * cfg->event.hysteresis;
    float Mp   = pcell_measurement.ms[cfg->trigger_quantity];

    eutra_event_s::event_id_c_ event_id  = cfg->event.event_id;
    std::string                event_str = event_id.type().to_string();

    bool gen_report = false;

    if (cfg->trigger_type == report_cfg_t::EVENT) {

      // A1 & A2 are for serving cell only
      if (event_id.type().value < eutra_event_s::event_id_c_::types::event_a3) {

        bool enter_condition;
        bool exit_condition;
        if (event_id.type() == eutra_event_s::event_id_c_::types::event_a1) {
          uint8_t range;
          if (cfg->event.event_id.event_a1().a1_thres.type().value == thres_eutra_c::types::thres_rsrp) {
            range = cfg->event.event_id.event_a1().a1_thres.thres_rsrp();
          } else {
            range = cfg->event.event_id.event_a1().a1_thres.thres_rsrq();
          }
          enter_condition = Mp - hyst > range_to_value(cfg->trigger_quantity, range);
          exit_condition  = Mp + hyst < range_to_value(cfg->trigger_quantity, range);
        } else {
          uint8_t range;
          if (cfg->event.event_id.event_a2().a2_thres.type() == thres_eutra_c::types::thres_rsrp) {
            range = cfg->event.event_id.event_a2().a2_thres.thres_rsrp();
          } else {
            range = cfg->event.event_id.event_a2().a2_thres.thres_rsrq();
          }
          enter_condition = Mp + hyst < range_to_value(cfg->trigger_quantity, range);
          exit_condition  = Mp - hyst > range_to_value(cfg->trigger_quantity, range);
        }

        // check only if
        gen_report |= process_event(&cfg->event, tti, enter_condition, exit_condition, &m->second, &pcell_measurement);

        if (gen_report) {
          log_h->info("Triggered by A1/A2 event\n");
        }
        // Rest are evaluated for every cell in frequency
      } else {
        meas_obj_t *obj = &objects[m->second.object_id];
        for (std::map<uint32_t, meas_cell_t>::iterator cell = obj->found_cells.begin(); cell != obj->found_cells.end(); ++cell) {
          if (m->second.cell_values.count(cell->second.pci)) {
            float Ofn = obj->q_offset;
            float Ocn = cell->second.q_offset;
            float Mn = m->second.cell_values[cell->second.pci].ms[cfg->trigger_quantity];
            double  Off = 0;
            float   th = 0, th1 = 0, th2 = 0;
            bool enter_condition = false;
            bool exit_condition  = false;
            uint8_t range, range2;
            switch (event_id.type().value) {
              case eutra_event_s::event_id_c_::types::event_a3:
                Off             = 0.5 * cfg->event.event_id.event_a3().a3_offset;
                enter_condition = Mn + Ofn + Ocn - hyst > Mp + Ofp + Ocp + Off;
                exit_condition  = Mn + Ofn + Ocn + hyst < Mp + Ofp + Ocp + Off;
                break;
              case eutra_event_s::event_id_c_::types::event_a4:
                if (cfg->event.event_id.event_a4().a4_thres.type() == thres_eutra_c::types::thres_rsrp) {
                  range = cfg->event.event_id.event_a4().a4_thres.thres_rsrp();
                } else {
                  range = cfg->event.event_id.event_a4().a4_thres.thres_rsrq();
                }
                th              = range_to_value(cfg->trigger_quantity, range);
                enter_condition = Mn + Ofn + Ocn - hyst > th;
                exit_condition  = Mn + Ofn + Ocn + hyst < th;
                break;
              case eutra_event_s::event_id_c_::types::event_a5:
                if (cfg->event.event_id.event_a5().a5_thres1.type() == thres_eutra_c::types::thres_rsrp) {
                  range = cfg->event.event_id.event_a5().a5_thres1.thres_rsrp();
                } else {
                  range = cfg->event.event_id.event_a5().a5_thres1.thres_rsrq();
                }
                if (cfg->event.event_id.event_a5().a5_thres2.type() == thres_eutra_c::types::thres_rsrp) {
                  range2 = cfg->event.event_id.event_a5().a5_thres2.thres_rsrp();
                } else {
                  range2 = cfg->event.event_id.event_a5().a5_thres2.thres_rsrq();
                }
                th1             = range_to_value(cfg->trigger_quantity, range);
                th2             = range_to_value(cfg->trigger_quantity, range2);
                enter_condition = (Mp + hyst < th1) && (Mn + Ofn + Ocn - hyst > th2);
                exit_condition  = (Mp - hyst > th1) && (Mn + Ofn + Ocn + hyst < th2);
                break;
              default:
                log_h->error("Error event %s not implemented\n", event_str.c_str());
            }
            gen_report |= process_event(&cfg->event, tti, enter_condition, exit_condition,
                                        &m->second, &m->second.cell_values[cell->second.pci]);
          }
        }
      }
    }
    if (gen_report) {
      log_h->info("Generate report MeasId=%d, from event\n", m->first);
      generate_report(m->first);
    }
  }
}

// Procedure upon handover or reestablishment 5.5.6.1
void rrc::rrc_meas::ho_finish() {
  // Remove all measId with trigger periodic
  std::map<uint32_t, meas_t>::iterator iter = active.begin();
  while (iter != active.end()) {
    if (reports_cfg[iter->second.report_id].trigger_type == report_cfg_t::PERIODIC) {
      remove_meas_id(iter++);
    } else {
      ++iter;
    }
  }

  //TODO: Inter-frequency handover

  // Stop all reports
  for (std::map<uint32_t, meas_t>::iterator iter = active.begin(); iter != active.end(); ++iter) {
    stop_reports(&iter->second);
  }
}

// 5.5.4.1 expiry of periodical reporting timer
bool rrc::rrc_meas::timer_expired(uint32_t timer_id) {
  for (std::map<uint32_t, meas_t>::iterator iter = active.begin(); iter != active.end(); ++iter) {
    if (iter->second.periodic_timer == timer_id) {
      log_h->info("Generate report MeasId=%d, from timerId=%d\n", iter->first, timer_id);
      generate_report(iter->first);
      return true;
    }
  }
  return false;
}

void rrc::rrc_meas::stop_reports(meas_t *m) {
  mac_timers->timer_get(m->periodic_timer)->stop();
  m->triggered = false;
}

void rrc::rrc_meas::stop_reports_object(uint32_t object_id) {
  for (std::map<uint32_t, meas_t>::iterator iter = active.begin(); iter != active.end(); ++iter) {
    if (iter->second.object_id == object_id) {
      stop_reports(&iter->second);
    }
  }
}

void rrc::rrc_meas::remove_meas_object(uint32_t object_id) {
  std::map<uint32_t, meas_t>::iterator iter = active.begin();
  while (iter != active.end()) {
    if (iter->second.object_id == object_id) {
      remove_meas_id(iter++);
    } else {
      ++iter;
    }
  }
}

void rrc::rrc_meas::remove_meas_report(uint32_t report_id) {
  std::map<uint32_t, meas_t>::iterator iter = active.begin();
  while (iter != active.end()) {
    if (iter->second.report_id == report_id) {
      remove_meas_id(iter++);
    } else {
      ++iter;
    }
  }
}

void rrc::rrc_meas::remove_meas_id(uint32_t measId) {
  if (active.count(measId)) {
    mac_timers->timer_get(active[measId].periodic_timer)->stop();
    mac_timers->timer_release_id(active[measId].periodic_timer);
    log_h->info("MEAS: Removed measId=%d\n", measId);
    active.erase(measId);
  } else {
    log_h->warning("MEAS: Removing unexistent measId=%d\n", measId);
  }
}

void rrc::rrc_meas::remove_meas_id(std::map<uint32_t, meas_t>::iterator it) {
  mac_timers->timer_get(it->second.periodic_timer)->stop();
  mac_timers->timer_release_id(it->second.periodic_timer);
  log_h->info("MEAS: Removed measId=%d\n", it->first);
  active.erase(it);
}

/* Parses MeasConfig object from RRCConnectionReconfiguration message and applies configuration
 * as per section 5.5.2
 */
bool rrc::rrc_meas::parse_meas_config(meas_cfg_s* cfg)
{
  // Measurement object removal 5.5.2.4
  for (uint32_t i = 0; i < cfg->meas_obj_to_rem_list.size(); i++) {
    objects.erase(cfg->meas_obj_to_rem_list[i]);
    remove_meas_object(cfg->meas_obj_to_rem_list[i]);
    log_h->info("MEAS: Removed measObjectId=%d\n", cfg->meas_obj_to_rem_list[i]);
  }

  // Measurement object addition/modification Section 5.5.2.5
  if (cfg->meas_obj_to_add_mod_list_present) {
    for (uint32_t i = 0; i < cfg->meas_obj_to_add_mod_list.size(); i++) {
      if (cfg->meas_obj_to_add_mod_list[i].meas_obj.type() ==
          meas_obj_to_add_mod_s::meas_obj_c_::types::meas_obj_eutra) {
        meas_obj_eutra_s* src_obj = &cfg->meas_obj_to_add_mod_list[i].meas_obj.meas_obj_eutra();

        // Access the object if exists or create it
        meas_obj_t* dst_obj = &objects[cfg->meas_obj_to_add_mod_list[i].meas_obj_id];

        dst_obj->earfcn   = src_obj->carrier_freq;;
        if (src_obj->offset_freq_present) {
          dst_obj->q_offset = src_obj->offset_freq.to_number();
        } else {
          dst_obj->q_offset = 0;
        }

        if (src_obj->black_cells_to_rem_list_present) {
          for (uint32_t j = 0; j < src_obj->black_cells_to_rem_list.size(); j++) {
            dst_obj->meas_cells.erase(src_obj->black_cells_to_rem_list[i]);
          }
        }

        for (uint32_t j = 0; j < src_obj->cells_to_add_mod_list.size(); j++) {
          dst_obj->meas_cells[src_obj->cells_to_add_mod_list[j].cell_idx].q_offset =
              src_obj->cells_to_add_mod_list[j].cell_individual_offset.to_number();
          dst_obj->meas_cells[src_obj->cells_to_add_mod_list[j].cell_idx].pci = src_obj->cells_to_add_mod_list[j].pci;

          log_h->info("MEAS: Added measObjectId=%d, earfcn=%d, q_offset=%f, pci=%d, offset_cell=%f\n",
                      cfg->meas_obj_to_add_mod_list[i].meas_obj_id, dst_obj->earfcn, dst_obj->q_offset,
                      dst_obj->meas_cells[src_obj->cells_to_add_mod_list[j].cell_idx].pci,
                      dst_obj->meas_cells[src_obj->cells_to_add_mod_list[j].cell_idx].q_offset);
        }

        // Untrigger reports and stop timers
        stop_reports_object(cfg->meas_obj_to_add_mod_list[i].meas_obj_id);

        // TODO: Blackcells
        // TODO: meassubframepattern

      } else {
        log_h->warning("MEAS: Unsupported MeasObject type %s\n",
                       cfg->meas_obj_to_add_mod_list[i].meas_obj.type().to_string().c_str());
      }
    }
  }

  // Reporting configuration removal 5.5.2.6
  for (uint32_t i = 0; i < cfg->report_cfg_to_rem_list.size(); i++) {
    reports_cfg.erase(cfg->report_cfg_to_rem_list[i]);
    remove_meas_report(cfg->report_cfg_to_rem_list[i]);
    log_h->info("MEAS: Removed reportConfigId=%d\n", cfg->report_cfg_to_rem_list[i]);
  }

  // Reporting configuration addition/modification 5.5.2.7
  if (cfg->report_cfg_to_add_mod_list_present) {
    for (uint32_t i = 0; i < cfg->report_cfg_to_add_mod_list.size(); i++) {
      if (cfg->report_cfg_to_add_mod_list[i].report_cfg.type() ==
          report_cfg_to_add_mod_s::report_cfg_c_::types::report_cfg_eutra) {
        report_cfg_eutra_s* src_rep = &cfg->report_cfg_to_add_mod_list[i].report_cfg.report_cfg_eutra();
        // Access the object if exists or create it
        report_cfg_t* dst_rep = &reports_cfg[cfg->report_cfg_to_add_mod_list[i].report_cfg_id];

        dst_rep->trigger_type = src_rep->trigger_type.type() == report_cfg_eutra_s::trigger_type_c_::types::event
                                    ? report_cfg_t::EVENT
                                    : report_cfg_t::PERIODIC;
        if (dst_rep->trigger_type == report_cfg_t::EVENT) {
          dst_rep->event = src_rep->trigger_type.event();
        }
        dst_rep->amount           = (uint8_t)src_rep->report_amount.to_number();
        dst_rep->interval         = src_rep->report_interv.to_number();
        dst_rep->max_cell         = src_rep->max_report_cells;
        dst_rep->trigger_quantity = (quantity_t)src_rep->trigger_quant.value;
        dst_rep->report_quantity  = src_rep->report_quant == report_cfg_eutra_s::report_quant_e_::same_as_trigger_quant
                                       ? dst_rep->trigger_quantity
                                       : BOTH;

        if (dst_rep->trigger_type == report_cfg_t::EVENT) {
          log_h->info("MEAS: Added reportConfigId=%d, event=%s, amount=%d, interval=%d\n",
                      cfg->report_cfg_to_add_mod_list[i].report_cfg_id,
                      dst_rep->event.event_id.type().to_string().c_str(), dst_rep->amount, dst_rep->interval);
        } else {
          log_h->info("MEAS: Added reportConfigId=%d, type=periodical, amount=%d, interval=%d\n",
                      cfg->report_cfg_to_add_mod_list[i].report_cfg_id, dst_rep->amount, dst_rep->interval);
        }

        // Reset reports counter
        for (std::map<uint32_t, meas_t>::iterator iter = active.begin(); iter != active.end(); ++iter) {
          if (iter->second.report_id == cfg->report_cfg_to_add_mod_list[i].report_cfg_id) {
            iter->second.nof_reports_sent = 0;
            stop_reports(&iter->second);
          }
        }
      } else {
        log_h->warning("MEAS: Unsupported reportConfigType %s\n",
                       cfg->report_cfg_to_add_mod_list[i].report_cfg.type().to_string().c_str());
      }
    }
  }

  // Quantity configuration 5.5.2.8
  if (cfg->quant_cfg_present && cfg->quant_cfg.quant_cfg_eutra_present) {
    if (cfg->quant_cfg.quant_cfg_eutra.filt_coef_rsrp_present) {
      filter_k_rsrp = cfg->quant_cfg.quant_cfg_eutra.filt_coef_rsrp.to_number();
    } else {
      filter_k_rsrp = filt_coef_e(filt_coef_e::fc4).to_number();
    }
    if (cfg->quant_cfg.quant_cfg_eutra.filt_coef_rsrq_present) {
      filter_k_rsrq = cfg->quant_cfg.quant_cfg_eutra.filt_coef_rsrq.to_number();
    } else {
      filter_k_rsrq = filt_coef_e(filt_coef_e::fc4).to_number();
    }
    filter_a[RSRP] = powf(0.5, (float)filter_k_rsrp / 4);
    filter_a[RSRQ] = powf(0.5, (float)filter_k_rsrq / 4);

    log_h->info("MEAS: Quantity configuration k_rsrp=%d, k_rsrq=%d\n", filter_k_rsrp, filter_k_rsrq);
  }

  // Measurement identity removal 5.5.2.2
  for (uint32_t i = 0; i < cfg->meas_id_to_rem_list.size(); i++) {
    remove_meas_id(cfg->meas_id_to_rem_list[i]);
  }

  log_h->info("nof active measId=%zd\n", active.size());

  // Measurement identity addition/modification 5.5.2.3
  if (cfg->meas_id_to_add_mod_list_present) {
    for (uint32_t i = 0; i < cfg->meas_id_to_add_mod_list.size(); i++) {
      meas_id_to_add_mod_s* meas_id = &cfg->meas_id_to_add_mod_list[i];
      // Stop the timer if the entry exists or create the timer if not
      bool is_new = false;
      if (active.count(meas_id->meas_id)) {
        mac_timers->timer_get(active[meas_id->meas_id].periodic_timer)->stop();
      } else {
        is_new                                  = true;
        active[meas_id->meas_id].periodic_timer = mac_timers->timer_get_unique_id();
      }
      active[meas_id->meas_id].object_id = meas_id->meas_obj_id;
      active[meas_id->meas_id].report_id = meas_id->report_cfg_id;
      log_h->info("MEAS: %s measId=%d, measObjectId=%d, reportConfigId=%d, timer_id=%u, nof_values=%zd\n",
                  is_new ? "Added" : "Updated", meas_id->meas_id, meas_id->meas_obj_id, meas_id->report_cfg_id,
                  active[meas_id->meas_id].periodic_timer, active[meas_id->meas_id].cell_values.size());
    }
  }

  // S-Measure
  if (cfg->s_measure_present) {
    if (cfg->s_measure) {
      s_measure_enabled = true;
      s_measure_value   = range_to_value(RSRP, cfg->s_measure);
    } else {
      s_measure_enabled = false;
    }
  }

  update_phy();

  return true;
}

/* Instruct PHY to start measurement */
void rrc::rrc_meas::update_phy()
{
  phy->meas_reset();
  for(std::map<uint32_t, meas_obj_t>::iterator iter=objects.begin(); iter!=objects.end(); ++iter) {
    meas_obj_t o = iter->second;
    // Instruct PHY to look for neighbour cells on this frequency
    phy->meas_start(o.earfcn);
    for(std::map<uint32_t, meas_cell_t>::iterator iter=o.meas_cells.begin(); iter!=o.meas_cells.end(); ++iter) {
      // Instruct PHY to look for cells IDs on this frequency
      phy->meas_start(o.earfcn, iter->second.pci);
    }
  }
}


uint8_t rrc::rrc_meas::value_to_range(quantity_t quant, float value) {
  uint8_t range = 0;
  switch(quant) {
    case RSRP:
      if (value < -140) {
        range = 0;
      } else if (-140 <= value && value < -44) {
        range = 1u + (uint8_t)(value + 140);
      } else {
        range = 97;
      }
      break;
    case RSRQ:
      if (value < -19.5) {
        range = 0;
      } else if (-19.5 <= value && value < -3) {
        range = 1u + (uint8_t)(2 * (value + 19.5));
      } else {
        range = 34;
      }
      break;
    case BOTH:
      printf("Error quantity both not supported in value_to_range\n");
      break;
  }
  return range;
}

float rrc::rrc_meas::range_to_value(quantity_t quant, uint8_t range)
{
  float val = 0;
  switch (quant) {
    case RSRP:
      val = -140 + (float)range;
      break;
    case RSRQ:
      val = -19.5f + (float)range / 2;
      break;
    case BOTH:
      printf("Error quantity both not supported in range_to_value\n");
      break;
  }
  return val;
}

const std::string rrc::rb_id_str[] = {"SRB0", "SRB1", "SRB2",
                                      "DRB1", "DRB2", "DRB3",
                                      "DRB4", "DRB5", "DRB6",
                                      "DRB7", "DRB8"};

} // namespace srsue
