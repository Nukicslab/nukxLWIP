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

#ifndef SRSUE_LWIPEP_H
#define SRSUE_LWIPEP_H

#include <map>
#include "srslte/interfaces/ue_interfaces.h"
#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/upper/lwipep.h"

//#define SRC_ADDR "192.168.50.49"
//#define DST_ADDR "192.168.50.227"
#define SRC_ADDR "49.50.168.192"
#define DST_ADDR "38.50.168.192"

namespace srsue {
  
class lwipep :	public lwipep_interface_pdcp, 
				public lwipep_interface_gw,
				public lwipep_interface_rrc,
				public lwipep_interface_lwipep_ipsec,
				public lwipep_interface_phy
//				public thread
{
public:
	
	lwipep();
	void init(srsue::rlc_interface_lwipep *rlc_,
			  srsue::pdcp_interface_lwipep *pdcp_, 
			  srsue::lwipep_ipsec_interface_lwipep *lwipep_ipsec_, 
			  srsue::rrc_interface_lwipep *rrc_,
			  srslte::mac_interface_timers *mac_timers_,
			  srsue::gw_interface_lwipep *gw_,
			  srslte::log *lwipep_log_, uint32_t lcid_);	// ,srslte::srslte_lwipep_config_t cfg_

	void stop(); 

	// lwipep_interface_pdcp && ipsec
	void write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu, uint32_t net_if);
//	void lwipep_interface_lwipep_ipsec::write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu);
//	void lwipep_interface_pdcp::write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu);

	// lwipep_interface_rrc
	void reestablish(uint16_t rnti);  
	void set_crnti(uint16_t rnti);  
	
	void reset(uint16_t rnti);
	void rem_user(uint16_t rnti); 
	void add_bearer(uint16_t rnti, uint32_t lcid, srslte::srslte_pdcp_config_t cnfg);
	void config_security(uint16_t rnti, 
						 uint32_t lcid,
						 uint8_t *k_rrc_enc_,
						 uint8_t *k_rrc_int_,
						 srslte::CIPHERING_ALGORITHM_ID_ENUM cipher_algo_,
						 srslte::INTEGRITY_ALGORITHM_ID_ENUM integ_algo_);

	//lwipep_interface_gw, lwipep_interface_rrc
	void write_sdu(uint32_t lcid, srslte::byte_buffer_t *sdu, bool blocking);
	
	//lwipep_interface_xw, it be "run_thread" function
	void receive_from_ue(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t *sdu);
	
	//not work, have not define
//	void run_thread();
	
	void set_lwipep_auto_control_ratio(bool dl_auto, bool ul_auto);
	void set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate);
	void lwipep_state();
	void set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio);
	void set_lwipep_reorder_buffer_size(uint32_t buffer_size);
	void set_lwipep_report_period(uint32_t report_period);
	void set_reorder_timeout(uint32_t reorder_timeout);
	void set_rnti(uint16_t rnti_);
	
	
	
	void get_mac(uint8_t *DST_MAC0, uint8_t *DST_MAC1, uint8_t *DST_MAC2, uint8_t *DST_MAC3, uint8_t *DST_MAC4, uint8_t *DST_MAC5);
	uint8_t DST_MAC0 ;
	uint8_t DST_MAC1 ;
	uint8_t DST_MAC2 ;
	uint8_t DST_MAC3 ;
	uint8_t DST_MAC4 ;
	uint8_t DST_MAC5 ;
	
	private: 
		
	uint16_t rnti;

//	void clear_user(user_interface *ue);

//	std::map<uint32_t,user_interface> users;

//	pthread_rwlock_t rwlock;

	srsue::rlc_interface_lwipep 			*rlc;
	srsue::lwipep_ipsec_interface_lwipep 	*lwipep_ipsec;
	srsue::rrc_interface_lwipep 			*rrc;
	srsue::gw_interface_lwipep  			*gw;
	srsue::pdcp_interface_lwipep 			*pdcp; 
	srslte::mac_interface_timers			*mac_timers; 
	mac_interface_rrc::ue_rnti_t			*rntis;
	srslte::lwipep							*lwipep_entity;
	
	srslte::log				*lwipep_log;
	srslte::byte_buffer_pool *pool;
	uint32_t lcid;
	
	class tunnel_interface_lwipep_ipsec : public srsue::tunnel_interface_lwipep
	{
	public:
		uint16_t rnti; 
		srsue::rlc_interface_lwipep 			*rlc;
		srsue::lwipep_ipsec_interface_lwipep	*lwipep_ipsec; 
		void write_sdu(uint32_t lcid, srslte::byte_buffer_t *sdu);
		void stop();
		uint32_t get_buffer_state(uint32_t lcid);
	};
	
	tunnel_interface_lwipep_ipsec 	*tunnel;
	
	
	/*
	class tunnel_interface 
	{
	public: 
		tunnel_interface_lwipep_ipsec		lwipep_itf;
	}; 
	
	std::map<uint32_t,tunnel_interface> tunnel;
	*/
	//srslte::srslte_lwaap_config_t cfg;
};

}

#endif // SRSENB_PDCP_H
