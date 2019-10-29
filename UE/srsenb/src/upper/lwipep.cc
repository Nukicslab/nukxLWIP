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

#include "srsenb/hdr/upper/lwipep.h"

namespace srsenb {

	lwipep::lwipep()
	{
		/*
		DST_MAC0 = 0x00;
		DST_MAC1 = 0x00;
		DST_MAC2 = 0x00;
		DST_MAC3 = 0x00;
		DST_MAC4 = 0x00;
		DST_MAC5 = 0x00;
		
		DST_MAC0 = 0x10;
		DST_MAC1 = 0x7b;
		DST_MAC2 = 0x44;
		DST_MAC3 = 0x23;
		DST_MAC4 = 0x07;
		DST_MAC5 = 0x55;
		
		// big
		DST_MAC0 = 0xf4;
		DST_MAC1 = 0x96;
		DST_MAC2 = 0x34;
		DST_MAC3 = 0x03;
		DST_MAC4 = 0x1a;
		DST_MAC5 = 0x74;
		
		// small
		DST_MAC0 = 0xf4;
		DST_MAC1 = 0x8c;
		DST_MAC2 = 0x50;
		DST_MAC3 = 0xb4;
		DST_MAC4 = 0x06;
		DST_MAC5 = 0xdb;
		
		*/
		
	}
	void lwipep::init(rlc_interface_lwipep *rlc_,
					  pdcp_interface_lwipep *pdcp_, 
					  lwipep_hdl_rpt_interface_lwipep *lwipep_hdl_rpt_,
					  lwipep_xw_interface_lwipep *xw_, 
					  rrc_interface_lwipep *rrc_, 
					  srslte::mac_interface_timers *mac_timers_, 
					  gtpu_interface_lwipep *gtpu_, srslte::log *lwipep_log_)
	{
		rlc			= rlc_;
		pdcp		= pdcp_; 
		lwipep_xw	= xw_;
		rrc			= rrc_; 
		mac_timers	= mac_timers_;
		gtpu		= gtpu_;
		lwipep_log	= lwipep_log_;
		
		DST_MAC0 = 0xf4;
		DST_MAC1 = 0x96;
		DST_MAC2 = 0x34;
		DST_MAC3 = 0x03;
		DST_MAC4 = 0x1a;
		DST_MAC5 = 0x74;

		pool = srslte::byte_buffer_pool::get_instance();

		pthread_rwlock_init(&rwlock, NULL);
	}

	//it's not work, just write a model
	void lwipep::stop()		
	{
		for(std::map<uint32_t, user_interface>::iterator iter=users.begin(); iter!=users.end(); ++iter) {
		clear_user(&iter->second);
		}
		users.clear();
	}

	void lwipep::add_user(uint16_t rnti)
	{
		pthread_rwlock_rdlock(&rwlock);
		if (users.count(rnti) == 0) 
		{
			srslte::lwipep *obj = new srslte::lwipep;     
			obj->init(&users[rnti].pdcp_itf,
					  &users[rnti].lwipep_hdl_rpt_itf,
					  &users[rnti].xw_itf,
					  &users[rnti].rrc_itf,
					  mac_timers,
					  &users[rnti].gtpu_itf, 
					  lwipep_log, 3/*DRB1*/, rnti);
			
			//drbid = lcid = 3
			users[rnti].pdcp_itf.rnti 			= rnti;
			users[rnti].lwipep_hdl_rpt_itf.rnti	= rnti;
			users[rnti].xw_itf.rnti				= rnti;
			users[rnti].rrc_itf.rnti			= rnti;
			users[rnti].gtpu_itf.rnti			= rnti;
			
			users[rnti].xw_itf.rlc							= rlc;
			users[rnti].pdcp_itf.pdcp						= pdcp;
			users[rnti].lwipep_hdl_rpt_itf.lwipep_hdl_rpt	= lwipep_hdl_rpt;
			users[rnti].xw_itf.lwipep_xw					= lwipep_xw;
			users[rnti].rrc_itf.rrc							= rrc;
			users[rnti].gtpu_itf.gtpu						= gtpu;
			users[rnti].lwipep = obj;
			
			users[rnti].lwipep->set_addr(inet_addr(SRC_ADDR), inet_addr(DST_ADDR));
			
		}
		pthread_rwlock_unlock(&rwlock);
	}

	// Private unlocked deallocation of user
	void lwipep::clear_user(user_interface *ue)
	{
		ue->lwipep->stop();
		delete ue->lwipep;
		ue->lwipep = NULL;
	}

	void lwipep::rem_user(uint16_t rnti)
	{
		pthread_rwlock_wrlock(&rwlock);
		if (users.count(rnti)) 
		{
			clear_user(&users[rnti]);
			users.erase(rnti);
		}
		pthread_rwlock_unlock(&rwlock);
	}

	//it's not work, just write a model
	/*
	void lwipep::add_bearer(uint16_t rnti, uint32_t lcid, srslte::srslte_lwipep_config_t cfg)	
	{
		/*
		pthread_rwlock_rdlock(&rwlock);
		if (users.count(rnti)) 
		{
			if(rnti != SRSLTE_MRNTI)
			{	users[rnti].lwipep->add_bearer(lcid, cfg);	}
			else 
			{	users[rnti].lwipep->add_bearer_mrb(lcid, cfg);	}
		}
		pthread_rwlock_unlock(&rwlock);
		
	}
	*/

//===================================================================================================//
//============================================write_pdu==============================================//
//===================================================================================================//

	//lwipep_interface_lwipep_xw, lwipep_interface_pdcp
	void lwipep::write_pdu(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t* sdu)
	{
		if (users.count(rnti)) 
		{	users[rnti].lwipep->write_pdu(lcid, sdu);	} 
		else 
		{	pool->deallocate(sdu);	}
	}
	
	
	/*
	void lwipep::write_pdu(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t* sdu)
	{
		if (users.count(rnti)) 
		{	users[rnti].lwipep->write_pdu(lcid, sdu);	} 
		else 
		{	pool->deallocate(sdu);	}
	}
	*/
//===================================================================================================//
//============================================write_pdu==============================================//
//===================================================================================================//
	
	void lwipep::write_sdu(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t* sdu)
	{
		if (users.count(rnti)) 
		{
			if(rnti != SRSLTE_MRNTI)
			{	users[rnti].lwipep->write_sdu(lcid, sdu);	}
			else 
			{	pool->deallocate(sdu);	}
			
		}
	}

	//run_thread
	/*
	void lwipep::receive_from_ue(uint16_t rnti, uint32_t lcid, srslte::byte_buffer_t* sdu)	
	{
		//read gtp header
		lwipep->write_pdu(rnti, lcid, sdu);
	}
	*/
	void lwipep::user_interface_xw::write_sdu(uint32_t lcid,  srslte::byte_buffer_t *sdu)
	{
		lwipep_xw->write_sdu(lcid, sdu);
	}
	
	void lwipep::user_interface_xw::stop()
	{
		lwipep_xw->stop();
	}
	
	void lwipep::user_interface_gtpu::write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu)
	{
		gtpu->write_pdu(rnti, lcid, pdu);
	}

	void lwipep::user_interface_pdcp::write_sdu(uint32_t lcid, srslte::byte_buffer_t *pdu, bool blocking)
	{
		pdcp->write_sdu(rnti, lcid, pdu);
	}

	void lwipep::user_interface_rrc::write_pdu(uint32_t lcid, srslte::byte_buffer_t* pdu)
	{
		rrc->write_pdu(rnti, lcid, pdu);
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

	uint32_t lwipep::user_interface_xw::get_buffer_state(uint32_t lcid)
	{
		rlc->get_buffer_state(rnti, lcid);
	}
	
	void  lwipep::user_interface_xw::tunnel_speed_limit(uint32_t limit)
	{
		return;
	}
	
	void lwipep::user_interface_lwipep_hdl_rpt::handle_args(void *args)
	{
		lwipep_hdl_rpt->handle_args(args);
	}
	
	void lwipep::user_interface_lwipep_hdl_rpt::stop()
	{
		lwipep_hdl_rpt->stop();
	}
	
	void lwipep::user_interface_lwipep_hdl_rpt::update_route(void *args)
	{
		lwipep_hdl_rpt->update_route(args);
	}
	
	
//===================================================================================================//
//============================================SETTING================================================//
//===================================================================================================//
	void lwipep::set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate)
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
		{
			user->second.lwipep->set_lwipep_wlan(dl_aggregate, ul_aggregate);
			lwipep_log->console("set_lwipep_wlan\n");
		}
	}
	
	void lwipep::set_lwipep_auto_control_ratio()
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
			user->second.lwipep->set_lwipep_auto_control_ratio();
	}
	
	void lwipep::set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio)
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
			user->second.lwipep->set_lwipep_tx_ratio(lte_tx_ratio, wlan_ratio);
	}

	void lwipep::set_lwipep_reorder_buffer_size(uint32_t buffer_size)
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
			user->second.lwipep->set_lwipep_reorder_buffer_size(buffer_size);
	}

	void lwipep::set_lwipep_report_period(uint32_t report_period)
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
			user->second.lwipep->set_lwipep_report_period(report_period);
	}

	void lwipep::set_reorder_timeout(uint32_t reorder_timeout)
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
			user->second.lwipep->set_reorder_timeout(reorder_timeout);
	}

	void lwipep::lwipep_state()
	{
		for(std::map<uint32_t,user_interface>::iterator user = users.begin(); user != users.end(); user++)
			user->second.lwipep->lwipep_state();
	}	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	

}
