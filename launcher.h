#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "imgui.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

struct ChildData {
    char name[16];
    int fd;
    char buffer[4096];
    size_t size;
    size_t lines;
    ImGuiTextBuffer linesBuffer;
    bool autoScroll;
    bool scrollToBottom;

    ChildData() {
        name[0] = '\0';
        fd = -1;
        autoScroll = true;
        scrollToBottom = false;
        clear();
    }

    void setName(const char * _name) {
        strncpy(name, _name, 15);
    }

    void clear(void) {
        buffer[0] = '\0';
        size = 0;
        lines = 0;
        linesBuffer.clear();
    }

    void addLine(const char * _line) {
        linesBuffer.appendf("%s", _line);
        lines++;
    }

    void extractLines(void);
    int recvResponse(void);
};

struct Ioc {
    char * stagePath;
    char * instanceName;
    char * deviceName;
    char * prefix;
    bool started;
    bool wantStart;
    bool wantStop;
    pid_t pid;
    int childStdin;
    char stdinBuffer[256];
    ChildData childStdout;
    ChildData childStderr;
    bool open;

    Ioc(const char * _stagePath, const char * _instanceName, const char * _deviceName, const char * _prefix) {
        stagePath = strdup(_stagePath);
        instanceName = strdup(_instanceName);
        deviceName = strdup(_deviceName);
        prefix = strdup(_prefix);
        started = false;
        wantStart = false;
        wantStop = false;
        pid = 0;
        childStdin = -1;
        stdinBuffer[0] = 0;
        childStdout.setName("stdout");
        childStdout.clear();
        childStderr.setName("stderr");
        childStderr.clear();
        open = false;
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
    void draw(void);
    void show(bool * _open);
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
