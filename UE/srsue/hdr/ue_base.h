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

/******************************************************************************
 * File:        ue_base.h
 * Description: Base class for UEs.
 *****************************************************************************/

#ifndef SRSUE_UE_BASE_H
#define SRSUE_UE_BASE_H

#include <stdarg.h>
#include <string>
#include <pthread.h>
#include "srslte/radio/radio_multi.h"
#include "phy/phy.h"
#include "upper/usim.h"
#include "upper/rrc.h"
#include "upper/nas.h"
#include "srslte/interfaces/ue_interfaces.h"

#include "srslte/common/logger.h"
#include "srslte/common/log_filter.h"

#include "ue_metrics_interface.h"

namespace srsue {

/*******************************************************************************
  UE Parameters
*******************************************************************************/

typedef struct {
  std::string   dl_earfcn;
  float         dl_freq;
  float         ul_freq;
  float         freq_offset;
  float         rx_gain;
  float         tx_gain;
  uint32_t      nof_rx_ant;
  std::string   device_name;
  std::string   device_args;
  std::string   time_adv_nsamples;
  std::string   burst_preamble;
  std::string   continuous_tx;
}rf_args_t;

typedef struct {
  bool          enable;
  std::string   filename;
  bool          nas_enable;
  std::string   nas_filename;
}pcap_args_t;

typedef struct {
  bool          enable;
  std::string   phy_filename;
  std::string   radio_filename;
}trace_args_t;

typedef struct {
  std::string   phy_level;
  std::string   phy_lib_level;
  std::string   mac_level;
  std::string   rlc_level;
  std::string   pdcp_level;
  std::string   lwipep_level;
  std::string   lwipep_ipsec_level;
  std::string	lwipep_hdl_rpt_level;
  std::string   rrc_level;
  std::string   gw_level;
  std::string   nas_level;
  std::string   usim_level;
  std::string   all_level;
  int           phy_hex_limit;
  int           mac_hex_limit;
  int           rlc_hex_limit;
  int           pdcp_hex_limit;
  int           lwipep_hex_limit;
  int           lwipep_ipsec_hex_limit;
  int			lwipep_hdl_rpt_hex_limit;
  int           rrc_hex_limit;
  int           gw_hex_limit;
  int           nas_hex_limit;
  int           usim_hex_limit;
  int           all_hex_limit;
  int           file_max_size;
  std::string   filename;
}log_args_t;

typedef struct {
  bool          enable;
}gui_args_t;

typedef struct {
  std::string   ip_netmask;
  std::string   ip_devname;
  phy_args_t    phy;
  float         metrics_period_secs;
  bool          pregenerate_signals;
  bool          print_buffer_state;
  bool          metrics_csv_enable;
  std::string   metrics_csv_filename;
  int           mbms_service;
}expert_args_t;

typedef struct {
  rf_args_t     rf;
  pcap_args_t   pcap;
  trace_args_t  trace;
  log_args_t    log;
  gui_args_t    gui;
  usim_args_t   usim;
  rrc_args_t    rrc;
  std::string   ue_category_str;
  nas_args_t    nas;
  expert_args_t expert;
}all_args_t;

typedef enum {
  LTE = 0,
  SRSUE_INSTANCE_TYPE_NITEMS
} srsue_instance_type_t;
static const char srsue_instance_type_text[SRSUE_INSTANCE_TYPE_NITEMS][10] = { "LTE" };


/*******************************************************************************
  Main UE class
*******************************************************************************/

class ue_base
    :public ue_interface
    ,public ue_metrics_interface
{
public:
  ue_base();
  virtual ~ue_base();

  static ue_base* get_instance(srsue_instance_type_t type);

  void cleanup(void);

  virtual bool init(all_args_t *args_) = 0;
  virtual void stop() = 0;
  virtual bool switch_on() = 0;
  virtual bool switch_off() = 0;
  virtual bool is_attached() = 0;
  virtual void start_plot() = 0;

  virtual void print_pool() = 0;

  virtual void radio_overflow() = 0;

  virtual void print_mbms() = 0;
  virtual bool mbms_service_start(uint32_t serv, uint32_t port) = 0;
  
  void handle_rf_msg(srslte_rf_error_t error);

  // UE metrics interface
  virtual bool get_metrics(ue_metrics_t &m) = 0;

  virtual void pregenerate_signals(bool enable) = 0;

  virtual void set_lwipep_auto_control_ratio() = 0;
  virtual void set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate) = 0;
  virtual void lwipep_state() = 0;
  virtual void set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio) = 0;
  virtual void set_lwipep_reorder_buffer_size(uint32_t buffer_size) = 0;
  virtual void set_lwipep_report_period(uint32_t report_period) = 0;
  virtual void set_reorder_timeout(uint32_t reorder_timeout) = 0;
  
  srslte::log_filter rf_log;
  rf_metrics_t     rf_metrics;
  srslte::LOG_LEVEL_ENUM level(std::string l);

  std::string get_build_mode();
  std::string get_build_info();
  std::string get_build_string();

private:
  srslte::byte_buffer_pool *pool;
};

} // namespace srsue

#endif // SRSUE_UE_BASE_H
  
