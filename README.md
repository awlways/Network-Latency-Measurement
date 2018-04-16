# accurate_rtt_measurement

This tool supports accurate round trip time (RTT) and one-way delay measurement based on software/hardware timestamping. The tool is developed and tested under Linux kernel 4.4.114 with Mellanox ConnectX-4 NIC support. 
  - For software timestamping (defalut), there are ~10s of microseconds error (dominated by software delay). 
  - For hardware timestamping, the error can be reduced to ~10s of nanoseconds. 
  - For RTT, the software latency in receiver side is involved even when hardware timestamping is used. 
  - For one-way delay, the clock syncronization between two servers is required. To this end, network time protocol (NTP) and precision time protocol (PTP) can be used. NTP achieves millisecond-scale accuracy. PTP achieves nanosecond-scale accuracy but requires hardware support.

Useful links:
[Linux timestamping](https://www.kernel.org/doc/Documentation/networking/timestamping.txt); 
[Linux clock syncronization - linuxptp](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/6/html/deployment_guide/ch-configuring_ptp_using_ptp4l).
