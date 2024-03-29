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


#include "srsue/hdr/ue.h"
#include "srslte/srslte.h"
#include <pthread.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <iterator>
#include <sstream>

using namespace srslte;

namespace srsue{

ue::ue()
    :started(false), mac_log()
{
  usim = NULL;
  logger = NULL;
  args = NULL;
}

ue::~ue()
{
  for (uint32_t i = 0; i < phy_log.size(); i++) {
    if (phy_log[i]) {
      delete(phy_log[i]);
    }
  }
  if (usim) {
    delete usim;
  }
}

bool ue::init(all_args_t *args_) 
{
  args = args_;

  int nof_phy_threads = args->expert.phy.nof_phy_threads;
  if (nof_phy_threads > 3) {
    nof_phy_threads = 3;
  }

  if (!args->log.filename.compare("stdout")) {
    logger = &logger_stdout;
  } else {
    logger_file.init(args->log.filename, args->log.file_max_size);
    logger_file.log("\n\n");
    logger_file.log(get_build_string().c_str());
    logger = &logger_file;
  }

  rf_log.init("RF  ", logger);
  // Create array of pointers to phy_logs
  for (int i=0;i<nof_phy_threads;i++) {
    srslte::log_filter *mylog = new srslte::log_filter;
    char tmp[16];
    sprintf(tmp, "PHY%d",i);
    mylog->init(tmp, logger, true);
    phy_log.push_back(mylog);
  }

  mac_log.init("MAC ", logger, true);
  rlc_log.init("RLC ", logger);
  pdcp_log.init("PDCP", logger);
  
  lwipep_log.init("LWIPEP", logger);
  lwipep_ipsec_log.init("LWIPEP_IPSEC", logger);
  
  rrc_log.init("RRC ", logger);
  nas_log.init("NAS ", logger);
  gw_log.init("GW  ", logger);
  usim_log.init("USIM", logger);

  pool_log.init("POOL", logger);
  pool_log.set_level(srslte::LOG_LEVEL_ERROR);
  byte_buffer_pool::get_instance()->set_log(&pool_log);

  // Init logs
  rf_log.set_level(srslte::LOG_LEVEL_INFO);
  rf_log.info("Starting UE\n");
  for (int i=0;i<nof_phy_threads;i++) {
    ((srslte::log_filter*) phy_log[i])->set_level(level(args->log.phy_level));
  }
  
  /* here we add a log layer to handle logging from the phy library*/
  if (level(args->log.phy_lib_level) != LOG_LEVEL_NONE) {
    srslte::log_filter *lib_log = new srslte::log_filter;
    char tmp[16];
    sprintf(tmp, "PHY_LIB");
    lib_log->init(tmp, logger, true);
    phy_log.push_back(lib_log);
    ((srslte::log_filter*) phy_log[nof_phy_threads])->set_level(level(args->log.phy_lib_level));
  } else {
    phy_log.push_back(NULL);
  }

  
  mac_log.set_level(level(args->log.mac_level));
  rlc_log.set_level(level(args->log.rlc_level));
  pdcp_log.set_level(level(args->log.pdcp_level));
  
  lwipep_log.set_level(level(args->log.lwipep_level));
  lwipep_ipsec_log.set_level(level(args->log.lwipep_ipsec_level));
  
  rrc_log.set_level(level(args->log.rrc_level));
  nas_log.set_level(level(args->log.nas_level));
  gw_log.set_level(level(args->log.gw_level));
  usim_log.set_level(level(args->log.usim_level));

  for (int i=0;i<nof_phy_threads + 1;i++) {
    if (phy_log[i]) {
      ((srslte::log_filter*) phy_log[i])->set_hex_limit(args->log.phy_hex_limit);
    }
  }
  mac_log.set_hex_limit(args->log.mac_hex_limit);
  rlc_log.set_hex_limit(args->log.rlc_hex_limit);
  pdcp_log.set_hex_limit(args->log.pdcp_hex_limit);
  
  lwipep_log.set_hex_limit(args->log.lwipep_hex_limit);
  lwipep_ipsec_log.set_hex_limit(args->log.lwipep_ipsec_hex_limit);
  
  rrc_log.set_hex_limit(args->log.rrc_hex_limit);
  nas_log.set_hex_limit(args->log.nas_hex_limit);
  gw_log.set_hex_limit(args->log.gw_hex_limit);
  usim_log.set_hex_limit(args->log.usim_hex_limit);

  // Set up pcap and trace
  if(args->pcap.enable) {
    mac_pcap.open(args->pcap.filename.c_str());
    mac.start_pcap(&mac_pcap);
  }
  if(args->pcap.nas_enable) {
    nas_pcap.open(args->pcap.nas_filename.c_str());
    nas.start_pcap(&nas_pcap);
  }
  if(args->trace.enable) {
    phy.start_trace();
    radio.start_trace();
  }

  // populate EARFCN list
  std::vector<uint32_t> earfcn_list;
  if (!args->rf.dl_earfcn.empty()) {
    std::stringstream ss(args->rf.dl_earfcn);
    int idx = 0;
    while (ss.good()) {
      std::string substr;
      getline(ss, substr, ',');
      const int earfcn               = atoi(substr.c_str());
      args->rrc.supported_bands[idx] = srslte_band_get_band(earfcn);
      args->rrc.nof_supported_bands  = ++idx;
      earfcn_list.push_back(earfcn);
    }
  } else {
    printf("Error: dl_earfcn list is empty\n");
    return false;
  }

  // Init layers

  // Init USIM first to allow early exit in case reader couldn't be found
  usim = usim_base::get_instance(&args->usim, &usim_log);
  if (usim->init(&args->usim, &usim_log)) {
    usim_log.console("Failed to initialize USIM.\n");
    return false;
  }

  // PHY inits in background, start before radio
  args->expert.phy.nof_rx_ant = args->rf.nof_rx_ant;
  phy.init(&radio, &mac, &lwipep, &rrc, phy_log, &args->expert.phy);

  /* Start Radio */
  char *dev_name = NULL;
  if (args->rf.device_name.compare("auto")) {
    dev_name = (char*) args->rf.device_name.c_str();
  }
  
  char *dev_args = NULL;
  if (args->rf.device_args.compare("auto")) {
    dev_args = (char*) args->rf.device_args.c_str();
  }
  
  printf("Opening RF device with %d RX antennas...\n", args->rf.nof_rx_ant);
  if(!radio.init_multi(args->rf.nof_rx_ant, dev_args, dev_name)) {
    printf("Failed to find device %s with args %s\n",
           args->rf.device_name.c_str(), args->rf.device_args.c_str());
    return false;
  }    
  
  // Set RF options
  if (args->rf.time_adv_nsamples.compare("auto")) {
    int t = atoi(args->rf.time_adv_nsamples.c_str());
    radio.set_tx_adv(abs(t));
    radio.set_tx_adv_neg(t<0);
  }
  if (args->rf.burst_preamble.compare("auto")) {
    radio.set_burst_preamble(atof(args->rf.burst_preamble.c_str()));    
  }
  if (args->rf.continuous_tx.compare("auto")) {
    printf("set continuous %s\n", args->rf.continuous_tx.c_str());
    radio.set_continuous_tx(args->rf.continuous_tx.compare("yes")?false:true);
  }

  // Set PHY options
  if (args->rf.tx_gain > 0) {
    args->expert.phy.ul_pwr_ctrl_en = false; 
  } else {
    args->expert.phy.ul_pwr_ctrl_en = true; 
  }

  if (args->rf.rx_gain < 0) {
    radio.start_agc(false);    
  } else {
    radio.set_rx_gain(args->rf.rx_gain);
  }
  if (args->rf.tx_gain > 0) {
    radio.set_tx_gain(args->rf.tx_gain);    
  } else {
    radio.set_tx_gain(args->rf.rx_gain);
    std::cout << std::endl << 
                "Warning: TX gain was not set. " << 
                "Using open-loop power control (not working properly)" << std::endl << std::endl; 
  }

  radio.register_error_handler(rf_msg);
  radio.set_freq_offset(args->rf.freq_offset);

  mac.init(&phy, &rlc, &rrc, &mac_log);
  rlc.init(&pdcp, &rrc, this, &rlc_log, &mac, 0 /* RB_ID_SRB0 */);
  pdcp.init(&rlc, &lwipep, &rrc, &gw, &pdcp_log, 0 /* RB_ID_SRB0 */, SECURITY_DIRECTION_UPLINK);

  lwipep.init(&rlc, &pdcp, &lwipep_ipsec, &rrc, &mac, &gw, &lwipep_log, 3 /*RB_ID_SRB3*/);
  lwipep_ipsec.init(&lwipep, &lwipep_ipsec_log);
  
  srslte_nas_config_t nas_cfg(1, args->nas.apn_name, args->nas.apn_protocol, args->nas.apn_user, args->nas.apn_pass, args->nas.force_imsi_attach); /* RB_ID_SRB1 */
  nas.init(usim, &rrc, &gw, &nas_log, nas_cfg);
  gw.init(&pdcp, &lwipep, &nas, &gw_log, 3 /* RB_ID_DRB1 */);
  gw.set_netmask(args->expert.ip_netmask);
  gw.set_tundevname(args->expert.ip_devname);

  args->rrc.ue_category = atoi(args->ue_category_str.c_str());

  // set args and initialize RRC
  rrc.init(&phy, &mac, &rlc, &pdcp, &lwipep, &nas, usim, &gw, &mac, &rrc_log);
  rrc.set_args(args->rrc);

  phy.set_earfcn(earfcn_list);

  if (args->rf.dl_freq > 0 && args->rf.ul_freq > 0) {
    phy.force_freq(args->rf.dl_freq, args->rf.ul_freq);
  }

  printf("Waiting PHY to initialize...\n");
  phy.wait_initialize();

  // Enable AGC once PHY is initialized
  if (args->rf.rx_gain < 0) {
    phy.set_agc_enable(true);
  }

  printf("...\n");

  started = true;
  return true;
}

void ue::pregenerate_signals(bool enable)
{
  phy.enable_pregen_signals(enable);
}

void ue::stop()
{
  if(started)
  {
    usim->stop();
    nas.stop();
    rrc.stop();
    
    // Caution here order of stop is very important to avoid locks

    
    // Stop RLC and PDCP before GW to avoid locking on queue
    rlc.stop();
    pdcp.stop();
	lwipep.stop();
    gw.stop();

    // PHY must be stopped before radio otherwise it will lock on rf_recv()
    mac.stop();
    phy.stop();
    radio.stop();
    
    usleep(1e5);
    if(args->pcap.enable) {
       mac_pcap.close();
    }
    if(args->pcap.nas_enable) {
       nas_pcap.close();
    }
    if(args->trace.enable) {
      phy.write_trace(args->trace.phy_filename);
      radio.write_trace(args->trace.radio_filename);
    }
    started = false;
  }
}

bool ue::switch_on() {
  return nas.attach_request();
}

bool ue::switch_off() {
  // generate detach request
  nas.detach_request();

  // wait for max. 5s for it to be sent (according to TS 24.301 Sec 25.5.2.2)
  const uint32_t RB_ID_SRB1 = 1;
  int cnt = 0, timeout = 5;
  while (rlc.has_data(RB_ID_SRB1) && ++cnt <= timeout) {
    sleep(1);
  }
  bool detach_sent = true;
  if (rlc.has_data(RB_ID_SRB1)) {
    nas_log.warning("Detach couldn't be sent after %ds.\n", timeout);
    detach_sent = false;
  }

  return detach_sent;
}

bool ue::is_attached()
{
  return rrc.is_connected();
}

void ue::start_plot() {
  phy.start_plot();
}

void ue::print_pool() {
  byte_buffer_pool::get_instance()->print_all_buffers();
}

bool ue::get_metrics(ue_metrics_t &m)
{
  bzero(&m, sizeof(ue_metrics_t));
  m.rf = rf_metrics;
  bzero(&rf_metrics, sizeof(rf_metrics_t));
  rf_metrics.rf_error = false; // Reset error flag

  if(EMM_STATE_REGISTERED == nas.get_state()) {
    if(RRC_STATE_CONNECTED == rrc.get_state()) {
      phy.get_metrics(m.phy);
      mac.get_metrics(m.mac);
      rlc.get_metrics(m.rlc);
      gw.get_metrics(m.gw);
      return true;
    }
  }
  return false;
}
//==================================================================================

void ue::set_lwipep_auto_control_ratio(bool dl_auto, bool ul_auto)
{
	lwipep.set_lwipep_auto_control_ratio(ul_auto, dl_auto);
	return;
}

void ue::set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate)
{
	lwipep.set_lwipep_wlan(ul_aggregate, dl_aggregate);
	return;
}

void ue::lwipep_state()
{
	lwipep.lwipep_state();
	return;
}

void ue::set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio)
{
	if(lte_tx_ratio == 0 && wlan_ratio == 0)
		return;
	lwipep.set_lwipep_tx_ratio(lte_tx_ratio, wlan_ratio);
	
}

void ue::set_lwipep_reorder_buffer_size(uint32_t buffer_size)
{
	if(buffer_size == 12 || buffer_size == 15 || buffer_size == 17 || buffer_size == 18)
		lwipep.set_lwipep_reorder_buffer_size(buffer_size);
	return;
}

void ue::set_lwipep_report_period(uint32_t report_period)
{
	lwipep.set_lwipep_report_period(report_period);
	return;
}
void ue::set_reorder_timeout(uint32_t reorder_timeout)
{
	lwipep.set_reorder_timeout(reorder_timeout);
	return;
}

//==================================================================================
void ue::radio_overflow() {
  phy.radio_overflow();
}
void ue::print_mbms()
{
  rrc.print_mbms();
}

bool ue::mbms_service_start(uint32_t serv, uint32_t port)
{
  return rrc.mbms_service_start(serv, port);
}

void ue::rf_msg(srslte_rf_error_t error)
{
  ue_base *ue = ue_base::get_instance(LTE);
  ue->handle_rf_msg(error);
  if (error.type == srslte_rf_error_t::SRSLTE_RF_ERROR_OVERFLOW) {
    ue->radio_overflow();
  } else
  if (error.type == srslte_rf_error_t::SRSLTE_RF_ERROR_RX) {
    ue->stop();
    ue->cleanup();
    exit(-1);
  }
}

} // namespace srsue
