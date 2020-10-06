Danit Noa Yechezkel
203964036 

Dekel Menashe
311224117

Keren Halpert     
313604621    




the program does NOT need to be provided with an IP address, only a folder to monitor.

COMPILE:
  gcc -g -finstrument-functions myFileSystemMonitor.c -o myFileSystemMonitor -lpthread -lcli

RUN:
  ./myFileSystemMonitor -d /../..
  
NETCAT:
  netcat -l -u -p 10000
  
TELNET:
  telnet 0 10000
