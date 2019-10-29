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

#include "srsue/hdr/upper/lwipep.h"

namespace srsue {

	lwipep::lwipep()
	{
		rnti = 0;
		/*
		DST_MAC0 = 0x00;
		DST_MAC1 = 0x00;
		DST_MAC2 = 0x00;
		DST_MAC3 = 0x00;
		DST_MAC4 = 0x00;
		DST_MAC5 = 0x00;
		
		DST_MAC0 = 0x98;
		DST_MAC1 = 0xde;
		DST_MAC2 = 0xd0;
		DST_MAC3 = 0x13;
		DST_MAC4 = 0x90;
		DST_MAC5 = 0x70;
		
		DST_MAC0 = 0x78;
		DST_MAC1 = 0x24;
		DST_MAC2 = 0xaf;
		DST_MAC3 = 0x04;
		DST_MAC4 = 0x55;
		DST_MAC5 = 0x65;
		*/
		DST_MAC0 = 0xf4;
		DST_MAC1 = 0x96;
		DST_MAC2 = 0x34;
		DST_MAC3 = 0x03;
		DST_MAC4 = 0x1a;
		DST_MAC5 = 0x74;
	}
	void lwipep::init(srsue::rlc_interface_lwipep *rlc_,
					  srsue::pdcp_interface_lwipep *pdcp_, 
					  srsue::lwipep_ipsec_interface_lwipep *lwipep_ipsec_, 
					  srsue::rrc_interface_lwipep *rrc_,
					  srslte::mac_interface_timers *mac_timers_,
					  srsue::gw_interface_lwipep *gw_,
					  srslte::log *lwipep_log_, uint32_t lcid_)
	{
		rlc					= rlc_;
		pdcp				= pdcp_; 
		lwipep_ipsec		= lwipep_ipsec_;
		rrc					= rrc_; 
		mac_timers			= mac_timers_;
		gw					= gw_;
		lwipep_log			= lwipep_log_;
		lcid				= lcid_;
		
		DST_MAC0 = 0xf4;
		DST_MAC1 = 0x96;
		DST_MAC2 = 0x34;
		DST_MAC3 = 0x03;
		DST_MAC4 = 0x1a;
		DST_MAC5 = 0x74;
		
		lwipep_entity = new srslte::lwipep;
		tunnel = new tunnel_interface_lwipep_ipsec;
		tunnel->lwipep_ipsec = lwipep_ipsec;
		tunnel->rlc = rlc;
		lwipep_entity->init(pdcp, tunnel, rrc, mac_timers, gw, lwipep_log, lcid, 0);

//		pthread_rwlock_init(&rwlock, NULL);
	}

	void lwipep::stop()		
	{
		lwipep_entity->stop();
	}

	void lwipep::reestablish(uint16_t rnti_)
	{
	}

	void lwipep::write_pdu(uint32_t lcid, srslte::byte_buffer_t* sdu, uint32_t net_if)
	{	lwipep_entity->write_pdu(lcid, sdu, net_if);	}

	void lwipep::write_sdu(uint32_t lcid, srslte::byte_buffer_t* sdu, bool blocking)
	{	lwipep_entity->write_sdu(lcid, sdu);	}
	
	void lwipep::set_crnti(uint16_t rnti_)
	{	
		lwipep_entity->set_rnti(rnti_);	
		lwipep_entity->set_addr(inet_addr(SRC_ADDR), inet_addr(DST_ADDR));
		lwipep_log->console("lwipep rnti = 0x%x\n", rnti_);
	}
	
	void lwipep::get_mac(uint8_t *DST_MAC0_, uint8_t *DST_MAC1_, uint8_t *DST_MAC2_, uint8_t *DST_MAC3_, uint8_t *DST_MAC4_, uint8_t *DST_MAC5_)
	{
		*DST_MAC0_ = DST_MAC0;
		*DST_MAC1_ = DST_MAC1;
		*DST_MAC2_ = DST_MAC2;
		*DST_MAC3_ = DST_MAC3;
		*DST_MAC4_ = DST_MAC4;
		*DST_MAC5_ = DST_MAC5;
	}


//===================================================================================================//
//============================================SETTING================================================//
//===================================================================================================//
	void lwipep::set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate)
	{	lwipep_entity->set_lwipep_wlan(dl_aggregate, ul_aggregate);	}
	
	void lwipep::set_lwipep_auto_control_ratio(bool dl_auto, bool ul_auto)
	{	lwipep_entity->set_lwipep_auto_control_ratio(dl_auto, ul_auto);	}
	
	void lwipep::set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio)
	{	lwipep_entity->set_lwipep_tx_ratio(lte_tx_ratio, wlan_ratio);	}

	void lwipep::set_lwipep_reorder_buffer_size(uint32_t buffer_size)
	{	lwipep_entity->set_lwipep_reorder_buffer_size(buffer_size);	}

	void lwipep::set_lwipep_report_period(uint32_t report_period)
	{	lwipep_entity->set_lwipep_report_period(report_period);	}

	void lwipep::set_reorder_timeout(uint32_t reorder_timeout)
	{	lwipep_entity->set_reorder_timeout(reorder_timeout);	}

	void lwipep::lwipep_state()
	{	lwipep_entity->lwipep_state();	}	
	
	void lwipep::tunnel_interface_lwipep_ipsec::write_sdu(uint32_t lcid, srslte::byte_buffer_t *sdu)
	{
		lwipep_ipsec->write_sdu(lcid, sdu);
	}
	
	void lwipep::tunnel_interface_lwipep_ipsec::stop()
	{
		lwipep_ipsec->stop();
	}
	
	uint32_t lwipep::tunnel_interface_lwipep_ipsec::get_buffer_state(uint32_t lcid)
	{
		rlc->get_buffer_state(lcid);
	}
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	

}
