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


#include "srsue/hdr/upper/ue_lwipep_handle_report.h"

namespace srsue 
{

	lwipep_hdl_rpt::lwipep_hdl_rpt()
	{
		handle = true;
		get_args = false;

		buffer_state = 1;
		tv_sec = 1;
		tv_nsec = 1;
		handle_delay_sn = 1;
		rcv_pkt = 1;
		rcv_pkt_wlan = 1;
		last_handle_current_max_sn = 1;
		nmp = 1;
		lost_lte = 1;
		handle_sn = 1;
		bif_wlan = 1;
		bif_lte = 1;
		wlan_tx_bytes = 1;
		lte_tx_bytes = 1;
		lte_tx_ratio = 1;
		wlan_tx_ratio = 1;
		total_tx_ratio = lte_tx_ratio + wlan_tx_ratio;
		delay_lte = 1; //ms
		delay_wlan = 1;
		alpha_part		= 1;
		alpha_whole		= 2;
		ema_part		= 1;
		ema_whole		= 1;
		sd_pkt_per_ms	= 0;
		lte_down = false;
		wlan_down = false;
		
		max_rcv_wlan = 0;
		max_rcv_lte = 0;
		max_lte_ratio = 0;
		max_wlan_ratio = 0;
		max_cnt = 0;
		
		handle_count			= 0;
		wlan_tx_packet_count = 1;
		lte_tx_packet_count = 1;
		
		delay_lte = 1; //ms
		delay_wlan = 1;
		max_ratio = 2.0;
		// Bits-in-flight
		rate_wlan = 1;
		rate_lte = 1;
		uplink_latency = 1;
		tx_rate_wlan = 1;
		ack_wlan = 1;
		ack_lte = 1;
		pdu_avg_size = 1500;
		pdu_size = 1;
		
		lwipep_hdl_rpt_time = 100;
		lwipep_rcv_rpt_time = 5000;					//5000 = 5s, 100 = 100ms
		
		
//		pthread_mutex_init(&mutex, NULL);
		enable_lwipep_handle_report = true;
		lwipep_handle_report_running = true;
		
		
		
	}
	void lwipep_hdl_rpt::init(srslte::mac_interface_timers *mac_timers_, srslte::log *lwipep_hdl_rpt_log_)
	{
		lwipep_hdl_rpt_log	= lwipep_hdl_rpt_log_;

		mac_timers			= mac_timers_;
		lwipep_hdl_rpt_time	= 100;
		lwipep_hdl_rpt_id	= mac_timers->timer_get_unique_id();
		lwipep_hdl_rpt_config		= mac_timers->timer_get(lwipep_hdl_rpt_id);
//		lwipep_config->set(this, lwipep_hdl_rpt_time);
//		lwipep_config->run();
		
		for (int i = 0; i < 9; i++)
		{	for (int j = 0; j < 9; j++)	ratio_record[i][j] = 0;	}
		
		pthread_mutex_init(&mutex, NULL);
		start(LWIPEP_HDL_RPT_THREAD_PRIO);
	}
	
	void lwipep_hdl_rpt::handle_args(void *args)
	{
		pthread_mutex_lock(&mutex);
		control_radio_args *hdl_args = (control_radio_args *) args;
		
		buffer_state = hdl_args->buffer_state;
		
		tv_sec = hdl_args->tv_sec;
		tv_nsec = hdl_args->tv_nsec;
		
		handle_delay_sn = hdl_args->handle_delay_sn;
		
		reconfigure_sn = hdl_args->reconfigure_sn;
		tx_packet_count = hdl_args->tx_packet_count;
		
		wlan_tx_packet_count = hdl_args->wlan_tx_packet_count;
		lte_tx_packet_count = hdl_args->lte_tx_packet_count;
		
		bif_wlan = wlan_tx_packet_count;
		bif_lte = lte_tx_packet_count;
		
		wlan_tx_bytes = hdl_args->wlan_tx_bytes;
		lte_tx_bytes = hdl_args->lte_tx_bytes;
		
		lte_tx_ratio = hdl_args->lte_tx_ratio;
		wlan_tx_ratio = hdl_args->wlan_tx_ratio;
		total_tx_ratio = hdl_args->total_tx_ratio;
		
		max_ratio = hdl_args->max_ratio;
		
		delay_lte = 0; //ms
		delay_wlan = 0;
		
		wlan_tx_packet_count = hdl_args->wlan_tx_packet_count;
		lte_tx_packet_count = hdl_args->lte_tx_packet_count;
		
		rcv_pkt_wlan = hdl_args->rcv_pkt_wlan ;
		rcv_pkt_lte = hdl_args->rcv_pkt_lte ;
		
		rcv_pkt = rcv_pkt_wlan + rcv_pkt_lte; 

		lwipep_hdl_rpt_time = hdl_args->lwipep_hdl_rpt_time;
		lwipep_rcv_rpt_time = hdl_args->lwipep_rcv_rpt_time;
		
//		rcv_pkt_wlan = hdl_args->check_rcv_wlan_pkt ;
//		rcv_pkt_lte = hdl_args->check_rcv_lte_pkt ;
		
		rcv_pkt_wlan = hdl_args->rcv_pkt_wlan ;
		rcv_pkt_lte = hdl_args->rcv_pkt_lte ;
		
//		hdl_args->get_args = true;
		handle = false;
		get_args = true;
		pthread_mutex_unlock(&mutex);
		lwipep_hdl_rpt_config->set(this, 20);
		lwipep_hdl_rpt_config->run();
		
		lwipep_hdl_rpt_log->console("rcv_pkt_wlan = %d, rcv_pkt_lte = %d\n", rcv_pkt_wlan, rcv_pkt_lte);	
		lwipep_hdl_rpt_log->console("\nlte_tx_bytes =%d \nlte_tx_packet_count = %d\n", lte_tx_bytes, lte_tx_packet_count);
/*
		lwipep_hdl_rpt_log->console("wlan_tx_bytes =%d \wlan_tx_packet_count = %d\n", wlan_tx_bytes, wlan_tx_packet_count);
		lwipep_hdl_rpt_log->console("wlan_tx_packet_count =%d \ntv_sec = %d\ntv_nsec = %d\n", wlan_tx_packet_count, tv_sec, tv_nsec);
		lwipep_hdl_rpt_log->console("nmp =%d \nlost_lte = %d\n", nmp, lost_lte);
		lwipep_hdl_rpt_log->console("handle_current_max_sn =%d \nlast_handle_current_max_sn = %d\nhandle_over_current_max_sn_times = %d\n", handle_current_max_sn, last_handle_current_max_sn, handle_over_current_max_sn_times);
		lwipep_hdl_rpt_log->console("last_handle_current_max_sn = %d\nlte_tx_ratio =%d \ntotal_tx_ratio = %d\n", handle_over_current_max_sn_times, lte_tx_ratio, total_tx_ratio);
*/
	
		return;
	}
	
	void lwipep_hdl_rpt::stop()
	{
		if(enable_lwipep_handle_report)
		{
			enable_lwipep_handle_report = false;

			// Wait thread to exit gracefully otherwise might leave a mutex locked
			int cnt=0;
			while(lwipep_handle_report_running == true && cnt<100) 
			{
				usleep(10000);
				cnt++;
			}
			if (lwipep_handle_report_running) {
				thread_cancel();
			}
			wait_thread_finish();
		}
	}
	
	void lwipep_hdl_rpt::update_route(void *args)
	{
		pthread_mutex_lock(&mutex);
		control_radio_args *hdl_args = (control_radio_args *) args;
		if (handle == true && get_args == true)
		{
			lwipep_hdl_rpt_log->console("get delay_lte =%d \ndelay_wlan = %d\n", delay_lte, delay_wlan);
			lwipep_hdl_rpt_log->console("get ema_part =%d \nema_whole = %d\n", ema_part, ema_whole);
			
			hdl_args->wlan_tx_packet_count = wlan_tx_packet_count;
			hdl_args->lte_tx_packet_count = lte_tx_packet_count;
			
			hdl_args->delay_lte = delay_lte;
			hdl_args->delay_wlan = delay_wlan;
			
			hdl_args->ema_part = ema_part;
			hdl_args->ema_whole = ema_whole;

			hdl_args->get_args = true;
			get_args = false;
		}
		pthread_mutex_unlock(&mutex);
		return;
	}

	void lwipep_hdl_rpt::timer_expired(uint32_t timer_id)
	{
		if(lwipep_hdl_rpt_id == timer_id)
		{
			uint32_t            int_delay_lte;
			uint32_t            int_delay_wlan;
			pthread_mutex_lock(&mutex);
			if (handle == false && get_args == true)
			{
				uint32_t	reg_ema_part = ema_part;
				uint32_t	reg_ema_whole = ema_whole;
				delay_lte = 0; //ms
				delay_wlan = 0;
				rate_wlan = 0;
				rate_lte = 0;
				uplink_latency = 0;
				tx_rate_wlan = 0;
				ack_wlan = 0;
				ack_lte = 0;
				pdu_avg_size = 1500;
				ema = 0;
				gcd = 1;
				
				ratio_record[ema_part][ema_whole] = rcv_pkt_wlan + rcv_pkt_lte;
				
/*
				lwipep_hdl_rpt_log->console("\nlte_tx_bytes =%d \nlte_tx_packet_count = %d\n", lte_tx_bytes, lte_tx_packet_count);
				lwipep_hdl_rpt_log->console("wlan_tx_bytes =%d \wlan_tx_packet_count = %d\n", wlan_tx_bytes, wlan_tx_packet_count);
				lwipep_hdl_rpt_log->console("wlan_tx_packet_count =%d \ntv_sec = %d\ntv_nsec = %d\n", wlan_tx_packet_count, tv_sec, tv_nsec);
				lwipep_hdl_rpt_log->console("nmp =%d \nlost_lte = %d\n", nmp, lost_lte);
				lwipep_hdl_rpt_log->console("handle_current_max_sn =%d \nlast_handle_current_max_sn = %d\nhandle_over_current_max_sn_times = %d\n", handle_current_max_sn, last_handle_current_max_sn, handle_over_current_max_sn_times);
				lwipep_hdl_rpt_log->console("last_handle_current_max_sn = %d\nlte_tx_ratio =%d \ntotal_tx_ratio = %d\n", handle_over_current_max_sn_times, lte_tx_ratio, total_tx_ratio);
*/				
				
				if (tx_packet_count >= reconfigure_sn) 
				{
					
					get_pkt_per_ms();
					// Bits-in-flight
					
					if (wlan_tx_packet_count > 0) 
					{
//						lwipep_hdl_rpt_log->console(" - lte_tx_ratio =%10d, wlan_tx_ratio =%10d, total_tx_ratio = %10d\n", lte_tx_ratio, wlan_tx_ratio, total_tx_ratio);
//						lwipep_hdl_rpt_log->console(" - rcv_pkt =%d\n", rcv_pkt);
//						lwipep_hdl_rpt_log->console(" - NMP =%10d, LOST_LTE =%d\n", nmp, lost_lte);
						//byte to bit, so *8
						pdu_avg_size = wlan_tx_bytes / wlan_tx_packet_count * 8;
						tx_rate_wlan = (float) (wlan_tx_packet_count / (tv_sec + (float) tv_nsec / 1000000000));
						wlan_tx_packet_count = 0;
						wlan_tx_bytes = 0;
						// nmp is all lost packet
				//		if (nmp > lost_lte) {	lost_wlan = (nmp - lost_lte);	}
						if (rcv_pkt > 0) 
						{
							
//							lwipep_hdl_rpt_log->console("=========================== WLAN  ===========================\n");	
//							lwipep_hdl_rpt_log->console(" - Wlan  Bif %5d, Ack %5d, lost_wlan =%5d, Rate %5.2fmbps\n", bif_wlan, ack_wlan, lost_wlan, wlan_pkt_per_ms * 1000 * 1500 * 8 / 1000 / 1000);
							
							if (bif_wlan > ack_wlan) 
							{
//								lwipep_hdl_rpt_log->console("\nif (bif_wlan > ack_wlan) bif_wlan = bif_wlan - ack_wlan\n{\n");
//								lwipep_hdl_rpt_log->console("	bif_wlan = %d - %d", bif_wlan, ack_wlan);
								bif_wlan -= ack_wlan;
//								lwipep_hdl_rpt_log->console("	bif_wlan = %d\n", bif_wlan);
//								lwipep_hdl_rpt_log->console("}\n");
							} 
							else 
							{
//								lwipep_hdl_rpt_log->console("\nif (bif_wlan < ack_wlan)	{	bif_wlan = 0	}\n");
								bif_wlan = 0;
							}
						}

						if (ack_wlan > lost_wlan)
						{
//							lwipep_hdl_rpt_log->console("\nif (ack_wlan > lost_wlan) ack_wlan = ack_wlan - lost_wlan\n{\n");
//							lwipep_hdl_rpt_log->console("	ack_wlan = %d - %d", ack_wlan, lost_wlan);
							ack_wlan -= lost_wlan;
//							lwipep_hdl_rpt_log->console("	ack_wlan = %d\n", ack_wlan);
//							lwipep_hdl_rpt_log->console("}\n");
						}
						else 
						{
//							lwipep_hdl_rpt_log->console("\nif (ack_wlan < lost_wlan)	{	ack_wlan = 0	}\n");
							ack_wlan = 0;
						}

						if (bif_wlan > uplink_latency * tx_rate_wlan) 
						{	bif_wlan -= uplink_latency * tx_rate_wlan;	} 
						else {	bif_wlan = 0;	}
						//ack_wlan *= pdu_avg_size;
						//bif_wlan *= pdu_avg_size;
					}

					// Calculate WLAN rate
					if (ack_wlan == 0) // || bif_wlan == 0
					{
//						lwipep_hdl_rpt_log->console("\nif (ack_wlan == 0) rate_wlan = rate_init_wlan / pdu_avg_size\n{\n");
//						lwipep_hdl_rpt_log->console("	rate_wlan = %d / %d\n", rate_init_wlan, pdu_avg_size);
						lwipep_hdl_rpt_log->console("	last_highest_rate_wlan = %d \n", last_highest_rate_wlan);
						if (wlan_down == false)
						{
							last_highest_rate_wlan = (last_highest_rate_wlan * 3 / 4 );
							if (last_highest_rate_wlan < 1) last_highest_rate_wlan = 1;
							wlan_down = true;
						}
						rate_wlan = last_highest_rate_wlan; // / pdu_avg_size;
//						rate_wlan = rate_init_wlan / pdu_avg_size;
						
//						rate_wlan = last_highest_rate_wlan / pdu_avg_size;
						lwipep_hdl_rpt_log->console("	rate_wlan = %.3f\n", rate_wlan);
//						lwipep_hdl_rpt_log->console("}\n");
					} 
					else 
					{
						wlan_down = false;
//						lwipep_hdl_rpt_log->console("\nif (ack_wlan > 0) rate_wlan = ack_wlan / time\n{\n");
//						lwipep_hdl_rpt_log->console("	rate_wlan = %d / %d\n", ack_wlan, tv_sec);
						rate_wlan = (float) ack_wlan / (tv_sec + (float) tv_nsec / 1000000000); 
						if (rate_wlan < last_highest_rate_wlan)	rate_wlan = last_highest_rate_wlan * 3 / 4 + rate_wlan / 4;
						else last_highest_rate_wlan = last_highest_rate_wlan * 3 / 4 + rate_wlan / 4;
						lwipep_hdl_rpt_log->console("	last_highest_rate_wlan = %.3f\n", last_highest_rate_wlan);
//						lwipep_hdl_rpt_log->console("	rate_wlan = %.3f\n", rate_wlan);
//						lwipep_hdl_rpt_log->console("}\n");
					}	//bitrate
					
					//======================================================================================================
//					lwipep_hdl_rpt_log->console("=========================== LTE  ===========================\n");	
//					lwipep_hdl_rpt_log->console(" - LTE  Bif %10d, Ack %10d, lost_wlan =%10d, Rate %8.2fmbps\n", bif_lte, ack_lte, lost_lte, lte_pkt_per_ms * 1000 * 1500 * 8 / 1000 / 1000);
					// Caluculate uplink latency(s)
					if (tv_sec * 1000 + tv_nsec / 1000000 > lwipep_rcv_rpt_time) 
					{
						uplink_latency  = tv_sec * 1000 + tv_nsec / 1000000 - lwipep_rcv_rpt_time;
						uplink_latency /= 1000;
					}
//					lwipep_hdl_rpt_log->console("uplink_latency  %10f\n", uplink_latency);
					// Calculate LTE bits-in-flight
					if (lte_tx_packet_count > 0) 
					{
//						lwipep_hdl_rpt_log->console("\nif (lte_tx_packet_count > 0) pdu_avg_size = lte_tx_bytes / lte_tx_packet_count * 8\n{\n");
//						lwipep_hdl_rpt_log->console("	pdu_avg_size = %d / %d * 8\n", lte_tx_bytes, lte_tx_packet_count);
						pdu_avg_size = lte_tx_bytes / lte_tx_packet_count * 8;
//						lwipep_hdl_rpt_log->console("	pdu_avg_size = %.3d\n", pdu_avg_size);
						
						lte_tx_packet_count = 0;
						lte_tx_bytes = 0;		
						if (bif_lte > ack_lte) 
						{
//							lwipep_hdl_rpt_log->console("		%d  >	%d\n", bif_lte, ack_lte);
//							lwipep_hdl_rpt_log->console("	if (bif_lte > ack_lte) bif_lte -= ack_lte;\n	{\n");
//							lwipep_hdl_rpt_log->console("		bif_lte = %d - %d > ack_lte %d\n", bif_lte, ack_lte);
							bif_lte -= ack_lte;
//							lwipep_hdl_rpt_log->console("		bif_lte = %d \n	}\n", bif_lte);
						}
						else 
						{
//							lwipep_hdl_rpt_log->console("		   %d    <   %d\n", bif_lte, ack_lte);
//							lwipep_hdl_rpt_log->console("\n	else if (bif_lte > ack_lte)	{	bif_lte = 0	}\n");
							bif_lte = 0;
						}

						//ack_lte *= pdu_avg_size;
						//bif_lte *= pdu_avg_size;	
//						lwipep_hdl_rpt_log->console("}\n");
					}

					// Calculate LTE rate(get RLC status report)
					if (ack_lte == 0) // || bif_lte == 0
					{
						if (lte_down == false)
						{
							last_highest_rate_lte = (last_highest_rate_lte * 3 / 4 );
							if (last_highest_rate_lte < 1) last_highest_rate_lte = 1;
							lte_down = true;
						}
						rate_lte = last_highest_rate_lte; // / pdu_avg_size;
	
//						rate_lte = (rate_init_lte / pdu_avg_size);
//						lwipep_hdl_rpt_log->console("\nif (ack_lte == 0) rate_lte = (rate_init_lte / pdu_avg_size)\n");
//						lwipep_hdl_rpt_log->console("{\n	rate_lte =  %.9f / %9d\n", rate_init_lte, pdu_avg_size);
//						lwipep_hdl_rpt_log->console("	rate_lte = %.9f \n", rate_lte);
						// If LTE not activated, try to transmit a part of WLAN bits-in-flight
						if (lte_tx_ratio == 0) 
						{
							bif_lte = bif_wlan * (max_ratio - 1) / max_ratio;
//							lwipep_hdl_rpt_log->console("\n	if (lte_tx_ratio == 0)	\n{	bif_lte = bif_wlan * (max_ratio - 1) / max_ratio;\n");
//							lwipep_hdl_rpt_log->console("		bif_lte = %d * %d / %d	\n		}\n", bif_wlan, max_ratio - 1, max_ratio);
						}
//						lwipep_hdl_rpt_log->console("}\n");
					} 
					else 
					{
						rate_lte = (float) ack_lte / (tv_sec + (float) tv_nsec / 1000000000);

						if (rate_lte < last_highest_rate_lte)	rate_lte = last_highest_rate_lte * 3 / 4 + rate_lte / 4;
						else last_highest_rate_lte = last_highest_rate_lte * 3 / 4 + rate_lte / 4;
//						lwipep_hdl_rpt_log->console("	last_highest_rate_lte = %.3f\n", last_highest_rate_lte);
						lte_down = false;

//						lwipep_hdl_rpt_log->console("\nelse if (ack_lte != 0) rate_lte = (float) ack_lte / (tv_sec + (float) tv_nsec / 1000000000);\n{\n");
//						lwipep_hdl_rpt_log->console("	rate_lte = %d / (%d + %d / 1000000000)\n", ack_lte, tv_sec, tv_nsec);
//						lwipep_hdl_rpt_log->console("	rate_lte = %.2f \n", rate_lte);
						if (rate_lte > rate_init_lte / pdu_avg_size) 
						{
//							lwipep_hdl_rpt_log->console("		if rate_lte = %.2f > (rate_init_lte / pdu_avg_size) = %.2f\n		{", rate_lte, (float) rate_init_lte / pdu_avg_size);
							bif_lte += (rate_lte - rate_init_lte / pdu_avg_size) * (tv_sec + (float) tv_nsec / 1000000000);
							rate_lte = rate_init_lte / pdu_avg_size;
//							lwipep_hdl_rpt_log->console("		bif_lte += (%.9f - %.2f / %d) * (%d + %d / 1000000000)\n", rate_lte, rate_init_lte, pdu_avg_size, tv_sec, tv_nsec);
//							lwipep_hdl_rpt_log->console("		rate_lte = %.2f / %d\n", rate_init_lte, pdu_avg_size);
//							lwipep_hdl_rpt_log->console("		rate_lte = %.2f\n		}\n", rate_lte);
						}
//						lwipep_hdl_rpt_log->console("}\n");
					}
lwipep_hdl_rpt_log->console("rate_lte = %.3f 	last_highest_rate_lte = %.3f\n", rate_lte, last_highest_rate_lte);
lwipep_hdl_rpt_log->console("rate_wlan = %.3f	last_highest_rate_wlan = %.3f\n", rate_wlan, last_highest_rate_wlan);

					// Packet size is 1500 bytes(1 PDU)		+ buffer_state * 8 / pdu_avg_size
//					lwipep_hdl_rpt_log->console("=========================== DELAY  ===========================\n");	
					delay_lte = (float) (((pdu_size + bif_lte) / rate_lte * 1000) * 1000); //ms
					delay_wlan = (float) (((pdu_size + bif_wlan) / rate_wlan * 1000) * 1000);
//					int_delay_lte = (int) delay_lte;
//					int_delay_wlan = (int) delay_wlan;
/*					
					lwipep_hdl_rpt_log->console("delay_lte = (((pdu_size + bif_lte + buffer_state * 8 / pdu_avg_size) / rate_lte * 1000) * 1000)\n");
					lwipep_hdl_rpt_log->console("delay_lte = (%d + %d + %d / %d) / %0.10f\n", pdu_size, bif_lte, buffer_state * 8, pdu_avg_size, rate_lte);
					lwipep_hdl_rpt_log->console("delay_wlan = (float) (((pdu_size + bif_wlan) / rate_wlan * 1000) * 1000)\n");
					lwipep_hdl_rpt_log->console("delay_wlan = (%d + %d) / %0.10f\n", pdu_size, bif_wlan, rate_wlan * 1000);
					lwipep_hdl_rpt_log->console(" - LTE  Ack %10d, Bif %10d, Rate %8.2f, Delay %8.3f ms\n", ack_lte, bif_lte, rate_lte / 1000000 * pdu_avg_size, delay_lte);
					lwipep_hdl_rpt_log->console(" - WLAN Ack %10d, Bif %10d, Rate %8.2f, Delay %8.3f ms\n", ack_wlan, bif_wlan, rate_wlan / 1000000 * 1500, delay_wlan);
*/
					lwipep_hdl_rpt_log->console("delay_lte  %.3f\ndelay_wlan %.3f\n\n", delay_lte, delay_wlan);
					
					if (isinf(delay_lte) == true)
						lwipep_hdl_rpt_log->console("isinf(delay_lte)\n");
					else if (isinf(delay_wlan) == true)
						lwipep_hdl_rpt_log->console("isinf(delay_wlan)\n");
					else
					{
						
//						ema_part   = alpha_part * int_delay_wlan * ema_whole + (alpha_whole - alpha_part) * ema_part * int_delay_lte;
//						ema_whole *= alpha_whole * int_delay_lte;
						
						
						ema_part   = alpha_part * delay_wlan * ema_whole + (alpha_whole - alpha_part) * ema_part * delay_lte;
						ema_whole *= alpha_whole * delay_lte;
						
									 lwipep_hdl_rpt_log->console("while ema_part = %lu\n",ema_part);
									 lwipep_hdl_rpt_log->console("while ema_whole = %lu\n",ema_whole);
						while ( ema_whole >= max_ratio && ema_part >= max_ratio )
						{
							ema_part  /= max_ratio;
							ema_whole /= max_ratio;
							
	
						//		lwipep_hdl_rpt_log->console("while ema_part = %lu\n",ema_part);
						//		lwipep_hdl_rpt_log->console("while ema_whole = %lu\n",ema_whole);
							///*		
							//(ema_part / max_ratio <= 1 && ema_part % max_ratio > 0) || (ema_whole / max_ratio <= 1 && ema_whole % max_ratio > 0 ) ||
							if (  (ema_part / max_ratio == ema_whole / max_ratio) ) 
							{
								//						lwipep_hdl_rpt_log->console("break\n");
								hdl_ema_part = (float)ema_part;
								hdl_ema_whole = (float)ema_whole;
	//							lwipep_hdl_rpt_log->console("while ema_part = %lu\n",ema_part);
	//							lwipep_hdl_rpt_log->console("while ema_whole = %lu\n",ema_whole);
								ema			= hdl_ema_part / hdl_ema_whole;
								
	//							lwipep_hdl_rpt_log->console("ema = %.9f\n",ema);
								if (ema > 0 && ema < 1)
								{
									ema_whole = 1;
									while (ema < 1)
									{
										ema = ema*10;
										ema_whole = ema_whole*10;
									}
									ema_part = ema;
									
								}
								else if(ema > 1)
								{
									if (ema > 1.5 && ema < 2)
									{
										ema_whole = 10;
										ema_part = ema * 10;
									}
									else
									{
										ema_whole = 1;
										ema_part = ema;
									}
								}
								
								gcd = get_gcd(ema_part, ema_whole);
								
								if (gcd > 1)
								{
									ema_part = ema_part / gcd;
									ema_whole = ema_whole / gcd;
								}
								
								break;
							}
							//*/			
						}
					}

					
					if ((reg_ema_part / reg_ema_whole < ema_part / ema_whole) && delay_lte > delay_wlan)
					{
						ema_part = 1;
						ema_whole = max_ratio;
					}
					else if ((reg_ema_part / reg_ema_whole > ema_part / ema_whole) && delay_lte < delay_wlan)
					{
						ema_part = max_ratio;
						ema_whole = 1;
					}
//====================================================================================================================
					if (reg_ema_part * max_ratio <= reg_ema_whole && ema_part >= ema_whole)
					{
						ema_part = 1;
						ema_whole = max_ratio;
					}
					else if (reg_ema_whole * max_ratio <= reg_ema_part && ema_part <= ema_whole)
					{
						ema_part = max_ratio;
						ema_whole = 1;
					}
					
					else if (reg_ema_part / reg_ema_whole > 2 && ema_part / ema_whole < 2)
					{
						ema_part = max_lte_ratio;
						ema_whole = max_wlan_ratio;
					}
					else if (reg_ema_part / reg_ema_whole < 2 && ema_part / ema_whole > 2)
					{
						ema_part = max_lte_ratio;
						ema_whole = max_wlan_ratio;
					}
					
//====================================================================================================================
					if (ema_whole > max_ratio * ema_part && reg_ema_part > reg_ema_whole ) 
					{	
						//set_lwipep_tx_ratio(0, 1);
						ema_part = max_lte_ratio;
						ema_whole = max_wlan_ratio;
					} 
					else if (ema_part > max_ratio * ema_whole && reg_ema_part < reg_ema_whole ) 
					{
						//set_lwipep_tx_ratio(1, 0);
						ema_part = max_lte_ratio;
						ema_whole = max_wlan_ratio;
					}
					
	//				if ((rcv_pkt_wlan > rcv_pkt_lte && ema_part > ema_whole) || (rcv_pkt_wlan < rcv_pkt_lte && ema_part < ema_whole))
	//				{
	//					ema_part = reg_ema_part;
	//					ema_whole = reg_ema_whole;
	//				}
					
					lwipep_hdl_rpt_log->console("max_lte_ratio = %d,	max_wlan_ratio = %d\n", max_lte_ratio, max_wlan_ratio);
					lwipep_hdl_rpt_log->console("max_rcv_wlan + max_rcv_lte  = %d,	rcv_pkt_wlan + rcv_pkt_lte = %d\n", max_rcv_wlan + max_rcv_lte , rcv_pkt_wlan + rcv_pkt_lte);
					if ((max_rcv_wlan + max_rcv_lte) < (rcv_pkt_wlan + rcv_pkt_lte))
					{
						max_rcv_wlan = rcv_pkt_wlan;
						max_rcv_lte = rcv_pkt_lte;
						max_lte_ratio = reg_ema_part;
						max_wlan_ratio = reg_ema_whole;
						max_cnt = 0;
						
						lwipep_hdl_rpt_log->console("%d < %d\n", max_rcv_wlan + max_rcv_lte , rcv_pkt_wlan + rcv_pkt_lte);
						lwipep_hdl_rpt_log->console("max_lte_ratio = %d,	max_wlan_ratio = %d\n", max_lte_ratio, max_wlan_ratio);
					}
					else 
					{
						if (max_cnt == 2)
						{
							ema_part = max_lte_ratio;
							ema_whole = max_wlan_ratio;
							max_cnt = 0;
						}
						else	max_cnt++;
					}
					
					if (ema_part <= 8 && ema_whole <= 8 && ratio_record[ema_part][ema_whole] != 0)
					{
						if (ratio_record[ema_part][ema_whole] < ratio_record[max_lte_ratio][max_wlan_ratio])
						{	
							ema_part = max_lte_ratio;
							ema_whole = max_wlan_ratio;
						}
					}
					/*
					else if (lte_tx_ratio > wlan_tx_ratio && lte_tx_ratio <= wlan_tx_ratio * 2 && ema_part * 2 < ema_whole)
					{
						ema_whole = wlan_tx_ratio + 1;
					}
					else if (lte_tx_ratio < wlan_tx_ratio && lte_tx_ratio * 2 >= wlan_tx_ratio && ema_part > ema_whole * 2)
					{
						ema_part = lte_tx_ratio + 1;
					}
					*/
					/*
						lte_tx_ratio = hdl_args->lte_tx_ratio;
						wlan_tx_ratio
					*/
									lwipep_hdl_rpt_log->console("caled ema_part = %lu\n",ema_part);
									lwipep_hdl_rpt_log->console("caled ema_whole = %lu\n",ema_whole);

//					lwipep_hdl_rpt_log->console("after while ema_part = %lu\n",ema_part);
//					lwipep_hdl_rpt_log->console("after while ema_whole = %lu\n",ema_whole);
					handle_count = handle_count + 1;
//					lwipep_hdl_rpt_log->console("\ninside count = %d\n", handle_count);
//					lwipep_hdl_rpt_log->console("\ninside hdl_args->count = %d\n", handle_count);
					//				pthread_mutex_unlock(&mutex);
					
				
				}
				
				handle = true;
				pthread_mutex_unlock(&mutex);
			}
			
			
		}
	}
	
	
	void lwipep_hdl_rpt::run_thread()
	{
		
		float test = 0;
		
		lwipep_handle_report_running = true;
		uint32_t a = 11888;
		uint32_t b = 14681320;
		test = (float) (11888 / 14681320);
		lwipep_hdl_rpt_log->console("threrad = %.3f\n",test);
		test =  ((float)a / (float)b);;
		lwipep_hdl_rpt_log->console("threrad = %.9f\n",test);
		//while(enable_lwipep_handle_report == false){			}

		lwipep_handle_report_running = false;
//		lwipep_hdl_rpt_log->console("~~~~~~~~~~~~~~~~~lwipep thread close~~~~~~~~~~~~~~~~~\n");
	}

	void lwipep_hdl_rpt::get_pkt_per_ms ()
	{
		lwipep_hdl_rpt_log->console("======================================================================================\n");
		sd_pkt_per_ms = (lte_tx_packet_count + wlan_tx_packet_count) / lwipep_rcv_rpt_time;
		
		lte_pkt_per_ms = rate_init_lte / 1000 / 8 / 1500;	// pkt per 1ms 
		wlan_pkt_per_ms = rate_init_wlan / 1000 / 8 / 1500;	// pkt per 1ms 
		ack_lte = 0;
		ack_wlan = 0;
		
		if ( lte_tx_ratio != 0 && (lte_pkt_per_ms > sd_pkt_per_ms * lte_tx_ratio / total_tx_ratio) )
		{
			lte_pkt_per_ms = sd_pkt_per_ms * lte_tx_ratio / total_tx_ratio;
		}
		else if (lte_tx_ratio == 0)
		{
			lte_pkt_per_ms = 0;
		}
		if (wlan_tx_ratio != 0 && (wlan_pkt_per_ms > sd_pkt_per_ms * wlan_tx_ratio / total_tx_ratio))
		{
			wlan_pkt_per_ms = sd_pkt_per_ms * wlan_tx_ratio / total_tx_ratio;
		}
		else if (wlan_tx_ratio == 0)
		{
			wlan_pkt_per_ms = 0;
		}
		
		if (lte_tx_packet_count != 0)	ack_lte  = rcv_pkt_lte;
		else if (lte_tx_packet_count == 0 && ack_lte != 0)	last_highest_rate_lte = (float) last_highest_rate_lte * 3 / 4 + ack_lte / (tv_sec + (float) tv_nsec / 1000000000) / 4;
		if (wlan_tx_packet_count != 0)	ack_wlan = rcv_pkt_wlan;
	
	lwipep_hdl_rpt_log->console("SERVER RCV lte %d	wlan %d\n", rcv_pkt_lte, rcv_pkt_wlan);
//		lwipep_hdl_rpt_log->console("SERVER RCV lte %.3f pkt/ms	wlan %.3f pkt/ms\n", lte_pkt_per_ms, wlan_pkt_per_ms);
//		lwipep_hdl_rpt_log->console("- LTE  lte_tx_packet_count %10d, - WLAN wlan_tx_packet_count %10d \n",lte_tx_packet_count, wlan_tx_packet_count);
//		lwipep_hdl_rpt_log->console("RCV %d PKT - LTE  Ack %10d, - WLAN ACK %10d \n",rcv_pkt, ack_lte, ack_wlan);
		
		return;
	}
	
	uint32_t lwipep_hdl_rpt::get_gcd(uint32_t x, uint32_t y)
	{
		uint32_t tmp;
		while (y != 0) {
			tmp = x % y;
			x = y;
			y = tmp;
		}
		return x;
	}
	
}
