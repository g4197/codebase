#ifndef NET_UTILS_H_
#define NET_UTILS_H_

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

inline std::string getIP(const std::string &if_name) {
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, if_name.c_str(), IFNAMSIZ - 1);

    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    return std::string(inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}

inline std::string getMac(const std::string &if_name) {
    static struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, if_name.c_str(), IFNAMSIZ - 1);

    ioctl(fd, SIOCGIFHWADDR, &ifr);
    close(fd);

    return std::string(ifr.ifr_hwaddr.sa_data);
}

#endif  // NET_UTILS_H_