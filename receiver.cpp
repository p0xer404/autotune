#include <iostream>
#include <string>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>
using json = nlohmann::json;

int main() {
    std::string server_ip = "SERVER_PUBLIC_IP"; // IP pública o DNS del sender
    int port = 5000;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){ std::cerr<<"Error creant socket\n"; return 1; }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
#ifdef _WIN32
    server.sin_addr.S_un.S_addr = inet_addr(server_ip.c_str());
#else
    server.sin_addr.s_addr = inet_addr(server_ip.c_str());
#endif

    if(connect(sock,(struct sockaddr*)&server,sizeof(server))<0){
        std::cerr<<"Error connectant amb el servidor\n";
        return 1;
    }

    json j;
    j["os"]="Desktop Windows";
    j["device_type"]="desktop";
    j["volume"]=0.4;
    j["bandwidth"]=0.8;

    std::string message = j.dump();
    send(sock,message.c_str(),message.size(),0);

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    std::cout<<"Info enviada al transmissor!\n";
    return 0;
}
