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

#ifndef SRSLTE_LWIPEP_H
#define SRSLTE_LWIPEP_H


#include "srslte/common/buffer_pool.h"
#include "srslte/common/log.h"
#include "srslte/common/common.h"
#include "srslte/interfaces/ue_interfaces.h"
#include "srslte/common/security.h"
//#include "srslte/common/msg_queue.h"
#include "srslte/common/threads.h"
#include "srslte/common/timers.h"
#include "srslte/common/tti_sync_cv.h"

#include <linux/ip.h>


namespace srslte {

typedef enum
{
	DELAY = 0,
	WAIT = 1,
	ROUTE_LTE = 2,
	ROUTE_WLAN = 3,
}route_t;

/****************************************************************************
 * LWIPEP Header
 * Ref: 3GPP TS 29.281 v10.1.0 Section 5
 *
 *        | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 |
 *
 * 1      |  Version  |PT | * | E | S |PN |
 * 2      |           Message Type        |
 * 3      |         Length (1st Octet)    |
 * 4      |         Length (2nd Octet)    |
 * 5      |          TEID (1st Octet)     |
 * 6      |          TEID (2nd Octet)     |
 * 7      |          TEID (3rd Octet)     |
 * 8      |          TEID (4th Octet)     |
 ***************************************************************************/

#define GRE_HEADER_LEN 12				//12 BYTE
#define IP_HEADER_LEN 20
#define SN_LEN 12
#define IP_TYPE_GRE	0x002F

typedef struct
{
	unsigned short checksum;			//has_checksum:1
	unsigned short routing;				//has_routing:1,
	unsigned short key;					//has_key:1,
	unsigned short seq;					//has_seq:1,
	unsigned short strict_source_route;		//strict_source_route:1,
	unsigned short recursion_control;		//recursion_control:3,
	unsigned short flags;					//flags:5,
	unsigned short version;					//version:3
}gre_header_flag_t;

typedef struct
{
	uint16_t flag;			// __be16 == unsigned short == 2 octets == 2byte
	uint16_t proto;
	uint16_t checksum;
	uint16_t offset;
	uint32_t key;			//user rnti
	uint32_t seq;			// __be32 == unsigned int
}gre_header_t;

typedef struct
{
	uint8_t version_ihl;
	uint8_t tos;
	uint16_t tot_len;
	uint16_t id;
	uint16_t frag_off;	
	uint8_t ttl;
	uint8_t protocol;
	uint16_t check;
	uint32_t saddr;
	uint32_t daddr;
}ip_header_t;
/*
typedef struct
{
	uint8_t pdu_type;		//4bits
	uint8_t spare;			//2bits
	uint8_t final_frame;	//1bit
	uint8_t lost_pkt_rpt;	//1bit
	uint32_t highest_xw_seq_num;	//24bits
	uint32_t desired_buffer;//32bits
	uint32_t min_buffer;	//32bits
	uint8_t lost_range;		//8bits
	uint32_t start_of_lost[255];	//24bits
	uint32_t end_of_lost[255];	//24bits
	uint32_t			rcv_wlan_pkt;
	uint32_t			rcv_lte_pkt;
	uint32_t paddint;		//0~3bytes
}xw_rpt_pkt;
*/
typedef struct 
{
    uint8_t  tx_diff;
	long     rx_diff;
	timespec rx_time;
}diff_time_t;

class lwipep: public srslte::timer_callback
{
public:
	lwipep();
	void init(srsue::pdcp_interface_lwipep *pdcp_,
			  srsue::tunnel_interface_lwipep *tunnel_,
			  srsue::rrc_interface_lwipep *rrc_,
			  srslte::mac_interface_timers *mac_timers_,
			  srsue::gw_interface_lwipep *gw_,
			  log *lwipep_log_,
			  uint32_t lcid_, uint16_t rnti_);

	
	
	void write_pdu(uint32_t lcid, srslte::byte_buffer_t* pdu, uint32_t net_if);
	void write_sdu(uint32_t lcid, srslte::byte_buffer_t* sdu);
	
	bool lwipep_read_gre_header(srslte::byte_buffer_t *pdu, gre_header_t *gre_header, srslte::log *lwipep_log);
	void lwipep_build_gre_header(uint32_t packet_sn, gre_header_t *gre_header, gre_header_flag_t *gre_header_flag);
	void lwipep_write_gre_header(gre_header_t *gre_header, srslte::byte_buffer_t *sdu, srslte::log *lwipep_log);
	
	bool lwipep_read_ip_header(srslte::byte_buffer_t *pdu, ip_header_t *ip_header, srslte::log *lwipep_log);
	void lwipep_build_ip_header(ip_header_t *ip_header, srslte::byte_buffer_t *sdu);
	void lwipep_write_ip_header(ip_header_t *ip_header, srslte::byte_buffer_t *sdu);
	
	//for xw
	bool lwipep_read_gtp_header(srslte::byte_buffer_t *pdu, gre_header_t *header, srslte::log *lwipep_log);
	bool lwipep_write_gtp_header(gre_header_t *header, srslte::byte_buffer_t *sdu, srslte::log *lwipep_log);
	bool lwipep_build_gtp_header(uint32_t packet_sn, gre_header_t *gre_header, gre_header_flag_t *gre_header_flag);
	void rntilcid_to_teidin(uint16_t lcid, uint32_t *teidin);
	void teidin_to_rntilcid(uint32_t teidin, uint16_t *rnti, uint16_t *lcid);

	//for ipsec
	bool lwipep_read_ipsec_header(srslte::byte_buffer_t *pdu, gre_header_t *header, srslte::log *lwipep_log);
	bool lwipep_write_ipsec_header(gre_header_t *header, srslte::byte_buffer_t *sdu, srslte::log *lwipep_log);
	
//===================================================================================================//
//============================================SETTING================================================//
//===================================================================================================//
	void stop();
	void set_lwipep_auto_control_ratio(bool dl_auto, bool ul_auto);
	void set_lwipep_wlan(bool dl_aggregate, bool ul_aggregate);
	void lwipep_state();
	void set_lwipep_tx_ratio(uint32_t lte_tx_ratio, uint32_t wlan_ratio);
	void set_lwipep_reorder_buffer_size(uint32_t buffer_size);
	void set_lwipep_report_period(uint32_t report_period);
	void set_reorder_timeout(uint32_t reorder_timeout);
	void set_rnti(uint16_t rnti_);
	void set_addr(uint32_t saddr_, uint32_t daddr_);
	
private:
	pthread_mutex_t			mutex;
	
	srslte::log             *lwipep_log;
	srslte::byte_buffer_pool *pool;
	std::map<uint32_t, route_t> tx_route;
	std::map<uint32_t, route_t> rx_route;
	std::map<uint32_t, byte_buffer_t *> rx_window;
	
	const static uint32_t BUFFER_SIZE = (1 << 12);
	const static uint32_t WINDOWS_SIZE = (1 << 11);			//2048
	
	srslte::timers::timer	*lwipep_reordering;
	uint32_t				lwipep_reordering_id;
	int32_t					lwipep_reordering_time;
	
	srslte::timers::timer	*lwipep_report;
	uint32_t				lwipep_report_id;
	int32_t					lwipep_report_time;				//1ms, 1000ms = 1s
	
	srslte::timers::timer	*lwipep_config;
	uint32_t				lwipep_config_id;
	int32_t					lwipep_config_time;				//1ms, 1000ms = 1s
	

	bool 		enable_aggregate;			//all false that using lte only
	bool 		enable_lwipep_dl_aggregate;
	bool 		enable_lwipep_ul_aggregate;
	
	bool 		enable_lwipep_write_pdu_aggregate;
	bool 		enable_lwipep_write_sdu_aggregate;
	bool		enable_lwipep_reordering;
	bool		enable_lwipep_measurement_report;
	bool		auto_control_ratio;
	bool		enable_audo_control_lwipep_write_pdu_ratio;
	bool		enable_audo_control_lwipep_write_sdu_ratio;
	bool		enable_lwipep_discard_delay_packet;
	bool		enable_lwipep_discard_duplicate;
	bool		enable_ema;
	
	const static float  MAX_RATIO		= 256;
	const static float  rate_init_lte	= 60000000; //60Mbps
	const static float  rate_init_wlan	= 500000000; //500Mbps
	
	uint32_t			lcid; // default LCID that is maintained active by PDCP instance
	uint16_t			rnti; 
	
	uint32_t			saddr;
	uint32_t			daddr;
	
	uint32_t            submitted_sn;
	uint32_t			current_max_sn;
	uint32_t            next_sn;
	uint32_t			delay_sn;
	uint32_t			reordering_sn;
	uint32_t			rcv_wlan_pkt;
	uint32_t			rcv_lte_pkt;
	uint32_t			highest_xw_seq_num;
	uint32_t			last_highest_xw_seq_num;
	uint32_t			lost_range;

	
	uint32_t			last_handle_current_max_sn;
	uint32_t            alpha_part;
	uint32_t            alpha_whole;
	uint32_t            ema_part;
	uint32_t            ema_whole;
	
	uint32_t            delayed_cnt;
	uint32_t            duplicate_cnt;

	uint32_t            tx_packet_count;
	uint32_t            rx_packet_count;
	
	uint32_t            lte_half_tx_ratio;
	uint32_t            lte_tx_ratio;
	uint32_t            lte_tx_ratio_count;
	uint32_t            lte_tx_bytes;
	uint32_t            lte_tx_packet_count;
	
	uint32_t            wlan_half_tx_ratio;
	uint32_t            wlan_tx_ratio;
	uint32_t            wlan_tx_ratio_count;
	uint32_t            wlan_tx_bytes;
	uint32_t            wlan_tx_packet_count;
	uint32_t            base_tx_ratio;
	
	uint32_t            total_tx_ratio;
	
	
	uint32_t            lte_ratio_rx;
	uint32_t            lte_rx_bytes;
	uint32_t            lte_rx_packet_count;
	uint32_t            wlan_ratio_rx;
	uint32_t            wlan_rx_bytes;
	uint32_t            wlan_rx_packet_count;
	uint32_t			total_rx_count;
	uint32_t            base_rx_ratio;
	
	uint32_t            reconfigure_sn;
	
	uint32_t            lte_ratio_count;
	uint32_t            wlan_ratio_count;
	
	uint32_t			report_period;
	uint32_t			report_count;
	struct timespec     timestamp_time[3];
	struct timespec     report_time[3];
	std::map<uint32_t, diff_time_t> diff_time;
	
	srsue::pdcp_interface_lwipep	*pdcp;
	srsue::tunnel_interface_lwipep	*tunnel;		//xw or ipsec
	srsue::rrc_interface_lwipep		*rrc;
	srslte::mac_interface_timers	*mac_timers;
	srsue::gw_interface_lwipep		*gw;
//	void lwipep::write_sdu_aggregate(uint32_t lcid, srslte::byte_buffer_t* sdu);
//	void lwipep::write_pdu_aggregate(uint32_t lcid, srslte::byte_buffer_t* pdu);
	
	void timer_expired(uint32_t timer_id);
	void measurement_report();
	void handle_lwipep_report(byte_buffer_t *pdu);
	bool inside_reordering_window(uint32_t sn);
	bool equal_current_max_sn(uint32_t sn);
	bool over_current_max_sn(uint32_t sn);
	bool delay_packet(uint32_t sn);
	bool is_lwipep_report(byte_buffer_t *pdu);
	route_t get_route();
	uint32_t get_gcd(uint32_t x, uint32_t y);
	
	
	inline void uint8_to_uint32(uint8_t *buf, uint32_t *i)
	{
		*i =  (uint32_t)buf[0] << 24 |
			  (uint32_t)buf[1] << 16 |
			  (uint32_t)buf[2] << 8  |
			  (uint32_t)buf[3];
	}

	inline void uint32_to_uint8(uint32_t i, uint8_t *buf)
	{
		buf[0] = (i >> 24) & 0xFF;
		buf[1] = (i >> 16) & 0xFF;
		buf[2] = (i >> 8) & 0xFF;
		buf[3] = i & 0xFF;
	}

	inline void uint8_to_uint16(uint8_t *buf, uint16_t *i)
	{
		*i =  (uint32_t)buf[0] << 8  |
			  (uint32_t)buf[1];
	}

	inline void uint16_to_uint8(uint16_t i, uint8_t *buf)
	{
		buf[0] = (i >> 8) & 0xFF;
		buf[1] = i & 0xFF;
	}
	
	void get_time_interval(struct timeval * tdata);
	void get_timestamp_interval(struct timespec * tdata);
	
	uint16_t in_cksum(uint16_t *addr, int len);
};

}//namespace

#endif
