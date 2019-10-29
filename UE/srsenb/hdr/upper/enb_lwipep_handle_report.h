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



#ifndef SRSENB_LWIPEP_HDL_RPT_H
#define SRSENB_LWIPEP_HDL_RPT_H

#include <map>
#include "srslte/common/buffer_pool.h"
#include "srslte/common/log.h"
#include "srslte/common/common.h"
#include "srslte/common/threads.h"
#include "srslte/common/interfaces_common.h"
#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/interfaces/ue_interfaces.h"
#include "srslte/upper/lwipep.h"


namespace srsenb {

//,public srsue::tunnel_interface_lwipep
class lwipep_hdl_rpt:	public thread,
						public lwipep_hdl_rpt_interface_lwipep		//public srsue::lwipep_ipsec_interface_lwipep_segw
{
public:
	lwipep_hdl_rpt();
	void			init(srslte::log *lwipep_ipsec_log_);
	void			stop();
	void			add_user(uint16_t rnti);
	void			handle_args(void *args);	
	void			update_route(void *args);

private:

	typedef struct 
	{
		uint32_t handle_current_max_sn;
		uint32_t handle_over_current_max_sn_times;
		uint32_t nmp;
		uint32_t sn;
		uint32_t lost_lte;
		uint32_t lost_wlan;

		uint32_t bif_wlan;
		uint32_t bif_lte;

		uint32_t			buffer_state;
		uint32_t			handle_delay_sn;
		uint32_t            reconfigure_sn;
		uint32_t			tx_packet_count;
		uint32_t            lte_tx_packet_count;
		uint32_t            wlan_tx_packet_count;
		uint32_t			lte_tx_ratio;
		uint32_t            lte_tx_bytes;
		uint32_t			wlan_tx_ratio;
		uint32_t            wlan_tx_bytes;
		uint32_t            total_tx_ratio;
		uint32_t			last_handle_current_max_sn;
		uint32_t            alpha_part;
		uint32_t            alpha_whole;
		uint32_t            ema_part;
		uint32_t            ema_whole;
		
		int32_t				lwipep_report_time;
		float				delay_lte; //ms
		float				delay_wlan;
		long				tv_sec;
		long				tv_nsec;
		
		uint32_t            count;
		bool				get_args;
	}control_radio_args;
	
	//==========================args===================================
	pthread_mutex_t		mutex;
	const static bool	enable_ema = true;
	const static float  MAX_RATIO		= 2;
	const static float  rate_init_lte	= 1500000; //60000000; 60Mbps
	const static float  rate_init_wlan	= 500000000; //500Mbps
	
	uint32_t handle_current_max_sn;
	uint32_t handle_over_current_max_sn_times;
	uint32_t nmp;
	uint32_t handle_sn;
	uint32_t lost_lte;
	uint32_t lost_wlan;

	uint32_t bif_wlan;
	uint32_t bif_lte;

	uint32_t			buffer_state;
	uint32_t			handle_delay_sn;
	uint32_t            reconfigure_sn;
	uint32_t			tx_packet_count;
	uint32_t            lte_tx_packet_count;
	uint32_t            wlan_tx_packet_count;
	uint32_t			lte_tx_ratio;
	uint32_t            lte_tx_bytes;
	uint32_t			wlan_tx_ratio;
	uint32_t            wlan_tx_bytes;
	uint32_t            total_tx_ratio;
	uint32_t			last_handle_current_max_sn;
	uint32_t            alpha_part;
	uint32_t            alpha_whole;
	uint32_t            ema_part;
	uint32_t            ema_whole;
	
	int32_t				lwipep_report_time;
	float				delay_lte; //ms
	float				delay_wlan;
	long				tv_sec;
	long				tv_nsec;
	float				max_ratio;
	float				rate_wlan;
	float				rate_lte;
	float				uplink_latency;
	float				tx_rate_wlan;
	uint32_t			show_time;
	uint32_t			show_time2;
	uint32_t			ack_wlan;
	uint32_t			ack_lte;
	uint32_t			pdu_avg_size;
	uint32_t			pdu_size;
	bool				get_args;
	//=============================================================
	
	/*
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
	alpha_part;
	alpha_whole;
	ema_part;
	ema_whole;
	*/
	
	uint32_t            handle_count;
	
	bool	enable_lwipep_handle_report;
	bool	lwipep_handle_report_running;
	static const int 	LWIPEP_HDL_RPT_THREAD_PRIO = 7;
	
	void			run_thread();
	
	
	srslte::log	*lwipep_hdl_rpt_log;

};



}//srsue

#endif
