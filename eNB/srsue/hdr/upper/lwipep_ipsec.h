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



#ifndef SRSUE_LWIPEP_IPSEC_H
#define SRSUE_LWIPEP_IPSEC_H

#include <map>
#include "srslte/common/buffer_pool.h"
#include "srslte/common/log.h"
#include "srslte/common/common.h"
#include "srslte/common/threads.h"
#include "srslte/common/interfaces_common.h"
#include "srslte/interfaces/enb_interfaces.h"
#include "srslte/interfaces/ue_interfaces.h"



#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>


#include <arpa/inet.h>
#include <linux/if_packet.h>

#define WIFI_IF		"wlp3s0"

#define ETH_HEADER_LEN 14				//14 BYTE
#define IP_HEADER_LEN 20				//20 BYTE

//ETH_HEADER_LEN 14				
//IP_HEADER_LEN 20				
//GRE_HEADER_LEN 12			
//IP_HEADER_LEN 20				
//TCP_HEADER_LEN 20		

#define ETH_TYPE_IP		0x0800
#define IP_TYPE_GRE		0x002F


namespace srsue {

typedef struct {
  uint32_t      enb_id;     // 20-bit id (lsb bits)
  uint8_t       lwipep_wlan_id;    // 8-bit cell id
  uint16_t      tac;        // 16-bit tac
  uint16_t      mcc;        // BCD-coded with 0xF filler
  uint16_t      mnc;        // BCD-coded with 0xF filler
  std::string   mme_addr;
  std::string   gtp_bind_addr;
  std::string   s1c_bind_addr;
  std::string   enb_name;
  std::string	wlan_name;
}ipsec_args_t;

typedef struct {
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

//,public srsue::tunnel_interface_lwipep
class lwipep_ipsec:	public thread,
					public lwipep_ipsec_interface_lwipep		//public srsue::lwipep_ipsec_interface_lwipep_segw
{
public:
	lwipep_ipsec();
	void			init(srsue::lwipep_interface_lwipep_ipsec *lwipep_, /*, lwipep_spgw_interface_ipsec *lwipep_spgw_,*/ srslte::log *lwipep_ipsec_log_);
	void			stop();
	void			add_user(uint16_t rnti);
	void			write_sdu(uint32_t lcid, srslte::byte_buffer_t *sdu);	//tunnel_interface_lwipep
	void			write_pdu(uint32_t lcid, srslte::byte_buffer_t *pdu);
	
	void			run_thread();

private:
	uint8_t DST_MAC0;
	uint8_t DST_MAC1;
	uint8_t DST_MAC2;
	uint8_t DST_MAC3;
	uint8_t DST_MAC4;
	uint8_t DST_MAC5;
	
	uint8_t SRC_MAC0 ;
	uint8_t SRC_MAC1 ;
	uint8_t SRC_MAC2 ;
	uint8_t SRC_MAC3 ;
	uint8_t SRC_MAC4 ;
	uint8_t SRC_MAC5 ;
	
	unsigned char   SRC_MAC[ETH_ALEN] = {0x00,0x00,0x00,0x00,0x00,0x00};
	unsigned char   DST_MAC[ETH_ALEN] = {0x00,0x00,0x00,0x00,0x00,0x00};
	
	static const int S1AP_THREAD_PRIO = 65;
	static const int MME_PORT         = 36412;
	static const int ADDR_FAMILY      = AF_INET;
	static const int SOCK_TYPE        = SOCK_STREAM;
	static const int PROTO            = IPPROTO_SCTP;
	static const int PPID             = 18;
	static const int NONUE_STREAM_ID  = 0;
	
	struct ifreq 	if_req;
	struct ifreq 	if_idx;
	struct ifreq 	if_mac;
	
	bool	mme_connected;
	int		socket_fd;						
	int     socket_opt;
	struct	sockaddr_in		lwipep_spgw_addr;		
//	struct	timeval			metrics_time[3];
	
	long                ul_tput_bytes;
	long                dl_tput_bytes;
	
	/*
	int sock_success = 0;
	
	struct ifreq	req;
	struct ifreq 	if_mac;
	int		socket_fd;
	int		socket_flag = 0;
	int		socket_opt = 0;
	char	*wifi_dev;
	*/
	
	bool	enable_lwipep_ipsec_rcv_pkt;
	bool	lwipep_ipsec_rcv_pkt_running;
	static const int 	LWIPEP_IPSEC_THREAD_PRIO = 7;
	
	srslte::log					*lwipep_ipsec_log;
	srslte::byte_buffer_pool	*pool;
	
	lwipep_interface_lwipep_ipsec					*lwipep;
	//lwipep_segw_interface_lwipep_ipsec			*lwipep_segw;	
	
	bool lwipep_ipsec_read_eth_header(srslte::byte_buffer_t *pdu, uint16_t *rnti, uint16_t *drbid);
	
	void lwipep_ipsec_build_ip_header(ip_header_t *ip_header, srslte::byte_buffer_t *sdu);
	
	void lwipep_ipsec_build_eth_header(ethhdr *eth_header);
	
	void lwipep_ipsec_write_eth_header(ethhdr *eth_header, srslte::byte_buffer_t *sdu);
	
	void teidin_to_rntilcid(uint32_t teidin, uint16_t *rnti, uint16_t *lcid);
	
	inline void uint8_to_uint32(uint8_t *buf, uint32_t *i)
	{
		*i =  (uint32_t)buf[0] << 24 |
			  (uint32_t)buf[1] << 16 |
			  (uint32_t)buf[2] << 8  |
			  (uint32_t)buf[3];
	};

	inline void uint32_to_uint8(uint32_t i, uint8_t *buf)
	{
		buf[0] = (i >> 24) & 0xFF;
		buf[1] = (i >> 16) & 0xFF;
		buf[2] = (i >> 8) & 0xFF;
		buf[3] = i & 0xFF;
	};

	inline void uint8_to_uint16(uint8_t *buf, uint16_t *i)
	{
		*i =  (uint32_t)buf[0] << 8  |
			  (uint32_t)buf[1];
	};

	inline void uint16_to_uint8(uint16_t i, uint8_t *buf)
	{
		buf[0] = (i >> 8) & 0xFF;
		buf[1] = i & 0xFF;
	};
	
	uint16_t in_cksum(uint16_t *addr, int len);
	

	
	/*
	class user_interface_lwipep : public srsue::lwipep_interface_lwipep_ipsec
	{
	public:
		uint16_t rnti; 
		srsue::lwipep_interface_lwipep_ipsec *lwipep; 
		void write_pdu(uint32_t lcid,  srslte::byte_buffer_t *sdu);
	};
	
	class user_interface_lwipep_spgw : public srsue::lwipep_spgw_interface_lwipep_ipsec
	{
	public:
		uint16_t rnti; 
		srsue::lwipep_spgw_interface_lwipep_ipsec *lwipep_spgw; 
		void write_sdu(uint32_t lcid,  srslte::byte_buffer_t *sdu);
	};
	
	class user_interface 
	{
	public: 
		user_interface_lwipep		lwipep_itf; 
		user_interface_lwipep_spgw	lwipep_spgw_itf;
		srsue::lwipep_ipsec			*lwipep_ipsec; 
	};
	
	std::map<uint32_t,user_interface> users;
	
	/*
	class user_interface_lwipep : public srsue::lwipep_interface_lwipep_ipsec
	{
	public:
		uint16_t rnti; 
		srsue::lwipep_interface_lwipep_ipsec *lwipep; 
		void write_pdu(uint32_t lcid,  srslte::byte_buffer_t *sdu);
	};
	
	
	
	class user_interface 
	{
	public: 
		user_interface_lwipep		lwipep_itf;
		user_interface_lwipep_spgw	lwipep_spgw_itf;
	}; 
	*/
};


/*
class lwipep_ipsec_interface_lwipep_segw: public thread
{
public:
	void			init();
	void			stop();
	void			write_pdu();
	void			send_to_lwipep();
	void			receive_from_lwipep();
	
private:
	void			run_thread();
	lwipep_segw_interface_lwipep_ipsec				lwipep_segw;	
	
	static const int S1AP_THREAD_PRIO = 65;
	static const int MME_PORT         = 36412;
	static const int ADDR_FAMILY      = AF_INET;
	static const int SOCK_TYPE        = SOCK_STREAM;
	static const int PROTO            = IPPROTO_SCTP;
	static const int PPID             = 18;
	static const int NONUE_STREAM_ID  = 0;
	
	static const int 	LWIPEP_ipsec_THREAD_PRIO = 7;
	struct ifreq 	if_req;
	struct ifreq 	if_idx;
	struct ifreq 	if_mac;
	int 			sockfd;
	int				sockopt;

	rrc_interface_s1ap    *rrc;
	s1ap_args_t            args;
	srslte::log           *s1ap_log;
	srslte::byte_buffer_pool   *pool;

	bool      mme_connected;
	bool      running;
	int       socket_fd;						// SCTP socket file descriptor
	struct    sockaddr_in lwipep_segw_addr;		// lwipep_segw address
	
	std::map<uint32_t, ipsec_args_t>				users;
	srslte::byte_buffer_pool					*pool;
};
*/
}//srsue

#endif
