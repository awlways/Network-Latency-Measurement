//--------------------------------------------------------------------
//
//	The code demonstrates the use of the Linux's 'timestamp' socket
//	option to obtain a measurement of the round-trip time.
//	Usage: 
//		server:$ sudo ./triptime-server <interface> <port>
//		client:$ sudo ./triptime-client <interface> <target> <port>
//	
//--------------------------------------------------------------------

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
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

	//---------------------------------------------------------
	// check for the required number of command-line arguments 
	//---------------------------------------------------------
	if ( argc < 2 ) 
		{ 
		fprintf( stderr, "usage: ./triptime-server <interface> <port>\n" );
		exit(1);
		} 

	//---------------------------------------------------------------
	// get the target hostname and port-number from the command-line
	//---------------------------------------------------------------
	char inf[ 32 ] = { 0 };
	strncpy( inf, argv[1], 31 );
	int	port = atoi( argv[2] );

	//-----------------------------------------------
	// get this station's hostname for later display
	//-----------------------------------------------
	char	hostname[ 64 ] = {0};
	gethostname( hostname, 63 ); 

	//------------------------------------------------------------------  	
	// create an internet datagram socket to receive incomming messages
	//------------------------------------------------------------------  	
	int	sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock < 0 ) { perror( "socket" ); exit(1); }

	//----------------------------------
	// enable hardware timestamping.
	//----------------------------------
	do_ioctl(inf, sock);

	//---------------------------------------------------------
	// set a socket-option allowing this socket to be reusable
	//---------------------------------------------------------
	int	reuse = 1;
	if ( setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse) ) < 0 )
		{ perror( "setsockopt REUSEADDR" ); exit(1); }

	//---------------------------------------
	// enable the SO_TIMESTAMPING socket-option
	//---------------------------------------
	int	oval = 0;
	oval |= SOF_TIMESTAMPING_RX_SOFTWARE;
	oval |= SOF_TIMESTAMPING_RX_HARDWARE;
	oval |= SOF_TIMESTAMPING_SOFTWARE; 
	oval |= SOF_TIMESTAMPING_RAW_HARDWARE;
	if ( setsockopt( sock, SOL_SOCKET, SO_TIMESTAMPING, &oval, sizeof(oval) ) < 0 )
		{ perror( "setsockopt TIMESTAMPING"); exit(1); }

	//------------------------------------------------------------------
	// initialize a socket-address structure using a 'wildcard' address
	//------------------------------------------------------------------
	struct sockaddr_in	saddr;
	socklen_t		salen = sizeof( saddr );
	bzero( &saddr, salen );
	saddr.sin_family 	= AF_INET;
	saddr.sin_port 		= htons( port );
	saddr.sin_addr.s_addr 	= htonl( INADDR_ANY );

	//--------------------------------------------------
	// bind the socket to this socket-address structure
	//--------------------------------------------------
	if ( bind( sock, (sockaddr*)&saddr, salen ) < 0 )
		{ perror( "bind" ); exit(1); }

	//------------------------------------------
	// main loop to receive connection-requests
	//------------------------------------------ 
	int	count = 0;
	do	{
		//-----------------------------------------------------
		// initialize objects for our message-header structure
		//-----------------------------------------------------
		struct sockaddr_in paddr = {0};
		socklen_t palen = sizeof( paddr );
		char buf[ 1500 ] = {0};
		int	blen = sizeof( buf );
		struct iovec myiov[ 1 ] = { { buf, blen } };
		unsigned char cbuf[ 64 ] = { 0 };
		int	clen = sizeof( cbuf );

		// allocate and initialize the 'struct msghdr' object	
		struct msghdr mymsg = {0};
		mymsg.msg_name 		= &paddr;
		mymsg.msg_namelen 	= palen; 
		mymsg.msg_iov		= myiov;
		mymsg.msg_iovlen	= 1;
		mymsg.msg_control 	= cbuf;
		mymsg.msg_controllen= clen;
		mymsg.msg_flags		= 0;
	
		// show the socket's port-number and the name of this host
		if (debug) {
			printf( "\n ok, server is listening" );
			printf( " to port %u on %s ... \n", port, hostname );
			fflush( stdout );
		}
		
		//--------------------------------------------------------
		// receive a message from the client
		//--------------------------------------------------------
		int	rx = recvmsg( sock, &mymsg, 0 );
		if ( rx < 0 ) { perror( "recvmsg" ); exit(1); }

		//-------------------------------------------------------
		// obtain rx timestamp in ancilliary data
		//-------------------------------------------------------
		struct msghdr *msgp = &mymsg;
		struct cmsghdr *cmsg;
		struct timespec time_rx_oneway[3] = {0};
		int time_len = 3*sizeof(struct timespec);
		for (cmsg = CMSG_FIRSTHDR( msgp ); cmsg != NULL; cmsg = CMSG_NXTHDR( msgp, cmsg )) {
			if (( cmsg->cmsg_level == SOL_SOCKET ) && ( cmsg->cmsg_type == SO_TIMESTAMPING ))
				memcpy( time_rx_oneway, CMSG_DATA( cmsg ), time_len );
		}	

		myiov[0] = {time_rx_oneway, time_len};
		mymsg.msg_control = NULL;
		mymsg.msg_controllen = 0;

		int	tx = sendmsg( sock, &mymsg, 0 );
		if ( tx < 0 ) { perror( "sendmsg" ); exit(1); }

		if (debug) {
			unsigned long long stamp_rx_sw = time_rx_oneway[0].tv_sec * 1000000000LL + time_rx_oneway[0].tv_nsec;
			unsigned long long stamp_rx_hw = time_rx_oneway[2].tv_sec * 1000000000LL + time_rx_oneway[2].tv_nsec;
			printf( " rx_oneway - sw: %llu hw: %llu \n", stamp_rx_sw, stamp_rx_hw);
			printf( " #%d: %d bytes received, %d bytes sent \n", ++count, rx, tx );
		}
	} while ( 1 );
}
