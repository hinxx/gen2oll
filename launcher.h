#ifndef LAUNCHER_H
#define LAUNCHER_H

#include "imgui.h"

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
    int childStdin;
    int childStdout;
    int childStderr;
    pid_t pid;
    char sendBuffer[256];
    char stdoutBuffer[4096];
    size_t stdoutSize;
    size_t stdoutLines;
    ImGuiTextBuffer stdoutLinesBuffer;
    char stderrBuffer[4096];
    size_t stderrSize;
    size_t stderrLines;
    ImGuiTextBuffer stderrLinesBuffer;
    bool open;
    bool autoScroll;
    bool scrollToBottom;

    Ioc(const char * _stagePath, const char * _instanceName, const char * _deviceName, const char * _prefix) {
        stagePath = strdup(_stagePath);
        instanceName = strdup(_instanceName);
        deviceName = strdup(_deviceName);
        prefix = strdup(_prefix);
        started = false;
        wantStart = false;
        wantStop = false;
        childStdin = -1;
        childStdout = -1;
        childStderr = -1;
        pid = 0;
        sendBuffer[0] = 0;
        stdoutBuffer[0] = 0;
        stdoutSize = 0;
        stderrBuffer[0] = 0;
        stderrSize = 0;
        autoScroll = true;
        scrollToBottom = false;
        open = false;
        clearStdoutBuffer();
        clearStderrBuffer();
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
    void extractLinesStdout();
    void extractLinesStderr();

    void clearStdoutBuffer(void) {
        stdoutLines = 0;
        stdoutLinesBuffer.clear();
    }

    void addStdoutLine(const char * _line) {
        stdoutLinesBuffer.appendf("%s", _line);
        stdoutLines++;
    }

    void clearStderrBuffer(void) {
        stderrLines = 0;
        stderrLinesBuffer.clear();
    }

    void addStderrLine(const char * _line) {
        stderrLinesBuffer.appendf("%s", _line);
        stderrLines++;
    }

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
