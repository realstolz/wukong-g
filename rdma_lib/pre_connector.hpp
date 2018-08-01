/*
 * Copyright (c) 2016 Shanghai Jiao Tong University.
 *     All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS
 *  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied.  See the License for the specific language
 *  governing permissions and limitations under the License.
 *
 * For more about this software visit:
 *
 *      http://ipads.se.sjtu.edu.cn/projects/wukong
 *
 */

#pragma once

#include "utils.hpp"

#include <netdb.h> //hostent
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>

namespace rdmaio {

constexpr struct timeval default_timeout = {0, 2000};

// helper class used to exchange QP information using TCP/IP
class PreConnector {
public:
    static int get_listen_socket(const std::string &addr, int port) {

        struct sockaddr_in serv_addr;
        auto sockfd =  socket(AF_INET, SOCK_STREAM, 0);
        CE(sockfd < 0, "ERROR opening socket");

        /* setup the host_addr structure for use in bind call */
        serv_addr.sin_family = AF_INET; // server byte order
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(port);   // port

        CE(bind(sockfd, (struct sockaddr *) &serv_addr,
                sizeof(serv_addr)) < 0, "ERROR on binding");
        return sockfd;
    }

    static int get_send_socket(const std::string &addr, int port,
                               struct timeval timeout = default_timeout) {
        int sockfd;
        struct sockaddr_in serv_addr;

        CE((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0, "ERROR open socket!");
        fcntl(sockfd, F_SETFL, O_NONBLOCK);

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        auto ip = host_to_ip(addr);
        serv_addr.sin_addr.s_addr = inet_addr(ip.c_str());

        connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

        // check return status
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);

        if (select(sockfd + 1, NULL, &fdset, NULL, &timeout) == 1) {
            int so_error;
            socklen_t len = sizeof so_error;

            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);

            if (so_error == 0) {
                // success
            } else {
                close(sockfd);
                return -1;
            }
        }

        return sockfd;
    }

    // timeout in microsend
    static bool wait_recv(int socket, uint32_t timeout = 6000) {
        // return util the socket has incoming replies

        struct timeval start_time_msec;
        struct timeval cur_time_msec;

        gettimeofday (&start_time_msec, NULL);
        while (true) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(socket, &rfds);

            int ready = select(socket + 1, &rfds, NULL, NULL, NULL);
            assert(ready != -1);

            if (ready == 0) {

                gettimeofday (&cur_time_msec, NULL);

                struct timeval res, out = {0, timeout};
                timersub(&cur_time_msec, &start_time_msec, &res);

                if ( timercmp(&res, &out, > )) // receive timeout
                    return false;

                continue;
            }

            if (FD_ISSET(socket, &rfds)) { // success
                break;
            }
        }
        return true;
    }

    static void wait_close(int socket) {
        shutdown(socket, SHUT_WR);
        char buf[2]; // a dummy buf

        // wait close for a timeout
        struct timeval timeout = {1, 0};
        auto ret = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                              (const char*)&timeout, sizeof(timeout));
        assert(ret == 0);

        // dummy receive, returned after the remote has closed
        recv(socket, buf, 2, 0);

        // safe to close the socket now
        close(socket);
    }

    static int send_to(int fd, char *usrbuf, size_t n) {
        size_t nleft = n;
        ssize_t nwritten;
        char *bufp = usrbuf;

        while (nleft > 0) {
            if ((nwritten = write(fd, bufp, nleft)) <= 0) {
                if (errno == EINTR)
                    nwritten = 0;
                else
                    return -1;
            }
            nleft -= nwritten;
            bufp += nwritten;
        }
        return n;
    }

    static std::string host_to_ip(const std::string &host) {
        std::string res = "";
        struct hostent *he;
        struct in_addr **addr_list;

        CE((he = gethostbyname(host.c_str())) == NULL, host.c_str());
        addr_list = (struct in_addr **) he->h_addr_list;

        for (auto i = 0; addr_list[i] != NULL; i++) {
            res = std::string(inet_ntoa(*addr_list[i]));
            break;
        }
        return res;
    }
};


}; // namespace rdmaio
