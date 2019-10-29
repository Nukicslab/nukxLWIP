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


#include "srsenb/hdr/upper/enb_lwipep_handle_report.h"

namespace srsenb 
{

	lwipep_hdl_rpt::lwipep_hdl_rpt()
	{
		get_args = true;
		
		buffer_state = 0;
		tv_sec = 0;
		tv_nsec = 0;
		handle_delay_sn = 0;
		handle_over_current_max_sn_times = 0;
		handle_current_max_sn = 0;
		last_handle_current_max_sn = 0;
		nmp = 0;
		lost_lte = 0;
		handle_sn = 0;
		bif_wlan = 0;
		bif_lte = 0;
		wlan_tx_bytes = 0;
		lte_tx_bytes = 0;
		delay_lte = 0; //ms
		delay_wlan = 0;
		alpha_part     = 1;
		alpha_whole    = 2;
		ema_part       = 1;
		ema_whole      = 1;
		lwipep_report_time = lwipep_report_time;
		handle_count			= 0;
		wlan_tx_packet_count = 0;
		lte_tx_packet_count = 0;
		
		delay_lte = 0; //ms
		delay_wlan = 0;
		max_ratio = 2.0f;
		// Bits-in-flight
		rate_wlan = 0;
		rate_lte = 0;
		uplink_latency = 0;
		tx_rate_wlan = 0;
		show_time = 0;
		show_time2 = 0;
		ack_wlan = 0;
		ack_lte = 0;
		pdu_avg_size = 1500;
		pdu_size = 1;
		
		pthread_mutex_init(&mutex, NULL);
		enable_lwipep_handle_report = true;
		lwipep_handle_report_running = true;
	}
	void lwipep_hdl_rpt::init(srslte::log *lwipep_hdl_rpt_log_)
	{
		lwipep_hdl_rpt_log	= lwipep_hdl_rpt_log_;
		pthread_mutex_init(&mutex, NULL);
		start(LWIPEP_HDL_RPT_THREAD_PRIO);
	}
	
	void lwipep_hdl_rpt::handle_args(void *args)
	{
		control_radio_args *hdl_args = (control_radio_args *) args;
		buffer_state = hdl_args->buffer_state;
		
		tv_sec = hdl_args->tv_sec;
		tv_nsec = hdl_args->tv_nsec;
		
		handle_delay_sn = hdl_args->handle_delay_sn;
		
		handle_over_current_max_sn_times = hdl_args->handle_over_current_max_sn_times;
		
		handle_current_max_sn = hdl_args->handle_current_max_sn;
		
		nmp = hdl_args->nmp;

		lost_lte = hdl_args->lost_lte;
		
		handle_sn = hdl_args->sn;
		
		reconfigure_sn = hdl_args->reconfigure_sn;
		tx_packet_count = hdl_args->tx_packet_count;
		
		wlan_tx_packet_count = hdl_args->bif_wlan;
		lte_tx_packet_count = hdl_args->bif_lte;
		
		wlan_tx_bytes = hdl_args->wlan_tx_bytes;
		lte_tx_bytes = hdl_args->lte_tx_bytes;
		
		total_tx_ratio = hdl_args->total_tx_ratio;
		
		delay_lte = 0; //ms
		delay_wlan = 0;
		
		wlan_tx_packet_count = hdl_args->wlan_tx_packet_count;
		lte_tx_packet_count = hdl_args->lte_tx_packet_count;
		
		wlan_tx_bytes = 0;
		lte_tx_bytes = 0;
		tx_packet_count = 0;
		lte_tx_packet_count = 0;
		wlan_tx_packet_count = 0;
		hdl_args->get_args = false;
		get_args = false;
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
		return;
	}

	void lwipep_hdl_rpt::run_thread()
	{

		lwipep_hdl_rpt_log->console("~~~~~~~~~~~~~~~~~lwipep thread start~~~~~~~~~~~~~~~~~\n");
		
		lwipep_handle_report_running = true;
		while(enable_lwipep_handle_report == true)
		{

			if (show_time == 100000000)
			{
				
				if (show_time == 100000000)
				{
					show_time2 = show_time2 + 1;
				}
				
				if (show_time2 == 50)
				{
					show_time2 = 0;
					lwipep_hdl_rpt_log->console("\ninside true\n");
				}
				
				show_time = 0;
			}
			show_time = show_time + 1;
			
			
			if (get_args == false)
			{
				
				delay_lte = 0; //ms
				delay_wlan = 0;
				rate_wlan = 0;
				rate_lte = 0;
				uplink_latency = 0;
				tx_rate_wlan = 0;
				ack_wlan = 0;
				ack_lte = 0;
				
				if (tx_packet_count >= reconfigure_sn) 
				{
					
					// Bits-in-flight
					if (wlan_tx_packet_count > 0) 
					{
						//byte to bit, so *8
						pdu_avg_size = wlan_tx_bytes / wlan_tx_packet_count * 8;
						tx_rate_wlan = (float) wlan_tx_packet_count / (tv_sec + (float) tv_nsec / 1000000000);
						wlan_tx_bytes = 0;
						wlan_tx_packet_count = 0;

						// nmp is all lost packet
						if (nmp > lost_lte) {	lost_wlan = (nmp - lost_lte);	}

						if (handle_current_max_sn != last_handle_current_max_sn || handle_over_current_max_sn_times > 0) 
						{
							handle_over_current_max_sn_times <<= SN_LEN;
							handle_over_current_max_sn_times  += handle_current_max_sn - last_handle_current_max_sn;
							ack_lte  = handle_over_current_max_sn_times * lte_tx_ratio / total_tx_ratio;
							ack_wlan = handle_over_current_max_sn_times * wlan_tx_ratio / total_tx_ratio;
							last_handle_current_max_sn = handle_current_max_sn;

							if (bif_wlan > ack_wlan) {	bif_wlan -= ack_wlan;	} 
							else {	bif_wlan = 0;	}
						}

						if (ack_wlan > lost_wlan) {	ack_wlan -= lost_wlan;	} 
						else {	ack_wlan = 0;	}

						if (bif_wlan > uplink_latency * tx_rate_wlan) 
						{	bif_wlan -= uplink_latency * tx_rate_wlan;	} 
						else {	bif_wlan = 0;	}
						//ack_wlan *= pdu_avg_size;
						//bif_wlan *= pdu_avg_size;
					}

					// Calculate WLAN rate
					if (ack_wlan == 0) // || bif_wlan == 0
					{	rate_wlan = rate_init_wlan / pdu_avg_size;	} 
					else 
					{	rate_wlan = (float) ack_wlan / (tv_sec + (float) tv_nsec / 1000000000); 	}	//bitrate

					// Caluculate uplink latency(s)
					if (tv_sec * 1000 + tv_nsec / 1000000 > lwipep_report_time) 
					{
						uplink_latency  = tv_sec * 1000 + tv_nsec / 1000000 - lwipep_report_time;
						uplink_latency /= 1000;
					}

					// Calculate LTE bits-in-flight
					if (lte_tx_packet_count > 0) 
					{
						pdu_avg_size = lte_tx_bytes / lte_tx_packet_count * 8;

						if (bif_lte > ack_lte) {	bif_lte -= ack_lte;	}
						else {	bif_lte = 0;	}

						//ack_lte *= pdu_avg_size;
						//bif_lte *= pdu_avg_size;
					}

					// Calculate LTE rate(get RLC status report)
					if (ack_lte == 0) // || bif_lte == 0
					{
						rate_lte = rate_init_lte / pdu_avg_size;
						// If LTE not activated, try to transmit a part of WLAN bits-in-flight
						if (lte_tx_ratio == 0) {	bif_lte = bif_wlan * (max_ratio - 1) / max_ratio;	}
					} 
					else 
					{
						rate_lte = (float) ack_lte / (tv_sec + (float) tv_nsec / 1000000000);
						if (rate_lte > rate_init_lte / pdu_avg_size) 
						{
							bif_lte += (rate_lte - rate_init_lte / pdu_avg_size) * (tv_sec + (float) tv_nsec / 1000000000);
							rate_lte = rate_init_lte / pdu_avg_size;
						}
					}

					// Packet size is 1500 bytes(1 PDU)

					delay_lte = (float) (pdu_size + bif_lte + buffer_state * 8 / pdu_avg_size) / rate_lte * 1000; //ms
					delay_wlan = (float) (pdu_size + bif_wlan) / rate_wlan * 1000;

					if (enable_ema == true) 
					{
						ema_part   = alpha_part * (delay_wlan * 1000) * ema_whole + (alpha_whole - alpha_part) * ema_part * (delay_lte * 1000);
						ema_whole *= alpha_whole * (delay_lte * 1000);

						while (ema_part >= MAX_RATIO && ema_whole >= MAX_RATIO) 
						{
							ema_part  /= MAX_RATIO;
							ema_whole /= MAX_RATIO;
						}

					} 
					else 
					{
						while (delay_wlan * 1000 >= MAX_RATIO && delay_lte * 1000 >= MAX_RATIO) 
						{
							delay_lte  /= MAX_RATIO;
							delay_wlan /= MAX_RATIO;
						}
					}
					get_args = true;
					handle_count = handle_count + 1;
					lwipep_hdl_rpt_log->console("\ninside hdl_args->count = %d\n", handle_count);
	//				pthread_mutex_unlock(&mutex);
					
					
				}

			}
			
			
			
			//		pthread_mutex_unlock(&control_radio_lock);
			
		}

		lwipep_handle_report_running = false;
		lwipep_hdl_rpt_log->console("~~~~~~~~~~~~~~~~~lwipep thread close~~~~~~~~~~~~~~~~~\n");
	}

}
