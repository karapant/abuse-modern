/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#pragma once

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#   pragma push_macro("ERROR")
#   include <winsock2.h>
#   pragma pop_macro("ERROR")
using socket_handle = SOCKET;
using socket_length = int;
inline constexpr socket_handle invalid_socket = INVALID_SOCKET;
inline void close_socket(socket_handle fd) { ::closesocket(fd); }
#else
#   include <netdb.h>
#   include <netinet/in.h>
#   include <sys/select.h>
#   include <sys/socket.h>
#   include <unistd.h>
using socket_handle = int;
using socket_length = socklen_t;
inline constexpr socket_handle invalid_socket = -1;
inline void close_socket(socket_handle fd) { ::close(fd); }
#endif

#include "sock.h"
#include "isllist.h"

static_assert(sizeof(socket_handle) <= sizeof(std::intptr_t));

class ip_address : public net_address
{
public:
  sockaddr_in addr;

  virtual protocol protocol_type() const { return net_address::IP; }
  virtual int equal(const net_address *who) const
  //{ {{
  {
    if (who->protocol_type()==IP &&
        !memcmp(&addr.sin_addr,& ((ip_address const *)who)->addr.sin_addr,sizeof(addr.sin_addr)))
      return 1;
    else return 0;
  }
    //}}}
  virtual int set_port(int port)  { addr.sin_port=htons(port); return 1; }
  ip_address(sockaddr_in *Addr) { memcpy(&addr,Addr,sizeof(addr)); }
  virtual void print()
  //{ {{
  {
    unsigned char *c=(unsigned char *) (&addr.sin_addr.s_addr);
    fprintf(stderr,"%d.%d.%d.%d",c[0],c[1],c[2],c[3]);
  }
    //}}}
  int get_port() { return htons(addr.sin_port); }
  net_address *copy()  { return new ip_address(&addr); }
  ip_address() { } ;
  void store_string(char *st, int st_length)
  //{ {{
  {
    char buf[100];
    unsigned char *c=(unsigned char *) (&addr.sin_addr.s_addr);
    sprintf(buf,"%d.%d.%d.%d:%d",c[0],c[1],c[2],c[3],htons(addr.sin_port));
    strncpy(st,buf,st_length);
    st[st_length-1]=0;
  }
  //}}}
} ;

class tcpip_protocol : public net_protocol
{
protected:
  bool sockets_available = true;

  // Request Data
  struct RequestItem
  {
      ip_address *addr;
      char name[256];   //name
  };
  typedef isllist<RequestItem *>::iterator p_request;
  isllist<RequestItem*> servers,returned;

  // Notification Data
  net_socket *notifier;
  char notify_data[512];
  int notify_len;

  net_socket *responder;
  ip_address *bcast;

  int handle_notification();
  int handle_responder();
public :
  fd_set master_set,master_write_set,read_set,exception_set,write_set;

  tcpip_protocol();
  net_address *get_local_address();
  net_address *get_node_address(char const *&server_name, int def_port, int force_port);
  net_socket *connect_to_server(net_address *addr,
        net_socket::socket_type sock_type=net_socket::SOCKET_SECURE);
  net_socket *create_listen_socket(int port, net_socket::socket_type sock_type);
  int installed() { return sockets_available; }
  char const *name() {
#ifdef _WIN32
    return "Windows TCP/IP";
#else
    return "UNIX generic TCPIP";
#endif
  }
  void cleanup();
  int select(int block);          // return # of sockets available for read & writing

  // Notification methods
  virtual net_socket *start_notify(int port, void *data, int len);
  virtual void end_notify();

  // Find notifiers methods
  virtual net_address *find_address(int port, char *name);   // name should be a 256 byte buffer
  virtual void reset_find_list();
  virtual ~tcpip_protocol();
} ;

extern tcpip_protocol tcpip;

class unix_fd : public net_socket
{
  protected :
  socket_handle fd;
  public :
  unix_fd(socket_handle fd) : fd(fd) { };
  virtual int error()                             { return FD_ISSET(fd,&tcpip.exception_set); }
  virtual int ready_to_read()                     { return FD_ISSET(fd,&tcpip.read_set); }
  virtual int ready_to_write()
  {
    struct timeval tv={ 0,0};     // don't wait
    fd_set write_check;
    FD_ZERO(&write_check);
    FD_SET(fd,&write_check);
    select(FD_SETSIZE,NULL,&write_check,NULL,&tv);
    return FD_ISSET(fd,&write_check);
  }
  virtual int write(void const *buf, int size, net_address *addr=NULL);
  virtual int read(void *buf, int size, net_address **addr);

  virtual ~unix_fd()                            { read_unselectable();  write_unselectable(); close_socket(fd); }
  virtual void read_selectable()                   { FD_SET(fd,&tcpip.master_set); }
  virtual void read_unselectable()                 { FD_CLR(fd,&tcpip.master_set); }
  virtual void write_selectable()                  { FD_SET(fd,&tcpip.master_write_set); }
  virtual void write_unselectable()                { FD_CLR(fd,&tcpip.master_write_set); }
  std::intptr_t get_fd() { return static_cast<std::intptr_t>(fd); }

  void broadcastable();
} ;

class tcp_socket : public unix_fd
{
  int listening;
  public :
  tcp_socket(socket_handle fd) : unix_fd(fd) { listening=0; };
  virtual int listen(int port)
  {
    sockaddr_in host;
    memset( (char*) &host,0, sizeof(host));
    host.sin_family = AF_INET;
    host.sin_port = htons(port);
    host.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *) &host, sizeof(sockaddr_in))==-1)
    {
      fprintf(stderr,"net driver : could not bind socket to port %d\n",port);
      return 0;
    }
    if (::listen(fd,5)==-1)
    {
      fprintf(stderr,"net driver : could not listen to socket on port %d\n",port);
      return 0;
    }
    listening=1;
    return 1;
  }
  virtual net_socket *accept(net_address *&addr)
  {
    if (listening)
    {
      struct sockaddr_in from;
      socket_length addr_len=sizeof(from);
      socket_handle new_fd=::accept(fd,(sockaddr *)&from,&addr_len);
      if (new_fd != invalid_socket)
      {
        addr=new ip_address(&from);
        return new tcp_socket(new_fd);
      }
      else
      { addr=NULL; return 0; }
    }
    return 0;
  }
} ;

class udp_socket : public unix_fd
{
  public :
  udp_socket(socket_handle fd) : unix_fd(fd) { };
  virtual int read(void *buf, int size, net_address **addr)
  {
    int tr;
    if (addr)
    {
      sockaddr_in from{};
      socket_length addr_size=sizeof(from);
      tr=recvfrom(fd,static_cast<char *>(buf),size,0,reinterpret_cast<sockaddr *>(&from),&addr_size);
      *addr = tr < 0 ? nullptr : new ip_address(&from);
    } else
      tr=recv(fd,static_cast<char *>(buf),size,0);
    return tr;
  }
  virtual int write(void const *buf, int size, net_address *addr=NULL)
  {
    if (addr)
      return sendto(fd,static_cast<const char *>(buf),size,0,(sockaddr *)(&((ip_address *)addr)->addr),sizeof(((ip_address *)addr)->addr));
    else
      return ::send(fd,static_cast<const char *>(buf),size,0);
  }
  virtual int listen(int port)
  {
    sockaddr_in host;
    memset( (char*) &host,0, sizeof(host));
    host.sin_family = AF_INET;
    host.sin_port = htons(port);
    host.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *) &host, sizeof(sockaddr_in))==-1)
    {
      fprintf(stderr,"net driver : could not bind socket to port %d\n",port);
      return 0;
    }
    return 1;
  }

} ;

