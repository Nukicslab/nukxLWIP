#include <stdio.h>

int		lwipep_rcv_rpt_time;		//1000 = 1s
unsigned int	mbps;
unsigned int	rate_init_lte; //60000000 = 60Mbps 1500000 = 1.5Mbps  50000000 = 50Mbps
unsigned int	rate_init_wlan; //500000000 500Mbps
unsigned int	lte_tx_ratio;					//1
unsigned int	wlan_tx_ratio;					//1
unsigned int	base_tx_ratio;
unsigned int	total_tx_ratio;
unsigned int	max_ratio;
unsigned int	lwipep_reordering_time;
unsigned int	lwipep_report_time;
unsigned int	alpha_part;
unsigned int	alpha_whole;
unsigned long	ema_part;
unsigned long	ema_whole;
unsigned int	pdu_size;
float		delay_lte; //ms
float		delay_wlan;
long		tv_sec;
long		tv_nsec;

float		rate_wlan = 0;
float		rate_lte = 0;
float		uplink_latency;
float		tx_rate_wlan = 0;
unsigned int	ack_wlan = 0;
unsigned int	ack_lte = 0;
unsigned int	pdu_avg_size = 1500;
unsigned int	handle_count = 0;

unsigned int	handle_delay_sn;
unsigned int	reconfigure_sn;
unsigned int	tx_packet_count;
unsigned int	lte_tx_packet_count;
unsigned int	wlan_tx_packet_count;
unsigned int	lte_tx_bytes;
unsigned int	wlan_tx_bytes;
unsigned int	last_handle_current_max_sn;
unsigned int	handle_current_max_sn;
unsigned int	handle_over_current_max_sn_times;
unsigned int	nmp;

unsigned int lost_lte;
unsigned int lost_wlan;

unsigned int bif_wlan;
unsigned int bif_lte;



unsigned int buffer_state = 100;

long tv_sec;
long tv_nsec;
unsigned int			gcd;
float				ema;
float				hdl_ema_part;
float            	hdl_ema_whole;


float	lte_pkt_per_ms;	// 3.75 pkt per 1ms 
float	wlan_pkt_per_ms;	// 54 pkt per 1ms 
float	sd_pkt_per_ms;	// total pkt per 1ms 

unsigned int get_gcd(unsigned int x, unsigned int y)
{
	unsigned int tmp;
	while (y != 0) {
		tmp = x % y;
		x = y;
		y = tmp;
	}
	return x;
}

void set_lwipep_tx_ratio(unsigned int set_lte_tx_ratio, unsigned int set_wlan_ratio)
{
	//lwipep_log->console("Now change tx ratio\n");
	if (set_lte_tx_ratio == 0 && set_wlan_ratio == 0)	return;
	

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
	
	//log->console("Now change ratio to %d:%d\n", lr, wr);

	total_tx_ratio = lte_tx_ratio + wlan_tx_ratio;
	printf("Now change tx ratio to %d:%d\n", lte_tx_ratio, wlan_tx_ratio);
}



int main(void)
{
	
	lwipep_rcv_rpt_time = 1000;		//1000 = 1s
	mbps = 192;
	rate_init_lte	= 45000000; //60000000 = 60Mbps 1500000 = 1.5Mbps  50000000 = 50Mbps
	rate_init_wlan	= 650000000; //500000000 500Mbps
	lte_tx_ratio = 1;					//1
	wlan_tx_ratio = 5;					//1
	base_tx_ratio = lte_tx_ratio;
	total_tx_ratio = lte_tx_ratio + wlan_tx_ratio;
	max_ratio = 8;
	lwipep_reordering_time = 40;
	lwipep_report_time = 5000;
	alpha_part = 1;
	alpha_whole = 4;
	ema_part = 1;
	ema_whole = 1;
	pdu_size = 1;
	delay_lte; //ms
	delay_wlan;
	tv_sec;
	tv_nsec;

	rate_wlan = 0;
	rate_lte = 0;
	uplink_latency;
	tx_rate_wlan = 0;
	ack_wlan = 0;
	ack_lte = 0;
	pdu_avg_size = 1500;
	tv_sec = lwipep_rcv_rpt_time + 20;
	tv_nsec = 0;

	lte_pkt_per_ms = rate_init_lte / 1000 / 8 / 1500;	// 3.75 pkt per 1ms 
	wlan_pkt_per_ms = rate_init_wlan / 1000 / 8 / 1500;	// 54 pkt per 1ms 
	sd_pkt_per_ms = mbps * 1000 * 1000 / 1000 / 8 / 1500;	// total pkt per 1ms 

	printf("send %d mbps,	sd %.3f pkt/ms\n", mbps, sd_pkt_per_ms);
	printf("lte %.3f pkt/ms	wlan %.3f pkt/ms\n", lte_pkt_per_ms, wlan_pkt_per_ms);


	while ((lte_tx_ratio != 0 && wlan_tx_ratio != 0) && handle_count < 100)
	{
		lost_wlan = 0;
		rate_wlan = 0;
		rate_lte = 0;
		uplink_latency = 0;
		tx_rate_wlan = 0;
		ack_wlan = 0;
		ack_lte = 0;
		pdu_avg_size = 1500;
		ema = 0;
		gcd = 1;
		
		
		lte_pkt_per_ms = rate_init_lte / 1000 / 8 / 1500;	// pkt per 1ms 
		wlan_pkt_per_ms = rate_init_wlan / 1000 / 8 / 1500;	// pkt per 1ms 
		
		//	lte_pkt_per_ms = 1 / lte_pkt_per_ms;
		//	wlan_pkt_per_ms = 1 / wlan_pkt_per_ms;
		
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
		
		handle_over_current_max_sn_times = (lte_pkt_per_ms + wlan_pkt_per_ms) * lwipep_rcv_rpt_time;
		printf("SERVER RCV lte %.3f pkt/ms	wlan %.3f pkt/ms\n", lte_pkt_per_ms, wlan_pkt_per_ms);
		printf("SERVER RCV %5d pkt, %5d mbps\n",handle_over_current_max_sn_times, handle_over_current_max_sn_times * 1500 * 8 / 1000 / 1000 );
		printf("lte_pkt_per_ms %.3f	wlan_pkt_per_ms %.3f\n", lte_pkt_per_ms, wlan_pkt_per_ms);

		nmp = sd_pkt_per_ms * lwipep_report_time - (lte_pkt_per_ms * lwipep_report_time + wlan_pkt_per_ms * lwipep_report_time);
		
		lost_lte = (handle_over_current_max_sn_times % 4096) / lte_tx_ratio;

		
		wlan_tx_packet_count = sd_pkt_per_ms * wlan_tx_ratio / total_tx_ratio * tv_sec;
		lte_tx_packet_count = sd_pkt_per_ms * lte_tx_ratio / total_tx_ratio * tv_sec;
		tx_packet_count = handle_over_current_max_sn_times;
		reconfigure_sn = tx_packet_count;
		
		bif_wlan = wlan_tx_packet_count;
		bif_lte = lte_tx_packet_count;
		
		wlan_tx_bytes = wlan_tx_packet_count * 1500;
		lte_tx_bytes = lte_tx_packet_count * 1500;
		
		delay_lte = 0; //ms
		delay_wlan = 0;
		
//==============================================================================================================================================
		
		if (tx_packet_count >= reconfigure_sn) 
		{
			
			
			// Bits-in-flight
			if (wlan_tx_packet_count > 0) 
			{
				
				//byte to bit, so *8
				pdu_avg_size = wlan_tx_bytes / wlan_tx_packet_count * 8;
				tx_rate_wlan = (float) (wlan_tx_packet_count / (tv_sec + (float) tv_nsec / 1000000000));
				wlan_tx_packet_count = 0;
				wlan_tx_bytes = 0;
				// nmp is all lost packet
				if (nmp > lost_lte) {	lost_wlan = (nmp - lost_lte);	}
				printf("lost_lte = %10d, lost_wlan = %10d\n", lost_lte, lost_wlan);
				if (handle_current_max_sn > 0 || handle_over_current_max_sn_times > 0) 
				{
					ack_lte  = handle_over_current_max_sn_times * lte_tx_ratio / total_tx_ratio;
					ack_wlan = handle_over_current_max_sn_times * wlan_tx_ratio / total_tx_ratio;
					last_handle_current_max_sn = handle_current_max_sn;
					printf(" - Wlan  Bif %10d, Ack %10d, lost_wlan =%10d, Rate %8.2f\n", bif_wlan, ack_wlan, lost_wlan, rate_wlan);
					printf(" - lte_tx_ratio =%10d, wlan_tx_ratio =%10d, total_tx_ratio = %10d\n", lte_tx_ratio, wlan_tx_ratio, total_tx_ratio);
					if (bif_wlan > ack_wlan) 
					{
						printf("\nif (bif_wlan > ack_wlan) bif_wlan = bif_wlan - ack_wlan\n{\n");
						printf("	bif_wlan = %d - %d", bif_wlan, ack_wlan);
						bif_wlan -= ack_wlan;
						printf("	bif_wlan = %d", bif_wlan);
						printf("}\n");
					} 
					else 
					{
						printf("\nif (bif_wlan < ack_wlan)	{	bif_wlan = 0	}\n");
						bif_wlan = 0;
					}
				}

				if (ack_wlan > lost_wlan)
				{
					printf("\nif (ack_wlan > lost_wlan) ack_wlan = ack_wlan - lost_wlan\n{\n");
					printf("	ack_wlan = %d - %d", ack_wlan, lost_wlan);
					ack_wlan -= lost_wlan;
					printf("	ack_wlan = %d", ack_wlan);
					printf("}\n");
				}
				else 
				{
					printf("\nif (ack_wlan < lost_wlan)	{	ack_wlan = 0	}\n");
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
			{	rate_wlan = rate_init_wlan / pdu_avg_size;	} 
			else 
			{	rate_wlan = (float) ack_wlan / (tv_sec + (float) tv_nsec / 1000000000); 	}	//bitrate
			
			//======================================================================================================	
			// Caluculate uplink latency(s)
			if (tv_sec * 1000 + tv_nsec / 1000000 > lwipep_rcv_rpt_time) 
			{
				uplink_latency  = tv_sec * 1000 + tv_nsec / 1000000 - lwipep_rcv_rpt_time;
				uplink_latency /= 1000;
			}
			printf(" - LTE uplink_latency  %10f\n", uplink_latency);
			// Calculate LTE bits-in-flight
			if (lte_tx_packet_count > 0) 
			{
				pdu_avg_size = lte_tx_bytes / lte_tx_packet_count * 8;
				lte_tx_packet_count = 0;
				lte_tx_bytes = 0;
				printf(" - LTE lte_tx_bytes  %10d, lte_tx_packet_count  %10d, pdu_avg_size  %10d\n", lte_tx_bytes, lte_tx_packet_count, pdu_avg_size);			
				if (bif_lte > ack_lte) 
				{
					printf(" - LTE bif_lte %d > ack_lte %d\n", bif_lte, ack_lte);
					bif_lte -= ack_lte;
					printf(" - LTE bif_lte %d \n", bif_lte);
				}
				else {	bif_lte = 0;	}

				//ack_lte *= pdu_avg_size;
				//bif_lte *= pdu_avg_size;
			}

			// Calculate LTE rate(get RLC status report)
			if (ack_lte == 0) // || bif_lte == 0
			{
				rate_lte = (rate_init_lte / pdu_avg_size);
				printf("\nif (ack_lte == 0) rate_lte = (rate_init_lte / pdu_avg_size)\n");
				printf("{\n	rate_lte =  %.9f / %9d\n", rate_init_lte, pdu_avg_size);
				printf("	rate_lte = %.9f \n", rate_lte);
				// If LTE not activated, try to transmit a part of WLAN bits-in-flight
				if (lte_tx_ratio == 0) 
				{
					bif_lte = bif_wlan * (max_ratio - 1) / max_ratio;
					printf("\n	if (lte_tx_ratio == 0)	\n{	bif_lte = bif_wlan * (max_ratio - 1) / max_ratio;\n");
					printf("		bif_lte = %d * %d / %d	\n		}\n", bif_wlan, max_ratio - 1, max_ratio);
				}
				printf("}\n");
			} 
			else 
			{
				rate_lte = (float) ack_lte / (tv_sec + (float) tv_nsec / 1000000000);
				printf("\nelse if (ack_lte != 0) rate_lte = (float) ack_lte / (tv_sec + (float) tv_nsec / 1000000000);\n{\n");
				printf("	rate_lte = %d / (%d + %d / 1000000000)\n", ack_lte, tv_sec, tv_nsec);
				printf("	rate_lte = %.2f \n", rate_lte);
				if (rate_lte > rate_init_lte / pdu_avg_size) 
				{
					printf("		if rate_lte = %.2f > (rate_init_lte / pdu_avg_size) = %.2f\n		{", rate_lte, (float) rate_init_lte / pdu_avg_size);
					bif_lte += (rate_lte - rate_init_lte / pdu_avg_size) * (tv_sec + (float) tv_nsec / 1000000000);
					rate_lte = rate_init_lte / pdu_avg_size;
					printf("		bif_lte += (%.9f - %.2f / %d) * (%d + %d / 1000000000)\n", rate_lte, rate_init_lte, pdu_avg_size, tv_sec, tv_nsec);
					printf("		rate_lte = %.2f / %d\n", rate_init_lte, pdu_avg_size);
					printf("		rate_lte = %.2f\n		}", rate_lte);
				}
				printf("}\n");
			}

			// Packet size is 1500 bytes(1 PDU)

			delay_lte = (float) (((pdu_size + bif_lte + buffer_state * 8 / pdu_avg_size) / rate_lte * 1000) * 1000); //ms
			delay_wlan = (float) (((pdu_size + bif_wlan) / rate_wlan * 1000) * 1000);
			printf("delay_lte = (%d + %d + %d / %d) / %0.10f\n", pdu_size, bif_lte, buffer_state * 8, pdu_avg_size, rate_lte);
			//			printf("delay_wlan = (%d + %d) / %0.10f\n", pdu_size, bif_wlan, rate_wlan * 1000);
			//			printf("*handle_over_current_max_sn_times %8d, Ratio %8.2f, Uplink * WLAN TX %8.2f, lte:wlan=%4d:%d\n", handle_over_current_max_sn_times, delay_lte / delay_wlan, uplink_latency * tx_rate_wlan, lte_tx_ratio, wlan_tx_ratio);
			printf(" - LTE  Ack %10d, Bif %10d, Rate %8.2f, Delay %8.3f ms\n", ack_lte, bif_lte, rate_lte / 1000000 * pdu_avg_size, delay_lte);
			printf(" - WLAN Ack %10d, Bif %10d, Rate %8.2f, Delay %8.3f ms\n", ack_wlan, bif_wlan, rate_wlan / 1000000 * pdu_avg_size, delay_wlan);

			
			
			ema_part   = alpha_part * delay_wlan * ema_whole + (alpha_whole - alpha_part) * ema_part * delay_lte;
			ema_whole *= alpha_whole * delay_lte;
			//				printf("ema_part = %lu\n",ema_part);
			//				printf("ema_whole = %lu\n",ema_whole);
			//				printf("%d = alpha_part * delay_wlan * ema_whole + (alpha_whole - alpha_part) * ema_part * delay_lte\n", ema_part);
			//				printf("%d *= alpha_whole * delay_lte\n\n", ema_whole);
			
			printf("before while ema_part = %lu\n",ema_part);
			printf("before while ema_whole = %lu\n",ema_whole);
			
			while (ema_part >= max_ratio && ema_whole >= max_ratio)
			{
				ema_part  /= max_ratio;
				ema_whole /= max_ratio;

				//		printf("while ema_part = %lu\n",ema_part);
				//		printf("while ema_whole = %lu\n",ema_whole);
				///*		
				//(ema_part / max_ratio <= 1 && ema_part % max_ratio > 0) || (ema_whole / max_ratio <= 1 && ema_whole % max_ratio > 0 ) ||
				if (  (ema_part / max_ratio == ema_whole / max_ratio) ) 
				{
					//						printf("break\n");
					hdl_ema_part = (float)ema_part;
					hdl_ema_whole = (float)ema_whole;
					printf("while ema_part = %lu\n",ema_part);
					printf("while ema_whole = %lu\n",ema_whole);
					ema			= hdl_ema_part / hdl_ema_whole;
					
					printf("ema = %.9f\n",ema);
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

			//				printf("caled ema_part = %lu\n",ema_part);
			//				printf("caled ema_whole = %lu\n",ema_whole);

			

			
			printf("after while ema_part = %lu\n",ema_part);
			printf("after while ema_whole = %lu\n",ema_whole);
			handle_count = handle_count + 1;
			printf("\ninside count = %d\n", handle_count);
			

			
		}
		
		if (ema_whole > max_ratio * ema_part) 
		{	set_lwipep_tx_ratio(0, 1);	} 
		else if (ema_part > max_ratio * ema_whole) 
		{	set_lwipep_tx_ratio(1, 0);	} 
		else  {	set_lwipep_tx_ratio(ema_part, ema_whole);	}		//else if (ema_whole >= ema_part)

		
	}
	

	return 0;
}
