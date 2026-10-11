#ifndef POLICY_HPP
#define POLICY_HPP

#include <string>
#include <vector>

#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>

class Policy {
private:
    int pid = 0;
    int idx = 0;
    int to_p[2], from_p[2];

    void launch() {
        std::string idxStr = std::to_string(idx);
        std::vector<const char*> args;

        args.push_back(path);
        args.push_back(idxStr.c_str());
        if (extraArgs) {
            for (int i = 0; extraArgs[i]; ++i)
                args.push_back(extraArgs[i]);
        }
        args.push_back(NULL);

        execvp(path, const_cast<char* const*>(args.data()));
        exit(1);
    }

public:
    static inline const char* path = nullptr;
    static inline const char** extraArgs = nullptr;

    static void setPathandArgs(const char* _path, const char** _extraArgs) {
        path = _path;
        extraArgs = _extraArgs;
    }

    Policy(){}
    Policy(int _idx) : idx(_idx) {
        if (pipe(to_p) < 0) exit(1);
        if (pipe(from_p) < 0) exit(1);
        
        fcntl(to_p[0], F_SETFD, FD_CLOEXEC);
        fcntl(to_p[1], F_SETFD, FD_CLOEXEC);
        fcntl(from_p[0], F_SETFD, FD_CLOEXEC);
        fcntl(from_p[1], F_SETFD, FD_CLOEXEC);
        
        // Set non-blocking mode on our end of pipes
        fcntl(to_p[1], F_SETFL, O_NONBLOCK);
        fcntl(from_p[0], F_SETFL, O_NONBLOCK);

        int policy_pid = fork();
        if (policy_pid < 0) exit(1);
        if (!policy_pid) { // policy process
            close(to_p[1]);
            close(from_p[0]);

            if (dup2(to_p[0], STDIN_FILENO) < 0) exit(1);
            if (dup2(from_p[1], STDOUT_FILENO) < 0) exit(1);
            
            launch();
        }
        else { // main process
            close(to_p[0]);
            close(from_p[1]);

            pid = policy_pid;
        }
    }

    // ~Policy() {
    //     close(to_p[1]);
    //     close(from_p[0]);
    // }

    int get_pid() { return pid; }
    int get_idx() { return idx; }

    int get_to_p(int idx) { return to_p[idx]; }
    int get_from_p(int idx) { return from_p[idx]; }
};

#endif