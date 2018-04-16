//--------------------------------------------------------------------
//
//	The code demonstrates the use of the Linux's 'timestamp' socket
//	option to obtain a measurement of the round-trip time.
//	Usage: 
//		server:$ sudo ./triptime-server <interface> <port>
//		client:$ sudo ./triptime-client <interface> <target> <port>
//	
//--------------------------------------------------------------------

#include <netdb.h>	
#include <stdio.h>	
#include <stdlib.h>	
#include <string.h>	
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>	
#include <net/if.h>
#include <sys/time.h>	
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/net_tstamp.h>


static void do_ioctl(char* inf, int sock)
{
#ifdef SIOCSHWTSTAMP
  	struct ifreq ifr;
	struct hwtstamp_config hwc;

	bzero(&ifr, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", inf);

	/* Standard kernel ioctl options */
	hwc.flags = 0;
	hwc.tx_type = HWTSTAMP_TX_ON;
	hwc.rx_filter = HWTSTAMP_FILTER_ALL;

	ifr.ifr_data = (char*)&hwc;

	if ( ioctl(sock, SIOCSHWTSTAMP, &ifr) < 0 ) 
		{ perror( "config hardware timestamping" ); exit(1); };

	return;
#else
	(void) sock;
	printf("SIOCHWTSTAMP ioctl not supported on this kernel.\n");
	exit(-ENOTSUP);
	return;
#endif
}


int main( int argc, char **argv )
{
	int debug = 1;
	int software = 1;
	int oneway_delay = 0;

	//---------------------------------------------------------
	// check for the required number of command-line arguments 
	//---------------------------------------------------------
	if ( argc < 3 ) 
		{ 
		fprintf( stderr, "usage: ./triptime-client <interface> <target> <port>\n" );
		exit(1);
		} 

	//---------------------------------------------------------------
	// get the target hostname and port-number from the command-line
	//---------------------------------------------------------------
	char inf[ 32 ] = { 0 };
	strncpy( inf, argv[1], 31 );
	char peername[ 64 ] = { 0 };
	strncpy( peername, argv[2], 63 );
	int	port = atoi( argv[3] );
	
	//-------------------------------------------------
	// obtain the internet address for the remote host
	//-------------------------------------------------
	char	peeraddr[ 16 ] = { 0 };
	struct hostent	*pp = gethostbyname( peername );
	if ( !pp ) { herror( "gethostbyname" ); exit(1); }
	strcpy( peeraddr, inet_ntoa( *(in_addr*)pp->h_addr ) );

	//--------------------------------------------------------
	// initialize a socket-address object for the remote host
	//--------------------------------------------------------
	struct sockaddr_in	paddr = { 0 };
	socklen_t palen = sizeof( paddr );
	paddr.sin_family = AF_INET;
	paddr.sin_port = htons( port );
	paddr.sin_addr.s_addr = *(uint32_t*)pp->h_addr;

	//----------------------------------
	// open an internet datagram socket
	//----------------------------------
	int	sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock < 0 ) { perror( "socket" ); exit(1); }

	//----------------------------------
	// enable hardware timestamping.
	//----------------------------------
	if (!software) do_ioctl(inf, sock);

	//---------------------------------------
	// enable the SO_TIMESTAMPING socket-option
	//---------------------------------------
	int	oval = 1;
	if (software) {
		if ( setsockopt( sock, SOL_SOCKET, SO_TIMESTAMPNS, &oval, sizeof(oval) ) < 0 )
			{ perror( "setsockopt TIMESTAMPNS"); exit(1); }
	}
	oval = 0;
	oval |= SOF_TIMESTAMPING_RX_HARDWARE; 
	oval |= SOF_TIMESTAMPING_TX_HARDWARE;
	oval |= SOF_TIMESTAMPING_SOFTWARE;
	oval |= SOF_TIMESTAMPING_RAW_HARDWARE;
	if ( setsockopt( sock, SOL_SOCKET, SO_TIMESTAMPING, &oval, sizeof(oval) ) < 0 )
		{ perror( "setsockopt TIMESTAMPING"); exit(1); }
	
	int	count = 0;
	do	{
		//-----------------------------------------------------
		// initialize objects for our message-header structure
		//-----------------------------------------------------
		char msg[] = "hi";
		int	sendlen = strlen( msg );
		struct iovec myiov[1] = { { msg, sendlen } };
		unsigned char cbuf[ 64 ] = { 0 };
		int	clen = sizeof( cbuf ); 

		//-----------------------------------------
		// initialize our message-header structure
		//-----------------------------------------
		struct msghdr mymsg = { 0 };
		mymsg.msg_name		= &paddr;
		mymsg.msg_namelen	= palen;
		mymsg.msg_iov		= myiov;
		mymsg.msg_iovlen	= 1;
		mymsg.msg_control	= NULL;
		mymsg.msg_controllen= 0;
		mymsg.msg_flags		= 0;

		//-------------------------------------
		// initizlize timestamp data
		//-------------------------------------
		struct msghdr	*msgp;
		struct cmsghdr	*cmsg;
		struct timespec time_tx[3] = {0};
		struct timespec time_rx[3] = {0};
		struct timespec time_rx_oneway[3] = {0};
		int time_len = 3*sizeof(struct timespec);

		//------------------------------------------------------
		// then immediately send our message to the remote host
		//------------------------------------------------------
		int	tx = sendmsg( sock, &mymsg, 0 );
		if ( tx < 0 ) { perror( "sendmsg" ); exit(1); }

		//-----------------------------------------------------
		// now adjust some fields in our message-header object 
		//-----------------------------------------------------
		char receivebuf[ 1500 ] = {0};
		int	receivelen = sizeof( receivebuf );
		myiov[0] = { receivebuf, receivelen };
		mymsg.msg_control = cbuf;
		mymsg.msg_controllen = clen;

		//--------------------------------------------------------
		// receive tx timestamp message
		//--------------------------------------------------------
		int err;
		do {
			err = recvmsg(sock, &mymsg, MSG_ERRQUEUE);
	  	} while (err < 0 && errno == EAGAIN);

	  	//-------------------------------------------------------
		// obtain tx timestamp in ancilliary data
		//-------------------------------------------------------
		msgp = &mymsg;
		for (cmsg = CMSG_FIRSTHDR( msgp ); cmsg != NULL; cmsg = CMSG_NXTHDR( msgp, cmsg )) {
			if (( cmsg->cmsg_level == SOL_SOCKET ) && ( cmsg->cmsg_type == SO_TIMESTAMPING ))
				memcpy( time_tx, CMSG_DATA( cmsg ), time_len );
		}	

		//--------------------------------------------------------
		// receive a reply from the server (or else timeout)
		//--------------------------------------------------------
		int	rx = recvmsg( sock, &mymsg, 0 );
		if ( rx < 0 ) { perror( "recvmsg" ); exit(1); }

		//-------------------------------------------------------
		// obtain rx timestamp in message data if oneway
		//-------------------------------------------------------
		memcpy( time_rx_oneway, receivebuf, time_len );

		//-------------------------------------------------------
		// obtain rx timestamp in ancilliary data if not oneway
		//-------------------------------------------------------
		msgp = &mymsg;
		for (cmsg = CMSG_FIRSTHDR( msgp ); cmsg != NULL; cmsg = CMSG_NXTHDR( msgp, cmsg )) {
			if (( cmsg->cmsg_level == SOL_SOCKET ) && ( cmsg->cmsg_type == SO_TIMESTAMPING ))
				memcpy( time_rx, CMSG_DATA( cmsg ), time_len );
		}			

		//-----------------------------
		// compute the round-trip time
		//-----------------------------
		unsigned long long stamp_tx_sw = time_tx[0].tv_sec * 1000000000LL + time_tx[0].tv_nsec;
		unsigned long long stamp_rx_sw = time_rx[0].tv_sec * 1000000000LL + time_rx[0].tv_nsec;
		unsigned long long stamp_rx_sw_oneway = time_rx_oneway[0].tv_sec * 1000000000LL + time_rx_oneway[0].tv_nsec;
		unsigned long long stamp_tx_hw = time_tx[2].tv_sec * 1000000000LL + time_tx[2].tv_nsec;
		unsigned long long stamp_rx_hw = time_rx[2].tv_sec * 1000000000LL + time_rx[2].tv_nsec;
		unsigned long long stamp_rx_hw_oneway = time_rx_oneway[2].tv_sec * 1000000000LL + time_rx_oneway[2].tv_nsec;
		unsigned long long rtt_sw = stamp_rx_sw - stamp_tx_sw;
		unsigned long long rtt_hw = stamp_rx_hw - stamp_tx_hw;
		unsigned long long delay_oneway_sw = stamp_rx_sw_oneway - stamp_tx_sw;
		unsigned long long delay_oneway_hw = stamp_rx_hw_oneway - stamp_tx_hw;

		//-------------------------------------
		// report this transaction to the user
		//-------------------------------------
		if (debug) {
			printf( "\n contacted server on \'%s\' (%s) \n", peername, peeraddr );
			printf( " #%d: %d bytes sent, %d bytes received \n", ++count, tx, rx );
			if (software) {
				printf( " ------- software timestamps ------- \n");
				printf( " tx       \t %llu \n rx       \t %llu \n rx_oneway\t %llu \n", 
					stamp_tx_sw, stamp_rx_sw, stamp_rx_sw_oneway);
				printf( " (software) round-trip time was %llu nanoseconds \n", rtt_sw);
				printf( " (software) oneway delay time was %llu nanoseconds \n", delay_oneway_sw);
			} else {
				printf( " ------- hardware timestamps ------- \n");
				printf( " tx       \t %llu \n rx       \t %llu \n rx_oneway\t %llu \n", 
					stamp_tx_hw, stamp_rx_hw, stamp_rx_hw_oneway);
				printf( " (hardware) round-trip time was %llu nanoseconds \n", rtt_hw);
				printf( " (hardware) oneway delay time was %llu nanoseconds \n", delay_oneway_hw);
			}
			printf( " Note: there might be clock drifting for oneway delay time\n\n");
		} else {
			if (oneway_delay) {
				if (software) {
					printf( " (software) oneway delay time was %llu nanoseconds \n", delay_oneway_sw);
				} else {
					printf( " (hardware) oneway delay time was %llu nanoseconds \n", delay_oneway_hw);
			}
			} 
			else if (software) {
				printf( " (software) round-trip time was %llu nanoseconds \n", rtt_sw);
			} else {
				printf( " (hardware) round-trip time was %llu nanoseconds \n", rtt_hw);
			}
		}

		usleep(200000); // sleep # usec

	} while( 1 );
}
