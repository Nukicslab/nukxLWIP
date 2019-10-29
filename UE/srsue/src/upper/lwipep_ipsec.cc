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


#include "srsue/hdr/upper/lwipep_ipsec.h"

namespace srsue 
{

	lwipep_ipsec::lwipep_ipsec()
	{
		DST_MAC0 = 0;
		DST_MAC1 = 0;
		DST_MAC2 = 0;
		DST_MAC3 = 0;
		DST_MAC4 = 0;
		DST_MAC5 = 0;
														//uec 8, 4,     3,    , 2,  , 1   , 1/2,  , 1/3  , 1/4,   , 1/8    , ueb 8,     4,  , 2,   , 1,   , 1/2,  , 1/4    , 1/8,		1/3,	3
		interference_ratio = 0;						//21:0 1:384, 2:180, 15:144, 3:90, 4:45, 5:22.5, 18:16, 6:11.25, 7:5.625, 8:120, 9:60, 10:30, 11:18, 12:7.5, 13:3.75, 14:1.87, 16:5.17, 17:46.5
		next_sn = 0;
		buffer_sn = 0;
		speed_limit = 0;
		enable_lwipep_ipsec_send_limit = true;
		enable_lwipep_ipsec_rcv_pkt = true;
		lwipep_ipsec_rcv_pkt_running = true;
		lwipep_ipsec_limit_config_time = 1;
	}
	void lwipep_ipsec::init(srsue::lwipep_interface_lwipep_ipsec *lwipep_, srslte::mac_interface_timers *mac_timers_, srslte::log *lwipep_ipsec_log_)
	{
		pool    			= srslte::byte_buffer_pool::get_instance();
		mac_timers			= mac_timers_;
		lwipep_ipsec_limit_config_id = mac_timers->timer_get_unique_id();
		lwipep_ipsec_limit_config = mac_timers->timer_get(lwipep_ipsec_limit_config_id);
		lwipep_ipsec_limit_config->set(this, lwipep_ipsec_limit_config_time);
		lwipep_ipsec_limit_config->run();
		pthread_mutex_init(&mutex, NULL);
		
		lwipep				= lwipep_;
		lwipep_ipsec_log	= lwipep_ipsec_log_;
		
		lwipep->get_mac(&DST_MAC0, &DST_MAC1, &DST_MAC2, &DST_MAC3, &DST_MAC4, &DST_MAC5);


//		gettimeofday(&metrics_time[1], NULL);
		dl_tput_bytes = 0;
		ul_tput_bytes = 0;

		memset(&if_req, 0, sizeof(struct ifreq));

		// Open PF_PACKET socket, listening for EtherType ETHER_TYPE_WIFI
		if ((socket_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_TYPE_IP))) == -1) {
			lwipep_ipsec_log->error("listener: socket");	
			return;
		}

		// Set interface to promiscuous mode
		strncpy(if_req.ifr_name, WIFI_IF, IFNAMSIZ - 1);
		ioctl(socket_fd, SIOCGIFFLAGS, &if_req);
		if_req.ifr_flags |= IFF_PROMISC;
		ioctl(socket_fd, SIOCSIFFLAGS, &if_req);
		// Allow the socket to be reused - incase connection is closed prematurely
		if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof socket_opt) == -1) {
			lwipep_ipsec_log->error("setsockopt");
			close(socket_fd);
			return;
		}
		// Bind to device
		if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, WIFI_IF, IFNAMSIZ - 1) == -1)	{
			lwipep_ipsec_log->error("SO_BINDTODEVICE");
			close(socket_fd);
			return;
		}
		
		// Get the index of the interface to send on
		memset(&if_idx, 0, sizeof(struct ifreq));
		strncpy(if_idx.ifr_name, WIFI_IF, IFNAMSIZ - 1);
		if (ioctl(socket_fd, SIOCGIFINDEX, &if_idx) < 0) {
			perror("SIOCGIFINDEX");
			exit(EXIT_FAILURE);
		}

		// Get the MAC address of the interface to send on
		memset(&if_mac, 0, sizeof(struct ifreq));
		strncpy(if_mac.ifr_name, WIFI_IF, IFNAMSIZ - 1);
		if (ioctl(socket_fd, SIOCGIFHWADDR, &if_mac) < 0) {
			perror("SIOCGIFHWADDR");
			exit(EXIT_FAILURE);
		}
		
		SRC_MAC0 = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
		SRC_MAC1 = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
		SRC_MAC2 = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
		SRC_MAC3 = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
		SRC_MAC4 = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
		SRC_MAC5 = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];
		
		lwipep_ipsec_log->console("lwipep MAC %x:%x:%x:%x:%x:%x\n",
						   ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0],
						   ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1],
						   ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2],
						   ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3],
						   ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4],
						   ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5]);

		start(LWIPEP_IPSEC_THREAD_PRIO);
	}
	
	void lwipep_ipsec::timer_expired(uint32_t timer_id)
	{
		if(lwipep_ipsec_limit_config_id == timer_id)
		{
			sockaddr_ll ueaddr;
			uint32_t	pkt_limit = 0;
			uint32_t	pkt_per_ms = 0;
			lwipep_ipsec_limit_config_time = 1;
			pthread_mutex_lock(&mutex);
			if (enable_lwipep_ipsec_send_limit == true)
			{
				switch(interference_ratio)
				{
					case 0:
						pkt_per_ms = 106;
						break;
					case 21:
						pkt_per_ms = 1;
						lwipep_ipsec_limit_config_time = 1000;
						break;
					case 1:		//360	pkt_per_ms = 31;		384	pkt_per_ms = 34;			SHOULD BE 32, BUT NOT ENOUGH, pkt_per_ms = 34 BE 388Mbps  	
						pkt_per_ms = 34;
						break;
					case 2:		//194
						pkt_per_ms = 17;
						break;
					case 3:		//97
						if (speed_limit == 1)		//8.5
						{
							pkt_per_ms = 9;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 8;
							speed_limit++;
						}	
						break;
					case 4:		//45		48
						if (speed_limit == 4)		//8.5
						{
							pkt_per_ms = 5;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 4;
							speed_limit++;
						}
						break;
					case 5:		//22.5		24  2.1
						if (speed_limit == 9)		//8.5
						{
							pkt_per_ms = 3;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 2;
							speed_limit++;
						}	
						break;
					case 6:		//11.25
						pkt_per_ms = 1;
						break;
					case 7:		//5.625
						pkt_per_ms = 1;
						lwipep_ipsec_limit_config_time = 2;
						break;
					case 8:		//120
						if (speed_limit == 1)		//10.5
						{
							pkt_per_ms = 10 + 1;					
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 10;
							speed_limit = 1;
						}
						break;
					case 9:		//60
						if (speed_limit == 2)		//5.24
						{
							pkt_per_ms = 5 + 1;					
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 5;
							speed_limit++;
						}
						break;
					case 10:		//30
						if (speed_limit == 2)		//2.6
						{
							pkt_per_ms = 2;					
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 2 + 1;
							speed_limit++;
						}
						break;
					case 11:		//16			pkt_per_ms = 1.3
						if (speed_limit == 2)		//2.6
						{
							pkt_per_ms = 2;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 1;
							speed_limit++;
						}
						break;
					case 12:		//7.5			pkt_per_ms = 0.65
						if (speed_limit == 2)		//2.6
						{
							pkt_per_ms = 2;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 1;
							speed_limit++;
						}				
						lwipep_ipsec_limit_config_time = 2;
						break;
					case 13:							//3.75
						pkt_per_ms = 1;					//0.33
						lwipep_ipsec_limit_config_time = 3;
						break;
					case 14:							//1.875
						pkt_per_ms = 1;					//0.16
						lwipep_ipsec_limit_config_time = 6;
						break;
					case 15:							//144
						if (speed_limit == 1)		//12.725
						{
							pkt_per_ms = 13;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 12;
							speed_limit++;
						}	
						break;
					case 16:		//7.5			pkt_per_ms = 0.65
						if (speed_limit == 1)		//2.6
						{
							pkt_per_ms = 2;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 1;
							speed_limit++;
						}
						lwipep_ipsec_limit_config_time = 3;
						break;
					case 17:		//46.5			pkt_per_ms = 0.65
						pkt_per_ms = 4;
					break;
					case 18:		//15			pkt_per_ms = 1.3
						if (speed_limit == 1)		//2.6
						{
							pkt_per_ms = 2;
							speed_limit = 0;
						}
						else
						{
							pkt_per_ms = 1;
							speed_limit++;
						}
					break;
					case 19:		//150			pkt_per_ms = 13.1
						pkt_per_ms = 13;
					break;
						
				}
				
	//			lwipep_ipsec_log->console("outside while %d\n", next_sn);
				while(pkt_limit < pkt_per_ms && tx_window.count(next_sn))
				{
					pkt_limit = pkt_limit + 1;
	//				lwipep_ipsec_log->console("while buffer_sn = %d,	N_bytes = %d\n", next_sn, tx_window[next_sn]->N_bytes);
					// Index of the network device
					ueaddr.sll_ifindex = if_idx.ifr_ifindex;
					// Address length
					ueaddr.sll_halen = ETH_ALEN;
					// Destination MAC
					ueaddr.sll_addr[0] = DST_MAC0;
					ueaddr.sll_addr[1] = DST_MAC1;
					ueaddr.sll_addr[2] = DST_MAC2;
					ueaddr.sll_addr[3] = DST_MAC3;
					ueaddr.sll_addr[4] = DST_MAC4;
					ueaddr.sll_addr[5] = DST_MAC5;

	//				ul_tput_bytes += sdu->N_bytes;
					
					// Send packet
					if (sendto(socket_fd, tx_window[next_sn]->msg, tx_window[next_sn]->N_bytes, MSG_EOR, (struct sockaddr*)&ueaddr, sizeof(struct sockaddr_ll)) < 0) 
					{
						perror("sendto");
						lwipep_ipsec_log->console("sendto l %d\n", tx_window[next_sn]->N_bytes);
					}
					pool->deallocate(tx_window[next_sn]);
					tx_window.erase(next_sn);
					next_sn = (next_sn + 1) % BUFFER_SIZE;
				//	lwipep_ipsec_log->console("w");
				}
				pthread_mutex_unlock(&mutex);
				lwipep_ipsec_limit_config->set(this, lwipep_ipsec_limit_config_time);
				lwipep_ipsec_limit_config->run();
			}
		}
		
	}
	
	void lwipep_ipsec::tunnel_speed_limit(uint32_t limit)
	{
		pthread_mutex_lock(&mutex);
			if (enable_lwipep_ipsec_send_limit == 0)
			{	
				interference_ratio = limit;	
			
			}
			else
			{	
				
				enable_lwipep_ipsec_send_limit = true;
				interference_ratio = limit;	
				lwipep_ipsec_limit_config->set(this, lwipep_ipsec_limit_config_time);
				lwipep_ipsec_limit_config->run();
			}
		pthread_mutex_unlock(&mutex);
	}
	
	void lwipep_ipsec::stop()
	{
		if(enable_lwipep_ipsec_rcv_pkt)
		{
			enable_lwipep_ipsec_rcv_pkt = false;

			close(socket_fd);
			
			// Wait thread to exit gracefully otherwise might leave a mutex locked
			int cnt=0;
			while(lwipep_ipsec_rcv_pkt_running && cnt<100) {
				usleep(10000);
				cnt++;
			}
			if (lwipep_ipsec_rcv_pkt_running) {
				thread_cancel();
			}
			wait_thread_finish();
		}
	}
	
	void lwipep_ipsec::write_sdu(uint32_t lcid, srslte::byte_buffer_t *sdu)
	{
		// Ethernet header
		ethhdr eth_header;
		
		lwipep_ipsec_build_eth_header(&eth_header);
		lwipep_ipsec_write_eth_header(&eth_header, sdu);
		
		pthread_mutex_lock(&mutex);
		
		
		if (enable_lwipep_ipsec_send_limit == true && tx_window.count(buffer_sn))
		{
			pool->deallocate(sdu);	
		//	lwipep_ipsec_log->console("drop buffer_sn %d\n", buffer_sn);
		
		}
		else if (enable_lwipep_ipsec_send_limit == true)
		{
			tx_window[buffer_sn] = sdu;
			buffer_sn = (buffer_sn + 1) % BUFFER_SIZE;
//			lwipep_ipsec_log->console("buffer_sn %d\n", buffer_sn);
		}
		else if (enable_lwipep_ipsec_send_limit == false)
		{
			sockaddr_ll ueaddr;
			while(tx_window.count(next_sn))
			{
				//				lwipep_ipsec_log->console("while buffer_sn = %d,	N_bytes = %d\n", next_sn, tx_window[next_sn]->N_bytes);
				// Index of the network device
				ueaddr.sll_ifindex = if_idx.ifr_ifindex;
				// Address length
				ueaddr.sll_halen = ETH_ALEN;
				// Destination MAC
				ueaddr.sll_addr[0] = DST_MAC0;
				ueaddr.sll_addr[1] = DST_MAC1;
				ueaddr.sll_addr[2] = DST_MAC2;
				ueaddr.sll_addr[3] = DST_MAC3;
				ueaddr.sll_addr[4] = DST_MAC4;
				ueaddr.sll_addr[5] = DST_MAC5;

				//				ul_tput_bytes += sdu->N_bytes;
				
				// Send packet
				if (sendto(socket_fd, tx_window[next_sn]->msg, tx_window[next_sn]->N_bytes, MSG_EOR, (struct sockaddr*)&ueaddr, sizeof(struct sockaddr_ll)) < 0) 
				{
					perror("sendto");
					lwipep_ipsec_log->console("sendto l %d\n", tx_window[next_sn]->N_bytes);
				}
				pool->deallocate(tx_window[next_sn]);
				tx_window.erase(next_sn);
				next_sn = (next_sn + 1) % BUFFER_SIZE;
				
			}
			
	//		lwipep_ipsec_log->console("s");
			// Index of the network device
			ueaddr.sll_ifindex = if_idx.ifr_ifindex;
			// Address length
			ueaddr.sll_halen = ETH_ALEN;
			// Destination MAC
			ueaddr.sll_addr[0] = DST_MAC0;
			ueaddr.sll_addr[1] = DST_MAC1;
			ueaddr.sll_addr[2] = DST_MAC2;
			ueaddr.sll_addr[3] = DST_MAC3;
			ueaddr.sll_addr[4] = DST_MAC4;
			ueaddr.sll_addr[5] = DST_MAC5;

			ul_tput_bytes += sdu->N_bytes;

			// Send packet
			if (sendto(socket_fd, sdu->msg, sdu->N_bytes, MSG_EOR, (struct sockaddr*)&ueaddr, sizeof(struct sockaddr_ll)) < 0) 
			{
				//	perror("sendto");
				//	lwipep_ipsec_log->console("sendto l %d\n", sdu->N_bytes);
			}
			pool->deallocate(sdu);
		}
		
		
		pthread_mutex_unlock(&mutex);

	}

	void lwipep_ipsec::run_thread()
	{
		int32                   N_bytes;
		uint16_t                drbid;		//drbid = lcid
		uint16_t 				rnti;
		srslte::byte_buffer_t  *pdu = pool_allocate;

		
		lwipep_ipsec_log->info("lwipep IP packet receiver thread run_enable\n");

		lwipep_ipsec_rcv_pkt_running = true;
		while(enable_lwipep_ipsec_rcv_pkt)
		{
			pdu->reset();
			do
			{
				N_bytes = recvfrom(socket_fd, pdu->msg, SRSLTE_MAX_BUFFER_SIZE_BYTES-SRSLTE_BUFFER_HEADER_OFFSET, 0, NULL, NULL);
			} while (N_bytes < 50);			//74
			//tcp/udp + ip + gre + ip + eth
			//20/8 + 20 + 12 + 20 + 14 = 74
			if(N_bytes > 73)
			{
				pdu->N_bytes = (uint32_t) N_bytes;

				// Check the packet is for me
				if (lwipep_ipsec_read_eth_header(pdu, &rnti, &drbid)) 
				{        
					//lwipep_ipsec_log->console("RX WLAN\n");
					// Send PDU directly to PDCP
					pdu->set_timestamp();
					dl_tput_bytes += N_bytes;
					lwipep->write_pdu(drbid, pdu);
					//pdu_count = pdu_count + 1;
					//lwipep_ipsec_log->console("RX WLAN END\n");
					do 
					{
						pdu = pool_allocate;
						
						if (!pdu) 
						{
							printf("Not enough buffers in pool\n");
							usleep(100000);
						}
					} while(!pdu);
					
				}				
			}
			else
			{	lwipep_ipsec_log->error("Failed to read from TUN interface - lwipep receive thread exiting.\n");	}
			/*			
			   if (pdu_count % 10000 == 0 && pdu_count > 1)
				lwipep_ipsec_log->console("ipsec pool_allocate %d\n", pdu_count);
			*/			
		}
		lwipep_ipsec_rcv_pkt_running = false;
		lwipep_ipsec_log->info("lwipep IP receiver thread exiting.\n");
	}
	/*
	void lwipep_ipsec::add_user(uint16_t rnti)
	{	
		if (users.count(rnti) == 0) 
		{
			srsenb::lwipep_ipsec *obj = new srsenb::lwipep_ipsec;     
			obj->init(&users[rnti].lwipep_itf.lwipep,
					  &users[rnti].lwipep_spgw_itf.lwipep_spgw,
					  log_h);
			
			users[rnti].lwipep_itf.rnti 		= rnti;
			users[rnti].lwipep_spgw_itf.rnti	= rnti;
			
			users[rnti].lwipep_itf.lwipep			= lwipep;
			users[rnti].lwipep_spgw_itf.lwipep_spgw	= lwipep_spgw;
			users[rnti].lwipep_ipsec = obj;
		}
	}
	*/
	
//================================================================================================//
//=============================================HEADER=============================================//
//================================================================================================//
	
	void lwipep_ipsec::lwipep_ipsec_build_ip_header(ip_header_t *ip_header, srslte::byte_buffer_t *sdu)
	{
		ip_header->version_ihl = 4 << 2 | sizeof(ip_header_t) ;
		ip_header->tos = 0;
		ip_header->tot_len = __constant_htons(sdu->N_bytes);
		ip_header->id = htons(0);
		ip_header->frag_off = 0;
		ip_header->ttl = 0x40;
		ip_header->protocol = __constant_htons(IP_TYPE_GRE);
		//ip_header->saddr = in_aton("10.41.0.3");				//fixed_ip_header->saddr = ip_chain_node->ifa_local;
		//ip_header->daddr = in_aton("10.42.0.1");
		ip_header->saddr = inet_addr("192.168.50.194");
		ip_header->daddr = inet_addr("192.168.50.49");
		
		ip_header->check = 0;
		ip_header->check = in_cksum((unsigned short *)ip_header, sizeof(ip_header_t));
		//ip_header->check = ip_fast_csum((unsigned char*)ip_header, sizeof(ip_header_t));	//ip_header->ihl
		
		return;
	}
	
	void lwipep_ipsec::lwipep_ipsec_build_eth_header(ethhdr *eth_header)
	{
		eth_header->h_source[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
		eth_header->h_source[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
		eth_header->h_source[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
		eth_header->h_source[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
		eth_header->h_source[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
		eth_header->h_source[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];
		eth_header->h_dest[0] = DST_MAC0;
		eth_header->h_dest[1] = DST_MAC1;
		eth_header->h_dest[2] = DST_MAC2;
		eth_header->h_dest[3] = DST_MAC3;
		eth_header->h_dest[4] = DST_MAC4;
		eth_header->h_dest[5] = DST_MAC5;
		
		eth_header->h_proto = __constant_htons(ETH_TYPE_IP);
		return;
	}
	
	void lwipep_ipsec::lwipep_ipsec_write_eth_header(ethhdr *eth_header, srslte::byte_buffer_t *sdu)
	{
		
		sdu->msg      -= ETH_HEADER_LEN;
		sdu->N_bytes  += ETH_HEADER_LEN;

		uint8_t *ptr = sdu->msg;

		*ptr        = eth_header->h_dest[0];
		*(ptr + 1)  = eth_header->h_dest[1];
		*(ptr + 2)  = eth_header->h_dest[2];
		*(ptr + 3)  = eth_header->h_dest[3];
		*(ptr + 4)  = eth_header->h_dest[4];
		*(ptr + 5)  = eth_header->h_dest[5];
		*(ptr + 6)  = eth_header->h_source[0];
		*(ptr + 7)  = eth_header->h_source[1];
		*(ptr + 8)  = eth_header->h_source[2];
		*(ptr + 9)  = eth_header->h_source[3];
		*(ptr + 10) = eth_header->h_source[4];
		*(ptr + 11) = eth_header->h_source[5];
		ptr += 12;

		*ptr        = eth_header->h_proto & 0xFF;
		*(ptr + 1)  = (eth_header->h_proto >> 8) & 0xFF;

		return;
	}
	
	bool lwipep_ipsec::lwipep_ipsec_read_eth_header(srslte::byte_buffer_t *pdu, uint16_t *rnti, uint16_t *drbid)
	{
		uint32_t teidin;
		uint8_t *ptr            = pdu->msg;
		struct ethhdr *eh = (struct ethhdr *) ptr;

		pdu->msg      += ETH_HEADER_LEN;
		pdu->N_bytes  -= ETH_HEADER_LEN;
/*
		if (eh->h_dest[0] != 0xFF && eh->h_dest[0] != 0x00 &&
				eh->h_dest[1] != 0xFF && eh->h_dest[1] != 0x00 &&
				eh->h_dest[2] != 0xFF && eh->h_dest[2] != 0x00 &&
				eh->h_dest[3] != 0xFF && eh->h_dest[3] != 0x00 &&
				eh->h_dest[4] != 0xFF && eh->h_dest[4] != 0x00 &&
				eh->h_dest[5] != 0xFF && eh->h_dest[5] != 0x00 )
			lwipep_ipsec_log->console("RCV\nDST MAC %x:%x:%x:%x:%x:%x\nSRC MAC %x:%x:%x:%x:%x:%x\n",
								   eh->h_dest[0], eh->h_dest[1], eh->h_dest[2],  eh->h_dest[3], eh->h_dest[4], eh->h_dest[5],
								   eh->h_source[0], eh->h_source[1], eh->h_source[2],  eh->h_source[3], eh->h_source[4], eh->h_source[5]);
*/		
		if (eh->h_dest[0] == SRC_MAC0 &&
			eh->h_dest[1] == SRC_MAC1 &&
			eh->h_dest[2] == SRC_MAC2 &&
			eh->h_dest[3] == SRC_MAC3 &&
			eh->h_dest[4] == SRC_MAC4 &&
			eh->h_dest[5] == SRC_MAC5) 
		{

			if (eh->h_proto == htons(ETH_TYPE_IP)) 
			{
				ptr		+= ETH_HEADER_LEN + 12;
	//				lwipep_xw_log->console("\nDST IP %d.%d.%d.%d\nSRC IP %d.%d.%d.%d\n",ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7]);
				ptr		+= 8;
				ptr		+= 4;
				uint8_to_uint32(ptr, &teidin);
				teidin_to_rntilcid(teidin, rnti, drbid);
				return true;
			}

			
			return false;
		}

		return false;
	}
	/****************************************************************************
	* TEID to RNIT/LCID helper functions
	***************************************************************************/
	void lwipep_ipsec::teidin_to_rntilcid(uint32_t teidin, uint16_t *rnti, uint16_t *lcid)
	{
	  *lcid = teidin & 0xFFFF;
	  *rnti = (teidin >> 16) & 0xFFFF;
	}
	
	
	
	uint16_t lwipep_ipsec::in_cksum(uint16_t *addr, int len)
	{
		int nleft = len;
		uint32_t sum = 0;
		uint16_t *w = addr;
		uint16_t answer = 0;

		// Adding 16 bits sequentially in sum
		while (nleft > 1) {
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
}
