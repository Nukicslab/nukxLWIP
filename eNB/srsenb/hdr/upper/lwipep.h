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

#include <map>
#include "srslte/interfaces/ue_interfaces.h"
#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/upper/lwipep.h"

#ifndef SRSENB_LWIPEP_H
#define SRSENB_LWIPEP_H

//#define SRC_ADDR "192.168.50.49"
//#define DST_ADDR "192.168.50.227"
#define SRC_ADDR "39.50.168.192"
#define DST_ADDR "49.50.168.192"

namespace srsenb {
  
class lwipep :	public lwipep_interface_pdcp, 
				public lwipep_interface_gtpu,
				public lwipep_interface_rrc,
				public lwipep_interface_lwipep_xw
{
public:
	
	lwipep();
	void init(rlc_interface_lwipep *rlc_,
			  pdcp_interface_lwipep *pdcp_, 
			  lwipep_xw_interface_lwipep *xw_, 
			  rrc_interface_lwipep *rrc_, 
			  srslte::mac_interface_timers *mac_timers_,
			  gtpu_interface_lwipep *gtpu_, srslte::log *lwipep_log_);
	void stop(); 

	// lwipep_interface_pdcp
	void write_pdu(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t *pdu, uint32_t net_if);
	void write_pdu_mch(uint32_t lcid, srslte::byte_buffer_t *sdu){}

	// lwipep_interface_rrc
	void add_user(uint16_t rnti);  
	
	void reset(uint16_t rnti);
	void rem_user(uint16_t rnti); 
	void add_bearer(uint16_t rnti, uint32_t lcid, srslte::srslte_pdcp_config_t cnfg);
	void config_security(uint16_t rnti, 
						 uint32_t lcid,
						 uint8_t *k_rrc_enc_,
						 uint8_t *k_rrc_int_,
						 srslte::CIPHERING_ALGORITHM_ID_ENUM cipher_algo_,
						 srslte::INTEGRITY_ALGORITHM_ID_ENUM integ_algo_);

	//lwipep_interface_gtpu, lwipep_interface_rrc
	void write_sdu(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t *sdu);
	
	//lwipep_interface_xw, it be "run_thread" function
	void receive_from_ue(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t *sdu);
	
	//not work, have not define
	//void run_thread();
	
	void set_lwipep_auto_control_ratio(bool dl_auto, bool ul_auto);
	void set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate);
	void lwipep_state();
	void set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio);
	void set_lwipep_reorder_buffer_size(uint32_t buffer_size);
	void set_lwipep_report_period(uint32_t report_period);
	void set_reorder_timeout(uint32_t reorder_timeout);
	
	void get_mac(uint8_t *DST_MAC0, uint8_t *DST_MAC1, uint8_t *DST_MAC2, uint8_t *DST_MAC3, uint8_t *DST_MAC4, uint8_t *DST_MAC5);
	
	uint8_t DST_MAC0 ;
	uint8_t DST_MAC1 ;
	uint8_t DST_MAC2 ;
	uint8_t DST_MAC3 ;
	uint8_t DST_MAC4 ;
	uint8_t DST_MAC5 ;
	
	private: 

		
	class user_interface_pdcp : public srsue::pdcp_interface_lwipep
	{
	public:
		uint16_t rnti; 
		srsenb::pdcp_interface_lwipep *pdcp; 
		// rlc_interface_pdcp
		void write_sdu(uint32_t lcid,  srslte::byte_buffer_t *sdu, bool blocking);
	};
	
	class user_interface_xw : public srsue::rlc_interface_lwipep,
							  public srsue::tunnel_interface_lwipep
	{
	public: 
		uint16_t rnti; 
		srsenb::lwipep_xw_interface_lwipep *lwipep_xw;
		srsenb::rlc_interface_lwipep *rlc; 
		// rlc_interface_pdcp
		uint32_t get_buffer_state(uint32_t lcid);
		// xw_interface_lwipep
		void write_sdu(uint32_t lcid, srslte::byte_buffer_t *pdu);
		void stop();
	};
	
	class user_interface_rrc : public srsue::rrc_interface_lwipep
	{
	public: 
		uint16_t rnti; 
		srsenb::rrc_interface_lwipep *rrc;
		// rrc_interface_pdcp
		void write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu);
	};

	class user_interface_gtpu : public srsue::gw_interface_lwipep
	{
	public: 
		uint16_t rnti; 
		srsenb::gtpu_interface_lwipep  *gtpu;
		// gw_interface_lwipep
		void write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu);
	}; 
	
	class user_interface 
	{
	public: 
		user_interface_pdcp		pdcp_itf; 
		user_interface_gtpu		gtpu_itf;
		user_interface_rrc		rrc_itf; 
		user_interface_xw		xw_itf;
		srslte::lwipep			*lwipep; 
	};

	void clear_user(user_interface *ue);

	std::map<uint32_t,user_interface> users;

	pthread_rwlock_t rwlock;

	srsenb::rlc_interface_lwipep 			*rlc;
	srsenb::lwipep_xw_interface_lwipep 		*lwipep_xw;
	srsenb::rrc_interface_lwipep 			*rrc;
	srsenb::gtpu_interface_lwipep  			*gtpu;
	srsenb::pdcp_interface_lwipep 			*pdcp; 
	srslte::mac_interface_timers			*mac_timers; 
	
	srslte::log					*lwipep_log;
	srslte::byte_buffer_pool	*pool;
	
	
};

}

#endif // SRSENB_PDCP_H
