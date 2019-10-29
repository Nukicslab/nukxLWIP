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
		
		enable_lwipep_ipsec_rcv_pkt = true;
		lwipep_ipsec_rcv_pkt_running = true;
	}
	void lwipep_ipsec::init(srsue::lwipep_interface_lwipep_ipsec *lwipep_, /*, lwipep_spgw_interface_ipsec *lwipep_spgw_,*/ srslte::log *lwipep_ipsec_log_)
	{
		pool    			= srslte::byte_buffer_pool::get_instance();
		
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
	/*	
		socket_fd = socket(PF_PACKET, SOCK_RAW, htons(0x0800));		//htons(IPPROTO_IP)

		if (socket_fd == -1)
		{
			printf( "\nsocket_fd failed\n" );
			sock_success = 1;
			close(socket_fd);
		}

		if ( sock_success == 0)
		{	
			strcpy( req.ifr_name, "eNB_wifi" );
			if ( ioctl( socket_fd, SIOCGIFFLAGS, &req ) < 0 )
			{
				printf( "\nfailed to do ioctl!\n" );
				sock_success = 2;
			}

			req.ifr_flags |= IFF_PROMISC;
			if ( ioctl( socket_fd, SIOCSIFFLAGS, &req ) < 0 )
			{
				printf( "\nfailed to set eth0 into promisc mode!\n" );
				sock_success = 3;
			}
			//================================================================================================================
			socket_flag = fcntl(socket_fd, F_GETFL, 0);
			
			if (socket_flag < 0)
			{	
				printf( "\nsocket_flag failed\n" );
				sock_success = 4;
			}
			if (fcntl(socket_fd, F_SETFL, socket_flag | O_NONBLOCK) < 0)
			{	
				printf( "\nNONBLOCK failed\n" );
				sock_success = 5;
			}
			//================================================================================================================
			if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof(socket_opt)) == -1)
			{
				printf( "\nSO_REUSEADDR failed\n" );
				sock_success = 5;
				close(socket_fd);
			}
			if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, wifi_dev, sizeof(wifi_dev)) == -1)
			{
				printf( "\nSO_BINDTODEVICE failed\n" );
				sock_success = 6;
				close(socket_fd);
			}
		}
		
		memset(&if_mac, 0, sizeof(struct ifreq));
		strncpy(if_mac.ifr_name, WIFI_IF, IFNAMSIZ - 1);
		if (ioctl(socket_fd, SIOCGIFHWADDR, &if_mac) < 0) {
			perror("SIOCGIFHWADDR");
			exit(EXIT_FAILURE);
		}
		*/
		start(LWIPEP_IPSEC_THREAD_PRIO);
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

		sockaddr_ll ueaddr;
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

//		lwipep_write_header(&header, lcid, sdu);

		ul_tput_bytes += sdu->N_bytes;

		// Send packet
		if (sendto(socket_fd, sdu->msg, sdu->N_bytes, MSG_EOR, (struct sockaddr*)&ueaddr, sizeof(struct sockaddr_ll)) < 0) 
		{
			perror("sendto");
			lwipep_ipsec_log->console("sendto l %d\n", sdu->N_bytes);
		}
		pool->deallocate(sdu);
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
			} while (N_bytes < 74);
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
					lwipep->write_pdu(drbid, pdu, 2);
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
