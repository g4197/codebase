#include <bits/stdc++.h>

#include "stdrdma.h"
#include "stdutils.h"

using namespace std;
using namespace rdma;

// Test the performance of on-chip CAS and FAA.

constexpr char *client_ip = "10.0.2.163";
constexpr int client_port = 1234;
constexpr char *server_ip = "10.0.2.172";
constexpr int server_port = 1234;

int main(int argc, char **argv) {
    if (argc != 2) return;
    if (atoi(argv[1]) == 0) {
        // Client
        Context ctx(client_ip, client_port);
    } else {
        Context ctx(server_ip, server_port);
    }
}