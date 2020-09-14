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

    D("[%d] ENTER %s\n", _level, _name);

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
            D0("[%d] %*s[%s]\n", _level, _level, "", entry->d_name);

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
            D0("[%d] %*s- %s\n", _level, _level, "", entry->d_name);

            // we are only interested in a instance.cmd file at last level
            if (_level == 3) {
                if (strncmp(entry->d_name, "instance.cmd", 12) == 0) {
                    bool ret = parseInstanceFile(_name, entry->d_name);
                    if (! ret) {
                        E("failed to add IOC from instance.cmd in path %s\n", _name);
                    }
                }
            }
        }
    }

    closedir(dir);
    D("[%d] LEAVE %s\n", _level, _name);
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
    D("GOT %s for %s in stage %s\n", _name, instanceName, stagePath);

    size_t pathSz = strlen(_path) + strlen(_name) + 2;
    char * path = (char *)calloc(1, pathSz);
    sprintf(path, "%s/%s", _path, _name);

    // find the lines, extrace the values:
    //    epicsEnvSet("LOCATION", "LAB")
    //    epicsEnvSet("DEVICE_NAME", "FLIR1")
    //    epicsEnvSet("CAMERA_NAME", "FLIR-Blackfly S BFS-PGE-70S7M-20177339")

    FILE *fp = fopen(path, "r");
    if (! fp) {
        E("fopen() failed %s\n", strerror(errno));
        free(path);
        return false;
    }

    D("processing file %s\n", path);
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
            D("found LOCATION: '%s'\n", loc);
        } else if (strncmp(line, "epicsEnvSet(\"DEVICE_NAME\"", 24) == 0) {
            dev = parseInstanceLine(line);
            D("found DEVICE_NAME: '%s'\n", dev);
        } else if (strncmp(line, "epicsEnvSet(\"CAMERA_NAME\"", 24) == 0) {
            deviceName = parseInstanceLine(line);
            D("found CAMERA_NAME: '%s'\n", deviceName);
        }
    }
    fclose(fp);

    // macro values might not be found for some reason
    if ((loc == NULL) || (dev == NULL) || (deviceName == NULL)) {
        D("skipping invalid %s file!\n", path);
        if (loc) free(loc);
        if (dev) free(dev);
        if (deviceName) free(deviceName);
        free(path);
        free(strdup1);
        free(strdup2);
        return false;
    }

    size_t prefixSz = strlen(loc) + strlen(dev) + 3;
    char * prefix = (char *)calloc(1, prefixSz);
    sprintf(prefix, "%s:%s:", loc, dev);
    // create a IOC object
    addIoc(new Ioc(stagePath, instanceName, deviceName, prefix));
    D("nr IOCs %ld\n", count());

    free(path);
    free(loc);
    free(dev);
    free(deviceName);
    free(prefix);
    free(strdup1);
    free(strdup2);

    return true;
}

size_t IocList::populate(void) {
    if (strlen(topPath) == 0) {
        E("empty top path\n");
        return 0;
    }

    D("using top path %s\n", topPath);
    listDir(topPath, 0);
    D("found %ld IOCs\n", count());

    return count();
}

void IocList::clear() {
    D("have %ld IOCs\n", count());
    for (size_t n = 0; n < count(); n++) {
        delete list[n];
    }
    list.clear();
}



void ChildData::extractLines(void) {
    char * s = buffer;
    char * e = buffer;
    char * eob = &buffer[size];
    char c;
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
    // handle the data residue without '\n'
    if (eob - s) {
        size_t rem = eob - s;
        size_t from = s - buffer;
        // move to the start of the buffer and remember the size
        memmove(&buffer[0], &buffer[from], rem);
        size = rem;
        buffer[size] = '\0';
        D("%s moved %zu bytes from %zu to start, size %ld: \n'%s'\n",
          name, rem, from, size, buffer);
    } else {
        // nothing left
        size = 0;
        buffer[size] = '\0';
    }
}

int ChildData::recvResponse(void) {
    struct pollfd fds;
    nfds_t nfds = 1;
    // timeout in milliseconds
    int timeout = 1;

    fds.fd = fd;
    fds.events = POLLIN;
    fds.revents = 0;

    int n = poll(&fds, nfds, timeout);
    if (n == -1) {
        E("poll() %s failed %s\n", name, strerror(errno));
    } else if (n) {
        D("%s revents %d ..\n", name, fds.revents);
        if (fds.revents & POLLHUP) {
            // remote end has closed the connection (exit issued?)
            errno = EPIPE;
            E("**** IOC not responding ***");
            addLine("**** IOC not responding ***");
            // return error
            n = -1;
        } else if (fds.revents & POLLIN) {
            D("%s size %zu ..\n", name, size);
            n = read(fd, buffer + size, 4095 - size);
            size += n;
            buffer[size] = '\0';
            D("%s nRecv %d size %zu, RECV: \n'%s'\n",
                    name, n, size, buffer);
            extractLines();
            // return number of bytes available in buffer
            n = size;
        } else {
            E("%s UNHANDLED revents %d ..\n", name, fds.revents);
        }
    } else {
        // D("timeout occured\n");
    }

    return n;
}





int Ioc::start() {
    int pipe_stdin[2];
    int pipe_stdout[2];
    int pipe_stderr[2];

    D("starting IOC %s\n", deviceName);
    if (started) {
        D("IOC %s already started, PID %d\n", deviceName, pid);
        return 0;
    }
    assert(pid == 0);
    assert(childStdin == -1);
    assert(childStdout.fd == -1);
    assert(childStderr.fd == -1);

    if (pipe(pipe_stdin)) {
        E("pipe() failed %s\n", strerror(errno));
        return -1;
    }
    if (pipe(pipe_stdout)) {
        E("pipe() failed %s\n", strerror(errno));
        return -1;
    }
    if (pipe(pipe_stderr)) {
        E("pipe() failed %s\n", strerror(errno));
        return -1;
    }
    D("IO pipe FDs pipe_stdin %d, %d pipe_stdout %d, %d pipe_stderr %d, %d\n",
      pipe_stdin[0], pipe_stdin[1], pipe_stdout[0], pipe_stdout[1], pipe_stderr[0], pipe_stderr[1]);

    pid_t p = fork();
    if (p == 0) {
        // child
        close(pipe_stdin[1]);
        dup2(pipe_stdin[0], 0);
        close(pipe_stdout[0]);
        dup2(pipe_stdout[1], 1);
        close(pipe_stderr[0]);
        dup2(pipe_stderr[1], 2);

        // ask kernel to deliver SIGTERM in case the parent dies
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        errno = 0;
        D("'%s %s %s %s %s %s'\n",
              "tools/start_ioc.sh", "start_ioc.sh", "dev", stagePath, instanceName, "0000");
        execl("tools/start_ioc.sh", "start_ioc.sh", "dev", stagePath, instanceName, "0000", (char*) NULL);

        // nothing below this line should be executed by child process
        // in case it is, execl() failed; lets exit
        E("execl() failed %s\n", strerror(errno));
        exit(1);
    }
    D("IOC %s started, PID %d!\n", deviceName, p);

    // the code below will be executed only by parent only
    // close unused pipe ends
    close(pipe_stdin[0]);
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    // store child info for later use
    pid = p;
    childStdin = pipe_stdin[1];
    childStdout.fd = pipe_stdout[0];
    childStdout.clear();
    childStderr.fd = pipe_stderr[0];
    childStderr.clear();
    started = true;

    return 0;
}

int Ioc::stop() {

    if (! started) {
        D("IOC %s not started\n", deviceName);
        return 0;
    }
    assert(pid != 0);
    D("stopping IOC %s, PID %d\n", deviceName, pid);

    // send SIGKILL signal to the child process
    kill(pid, SIGKILL);

    // wait child state to change
    int status = 0;
    int ret = waitpid(pid, &status, 0);
    if (ret < 0) {
        E("waitpid() failed %s\n", strerror(errno));
        return -1;
    }
    D("waitpid() returned %d\n", ret);
    if (ret == 0) {
        E("no child state change?!\n");
        return -1;
    }

    // child is gone..
    if (status > 0) {
        if (WIFEXITED(status)) {
            D("child %d terminated normally, status %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            D("child %d terminated by a signal %d\n", pid, WTERMSIG(status));
        }
    }

    started = false;
    pid = 0;
    close(childStdin);
    childStdin = -1;
    close(childStdout.fd);
    childStdout.fd = -1;
    close(childStderr.fd);
    childStderr.fd = -1;

    D("IOC %s stopped\n", deviceName);
    return 0;
}

int Ioc::sendCommand(const char * _command) {
    size_t cmdSz = strlen(_command);
    D("new command for child [%zu]] '%s'\n", cmdSz, _command);

    write(childStdin, _command, cmdSz);
    write(childStdin, "\n", 1);

    return 0;
}

int Ioc::recvResponse(void) {

    int ret = 0;

    ret |= childStderr.recvResponse();
    ret |= childStdout.recvResponse();
    if (ret == -1) {
        E("poll()/read() failed %s\n", strerror(errno));
    }

    return ret;
}

void Ioc::draw(void) {
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
    ImGui::Text("PID %d", pid);
    ImGui::Separator();

    ImGui::PushID("StdOut");
    ImGui::Checkbox("auto scroll", &childStdout.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("clear")) {
        childStdout.clear();
    }
    ImGui::SameLine();
    ImGui::Text("%zu lines, %d bytes", childStdout.lines, childStdout.linesBuffer.size());
    ImGui::PopID();

    ImGui::PushID("StdErr");
    ImGui::Checkbox("auto scroll", &childStderr.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("clear")) {
        childStderr.clear();
    }
    ImGui::SameLine();
    ImGui::Text("%zu lines, %d bytes", childStderr.lines, childStderr.linesBuffer.size());
    ImGui::PopID();

    ImGui::Separator();
    // show command input text field
    bool reclaim_focus = false;
    ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("Input", stdinBuffer, IM_ARRAYSIZE(stdinBuffer), input_text_flags)) {
        // send the command to the IOC shell
        sendCommand(stdinBuffer);
        strcpy(stdinBuffer, "");
        // on command input, we scroll to bottom even if AutoScroll==false
        childStdout.scrollToBottom = true;
        childStderr.scrollToBottom = true;
        reclaim_focus = true;
    }

    // Auto-focus on window apparition
    ImGui::SetItemDefaultFocus();
    if (reclaim_focus) {
        // Auto focus previous widget
        ImGui::SetKeyboardFocusHere(-1);
    }

    // get IOC shell output/error bytes
    int ret = recvResponse();
    if (ret < 0) {
        // child has closed the pipe.. stop the communication
        if (errno == EPIPE) {
            stop();
        }
    }

    // show the IOC shell output response
    ImGui::Separator();
    ImGui::BeginChild("OutLog", ImVec2(0, -103));
    ImGui::TextUnformatted(childStdout.linesBuffer.begin(), childStdout.linesBuffer.end());
    if (childStdout.scrollToBottom || (childStdout.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())){
        ImGui::SetScrollHereY(1.0f);
    }
    childStdout.scrollToBottom = false;
    ImGui::EndChild();

    // show the IOC shell error response
    ImGui::Separator();
    ImGui::BeginChild("ErrLog", ImVec2(0, 100));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::TextUnformatted(childStderr.linesBuffer.begin(), childStderr.linesBuffer.end());
    if (childStderr.scrollToBottom || (childStderr.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())){
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::PopStyleColor();
    childStderr.scrollToBottom = false;
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

IocList *  launcherInitialize(void) {
    IocList * iocs = new IocList();
    IM_ASSERT(iocs != NULL);
    D("starting loop!\n");
    return iocs;
}

void launcherDraw(IocList * _iocs) {
    IM_ASSERT(_iocs != NULL);

    ImGui::Begin("Main Window");

    // set the top IOC path
    if (strlen(_iocs->topPath) == 0) {
        strncpy(_iocs->topPath, "/data/bdee", 512);
    }
    ImGui::InputText("IOCs location", _iocs->topPath, IM_ARRAYSIZE(_iocs->topPath));

    if (ImGui::Button("Scan for IOCs")) {
        // removes all the IOC objects
        // XXX what happens to the ones that are started?
        _iocs->clear();
        _iocs->populate();
    }

    if (_iocs->count() > 0) {
        ImGui::Columns(5, "mycolumns");
        ImGui::Separator();
        ImGui::Text("ID"); ImGui::NextColumn();
        ImGui::Text("Name"); ImGui::NextColumn();
        ImGui::Text("Prefix"); ImGui::NextColumn();
        ImGui::Text("Started"); ImGui::NextColumn();
        ImGui::Text("Open"); ImGui::NextColumn();
        ImGui::Separator();
        for (size_t n = 0; n < _iocs->count(); n++) {
            ImGui::PushID(n);
            ImGui::Text("%04ld", n); ImGui::NextColumn();
            Ioc * ioc = _iocs->ioc(n);
            ImGui::Text("%s", ioc->deviceName); ImGui::NextColumn();
            ImGui::Text("%s", ioc->prefix); ImGui::NextColumn();
            ImGui::Text("%s", ioc->started ? "YES" : "NO"); ImGui::NextColumn();
            if (ImGui::Button("Open")) {
                ioc->open = true;
            }
            ImGui::NextColumn();
            // show the IOC control window
            if (ioc->open) {
                ioc->show(&ioc->open);
            }
            ImGui::PopID();
        }
        ImGui::Columns(1);
    } // iocs->count() > 0)

    ImGui::End();
}

void launcherDestroy(IocList * _iocs) {
    D("out of the loop\n");
    if (_iocs) {
        delete _iocs;
    }
}
