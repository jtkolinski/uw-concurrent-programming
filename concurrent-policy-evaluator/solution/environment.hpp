#ifndef ENVIRONMENT_HPP
#define ENVIRONMENT_HPP

#include <unistd.h>
#include <string>
#include <vector>
#include <sys/types.h>
#include <signal.h>

class Environment {
private:
    int pid;

    void launch(std::string &test_name) {
        std::vector<const char*> args;

        args.push_back(path);
        args.push_back(test_name.c_str());
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

    Environment(std::string &test_name, int* to_env, int* from_env) {
        int env_pid = fork();
        if (env_pid < 0) exit(1);
        if (!env_pid) { // env process
            close(to_env[1]);
            close(from_env[0]);

            if (dup2(to_env[0], STDIN_FILENO) < 0) exit(1);
            if (dup2(from_env[1], STDOUT_FILENO) < 0) exit(1);
            
            launch(test_name);
        }
        else { // main process
            close(to_env[0]);
            close(from_env[1]);

            pid = env_pid;
        }
    }

    int get_pid() { return pid; }
};

#endif