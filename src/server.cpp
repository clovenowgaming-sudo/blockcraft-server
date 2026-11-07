// BlockCraft dedicated server: line-based TCP protocol, block edits persisted to world.dat.
// Usage: bc_server [port=25566] [seed=random] [mode=1]   (mode 0=survival, 1=creative)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <fstream>
#include <sstream>
#include <algorithm>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BADSOCK INVALID_SOCKET
#define CLOSESOCK ::closesocket
static bool wouldBlock(){return WSAGetLastError()==WSAEWOULDBLOCK;}
static void nonblock(sock_t s){u_long m=1;ioctlsocket(s,FIONBIO,&m);}
static void initNet(){WSADATA w;WSAStartup(MAKEWORD(2,2),&w);}
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <csignal>
typedef int sock_t;
#define BADSOCK (-1)
#define CLOSESOCK ::close
static bool wouldBlock(){return errno==EAGAIN||errno==EWOULDBLOCK;}
static void nonblock(sock_t s){fcntl(s,F_SETFL,fcntl(s,F_GETFL,0)|O_NONBLOCK);}
static void initNet(){signal(SIGPIPE,SIG_IGN);}
#endif

struct Client{ sock_t s; std::string buf; int id=0; std::string name; bool joined=false,dead=false; };
static std::vector<Client*> clients;
static std::map<std::tuple<int,int,int>,int> edits;
static unsigned seed=0; static int mode=1; static int nextId=1;

static void sendTo(Client*c,const std::string&l){
  std::string d=l+"\n"; size_t off=0; int tries=0;
  while(off<d.size()&&tries<1000){
    int r=(int)send(c->s,d.data()+off,(int)(d.size()-off),0);
    if(r>0){off+=(size_t)r;continue;}
    if(r<0&&wouldBlock()){tries++;continue;}
    c->dead=true;return;
  }
}
static void broadcast(const std::string&l,int exceptId=-1){for(auto*c:clients)if(c->joined&&c->id!=exceptId&&!c->dead)sendTo(c,l);}

static void handle(Client*c,const std::string&line){
  std::istringstream ss(line); std::string cmd; ss>>cmd;
  if(cmd=="HELLO"&&!c->joined){
    std::string n; ss>>n; if(n.empty())n="Player"; if(n.size()>16)n.resize(16);
    for(auto&ch:n)if(!isalnum((unsigned char)ch)&&ch!='_')ch='_';
    c->name=n; c->id=nextId++;
    sendTo(c,"WELCOME "+std::to_string(c->id)+" "+std::to_string(seed)+" "+std::to_string(mode));
    for(auto&e:edits)sendTo(c,"SET "+std::to_string(std::get<0>(e.first))+" "+std::to_string(std::get<1>(e.first))+" "+std::to_string(std::get<2>(e.first))+" "+std::to_string(e.second));
    for(auto*o:clients)if(o->joined)sendTo(c,"JOIN "+std::to_string(o->id)+" "+o->name);
    c->joined=true; broadcast("JOIN "+std::to_string(c->id)+" "+c->name,c->id);
    printf("[+] %s (id %d) joined\n",c->name.c_str(),c->id); fflush(stdout);
    return;
  }
  if(!c->joined)return;
  if(cmd=="POS"){float x,y,z,yw;if(ss>>x>>y>>z>>yw)broadcast("POS "+std::to_string(c->id)+" "+std::to_string(x)+" "+std::to_string(y)+" "+std::to_string(z)+" "+std::to_string(yw),c->id);}
  else if(cmd=="SET"){
    int x,y,z,id;
    if(ss>>x>>y>>z>>id&&y>=0&&y<96&&id>=0&&id<15){
      edits[std::make_tuple(x,y,z)]=id;
      std::ofstream f("world.dat",std::ios::app); f<<x<<" "<<y<<" "<<z<<" "<<id<<"\n";
      broadcast("SET "+std::to_string(x)+" "+std::to_string(y)+" "+std::to_string(z)+" "+std::to_string(id),c->id);
    }
  }
  else if(cmd=="CHAT"){
    std::string rest; std::getline(ss,rest); if(!rest.empty()&&rest[0]==' ')rest.erase(0,1);
    if(rest.size()>100)rest.resize(100); if(rest.empty())return;
    printf("<%s> %s\n",c->name.c_str(),rest.c_str()); fflush(stdout);
    broadcast("CHAT "+c->name+": "+rest);
  }
}

int main(int argc,char**argv){
  int port=argc>1?atoi(argv[1]):25566; seed=argc>2?(unsigned)strtoul(argv[2],nullptr,10):0; if(argc>3)mode=atoi(argv[3])?1:0;
  initNet();
  bool haveSeed=argc>2;
  { std::ifstream f("world.dat"); std::string tag;
    if(f>>tag&&tag=="seed"){unsigned s;f>>s;if(!haveSeed)seed=s;int x,y,z,id;while(f>>x>>y>>z>>id)edits[std::make_tuple(x,y,z)]=id;} }
  if(!haveSeed&&seed==0)seed=(unsigned)time(nullptr);
  { std::ifstream t("world.dat"); bool exists=t.good(); t.close();
    if(!exists){std::ofstream f("world.dat");f<<"seed "<<seed<<"\n";} }
  sock_t ls=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,(const char*)&one,sizeof(one));
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY); a.sin_port=htons((unsigned short)port);
  if(bind(ls,(sockaddr*)&a,sizeof(a))!=0||listen(ls,16)!=0){fprintf(stderr,"Cannot listen on port %d\n",port);return 1;}
  nonblock(ls);
  printf("BlockCraft server on port %d | seed %u | mode %s | %zu saved edits\n",port,seed,mode?"creative":"survival",edits.size()); fflush(stdout);
  for(;;){
    fd_set rs; FD_ZERO(&rs); FD_SET(ls,&rs); int mx=(int)ls;
    for(auto*c:clients){FD_SET(c->s,&rs);mx=std::max(mx,(int)c->s);}
    timeval tv; tv.tv_sec=0; tv.tv_usec=50000;
    if(select(mx+1,&rs,nullptr,nullptr,&tv)<0)continue;
    if(FD_ISSET(ls,&rs)){
      sock_t ns=accept(ls,nullptr,nullptr);
      if(ns!=BADSOCK){nonblock(ns);setsockopt(ns,IPPROTO_TCP,TCP_NODELAY,(const char*)&one,sizeof(one));Client*c=new Client();c->s=ns;clients.push_back(c);}
    }
    for(auto*c:clients){
      if(c->dead||!FD_ISSET(c->s,&rs))continue;
      char tmp[4096];
      for(;;){
        int r=(int)recv(c->s,tmp,sizeof(tmp),0);
        if(r>0){c->buf.append(tmp,(size_t)r);if(c->buf.size()>65536){c->dead=true;break;}continue;}
        if(r==0){c->dead=true;break;}
        if(wouldBlock())break;
        c->dead=true;break;
      }
      size_t p;
      while(!c->dead&&(p=c->buf.find('\n'))!=std::string::npos){
        std::string l=c->buf.substr(0,p); c->buf.erase(0,p+1);
        if(!l.empty()&&l.back()=='\r')l.pop_back();
        if(!l.empty())handle(c,l);
      }
    }
    for(size_t i=0;i<clients.size();){
      Client*c=clients[i];
      if(c->dead){
        if(c->joined){broadcast("LEAVE "+std::to_string(c->id),c->id);printf("[-] %s left\n",c->name.c_str());fflush(stdout);}
        CLOSESOCK(c->s); delete c; clients.erase(clients.begin()+i);
      } else ++i;
    }
  }
}
