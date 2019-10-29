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



#ifndef SRSUE_LWIPEP_HDL_RPT_H
#define SRSUE_LWIPEP_HDL_RPT_H

#include <map>
#include "srslte/common/buffer_pool.h"
#include "srslte/common/log.h"
#include "srslte/common/common.h"
#include "srslte/common/threads.h"
#include "srslte/common/interfaces_common.h"
#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/interfaces/ue_interfaces.h"
#include "srslte/upper/lwipep.h"
#include "srslte/common/timers.h"
#include "srslte/common/tti_sync_cv.h"

namespace srsue {



//,public srsue::tunnel_interface_lwipep
class lwipep_hdl_rpt:	srslte::timer_callback,
						public thread,
						public lwipep_hdl_rpt_interface_lwipep		//public srsue::lwipep_ipsec_interface_lwipep_segw
{
public:
	lwipep_hdl_rpt();
	void			init(srslte::mac_interface_timers *mac_timers_, srslte::log *lwipep_ipsec_log_);
	void			stop();
	void			add_user(uint16_t rnti);
	void			handle_args(void *args);	
	void			update_route(void *args);
	void			get_pkt_per_ms ();

private:

	typedef struct 
	{
		uint32_t rcv_pkt;
		uint32_t rcv_pkt_wlan;
		uint32_t rcv_pkt_lte;

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
		uint32_t			check_rcv_wlan_pkt;
		uint32_t			check_rcv_lte_pkt;
		uint32_t			max_ratio;
		uint32_t            count;
		int32_t				lwipep_hdl_rpt_time;
		int32_t				lwipep_rcv_rpt_time;
		float				delay_lte; //ms
		float				delay_wlan;
		long				tv_sec;
		long				tv_nsec;
		bool				get_args;
	}control_radio_args;
	//==========================args===================================
	pthread_mutex_t		mutex;
	const static bool	enable_ema = true;
	const static float  MAX_RATIO		= 8;
	//const static 
	float  rate_init_lte	= 45000000; //60000000 = 60Mbps 1500000 = 1.5Mbps  50000000 = 50Mbps
	float  rate_init_wlan	= 650000000; //500,000,000 500Mbps
	float  last_highest_rate_lte	= 0; //60000000 = 60Mbps 1500000 = 1.5Mbps  50000000 = 50Mbps
	float  last_highest_rate_wlan	= 0; //500000000 500Mbps
	bool	lte_down;
	bool	wlan_down;
	uint32_t miss_hdl;
	uint32_t miss_hdl_count;
	uint32_t max_lte_ratio;
	uint32_t max_wlan_ratio;
	
	uint32_t ratio_record[9][9];
	uint32_t max_rcv_wlan;
	uint32_t max_rcv_lte;
	uint32_t max_cnt;
	
	uint32_t rcv_pkt;
	uint32_t rcv_pkt_wlan;
	uint32_t rcv_pkt_lte;
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
	uint64_t            ema_part;
	uint64_t            ema_whole;
	
	float	lte_pkt_per_ms;	// 3.75 pkt per 1ms 
	float	wlan_pkt_per_ms;	// 54 pkt per 1ms 
	float	sd_pkt_per_ms;	// total pkt per 1ms 
	int32_t				lwipep_rcv_rpt_time;
	float				delay_lte; //ms
	float				delay_wlan;
	long				tv_sec;
	long				tv_nsec;
	uint32_t			max_ratio;
	float				rate_wlan;
	float				rate_lte;
	float				uplink_latency;
	float				tx_rate_wlan;
	uint32_t			ack_wlan;
	uint32_t			ack_lte;
	uint32_t			pdu_avg_size;
	uint32_t			pdu_size;
	bool				handle;
	bool				get_args;
	//=============================================================
	/*
	delay_lte = 0; //ms
	delay_wlan = 0;
	
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
	uint32_t			gcd;
	float				ema;
	float				hdl_ema_part;
	float            	hdl_ema_whole;
	
	bool	enable_lwipep_handle_report;
	bool	lwipep_handle_report_running;
	static const int 	LWIPEP_HDL_RPT_THREAD_PRIO = 7;
	
	void			run_thread();
	void timer_expired(uint32_t timer_id);
	uint32_t get_gcd(uint32_t x, uint32_t y);
	
	srslte::timers::timer	*lwipep_hdl_rpt_config;
	uint32_t				lwipep_hdl_rpt_id;
	int32_t					lwipep_hdl_rpt_time;
	
	srslte::mac_interface_timers	*mac_timers;
	srslte::log	*lwipep_hdl_rpt_log;

};



}//srsue

#endif
