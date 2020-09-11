#ifndef LAUNCHER_H
#define LAUNCHER_H

//#include "console.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

struct Ioc {
    char * stagePath;
    char * instanceName;
    char * deviceName;
    char * prefix;
    bool started;
    bool wantStart;
    bool wantStop;
    int toChild;
    int fromChild;
    pid_t pid;
//    Console console;
//    bool consoleStatus;
    char sendBuffer[256];
    char recvBuffer[4096];
    size_t recvSize;
    bool recvNewData = false;

    Ioc(const char * _stagePath, const char * _instanceName, const char * _deviceName, const char * _prefix) {
        stagePath = strdup(_stagePath);
        instanceName = strdup(_instanceName);
        deviceName = strdup(_deviceName);
        prefix = strdup(_prefix);
        started = false;
        wantStart = false;
        wantStop = false;
        toChild = -1;
        fromChild = -1;
        pid = 0;
        sendBuffer[0] = 0;
        recvBuffer[0] = 0;
        recvSize = 0;
        recvNewData = false;
    }
    ~Ioc() {
        if (stagePath) { free(stagePath); }
        if (instanceName) { free(instanceName); }
        if (deviceName) { free(deviceName); }
        if (prefix) { free(prefix); }
    }
    bool isStarted(void) {
        return started;
    }
    int start();
    int stop();
    int sendCommand(const char * _command);
    int recvResponse(void);
};

struct IocList {
    std::vector<Ioc *> list;
    char * top;

    IocList() {
        clear();
    }
    ~IocList() {
        clear();
    }
    size_t populate(const char *_path);
    void clear();
    void listDir(const char * _name, int _level);
    bool parseInstanceFile(const char *_path, const char * _name);
    char * parseInstanceLine(char *_line);

    void addIoc(Ioc * _ioc) {
        list.push_back(_ioc);
    }
    size_t count() {
        return list.size();
    }
    Ioc * ioc(const size_t n) {
        if (n < list.size()) {
            return list[n];
        }
        return NULL;
    }
};

#endif // LAUNCHER_H
