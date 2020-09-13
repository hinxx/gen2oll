#include "launcher.h"

#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <poll.h>
#include <assert.h>
#include <libgen.h>

// we need to traverse this folder structure:
// lvl0 [root]
// lvl1 [root]/{aaa-stage, bbb-stage,..}
// lvl2 [root]/aaa-stage/{bin,lib,db,dbd,ioc,opi,..}
// lvl3 [root]/aaa-stage/ioc/{basler-acA2440-20gm-23219608, flir-BFS-PGE-70S7M-20177339,..}
// lvl4 [root]/aaa-stage/ioc/flir-BFS-PGE-70S7M-20177339/{instance.cmd,..}
//
// we are interested in lvl1 folder path and lvl4 file instance.cmd
//
void IocList::listDir(const char * _name, int _level) {
    DIR * dir;
    struct dirent * entry;

    printf("%s:%d [%d] ENTER %s\n", __FUNCTION__, __LINE__,  _level, _name);

    if (!(dir = opendir(_name))) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            // this is a directory we might want to recurse into

            // skip . and .. entries
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }


            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", _name, entry->d_name);
            fprintf(stderr, "[%d] %*s[%s]\n", _level, _level, "", entry->d_name);

            bool recurse = true;
            if (_level == 0) {
                // on top level
                // recurse into all sub-folders
            } else if (_level == 1) {
                // on stage level
                // remember the path

                // recurse only if we have folder called 'ioc'
                if (strncmp(entry->d_name, "ioc", 3) != 0) {
                    recurse = false;
                }
            } else if (_level == 2) {
                // recurse into all folders
            } else if (_level == 3) {
                // do not recurse into any folders; last level
                // look for instance.cmd file, see below
                recurse = false;
            } else {
                assert(1 == 0);
            }

            if (recurse) {
                // recurse!
                listDir(path, _level + 1);
            }
        } else {
            // this is a file
            fprintf(stderr, "[%d] %*s- %s\n", _level, _level, "", entry->d_name);

            // we are only interested in a instance.cmd file at last level
            if (_level == 3) {
                if (strncmp(entry->d_name, "instance.cmd", 12) == 0) {
                    bool ret = parseInstanceFile(_name, entry->d_name);
                    if (! ret) {
                        fprintf(stderr, "%s:%d failed to add IOC from instance.cmd in path %s\n", __FUNCTION__, __LINE__, _name);
                    }
                }
            }
        }
    }

    closedir(dir);
    fprintf(stderr, "%s:%d [%d] LEAVE %s\n", __FUNCTION__, __LINE__, _level, _name);
}

// extract value from lines like 'epicsEnvSet("LOCATION", "LAB")'
// where LAB is the value to extract
// value can contain printable characters
char * IocList::parseInstanceLine(char * _line) {
    int n = strlen(_line);
    char * s = _line;
    char * e = &_line[n - 1];
    // skip everthing until comma is reached
    while (s < e) {
        if (*s == ','){
            break;
        }
        s++;
    }
    // skip comma, whitespace and opening double quote
    while (s < e) {
        if (*s == '"') {
            s++;
            break;
        }
        s++;
    }
    // get rid of closing bracket and double quote
    while (e > s) {
        if (isalnum(*e)) {
            break;
        }
        e--;
    }
    *(e + 1) = '\0';
    return strdup(s);
}

bool IocList::parseInstanceFile(const char * _path, const char *_name) {

    char * strdup1 = strdup(_path);
    char * strdup2 = strdup(_path);
    char * instanceName = basename(strdup1);
    char * stagePath = dirname(strdup2);
    stagePath = dirname(stagePath);
    fprintf(stderr, "%s:%d GOT %s for %s in stage %s\n", __FUNCTION__, __LINE__, _name, instanceName, stagePath);

    size_t pathSz = strlen(_path) + strlen(_name) + 2;
    char * path = (char *)calloc(1, pathSz);
    sprintf(path, "%s/%s", _path, _name);

    // find the lines, extrace the values:
    //    epicsEnvSet("LOCATION", "LAB")
    //    epicsEnvSet("DEVICE_NAME", "FLIR1")
    //    epicsEnvSet("CAMERA_NAME", "FLIR-Blackfly S BFS-PGE-70S7M-20177339")

    FILE *fp = fopen(path, "r");
    if (! fp) {
        fprintf(stderr, "%s:%d fopen() failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
        free(path);
        return false;
    }

    char line[256];
    char * loc = NULL;
    char * dev = NULL;
    char * deviceName = NULL;
    while (! feof(fp)) {
        fgets(line, 255, fp);
        if (ferror(fp)) {
            fclose(fp);
            return false;
        }

        if (strncmp(line, "epicsEnvSet(\"LOCATION\"", 22) == 0) {
            loc = parseInstanceLine(line);
            fprintf(stderr, "%s:%d found LOCATION: '%s'\n", __FUNCTION__, __LINE__, loc);
        } else if (strncmp(line, "epicsEnvSet(\"DEVICE_NAME\"", 24) == 0) {
            dev = parseInstanceLine(line);
            fprintf(stderr, "%s:%d found DEVICE_NAME: '%s'\n", __FUNCTION__, __LINE__, dev);
        } else if (strncmp(line, "epicsEnvSet(\"CAMERA_NAME\"", 24) == 0) {
            deviceName = parseInstanceLine(line);
            fprintf(stderr, "%s:%d found CAMERA_NAME: '%s'\n", __FUNCTION__, __LINE__, deviceName);
        }
    }
    fclose(fp);

    size_t prefixSz = strlen(loc) + strlen(dev) + 3;
    char * prefix = (char *)calloc(1, prefixSz);
    sprintf(prefix, "%s:%s:", loc, dev);
    // create a IOC object
    addIoc(new Ioc(stagePath, instanceName, deviceName, prefix));
    fprintf(stderr, "%s:%d nr IOCs %ld\n", __FUNCTION__, __LINE__, count());

    free(path);
    free(loc);
    free(dev);
    free(deviceName);
    free(prefix);
    free(strdup1);
    free(strdup2);

    return true;
}

size_t IocList::populate(const char *_path) {
    fprintf(stderr, "%s:%d supplied top path %s\n", __FUNCTION__, __LINE__, _path);
    listDir(_path, 0);
    fprintf(stderr, "%s:%d found %ld IOCs\n", __FUNCTION__, __LINE__, count());

    return count();
}

void IocList::clear() {
    fprintf(stderr, "%s:%d have %ld IOCs\n", __FUNCTION__, __LINE__, count());
    for (size_t n = 0; n < count(); n++) {
        delete list[n];
    }
    list.clear();
}






int Ioc::start() {
    int pipe_stdin[2];
    int pipe_stdout[2];

    fprintf(stderr, "%s:%d starting IOC %s\n", __FUNCTION__, __LINE__, deviceName);
    if (started) {
        fprintf(stderr, "%s:%d IOC %s already started, PID %d\n", __FUNCTION__, __LINE__, deviceName, pid);
        return 0;
    }
    assert(pid == 0);
    assert(toChild == -1);
    assert(fromChild == -1);

    if (pipe(pipe_stdin)) {
        fprintf(stderr, "%s:%d pipe() failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
        return -1;
    }
    if (pipe(pipe_stdout)) {
        fprintf(stderr, "%s:%d pipe() failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
        return -1;
    }
    fprintf(stderr, "%s:%d IO pipe FDs pipe_stdin [0] = %d, [1] = %d | pipe_stdout [0] = %d, [1] = %d\n",
            __FUNCTION__, __LINE__, pipe_stdin[0], pipe_stdin[1], pipe_stdout[0], pipe_stdout[1]);

    pid_t p = fork();
    if (p == 0) {
        // child
        close(pipe_stdin[1]);
        dup2(pipe_stdin[0], 0);
        close(pipe_stdout[0]);
        dup2(pipe_stdout[1], 1);

        // ask kernel to deliver SIGTERM in case the parent dies
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        errno = 0;
        execl("./data/softIoc", "softIoc", (char*) NULL);

        // nothing below this line should be executed by child process
        // in case it is, execl() failed; lets exit
        fprintf(stderr, "%s:%d execl() failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
        exit(1);
    }
    fprintf(stderr, "%s:%d IOC %s started, PID %d!\n", __FUNCTION__, __LINE__, deviceName, p);

    // the code below will be executed only by parent only
    // close unused pipe ends
    close(pipe_stdin[0]);
    close(pipe_stdout[1]);

    // store child info for later use
    pid = p;
    toChild = pipe_stdin[1];
    fromChild = pipe_stdout[0];
    started = true;

    return 0;
}

int Ioc::stop() {

    if (! started) {
        fprintf(stderr, "%s:%d IOC %s not started\n", __FUNCTION__, __LINE__, deviceName);
        return 0;
    }
    assert(pid != 0);
    fprintf(stderr, "%s:%d stopping IOC %s, PID %d\n", __FUNCTION__, __LINE__, deviceName, pid);

    // send SIGKILL signal to the child process
    kill(pid, SIGKILL);

    // wait child state to change
    int status = 0;
    int ret = waitpid(pid, &status, 0);
    if (ret < 0) {
        fprintf(stderr, "%s:%d waitpid() failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
        return -1;
    }
    fprintf(stderr, "%s:%d waitpid() returned %d\n", __FUNCTION__, __LINE__, ret);
    if (ret == 0) {
        fprintf(stderr, "%s:%d no child state change?!\n", __FUNCTION__, __LINE__);
        return -1;
    }

    // child is gone..
    if (status > 0) {
        if (WIFEXITED(status)) {
            fprintf(stderr, "%s:%d child %d terminated normally, status %d\n",
                    __FUNCTION__, __LINE__, pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "%s:%d child %d terminated by a signal %d\n",
                    __FUNCTION__, __LINE__, pid, WTERMSIG(status));
        }
    }

    started = false;
    pid = 0;
    close(toChild);
    toChild = -1;
    close(fromChild);
    fromChild = -1;

    fprintf(stderr, "%s:%d IOC %s stopped\n", __FUNCTION__, __LINE__, deviceName);
    return 0;
}

int Ioc::sendCommand(const char * _command) {
    size_t cmdSz = strlen(_command);
    fprintf(stderr, "%s:%d new command for child [%zu]] '%s'\n",
            __FUNCTION__, __LINE__, cmdSz, _command);

    write(toChild, _command, cmdSz);
    write(toChild, "\n", 1);

    return 0;
}

int Ioc::recvResponse(void) {

    struct pollfd fds;
    nfds_t nfds = 1;
    // timeout in milliseconds
    int timeout = 1;

    fds.fd = fromChild;
    fds.events = POLLIN;
    fds.revents = 0;
    int n = poll(&fds, nfds, timeout);
//    fprintf(stderr, "%s:%d poll() returned %d, revents %d\n",
//            __FUNCTION__, __LINE__, n, fds.revents);

    if (n == -1) {
        fprintf(stderr, "%s:%d poll() failed %s\n", __FUNCTION__, __LINE__, strerror(errno));
        return -1;
    } else if (n) {
        fprintf(stderr, "%s:%d %d nonzero revents, recvOffset %ld ..\n", __FUNCTION__, __LINE__, n, recvOffset);
        ssize_t nRecv = read(fromChild, recvBuffer + recvOffset, 4095);
        recvBuffer[nRecv] = '\0';
        recvSize = nRecv;
        recvOffset += recvSize;
        fprintf(stderr, "%s:%d nRecv %zd, recvSize %zu, recvOffset %ld, RECV: \n'%s'\n", __FUNCTION__, __LINE__,
                nRecv, recvSize, recvOffset, recvBuffer);
    } else {
//        fprintf(stderr, "%s:%d timeout occured\n", __FUNCTION__, __LINE__);
//        recvNewData = false;
    }

    return 0;
}

void Ioc::extractLines(void) {
    char * s;
    char * e;
    char * eob;
    char c;
    s = recvBuffer;
    e = recvBuffer;
    eob = &recvBuffer[recvSize];
    while (e < eob) {
        if (*e == '\n') {
            e++;
            c = *e;
            *e = '\0';
            addLine(s);
            *e = c;
            s = e;
        }
        e++;
    }
    // XXX: fix handling of the remainder of the buffer
    //      there is always text at the end of the buffer
    //      NOT followed by '\n' : 'epics>'
    recvSize = 0;
    recvOffset = 0;
}

void Ioc::draw(void) {
    ImGui::Text("Buffer contents: %zu lines, %d bytes", lines, textBuf.size());
    if (ImGui::Button("Clear")) {
        clearBuffer();
    }

    // show IOC status
    if (started) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
        ImGui::Text("%s", "STARTED");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("%s", "STOPPED");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    // show IOC start / stop buttons
    if (ImGui::Button("Start")) {
        start();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        stop();
    }
    ImGui::SameLine();
    ImGui::Checkbox("AutoScroll", &autoScroll);

    // show command input text field
    ImGui::Separator();
    bool reclaim_focus = false;
    ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("Input", sendBuffer, IM_ARRAYSIZE(sendBuffer), input_text_flags)) {
        // send the command to the IOC shell
        sendCommand(sendBuffer);
        strcpy(sendBuffer, "");
        // on command input, we scroll to bottom even if AutoScroll==false
        scrollToBottom = true;
        reclaim_focus = true;
    }

    // Auto-focus on window apparition
    ImGui::SetItemDefaultFocus();
    if (reclaim_focus) {
        // Auto focus previous widget
        ImGui::SetKeyboardFocusHere(-1);
    }

    // get read out IOC shell output
    recvResponse();
    if (recvSize) {
        extractLines();
    }

    // show the IOC shell output/command response
    ImGui::Separator();
    ImGui::BeginChild("Log");
    ImGui::TextUnformatted(textBuf.begin(), textBuf.end());
    if (scrollToBottom || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())){
        ImGui::SetScrollHereY(1.0f);
    }
    scrollToBottom = false;
    ImGui::EndChild();
}

void Ioc::show(bool * _open) {
    ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
    if (! ImGui::Begin(deviceName, _open)) {
        ImGui::End();
        return;
    }

    draw();

    ImGui::End();
}
