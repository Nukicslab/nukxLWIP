/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2017 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of srsLTE.
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

#include <boost/algorithm/string.hpp>
#include "srsenb/hdr/enb.h"
#include "srslte/build_info.h"
#include <iostream>
#include <sstream>

namespace srsenb {

enb*          enb::instance = NULL;
pthread_mutex_t enb_instance_mutex = PTHREAD_MUTEX_INITIALIZER;

enb* enb::get_instance(void)
{
  pthread_mutex_lock(&enb_instance_mutex);
  if(NULL == instance) {
    instance = new enb();
  }
  pthread_mutex_unlock(&enb_instance_mutex);
  return(instance);
}
void enb::cleanup(void)
{
  srslte_dft_exit();
  srslte::byte_buffer_pool::cleanup();
  pthread_mutex_lock(&enb_instance_mutex);
  if(NULL != instance) {
      delete instance;
      instance = NULL;
  }
  pthread_mutex_unlock(&enb_instance_mutex);
}

enb::enb() : started(false) {
  // print build info
  std::cout << std::endl << get_build_string() << std::endl;

  srslte_dft_load();
  pool = srslte::byte_buffer_pool::get_instance(ENB_POOL_SIZE);

  logger = NULL;
  args = NULL;

  bzero(&rf_metrics, sizeof(rf_metrics));
}

enb::~enb()
{
  for (uint32_t i = 0; i < phy_log.size(); i++) {
    delete (phy_log[i]);
  }
}

bool enb::init(all_args_t *args_)
{
  args = args_;

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
  for (int i=0;i<args->expert.phy.nof_phy_threads;i++) {
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
  lwipep_xw_log.init("LWIPEP_XW", logger);
  
  rrc_log.init("RRC ", logger);
  gtpu_log.init("GTPU", logger);
  s1ap_log.init("S1AP", logger);

  pool_log.init("POOL", logger);
  pool_log.set_level(srslte::LOG_LEVEL_ERROR);
  pool->set_log(&pool_log);

  // Init logs
  rf_log.set_level(srslte::LOG_LEVEL_INFO);
  for (int i=0;i<args->expert.phy.nof_phy_threads;i++) {
    ((srslte::log_filter*) phy_log[i])->set_level(level(args->log.phy_level));
  }
  mac_log.set_level(level(args->log.mac_level));
  rlc_log.set_level(level(args->log.rlc_level));
  pdcp_log.set_level(level(args->log.pdcp_level));
  
  lwipep_log.set_level(level(args->log.lwipep_level));
  lwipep_xw_log.set_level(level(args->log.lwipep_xw_level));
  
  rrc_log.set_level(level(args->log.rrc_level));
  gtpu_log.set_level(level(args->log.gtpu_level));
  s1ap_log.set_level(level(args->log.s1ap_level));

  for (int i=0;i<args->expert.phy.nof_phy_threads;i++) {
    ((srslte::log_filter*) phy_log[i])->set_hex_limit(args->log.phy_hex_limit);
  }
  mac_log.set_hex_limit(args->log.mac_hex_limit);
  rlc_log.set_hex_limit(args->log.rlc_hex_limit);
  pdcp_log.set_hex_limit(args->log.pdcp_hex_limit);
  
  lwipep_log.set_hex_limit(args->log.lwipep_hex_limit);
  lwipep_xw_log.set_hex_limit(args->log.lwipep_xw_hex_limit);
  
  rrc_log.set_hex_limit(args->log.rrc_hex_limit);
  gtpu_log.set_hex_limit(args->log.gtpu_hex_limit);
  s1ap_log.set_hex_limit(args->log.s1ap_hex_limit);

  // Parse config files
  srslte_cell_t cell_cfg;
  phy_cfg_t     phy_cfg;
  rrc_cfg_t     rrc_cfg;

  if (parse_cell_cfg(args, &cell_cfg)) {
    fprintf(stderr, "Error parsing Cell configuration\n");
    return false;
  }
  if (parse_sibs(args, &rrc_cfg, &phy_cfg)) {
    fprintf(stderr, "Error parsing SIB configuration\n");
    return false;
  }
  if (parse_rr(args, &rrc_cfg)) {
    fprintf(stderr, "Error parsing Radio Resources configuration\n");
    return false;
  }
  if (parse_drb(args, &rrc_cfg)) {
    fprintf(stderr, "Error parsing DRB configuration\n");
    return false;
  }

  uint32_t prach_freq_offset = rrc_cfg.sibs[1].sib2().rr_cfg_common.prach_cfg.prach_cfg_info.prach_freq_offset;

  if (cell_cfg.nof_prb > 10) {
    if (prach_freq_offset + 6 > cell_cfg.nof_prb - SRSLTE_MAX(rrc_cfg.cqi_cfg.nof_prb, rrc_cfg.sr_cfg.nof_prb)) {
      fprintf(stderr, "Invalid PRACH configuration: frequency offset=%d outside bandwidth limits\n", prach_freq_offset);
      return false;
    }

    if (prach_freq_offset < SRSLTE_MAX(rrc_cfg.cqi_cfg.nof_prb, rrc_cfg.sr_cfg.nof_prb)) {
      fprintf(stderr, "Invalid PRACH configuration: frequency offset=%d lower than CQI offset: %d or SR offset: %d\n",
              prach_freq_offset, rrc_cfg.cqi_cfg.nof_prb, rrc_cfg.sr_cfg.nof_prb);
      return false;
    }
  } else { // 6 PRB case
    if (prach_freq_offset + 6 > cell_cfg.nof_prb) {
      fprintf(stderr,
              "Invalid PRACH configuration: frequency interval=(%d, %d) does not fit into the eNB PRBs=(0,%d)\n",
              prach_freq_offset, prach_freq_offset + 6, cell_cfg.nof_prb);
      return false;
    }
  }

  rrc_cfg.inactivity_timeout_ms = args->expert.rrc_inactivity_timer;
  rrc_cfg.enable_mbsfn          = args->expert.enable_mbsfn;

  // Copy cell struct to rrc and phy
  memcpy(&rrc_cfg.cell, &cell_cfg, sizeof(srslte_cell_t));
  memcpy(&phy_cfg.cell, &cell_cfg, sizeof(srslte_cell_t));

  // Set up pcap and trace
  if(args->pcap.enable)
  {
    mac_pcap.open(args->pcap.filename.c_str());
    mac.start_pcap(&mac_pcap);
  }

  // Init layers
  
  /* Start Radio */
  char *dev_name = NULL;
  if (args->rf.device_name.compare("auto")) {
    dev_name = (char*) args->rf.device_name.c_str();
  }
  
  char *dev_args = NULL;
  if (args->rf.device_args.compare("auto")) {
    dev_args = (char*) args->rf.device_args.c_str();
  }

  if(!radio.init(dev_args, dev_name, args->enb.nof_ports))
  {
    printf("Failed to find device %s with args %s\n",
           args->rf.device_name.c_str(), args->rf.device_args.c_str());
    return false;
  }    
  
  // Set RF options
  if (args->rf.time_adv_nsamples.compare("auto")) {
    radio.set_tx_adv(atoi(args->rf.time_adv_nsamples.c_str()));
  }  
  if (args->rf.burst_preamble.compare("auto")) {
    radio.set_burst_preamble(atof(args->rf.burst_preamble.c_str()));    
  }

  radio.set_rx_gain(args->rf.rx_gain);
  radio.set_tx_gain(args->rf.tx_gain);
  ((srslte::log_filter*) phy_log[0])->console("Setting frequency: DL=%.1f Mhz, UL=%.1f MHz\n", args->rf.dl_freq/1e6, args->rf.ul_freq/1e6);

  radio.set_tx_freq(args->rf.dl_freq);
  radio.set_rx_freq(args->rf.ul_freq);

  radio.register_error_handler(rf_msg);

  // Init all layers   
  phy.init(&args->expert.phy, &phy_cfg, &radio, &mac, phy_log);
  mac.init(&args->expert.mac, &cell_cfg, &phy, &rlc, &rrc, &mac_log);
  rlc.init(&pdcp, &rrc, &mac, &mac, &rlc_log);
  pdcp.init(&rlc, &lwipep, &rrc, &gtpu, &pdcp_log);
  
  lwipep.init(&rlc, &pdcp, &lwipep_xw, &rrc, &mac, &gtpu, &lwipep_log);
  lwipep_xw.init(&lwipep, &lwipep_xw_log);
  
  rrc.init(&rrc_cfg, &phy, &mac, &rlc, &pdcp, &lwipep, &s1ap, &gtpu, &rrc_log);
  s1ap.init(args->enb.s1ap, &rrc, &s1ap_log);
  gtpu.init(args->enb.s1ap.gtp_bind_addr, args->enb.s1ap.mme_addr, args->expert.m1u_multiaddr, args->expert.m1u_if_addr, &pdcp, &lwipep, &gtpu_log, args->expert.enable_mbsfn);

  started = true;
  return true;
}

void enb::pregenerate_signals(bool enable)
{
  //phy.enable_pregen_signals(enable);
}

void enb::stop()
{
  if(started)
  {
    s1ap.stop();
    gtpu.stop();
    phy.stop();
    mac.stop();
    usleep(50000);

    rlc.stop();
    pdcp.stop();
	
	lwipep.stop();
	lwipep_xw.stop();
	
    rrc.stop();

    usleep(10000);
    if(args->pcap.enable)
    {
       mac_pcap.close();
    }
    radio.stop();
    started = false;
  }
}

void enb::start_plot() {
  phy.start_plot();
}

void enb::print_pool() {
  srslte::byte_buffer_pool::get_instance()->print_all_buffers();
}

bool enb::get_metrics(enb_metrics_t &m)
{
  m.rf = rf_metrics;
  bzero(&rf_metrics, sizeof(rf_metrics_t));
  rf_metrics.rf_error = false; // Reset error flag

  phy.get_metrics(m.phy);
  mac.get_metrics(m.mac);
  rrc.get_metrics(m.rrc);
  s1ap.get_metrics(m.s1ap);

  m.running = started;  
  return true;
}

void enb::rf_msg(srslte_rf_error_t error)
{
  enb *u = enb::get_instance();
  u->handle_rf_msg(error);
}

void enb::handle_rf_msg(srslte_rf_error_t error)
{
  if(error.type == srslte_rf_error_t::SRSLTE_RF_ERROR_OVERFLOW) {
    rf_metrics.rf_o++;
    rf_metrics.rf_error = true;
    rf_log.warning("Overflow\n");
  }else if(error.type == srslte_rf_error_t::SRSLTE_RF_ERROR_UNDERFLOW) {
    rf_metrics.rf_u++;
    rf_metrics.rf_error = true;
    rf_log.warning("Underflow\n");
  } else if(error.type == srslte_rf_error_t::SRSLTE_RF_ERROR_LATE) {
    rf_metrics.rf_l++;
    rf_metrics.rf_error = true;
    rf_log.warning("Late\n");
  } else if (error.type == srslte_rf_error_t::SRSLTE_RF_ERROR_OTHER) {
    std::string str(error.msg);
    str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
    str.erase(std::remove(str.begin(), str.end(), '\r'), str.end());
    str.push_back('\n');
    rf_log.info("%s\n", str.c_str());
  }
}

srslte::LOG_LEVEL_ENUM enb::level(std::string l)
{
  boost::to_upper(l);
  if("NONE" == l){
    return srslte::LOG_LEVEL_NONE;
  }else if("ERROR" == l){
    return srslte::LOG_LEVEL_ERROR;
  }else if("WARNING" == l){
    return srslte::LOG_LEVEL_WARNING;
  }else if("INFO" == l){
    return srslte::LOG_LEVEL_INFO;
  }else if("DEBUG" == l){
    return srslte::LOG_LEVEL_DEBUG;
  }else{
    return srslte::LOG_LEVEL_NONE;
  }
}

std::string enb::get_build_mode()
{
  return std::string(srslte_get_build_mode());
}

std::string enb::get_build_info()
{
  if (std::string(srslte_get_build_info()).find("  ") != std::string::npos) {
    return std::string(srslte_get_version());
  }
  return std::string(srslte_get_build_info());
}

std::string enb::get_build_string()
{
  std::stringstream ss;
  ss << "Built in " << get_build_mode() << " mode using " << get_build_info() << "." << std::endl;
  return ss.str();
}

void enb::set_lwipep_auto_control_ratio(bool dl_auto, bool ul_auto)
{
	lwipep.set_lwipep_auto_control_ratio(ul_auto, dl_auto);
	return;
}

void enb::set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate)
{
	lwipep.set_lwipep_wlan(ul_aggregate, dl_aggregate);
	return;
}

void enb::lwipep_state()
{
	lwipep.lwipep_state();
	return;
}

void enb::set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio)
{
	if(lte_tx_ratio == 0 && wlan_ratio == 0)
		return;
	lwipep.set_lwipep_tx_ratio(lte_tx_ratio, wlan_ratio);
	
}

void enb::set_lwipep_reorder_buffer_size(uint32_t buffer_size)
{
	if(buffer_size == 12 || buffer_size == 15 || buffer_size == 17 || buffer_size == 18)
		lwipep.set_lwipep_reorder_buffer_size(buffer_size);
	return;
}

void enb::set_lwipep_report_period(uint32_t report_period)
{
	lwipep.set_lwipep_report_period(report_period);
	return;
}
void enb::set_reorder_timeout(uint32_t reorder_timeout)
{
	lwipep.set_reorder_timeout(reorder_timeout);
	return;
}

} // namespace srsenb
