/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2017 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
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


#include "srslte/upper/lwipep.h"


//like a cicle link list
#define RX_MOD_BASE(x) (x-current_max_sn-WINDOWS_SIZE)%BUFFER_SIZE
namespace srslte {

/****************************************************************************
 * Header pack/unpack helper functions
 * Ref: 3GPP TS 29.281 v10.1.0 Section 5
 ***************************************************************************/


lwipep::lwipep()
{
	pdcp = NULL;
	rrc = NULL;
	gw = NULL;
	lwipep_log = NULL;
	lcid = 0;
	rnti = 0;
	
	submitted_sn = 0;
	current_max_sn = 0;
	next_sn = 0;
	delay_sn = 0;
	reordering_sn = 0;
	
	delayed_cnt = 0;
	duplicate_cnt = 0;

	tx_packet_count = 0;
	rx_packet_count = 0;
	
	lte_half_tx_ratio = 0;
	lte_tx_ratio = 1;					//1
	lte_tx_ratio_count = 0;
	lte_tx_bytes = 0;
	lte_tx_packet_count = 0;
	
	wlan_half_tx_ratio = 0;
	wlan_tx_ratio = 1;					//1
	wlan_tx_ratio_count = 0;
	wlan_tx_bytes = 0;
	wlan_tx_packet_count = 0;
	base_tx_ratio = 1;		//lte_tx_ratio
	
	total_tx_ratio = wlan_tx_ratio + lte_tx_ratio;
	
	lte_ratio_rx = 0;
	lte_rx_bytes = 0;
	lte_rx_packet_count = 0;
	wlan_ratio_rx = 0;
	wlan_rx_bytes = 0;
	wlan_rx_packet_count = 0;
	base_rx_ratio = 0;
	
	start_lte_tx_packet_count = 0;
	start_wlan_tx_packet_count = 0;
	
	reconfigure_sn = 0;
	
	enable_lwipep_dl_aggregate = true;
	enable_lwipep_ul_aggregate = true;
	enable_lwipep_write_pdu_aggregate = true;		//false
	enable_lwipep_write_sdu_aggregate = true;		//false
	enable_audo_control_lwipep_write_pdu_ratio = false;
	enable_audo_control_lwipep_write_sdu_ratio = false;
	
	enable_lwipep_reordering = false;
	lwipep_reordering_time = 100;
	
	enable_auto_control_ratio = false;
	
	lwipep_report_time = 5000;
	enable_lwipep_measurement_report = true;
	
	
	
	lwipep_hdl_rpt_time = 20;						//10 = 10ms
	lwipep_rcv_rpt_time = 1000;						//100 = 100ms, 1000 = 1s
	
	enable_lwipep_discard_delay_packet = false;
	enable_lwipep_discard_duplicate = false;
	
	max_ratio = 8;
//	alpha_part     = 1;
//	alpha_whole    = 2;
	ema_part       = 1;
	ema_whole      = 1;
	delay_lte = 1; //ms
	delay_wlan = 1;
	hdl_rpt_count = 0;
	control_count = 0;

	//half_lte_tx_ratio_count = 1;
	//half_wlan_tx_ratio_count = 1;
	
	last_bytes = 0;
	enable_config = true;
	
	args.get_args = false;
	enable_update_route = false;
	skip_hdl_rpt = false;
	
	switch_config = 0;
	lwipep_config_time = 100;						//1000 = 1s
	
	args.buffer_state = 0;
	args.tv_sec = 0;
	args.tv_nsec = 0;
	args.handle_delay_sn = 0;
	args.rcv_pkt = 0;
	args.rcv_pkt_wlan = 0;
	args.rcv_pkt_lte = 0;
	args.last_handle_current_max_sn = 0;
	args.bif_wlan = 0;
	args.bif_lte = 0;
	args.wlan_tx_bytes = 0;
	args.lte_tx_bytes = 0;
	args.delay_lte = 0; //ms
	args.delay_wlan = 0;
//	args.alpha_part     = alpha_part;
//	args.alpha_whole    = alpha_whole;
	args.ema_part       = ema_part;
	args.ema_whole      = ema_whole;
	args.lwipep_rcv_rpt_time = lwipep_rcv_rpt_time;
	args.count			= 0;
	args.wlan_tx_packet_count = 0;
	args.lte_tx_packet_count = 0;
	
	last_highest_xw_seq_num = 0;
	
	pthread_mutex_init(&mutex, NULL);
}


void lwipep::init(srsue::pdcp_interface_lwipep *pdcp_,
				  srsue::lwipep_hdl_rpt_interface_lwipep *lwipep_hdl_rpt_,
				  srsue::tunnel_interface_lwipep *tunnel_,
				  srsue::rrc_interface_lwipep *rrc_,
				  srslte::mac_interface_timers   *mac_timers_,
				  srsue::gw_interface_lwipep *gw_,
				  log *lwipep_log_,
				  uint32_t lcid_, uint16_t rnti_)
{
	pdcp			= pdcp_;
	lwipep_hdl_rpt	= lwipep_hdl_rpt_;
	tunnel			= tunnel_;
	rrc				= rrc_;
	gw				= gw_;
	lwipep_log		= lwipep_log_;
	lcid			= lcid_;
	rnti			= rnti_;
	
	pool = srslte::byte_buffer_pool::get_instance();
	
	mac_timers					= mac_timers_;
	lwipep_reordering_id		= mac_timers->timer_get_unique_id();
	lwipep_reordering			= mac_timers->timer_get(lwipep_reordering_id);
	lwipep_report_id			= mac_timers->timer_get_unique_id();
	lwipep_report				= mac_timers->timer_get(lwipep_report_id);
	lwipep_config_id			= mac_timers->timer_get_unique_id();
	lwipep_config				= mac_timers->timer_get(lwipep_config_id);
	
	
	clock_gettime(CLOCK_MONOTONIC, &report_time[1]);
	std::srand(time(NULL));
	
	
	if (enable_lwipep_measurement_report == true)
	{
		lwipep_report->set(this, lwipep_report_time);
		lwipep_report->run();
	}
	
//	lwipep_log->console("\nlwipep args addr = %d\n", &args);
//	11888 / 14681320
	lwipep_log->console("\nlwipep lib init rnti = 0x%x\n", rnti);

}

void lwipep::timer_expired(uint32_t timer_id)
{
	if(lwipep_reordering_id == timer_id)
	{
		pthread_mutex_lock(&mutex);
		
		// Status report
		delayed_cnt++;
		lwipep_log->console("Timeout PDU SN: %d,	delayed_cnt = %d\n", next_sn, delayed_cnt);
		
		//log->console("Timeout PDU SN: %d, Window %.1f %% used\n", submitted_sn, ((float)rx_window.size() / REORDERING_WINDOW_SIZE) * 100);
		//expired_cnt++;
		delay_sn = next_sn;
		
		//==================================================================
		while(!inside_reordering_window(next_sn))
		{
			if(rx_window.count(next_sn)) {
				gw->write_pdu(lcid, rx_window[next_sn]);
				rx_window.erase(next_sn);
				rx_packet_count++;
			}
			submitted_sn = next_sn;
			next_sn = (next_sn + 1) % BUFFER_SIZE;
		}
		//===================================================================	
		
		// Now update submitted_sn until we reach an SN we haven't yet received
		while(RX_MOD_BASE(next_sn) < RX_MOD_BASE(reordering_sn))
		{
			rx_route[next_sn] = WAIT;
			// Update submitted_sn until we reach an SN we haven't yet received
			if (rx_window.count(next_sn))
			{
				while (rx_window.count(next_sn)) 
				{
					gw->write_pdu(lcid, rx_window[next_sn]);
					rx_window.erase(next_sn);
					submitted_sn = next_sn;
					next_sn = (next_sn + 1) % BUFFER_SIZE;
					//rx_packet_count++;
					rx_route[next_sn] = ROUTE_LTE; 
				}
			}
			else	next_sn = (next_sn + 1) % BUFFER_SIZE;
		}
		
		lwipep_reordering->stop();
		if(enable_lwipep_reordering && RX_MOD_BASE(current_max_sn) > RX_MOD_BASE(next_sn))
		{
			lwipep_reordering->set(this, lwipep_reordering_time);
			lwipep_reordering->run();
			reordering_sn = current_max_sn;
			//reordering_cnt++;
		}

		pthread_mutex_unlock(&mutex);
	}
	else if(lwipep_report_id == timer_id) 
	{
		measurement_report();
		
		lwipep_report->stop();
		lwipep_report->set(this, lwipep_report_time);
		lwipep_report->run();
	}
	else if(lwipep_config_id == timer_id)
	{
		pthread_mutex_lock(&mutex);
		switch(switch_config)
		{
			case 0:
				switch_config = 20;
		//		tunnel->tunnel_speed_limit(21);
		//		lwipep_log->console("\n===========================close wifi==============================\n");
		//		lwipep_config_time = 10000;
		//		lwipep_config->set(this, lwipep_config_time);
		//		lwipep_config->run();
				break;
			case 1:										//WLAN 10s infinite
			//	set_lwipep_tx_ratio(0, 1);
				tunnel->tunnel_speed_limit(0);
				lwipep_config_time = 10000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 2:										//LTE 15s
			//	set_lwipep_tx_ratio(1, 0);
				tunnel->tunnel_speed_limit(21);
				lwipep_log->console("\n===========================close wifi==============================\n");
				lwipep_config_time = 10000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 3:										//eLWIP 15s wifi limit 1:2
																
//				enable_auto_control_ratio = true;
				tunnel->tunnel_speed_limit(3);
				lwipep_config_time = 15000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 4:										//eLWIP 15s wifi infinite
				tunnel->tunnel_speed_limit(0);
				lwipep_config_time = 15000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 5:										//WLAN 15s infinite
//				enable_auto_control_ratio = false;
//				set_lwipep_tx_ratio(0, 1);
//				tunnel->tunnel_speed_limit(21);
				lwipep_config_time = 15000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 6:										//eLWIP 15s wifi infinite
//				enable_auto_control_ratio = true;
//				set_lwipep_tx_ratio(1, 1);
				lwipep_config_time = 15000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 7:										//eLWIP 15s wifi limit 1:2
				tunnel->tunnel_speed_limit(3);					//wifi limit 1:2
				lwipep_config_time = 15000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			case 8:										//LTE 10s
				enable_auto_control_ratio = false;
				tunnel->tunnel_speed_limit(21);
				lwipep_log->console("\n===========================close wifi==============================\n");
//				set_lwipep_tx_ratio(1, 0);
				lwipep_config_time = 15000;
				lwipep_config->set(this, lwipep_config_time);
				lwipep_config->run();
				break;
			
		}
//		lwipep_log->console("switch_config %d\n", switch_config);
		pthread_mutex_unlock(&mutex);
//		switch_config++;
	}

}
//===================================================================================================================
//===================================================================================================================
void lwipep::measurement_report()
{

}


//===================================================================================================================
//===================================================================================================================
void lwipep::write_pdu(uint32_t lcid, srslte::byte_buffer_t* pdu)
{
	uint32_t			sn;
	uint32_t			get_header_value;
	uint32_t			wlan_more_than_lte;
	uint32_t			bytes = 0;
	uint32_t			handle_sn;
	gre_header_flag_t	gre_header_flag;
	gre_header_t		gre_header;
	ip_header_t			ip_header;
	xw_rpt_pkt			rpt;
	uint32_t			rpt_ptr = 0;
	uint32_t			check_rcv_wlan_pkt;
	uint32_t			check_rcv_lte_pkt;
	uint32_t			lost_cnt = 0;
	uint32_t			lte_lost = 0;
	uint32_t			wlan_lost = 0;
	uint32_t			rcv_range = 0;
	int start_of_lost = 0;
	int end_of_lost = 0;
	bool				lwipep_measurement_report;
	
	
	
	lwipep_read_ip_header(pdu, &ip_header, lwipep_log);
	
	lwipep_read_gre_header(pdu, &gre_header, lwipep_log);
	
	gre_header_flag.checksum				= (gre_header.flag >> 15) & 0xFFFF;								//has_checksum:1
	gre_header_flag.routing					= (gre_header_flag.routing >> 14) & 0xFFFF;						//has_routing:1,
	gre_header_flag.key						= (gre_header_flag.key >> 13) & 0xFFFF;							//has_key:1,
	gre_header_flag.seq						= (gre_header_flag.seq >> 12) & 0xFFFF;							//has_seq:1,
	gre_header_flag.strict_source_route		= (gre_header_flag.strict_source_route >> 11) & 0xFFFF;			//strict_source_route:1,
	gre_header_flag.recursion_control		= (gre_header_flag.recursion_control >> 8) & 0xFFFF;			//recursion_control:3,
	gre_header_flag.flags					= (gre_header_flag.flags >> 3) & 0xFFFF;						//flags:5,
	gre_header_flag.version					= gre_header_flag.version & 0xFFFF;								//version

	sn = gre_header.seq % BUFFER_SIZE;
	
	lwipep_measurement_report = false;
	lwipep_measurement_report = is_lwipep_report(pdu);
		
//	lwipep_log->console("control_count = %d, pdu->N_bytes = %d\n", control_count, pdu->N_bytes);
	bytes = pdu->N_bytes;
//	lwipep_log->console("rcv pkt \n");
	
	if(enable_lwipep_write_pdu_aggregate == true && enable_lwipep_reordering == true && lwipep_measurement_report == false) // 
	{
		pthread_mutex_lock(&mutex);

		if(delay_packet(sn))
		{
//			lwipep_log->console("SN: %d delay rx window [%d:%d] - discarding\n", sn, next_sn, current_max_sn);
			if (enable_lwipep_discard_delay_packet) 
			{	pool->deallocate(pdu);	}
			else 
			{	
				gw->write_pdu(lcid, pdu);
				rx_window.erase(sn);
				//rx_packet_count++;
				rx_route[sn] = ROUTE_LTE;
			}
			pthread_mutex_unlock(&mutex);
			return;
		}
		if (rx_window.count(sn))
		{
//			lwipep_log->console("Discarding duplicate SN: %d\n", sn);
			duplicate_cnt++;
			if (enable_lwipep_discard_duplicate) 
			{
				pool->deallocate(pdu);
				pthread_mutex_unlock(&mutex);
				return;
			} 
			else 
			{
				gw->write_pdu(lcid, rx_window[sn]);
				rx_window.erase(sn);
			}
		}
		
		rx_window[sn] = pdu;
		
		if (over_current_max_sn(sn)) // Update current_max_sn
		{	
			current_max_sn = sn % BUFFER_SIZE;	
			over_current_max_sn_times++;
//			lwipep_log->console("over_current_max_sn\n");
		}
		else if (equal_current_max_sn(sn))
		{	current_max_sn = sn % BUFFER_SIZE;	}
//==================================================================
		while(!inside_reordering_window(next_sn))
		{
			if(rx_window.count(next_sn)) {
				gw->write_pdu(lcid, rx_window[next_sn]);
				rx_window.erase(next_sn);
				rx_packet_count++;
			}
			submitted_sn = next_sn;
			next_sn = (next_sn + 1) % BUFFER_SIZE;
//			lwipep_log->console("!inside_reordering_window\n");
		}
//===================================================================		
		//rcv pkt when i need, pass to gw
		while(rx_window.count(next_sn))
		{
			gw->write_pdu(lcid, rx_window[next_sn]);
			rx_window.erase(next_sn);
			//rx_packet_count++;
			rx_route[next_sn] = ROUTE_LTE;
			submitted_sn = next_sn;
			next_sn = (next_sn + 1) % BUFFER_SIZE;
//			lwipep_log->console("rcv pkt when i need, pass to gw\n");
		}
		//RX_MOD_BASE(x) (X+WINDOW_SIZE)%BUFFER_SIZE
		// Update reordering variables and timers
		
		if (lwipep_reordering->is_running()) 
		{
			if (RX_MOD_BASE(submitted_sn) >= RX_MOD_BASE(reordering_sn) || (!inside_reordering_window(reordering_sn) && (reordering_sn != current_max_sn))) 
			{	lwipep_reordering->stop();	}
		} 
		
		if (!lwipep_reordering->is_running()) 
		{
			if (RX_MOD_BASE(current_max_sn) > RX_MOD_BASE(next_sn)) 
			{
				reordering_sn = current_max_sn;
				lwipep_reordering->set(this, lwipep_reordering_time);
				lwipep_reordering->run();
				rx_route[next_sn] = WAIT;
				//reordering_cnt++;
			}  
		}
		
		pthread_mutex_unlock(&mutex);
	}
	else if(enable_lwipep_write_pdu_aggregate == true && lwipep_measurement_report == true && enable_auto_control_ratio == true && args.get_args == false && enable_update_route == false)
	{
		
		pthread_mutex_lock(&mutex);
			
		if (skip_hdl_rpt == false)
		{
			if (pdu->N_bytes < 8) 
			{
				pool->deallocate(pdu);
				pthread_mutex_unlock(&mutex);
				return;
			}
//			lwipep_log->console("control_count = %d, pdu->N_bytes = %d\n", control_count, pdu->N_bytes);
			rpt.pdu_type = pdu->msg[rpt_ptr] >> 4;		//4bits
			rpt.spare = pdu->msg[rpt_ptr] >> 2 & 0x03;	//2bits
			rpt.final_frame = pdu->msg[rpt_ptr] >> 1 & 0x01;	//1bit
			rpt.lost_pkt_rpt = pdu->msg[rpt_ptr] & 0x01;	//1bit
			rpt_ptr++;
			rpt.highest_xw_seq_num = pdu->msg[rpt_ptr] << 16;
			rpt_ptr++;
			rpt.highest_xw_seq_num |= pdu->msg[rpt_ptr] << 8 ;
			rpt_ptr++;
			rpt.highest_xw_seq_num |= pdu->msg[rpt_ptr];
			rpt_ptr++;
			rpt.desired_buffer = pdu->msg[rpt_ptr] << 24;
			rpt_ptr++;
			rpt.desired_buffer |= pdu->msg[rpt_ptr] << 16;
			rpt_ptr++;
			rpt.desired_buffer |= pdu->msg[rpt_ptr] << 8 ;
			rpt_ptr++;
			rpt.desired_buffer |= pdu->msg[rpt_ptr];
			rpt_ptr++;
			rpt.min_buffer = pdu->msg[rpt_ptr] << 24;
			rpt_ptr++;
			rpt.min_buffer |= pdu->msg[rpt_ptr] << 16;
			rpt_ptr++;
			rpt.min_buffer |= pdu->msg[rpt_ptr] << 8 ;
			rpt_ptr++;
			rpt.min_buffer |= pdu->msg[rpt_ptr];
			rpt_ptr++;
			
			rpt.lost_range = pdu->msg[rpt_ptr];
			rpt_ptr++;
			args.rcv_pkt_wlan = 0;
			args.rcv_pkt_lte = 0;
			lwipep_log->console("sd_pkt_lte = %d,	sd_pkt_wlan = %d, lost_range = %d\n", lte_tx_packet_count, wlan_tx_packet_count, rpt.lost_range);

			for (int i = 0; i < rpt.lost_range ; i++)
			{
				rpt.start_of_lost[i] = pdu->msg[rpt_ptr] << 16;
				//				lwipep_log->console("start_of_lost = %d\n", pdu->msg[rpt_ptr]);
				rpt_ptr++;
				
				rpt.start_of_lost[i] |= pdu->msg[rpt_ptr] << 8;
				//				lwipep_log->console("start_of_lost = %d\n", pdu->msg[rpt_ptr]);
				rpt_ptr++;
				
				rpt.start_of_lost[i] |= pdu->msg[rpt_ptr];
				//				lwipep_log->console("start_of_lost = %d\n", pdu->msg[rpt_ptr]);
				rpt_ptr++;
				
				rpt.end_of_lost[i] = pdu->msg[rpt_ptr] << 16;
				//				lwipep_log->console("end_of_lost = %d\n", pdu->msg[rpt_ptr]);
				rpt_ptr++;
				
				rpt.end_of_lost[i] |= pdu->msg[rpt_ptr] << 8;
				//				lwipep_log->console("end_of_lost = %d\n", pdu->msg[rpt_ptr]);
				rpt_ptr++;
				
				rpt.end_of_lost[i] |= pdu->msg[rpt_ptr];
				//				lwipep_log->console("end_of_lost = %d\n", pdu->msg[rpt_ptr]);
				rpt_ptr++;
				
				if (rpt.end_of_lost[i] % 2 == 0)
				{
					args.rcv_pkt_lte = rpt.end_of_lost[i] / 2;
					args.rcv_pkt_wlan = rpt.end_of_lost[i] / 2;
				}
				else
				{
					args.rcv_pkt_lte = (rpt.end_of_lost[i] - 1) / 2;
					args.rcv_pkt_wlan = (rpt.end_of_lost[i] - 1) / 2;
				}
				
				for(int j = rpt.start_of_lost[i], rcv = rpt.start_of_lost[i]; j < rpt.end_of_lost[i]; j = j + 2)
				{
					if (rcv % 2 == 0) args.rcv_pkt_wlan--;
					else args.rcv_pkt_lte--;
				}
				
			}
			
			lwipep_log->console("args.rcv_pkt_lte = %d,	args.rcv_pkt_wlan = %d\n", args.rcv_pkt_lte, args.rcv_pkt_wlan);
			
			check_rcv_wlan_pkt = pdu->msg[rpt_ptr] << 16;
			rpt_ptr++;
			check_rcv_wlan_pkt |= pdu->msg[rpt_ptr] << 8;
			rpt_ptr++;
			check_rcv_wlan_pkt |= pdu->msg[rpt_ptr];
			rpt_ptr++;
			check_rcv_lte_pkt = pdu->msg[rpt_ptr] << 16;
			rpt_ptr++;
			check_rcv_lte_pkt |= pdu->msg[rpt_ptr] << 8;
			rpt_ptr++;
			check_rcv_lte_pkt |= pdu->msg[rpt_ptr];
			rpt_ptr++;
			lwipep_log->console("ack_pkt_lte = %d,	ack_pkt_wlan = %d, \n", args.rcv_pkt_lte, args.rcv_pkt_wlan);
			
			args.rcv_pkt_lte = check_rcv_lte_pkt;
			args.rcv_pkt_wlan = check_rcv_wlan_pkt;
			
			
			
			clock_gettime(CLOCK_MONOTONIC, &report_time[2]);
			get_timestamp_interval(report_time);
			memcpy(&report_time[1], &report_time[2], sizeof(struct timespec));
			
			args.buffer_state = tunnel->get_buffer_state(lcid);
			
			args.tv_sec = report_time[0].tv_sec;
			args.tv_nsec = report_time[0].tv_nsec;
			
			args.handle_delay_sn = 0;
			
//			
			
			
//			lwipep_log->console("sd_highest_pkt = %d,	rcv_highest_pkt = %d, \n", tx_packet_count, rpt.highest_xw_seq_num);
//			lwipep_log->console("rcv range [%d,	%d]	= %d,	delay = %d \n",last_highest_xw_seq_num, last_highest_xw_seq_num + wlan_tx_packet_count + lte_tx_packet_count, wlan_tx_packet_count + lte_tx_packet_count, tx_packet_count - rpt.highest_xw_seq_num);
			
			
			
			last_highest_xw_seq_num = rpt.highest_xw_seq_num;
			pool->deallocate(pdu);
			
			
			
			args.reconfigure_sn = reconfigure_sn;
			args.tx_packet_count = tx_packet_count;
			
			args.bif_wlan = wlan_tx_packet_count;
			args.bif_lte = lte_tx_packet_count;
			
			args.wlan_tx_bytes = wlan_tx_bytes;
			args.lte_tx_bytes = lte_tx_bytes;
			
			args.lte_tx_ratio = lte_tx_ratio;
			args.wlan_tx_ratio = wlan_tx_ratio;
			
			args.total_tx_ratio = total_tx_ratio;
			
			args.max_ratio = max_ratio;
			lwipep_log->console("max_ratio = %d\n", max_ratio);
			
			args.delay_lte = 0; //ms
			args.delay_wlan = 0;
			
			args.wlan_tx_packet_count = wlan_tx_packet_count;
			args.lte_tx_packet_count = lte_tx_packet_count;
			
			if ( wlan_tx_packet_count > 0)
			{
				wlan_tx_bytes = 0;
				wlan_tx_packet_count = 0;
			}
			if (lte_tx_packet_count > 0)
			{
				lte_tx_bytes = 0;
				lte_tx_packet_count = 0;
			}
			
			args.lwipep_hdl_rpt_time = lwipep_hdl_rpt_time;						//10ms
			args.lwipep_rcv_rpt_time = lwipep_rcv_rpt_time;
			
			enable_update_route = true;
			lwipep_log->console("check_rcv_lte_pkt = %d,	check_rcv_wlan_pkt = %d\n", check_rcv_lte_pkt, check_rcv_wlan_pkt);
			lwipep_hdl_rpt->handle_args(&args);
			
		}
		else	{	skip_hdl_rpt = false;	}
		
		pthread_mutex_unlock(&mutex);
	}
	else 		//else if (!enable_lwipep_reordering)
	{
		
		pthread_mutex_lock(&mutex);
//		if (bytes < 1000)	lwipep_log->console("RCV %d byte pkt\n", bytes);
		if (!inside_reordering_window(sn))	next_sn = (sn + 1) % BUFFER_SIZE;
		gw->write_pdu(lcid, pdu);
		//rx_packet_count++;
		pthread_mutex_unlock(&mutex);
	}
	/*
	else 
	{
		// Handle SRB messages
		if (cfg.is_control) {	lwipep_unpack_control_pdu(pdu, &sn);	}
		// pass to RRC
		rrc->write_pdu(lcid, pdu);
		rx_packet_count++;
	}	
	*/
}


void lwipep::write_sdu(uint32_t lcid, srslte::byte_buffer_t* sdu)
{
	gre_header_flag_t	gre_header_flag;
	gre_header_t		gre_header;
	ip_header_t			ip_header;
	route_t				route;
	uint32_t			random_value = 0;
	uint32_t			bytes = 0;
	
	lwipep_build_gre_header(tx_packet_count, &gre_header, &gre_header_flag);
	
	lwipep_write_gre_header(&gre_header, sdu, lwipep_log);
	
	lwipep_build_ip_header(&ip_header, sdu);
	
	lwipep_write_ip_header(&ip_header, sdu);
	
	route = get_route(sdu->N_bytes);
	
	if(enable_lwipep_write_sdu_aggregate)
	{
		if (route == ROUTE_WLAN)
		{
			
			tunnel->write_sdu(lcid, sdu);
		}
		else if (route == ROUTE_LTE)
		{
//			lwipep_log->console("send pkt by LTE\n");
			
			if (switch_config == 20) pool->deallocate(sdu);
			pdcp->write_sdu(lcid, sdu, false);
		}
	}
	else		
	{	pdcp->write_sdu(lcid, sdu, false);	}
	
	
//	lwipep_log->console("TX LTE %d(%d) wlan %d(%d)\n",  tx_packet_count - wlan_tx_packet_count, lte_tx_bytes, wlan_tx_packet_count, wlan_tx_bytes);
}

//====================================================================================================//
//============================================CHECKING================================================//
//====================================================================================================//

bool lwipep::inside_reordering_window(uint32_t sn)
{
	if( RX_MOD_BASE(sn) <= RX_MOD_BASE(current_max_sn) && RX_MOD_BASE(sn) >= RX_MOD_BASE(next_sn) )
	{	return true;	}
	else
	{	return false;	}
}

bool lwipep::equal_current_max_sn(uint32_t sn)
{
	if( RX_MOD_BASE(sn) == RX_MOD_BASE(current_max_sn))
	{	return true;	}
	else
	{	return false;	}
}

bool lwipep::over_current_max_sn(uint32_t sn)
{
	if( RX_MOD_BASE(sn) > RX_MOD_BASE(current_max_sn) && (RX_MOD_BASE(sn) - RX_MOD_BASE(submitted_sn) <= WINDOWS_SIZE) )
	{	return true;	}
	else
	{	return false;	}
}

bool lwipep::delay_packet(uint32_t sn)
{
	if( RX_MOD_BASE(sn) < RX_MOD_BASE(submitted_sn) || (RX_MOD_BASE(sn) - RX_MOD_BASE(submitted_sn) > WINDOWS_SIZE) )
	{	
		if (rx_route[sn] != WAIT)	return false;
		else return true;	
	}
	else
	{	return false;	}
}

route_t lwipep::get_route(uint32_t bytes)
{

	if (enable_config == true && bytes > 1000)
	{
		lwipep_config->set(this, lwipep_config_time);
		lwipep_config->run();
//		enable_auto_control_ratio = true;
		enable_config = false;
	}

	pthread_mutex_lock(&mutex);
	if (enable_auto_control_ratio == true)
	{
		
		lwipep_hdl_rpt->update_route(&args);

		if (args.get_args == true && enable_update_route == true)
		{
			
			if (enable_ema == true)
			{
				
				ema_whole = args.ema_whole;
				ema_part = args.ema_part;
				delay_lte = args.delay_lte;
				delay_wlan = args.delay_wlan;
				
				if (ema_whole > max_ratio * ema_part) 
				{	set_lwipep_tx_ratio(0, 1);	} 
				else if (ema_part > max_ratio * ema_whole) 
				{	set_lwipep_tx_ratio(1, 0);	} 
				else  {	set_lwipep_tx_ratio(ema_part, ema_whole);	}		//else if (ema_whole >= ema_part)
				//lwipep_log->console("unsigned int ema_part = %d ema_whole = %d\n", ema_part, ema_whole);
				
			} 
			else
			{
			//	lwipep_log->console("delay_lte = %d delay_wlan = %d\n", delay_lte, delay_wlan);
				set_lwipep_tx_ratio((uint32_t)(delay_wlan * 1000), (uint32_t)(delay_lte * 1000));
			}

			//lwipep_log->console("delay_lte = %.3f \ndelay_wlan = %.3f\n", delay_lte, delay_wlan);
			hdl_rpt_count++;
			args.get_args = false;
			enable_update_route = false;
//			skip_hdl_rpt = false;
			lwipep_log->console("HANDLE REPORT %d TIMES\n", hdl_rpt_count);
			
		}
		
	}
	
	while(lte_tx_ratio_count <= base_tx_ratio && wlan_tx_ratio_count <= base_tx_ratio)
	{
		lte_tx_ratio_count  += lte_tx_ratio;
		wlan_tx_ratio_count += wlan_tx_ratio;
	}


	if (lte_tx_ratio_count >= base_tx_ratio) 
	{
		lte_tx_ratio_count -= base_tx_ratio;
		tx_route[tx_packet_count % BUFFER_SIZE] = ROUTE_LTE;
//		lwipep_log->console("ROUTE_LTE %d\n", tx_packet_count % 4096);
		lte_tx_packet_count++;
		lte_tx_bytes += bytes;
		tx_packet_count++;
		pthread_mutex_unlock(&mutex);
//		lwipep_log->console("SEND LTE\n");
		return ROUTE_LTE;
	} 
	else if (wlan_tx_ratio_count >= base_tx_ratio) 
	{
		wlan_tx_ratio_count -= base_tx_ratio;
		tx_route[tx_packet_count % BUFFER_SIZE] = ROUTE_WLAN;
//		lwipep_log->console("ROUTE_WLAN %d\n", tx_packet_count % 4096);
		wlan_tx_packet_count++;
		wlan_tx_bytes += bytes;
		tx_packet_count++;
		pthread_mutex_unlock(&mutex);
//		lwipep_log->console("SEND WLAN\n");
		return ROUTE_WLAN;
	} 

	lte_tx_ratio_count -= base_tx_ratio;
	tx_route[tx_packet_count % BUFFER_SIZE] = ROUTE_LTE;
	lte_tx_packet_count++;
	lte_tx_bytes += bytes;
	tx_packet_count++;
	pthread_mutex_unlock(&mutex);
	return ROUTE_LTE;

}

//====================================================================================================//
//=============================================HEADER=================================================//
//====================================================================================================//

void lwipep::lwipep_write_gre_header(gre_header_t *gre_header, srslte::byte_buffer_t *sdu, srslte::log *lwipep_log)
{

	sdu->msg      -= GRE_HEADER_LEN; 
	sdu->N_bytes  += GRE_HEADER_LEN;
	uint8_t *ptr = sdu->msg;
/*
	uint16_to_uint8(gre_header->flag, ptr);
	ptr += 2;
	uint16_to_uint8(gre_header->proto, ptr);
	ptr += 2;
	uint16_to_uint8(gre_header->checksum, ptr);
	ptr += 2;
	uint16_to_uint8(gre_header->offset, ptr);
	ptr += 2;
	uint32_to_uint8(gre_header->key, ptr);
	ptr += 4;
	uint32_to_uint8(gre_header->seq, ptr);
*/
	
	uint16_to_uint8(gre_header->flag, ptr);
	ptr += 2;
	uint16_to_uint8(gre_header->proto, ptr);
	ptr += 2;
	uint32_to_uint8(gre_header->key, ptr);
	ptr += 4;
	uint32_to_uint8(gre_header->seq, ptr);
	
/*
	ptr += 4;
	uint32_to_uint8(gre_header->routing, ptr);
*/
	return;
}

void lwipep::lwipep_write_ip_header(ip_header_t *ip_header, srslte::byte_buffer_t *sdu)
{
	
	sdu->msg      -= sizeof(ip_header_t);
	sdu->N_bytes  += sizeof(ip_header_t);

	uint8_t *ptr = sdu->msg;

	*ptr        = ip_header->version_ihl;
	ptr	+= 1;
	*ptr		= ip_header->tos;
	ptr += 1;
	uint16_to_uint8(ip_header->tot_len, ptr);
	ptr += 2;
	uint16_to_uint8(ip_header->id, ptr);
	ptr += 2;
	uint16_to_uint8(ip_header->frag_off, ptr);
	ptr += 2;
	*ptr        = ip_header->ttl;
	ptr	+= 1;
	*ptr		= ip_header->protocol;
	ptr += 1;
	uint16_to_uint8(ip_header->check, ptr);
	ptr += 2;
	uint32_to_uint8(ip_header->saddr, ptr);
	ptr += 4;
	uint32_to_uint8(ip_header->daddr, ptr);
	ptr += 4;

	return;
}

bool lwipep::lwipep_read_gre_header(srslte::byte_buffer_t *pdu, gre_header_t *gre_header, srslte::log *lwipep_log)
{
  uint8_t *ptr  = pdu->msg;

  pdu->msg      += GRE_HEADER_LEN;
  pdu->N_bytes  -= GRE_HEADER_LEN;

  /*
  uint8_to_uint16(ptr, &gre_header->flag);
  ptr += 2;
  uint8_to_uint16(ptr, &gre_header->proto);
  ptr += 2;
  uint8_to_uint16(ptr, &gre_header->checksum);
  ptr += 2;
  uint8_to_uint16(ptr, &gre_header->offset);
  ptr += 2;
  uint8_to_uint32(ptr, &gre_header->key);
  ptr += 4;
  uint8_to_uint32(ptr, &gre_header->seq);
  */
  uint8_to_uint16(ptr, &gre_header->flag);
  ptr += 2;
  uint8_to_uint16(ptr, &gre_header->proto);
  ptr += 2;
  uint8_to_uint32(ptr, &gre_header->key);
  ptr += 4;
  uint8_to_uint32(ptr, &gre_header->seq);
  
  /*
  ptr += 4;
  uint8_to_uint32(ptr, &gre_header->routing);
  */
/*
  if(gre_header->seq != key) {
    lwipep_log->error("lwipep_read_header - Unhandled header flags: 0x%x\n", gre_header->flags);
    return false;
  }
*/
  return true;
}

bool lwipep::lwipep_read_ip_header(srslte::byte_buffer_t *pdu, ip_header_t *ip_header, srslte::log *lwipep_log)
{
	uint32_t teidin;
	uint8_t *ptr = pdu->msg + 12;

	pdu->msg      += IP_HEADER_LEN;
	pdu->N_bytes  -= IP_HEADER_LEN;

//	lwipep_log->console("lwipep \nSRC IP %d.%d.%d.%d\nDST IP %d.%d.%d.%d\n",ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7]);
	
	return true;
}

bool lwipep::is_lwipep_report(byte_buffer_t *pdu)
{	
	
	uint8_t pdu_type = pdu->msg[0] >> 4; 
	if (pdu_type == 1)	return true;
	else	return false;
	
	return false;
}

void lwipep::lwipep_build_gre_header(uint32_t sn, gre_header_t *gre_header, gre_header_flag_t *gre_header_flag)
{
	uint32_t teidin;
	rntilcid_to_teidin(lcid, &teidin);
	
	gre_header_flag->checksum					= 0;			//has_checksum:1
	gre_header_flag->routing					= 0;			//has_routing:1,
	gre_header_flag->key						= 0;			//has_key:1,
	gre_header_flag->seq						= 0;			//has_seq:1,
	gre_header_flag->strict_source_route		= 0;			//strict_source_route:1,
	gre_header_flag->recursion_control			= 0;			//recursion_control:3,
	gre_header_flag->flags						= 0;			//flags:5,
	gre_header_flag->version					= 0;			//version

	gre_header_flag->checksum					= 0<<15;		//has_checksum:1
	gre_header_flag->routing					= 0<<14;		//has_routing:1,
	gre_header_flag->key						= 1<<13;		//has_key:1,
	gre_header_flag->seq						= 1<<12;		//has_seq:1,
	gre_header_flag->strict_source_route		= 0<<11;		//strict_source_route:1,
	gre_header_flag->recursion_control			= 0<<8;			//recursion_control:3,
	gre_header_flag->flags						= 0<<3;			//flags:5,
	gre_header_flag->version					= 0;			//version:3

	gre_header->flag							= 0;
	gre_header->flag							= gre_header->flag | gre_header_flag->checksum | gre_header_flag->routing | gre_header_flag->key | gre_header_flag->seq |  gre_header_flag->strict_source_route | gre_header_flag->recursion_control | gre_header_flag->flags | gre_header_flag->version; 
	gre_header->proto							= 0x0800;
	gre_header->checksum						= 0;
	gre_header->offset							= 0;
	gre_header->key								= teidin;			//nti_bearers[rnti].teids_out[lcid];
	gre_header->seq								= sn;	// __be32 == unsigned int
	/*
	gre_header->routing							= 0;
	*/
	return;

}

void lwipep::lwipep_build_ip_header(ip_header_t *ip_header, srslte::byte_buffer_t *sdu)
{
	uint8_t version = 4;
	uint8_t ihl = 5 ;			// 20 -> 10100 shift-> 1001
	ip_header->version_ihl = (version << 4) | ihl ;
	ip_header->tos = 0;
	ip_header->tot_len = sdu->N_bytes + IP_HEADER_LEN;		//__constant_htons((sdu->N_bytes + sizeof(ip_header_t)));
	ip_header->id = htons(0);
	ip_header->frag_off = 0;
	ip_header->ttl = 0x40;
	ip_header->protocol = IP_TYPE_GRE;
//		ip_header->saddr = in_aton("10.41.0.3");				//fixed_ip_header->saddr = ip_chain_node->ifa_local;
//		ip_header->daddr = in_aton("10.42.0.1");
	ip_header->saddr = saddr;				//192.168.0.100
	ip_header->daddr = daddr;				//192.168.0.101

	
	ip_header->check = 0;
	ip_header->check = in_cksum((unsigned short *)ip_header, IP_HEADER_LEN);
//		ip_header->check = ip_fast_csum((unsigned char*)ip_header, sizeof(ip_header_t));
}

void lwipep::rntilcid_to_teidin(uint16_t lcid, uint32_t *teidin)
{
	*teidin = (rnti << 16) | lcid;
}

void lwipep::teidin_to_rntilcid(uint32_t teidin, uint16_t *rnti, uint16_t *lcid)
{
	*lcid = teidin & 0xFFFF;
	*rnti = (teidin >> 16) & 0xFFFF;
}

//===================================================================================================//
//============================================SETTING================================================//
//===================================================================================================//

void lwipep::stop()
{
	tunnel->stop();	
	

}

void lwipep::reset_handle_report()
{
	/*
	turn_on_config = false;
	args.get_args = false;
	enable_update_route = false;
	enable_auto_control_ratio = false;
	*/
}

void lwipep::set_rnti(uint16_t rnti_)
{	rnti = rnti_;	}

void lwipep::set_addr(uint32_t saddr_, uint32_t daddr_)
{
	saddr = saddr_;
	daddr = daddr_;
}

void lwipep::set_lwipep_tx_ratio(uint32_t set_lte_tx_ratio, uint32_t set_wlan_ratio)
{
	lwipep_log->console("Now change tx ratio\n");
	if (set_lte_tx_ratio == 0 && set_wlan_ratio == 0)	return;
	
	if (enable_lwipep_write_sdu_aggregate) 
	{
		if (lte_tx_ratio == set_lte_tx_ratio && wlan_tx_ratio == set_wlan_ratio)	return;
		
		lte_tx_ratio   = set_lte_tx_ratio;
		wlan_tx_ratio  = set_wlan_ratio;
		
		// Update base ratio
		if (lte_tx_ratio == 0)	base_tx_ratio = wlan_tx_ratio;
		else if (wlan_tx_ratio == 0)	base_tx_ratio = lte_tx_ratio;
		else 
		{
			base_tx_ratio = get_gcd(lte_tx_ratio, wlan_tx_ratio);
			if (base_tx_ratio > 1) {
				lte_tx_ratio  /= base_tx_ratio;
				wlan_tx_ratio /= base_tx_ratio;
			}
			base_tx_ratio = (lte_tx_ratio < wlan_tx_ratio ? lte_tx_ratio : wlan_tx_ratio);
		}
		
		//total_ratio = lte_tx_ratio + wlan_ratio;
		
		// Reset ratio count
		lte_tx_ratio_count  = 0;
		wlan_tx_ratio_count = 0;
		reconfigure_sn   = tx_packet_count;
		
		//log->console("Now change ratio to %d:%d\n", lr, wr);
	}
	total_tx_ratio = lte_tx_ratio + wlan_tx_ratio;
	control_count++;
	lwipep_log->console("Now change tx ratio to %d:%d\n", lte_tx_ratio, wlan_tx_ratio);
}

void lwipep::set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate)
{
	enable_lwipep_write_pdu_aggregate = dl_aggregate;		//false means downlink using lte only
	enable_lwipep_write_sdu_aggregate = ul_aggregate;		//false means uplink using lte only
	lwipep_log->console("Download aggreate? ");
	if (enable_lwipep_write_pdu_aggregate)	lwipep_log->console("Yes\n");
	else lwipep_log->console("No\n");
	lwipep_log->console("Upload aggreate? ");
	if (enable_lwipep_write_sdu_aggregate)	lwipep_log->console("Yes\n");
	else lwipep_log->console("No\n");
}

void lwipep::set_lwipep_auto_control_ratio()
{
	lwipep_log->console("Auto control TX ratio? ");
	if (enable_auto_control_ratio == false)	
	{	
		enable_auto_control_ratio = true;
		lwipep_log->console("Yes\n");
		
	}
	else 
	{	
		args.get_args = false;
		enable_update_route = false;
		enable_auto_control_ratio = false;
		lwipep_log->console("No\n");
	}
}

void lwipep::set_lwipep_reorder_buffer_size(uint32_t buffer_size)
{
	
}

void lwipep::set_lwipep_report_period(uint32_t period)
{
	bool match = false;
	for (int i = 10; i <= 100; i = i + 10)
	{	if (period == i)	match = true;	}
	if(period == 5 || period == 150 || period == 200 || period == 300 || period == 500 || period == 1000)
	{	match = true;	}
	
	if (match == true)
	{	
		lwipep_report_time = period;
		lwipep_log->console("lwipep report time %d ms", lwipep_report_time);
	}
	else	{	lwipep_log->console("set_lwipep_report_period: value not match spec");	}
	
	return;
}

void lwipep::set_reorder_timeout(uint32_t reorder_timeout)
{
	bool match = false;
	
	for (int i = 0; i <= 300; i = i + 20)
	{	if (reorder_timeout == i)	match = true;	}
	if(reorder_timeout == 750 || reorder_timeout == 500)
	{	match = true;	}
	
	if (match == true)
	{	
		lwipep_reordering_time = reorder_timeout;
		lwipep_log->console("lwipep reordering time %d ms", reorder_timeout);
	}
	else	{	lwipep_log->console("lwipep reordering timeout: value not match spec");	}
	return;
}

void lwipep::lwipep_state()
{						 
	lwipep_log->console("\n===================================STATE===================================\n");
	lwipep_log->console("| Download aggreate?                                                      |\n");
	if (enable_audo_control_lwipep_write_pdu_ratio)	lwipep_log->console("| Yes                                                                      |\n");
	else lwipep_log->console("| No                                                                      |\n");
	lwipep_log->console("| Upload aggreate?                                                        |\n");
	if (enable_audo_control_lwipep_write_sdu_ratio)	lwipep_log->console("| Yes                                                                      |\n");
	else lwipep_log->console("| No                                                                      |\n");
	//======================================================================
	lwipep_log->console("| Auto control Download ratio?                                            |\n");
	if (enable_audo_control_lwipep_write_pdu_ratio)	lwipep_log->console("Yes                                                                      |\n");
	else lwipep_log->console("| No                                                                      |\n");
	lwipep_log->console("| Auto control Upload ratio?                                              |\n");
	if (enable_audo_control_lwipep_write_sdu_ratio)	lwipep_log->console("| Yes                                                                      |\n");
	else lwipep_log->console("| No                                                                      |\n");
	//======================================================================
	lwipep_log->console("| Now tx ratio lte : %d wlan : %d                                           |\n", lte_tx_ratio, wlan_tx_ratio);
	//======================================================================
	lwipep_log->console("| lwipep report time %d ms                                           |\n", lwipep_report_time);
	lwipep_log->console("| lwipep reordering time %d ms                                       |\n", lwipep_reordering_time);
	lwipep_log->console("| lwipep MAX RATIO %d ms                          		     |\n", max_ratio);
	lwipep_log->console("===========================================================================\n");
	
	return;
}

uint32_t lwipep::get_gcd(uint32_t x, uint32_t y)
{
	uint32_t tmp;
	while (y != 0) {
		tmp = x % y;
		x = y;
		y = tmp;
	}
	return x;
}

uint16_t lwipep::in_cksum(uint16_t *addr, int len)
{
	int nleft = len;
	uint32_t sum = 0;
	uint16_t *w = addr;
	uint16_t answer = 0;

	// Adding 16 bits sequentially in sum
	while (nleft > 1) 
	{
		sum += *w;
		nleft -= 2;
		w++;
	}

	// If an odd byte is left
	if (nleft == 1) {
		*(unsigned char *) (&answer) = *(unsigned char *) w;
		sum += answer;
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;

	return answer;
}

void lwipep::get_time_interval(struct timeval * tdata) 
{

	tdata[0].tv_sec = tdata[2].tv_sec - tdata[1].tv_sec;
	tdata[0].tv_usec = tdata[2].tv_usec - tdata[1].tv_usec;
	if (tdata[0].tv_usec < 0) {
		tdata[0].tv_sec--;
		tdata[0].tv_usec += 1000000;
	}
}

void lwipep::get_timestamp_interval(struct timespec * tdata) 
{

	tdata[0].tv_sec = tdata[2].tv_sec - tdata[1].tv_sec;
	tdata[0].tv_nsec = tdata[2].tv_nsec - tdata[1].tv_nsec;
	if (tdata[0].tv_nsec < 0) {
		tdata[0].tv_sec--;
		tdata[0].tv_nsec += 1000000000;
	}
}

void lwipep::lost_pkt(control_radio_args *args, xw_rpt_pkt *rpt, uint32_t rpt_ptr, srslte::byte_buffer_t *pdu)
{
	for (int i = last_highest_xw_seq_num; i < rpt->highest_xw_seq_num ; i++)
	{
		if (tx_route[i] == ROUTE_LTE)
		{
			args->rcv_pkt_lte++;
		}
		else if (tx_route[i] == ROUTE_WLAN)
		{
			args->rcv_pkt_wlan++;
		}
	}
	
	
	   for (int i = 0, start_of_lost = 0, end_of_lost = 0; i < rpt->lost_range ; i++)
	   {
		rpt->start_of_lost[i] = pdu->msg[rpt_ptr] << 16;
	//				lwipep_log->console("start_of_lost = %d\n", pdu->msg[rpt_ptr]);
		rpt_ptr++;
		 
		rpt->start_of_lost[i] |= pdu->msg[rpt_ptr] << 8;
	//				lwipep_log->console("start_of_lost = %d\n", pdu->msg[rpt_ptr]);
		rpt_ptr++;
		 
		rpt->start_of_lost[i] |= pdu->msg[rpt_ptr];
	//				lwipep_log->console("start_of_lost = %d\n", pdu->msg[rpt_ptr]);
		rpt_ptr++;
		 
		rpt->end_of_lost[i] = pdu->msg[rpt_ptr] << 16;
	//				lwipep_log->console("end_of_lost = %d\n", pdu->msg[rpt_ptr]);
		rpt_ptr++;
		 
		rpt->end_of_lost[i] |= pdu->msg[rpt_ptr] << 8;
	//				lwipep_log->console("end_of_lost = %d\n", pdu->msg[rpt_ptr]);
		rpt_ptr++;
		 
		rpt->end_of_lost[i] |= pdu->msg[rpt_ptr];
	//				lwipep_log->console("end_of_lost = %d\n", pdu->msg[rpt_ptr]);
		rpt_ptr++;
		start_of_lost = rpt->start_of_lost[i];
		end_of_lost = rpt->end_of_lost[i];
//		lte_lost = 0;
//		wlan_lost = 0;
	//				lwipep_log->console("start_of_lost = %d,	end_of_lost = %d\n", rpt.start_of_lost[i], rpt.end_of_lost[i]);
		while ( (start_of_lost-end_of_lost-WINDOWS_SIZE)%BUFFER_SIZE <= (end_of_lost-end_of_lost-WINDOWS_SIZE)%BUFFER_SIZE )
		{
		 if (tx_route[start_of_lost] == ROUTE_LTE && args->rcv_pkt_lte > 1)
		 {
		  args->rcv_pkt_lte--;
//		  lte_lost++;
		 }
		 else if (tx_route[start_of_lost] == ROUTE_WLAN && args->rcv_pkt_wlan > 1)
		 {
		  args->rcv_pkt_wlan--;
//		  wlan_lost++;
		 }
//		 lost_cnt++;
		 start_of_lost = (start_of_lost + 1) % BUFFER_SIZE;
		}
	//				lwipep_log->console("start_of_lost = %d,	end_of_lost = %d,	lte_lost = %d\n", rpt.start_of_lost[i], rpt.end_of_lost[i], lte_lost);
	   }
}





































} // namespace srslte
