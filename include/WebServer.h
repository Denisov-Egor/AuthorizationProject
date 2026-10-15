#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "AuthModule.h"

class WebServer {
public:
    explicit WebServer(AuthModule& auth);
    int run(int port = 8080);
private:
    AuthModule& auth_;
};

#endif
