#ifndef STATE_SIZE
#define STATE_SIZE 10
#endif

#ifndef ACTION_SIZE
#define ACTION_SIZE 10
#endif

#ifndef NAME_SIZE
#define NAME_SIZE 10
#endif

#include "policy.hpp"
#include "environment.hpp"
#include "test.hpp"

#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <queue>
#include <set>
#include <algorithm>
#include <array>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

volatile sig_atomic_t interrupted = 0;

void sigint_handler(int signum) {
    (void)signum;
    interrupted = 1;
}

int main(int argc, const char** argv) {
    if (argc < 6) exit(1);

    signal(SIGPIPE, SIG_IGN);

    const char* policyPath = argv[1];
    const char* envPath = argv[2];
    int maxConcurrentPolicyCalls = std::stoi(argv[3]);
    int maxConcurrentCalls = std::stoi(argv[4]);
    int maxActiveEnvironments = std::stoi(argv[5]);

    maxConcurrentPolicyCalls = std::min(maxConcurrentPolicyCalls, maxConcurrentCalls); // ZOBACZYC CO Z TYM!!!!

    const char** extraArgs = nullptr;
    if (argc > 6) extraArgs = argv+6;

    // Install SIGINT handler
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0) exit(1);

    // Set stdin to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0) exit(1);
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) exit(1);

    Policy::setPathandArgs(policyPath, extraArgs);
    Environment::setPathandArgs(envPath, extraArgs);

    // Tests
    int test_nr = 0, out_nr = 0;
    std::map<int, std::pair<std::string, std::string>> finished_tests;
    std::map<int, std::unique_ptr<Test>> active_tests;
    std::queue<std::string> pending_tests;

    // Policy
    std::vector<std::unique_ptr<Policy>> free_policies;

    // Resource tracking
    int working_policies = 0;
    int working_environments = 0;
    int active_environments = 0;
    
    int next_policy_idx = 0;

    std::string test_name;
    char read_buf[NAME_SIZE+1];
    bool stdin_eof = false;

    bool progress = false;
    
    while (!interrupted && (!stdin_eof || !active_tests.empty() || !pending_tests.empty())) {

        // Test name input
        if (!stdin_eof) {
            int bytes_to_read = NAME_SIZE - int(test_name.size()) + 1;
            int bytes_read = read(STDIN_FILENO, read_buf, bytes_to_read);
            
            if (bytes_read > 0) {
                progress = true;
                bool all_read = (read_buf[bytes_read - 1] == '\n');
                test_name.append(read_buf, read_buf + bytes_read - (all_read ? 1 : 0));
                if (all_read) {
                    pending_tests.emplace(test_name);
                    test_name.clear();
                }
            } else if (bytes_read == 0) {
                stdin_eof = true;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                exit(1);
            }
        }

        // Launching new tests (with resource constraints)

        while (pending_tests.size() && 
               active_environments < maxActiveEnvironments &&
               working_environments + working_policies < maxConcurrentCalls) {
            progress = true;
            std::string test = pending_tests.front();
            pending_tests.pop();

            ++active_environments;
            ++working_environments;

            auto test_ptr = std::make_unique<Test>(test);
            active_tests[test_nr++] = std::move(test_ptr);
        }

        // Advancing tests logic

        std::vector<int> to_erase;
        for (auto &[nr, ptr] : active_tests) {
            if (ptr->get_status() == Status::FINISHED) {
                finished_tests[nr] = std::make_pair(ptr->get_name(), ptr->get_result());
                to_erase.emplace_back(nr);
                active_environments--;
            }
            else {
                if (ptr->get_status() == Status::WAITING_POLICY && free_policies.empty() && next_policy_idx < maxConcurrentPolicyCalls) {
                    free_policies.emplace_back(std::make_unique<Policy>(next_policy_idx++));
                }
                ptr->logic(working_policies, working_environments, maxConcurrentPolicyCalls,
                            maxConcurrentCalls, free_policies, progress);
            }
        }
        for (int nr : to_erase) active_tests.erase(nr);

        // Output finished results

        while (finished_tests.find(out_nr) != finished_tests.end()) {
            progress = true;
            auto &[name, state] = finished_tests[out_nr];
            std::cout << name << ' ' << state << std::endl;
            finished_tests.erase(out_nr);
            ++out_nr;
        }

        if (!progress) usleep(100);
        else progress = false;
    }

    // Cleanup: wait for all child processes
    for (auto& policy : free_policies) if (policy) {
        close(policy->get_to_p(1));
        close(policy->get_from_p(0));
        if (policy->get_pid() > 0) {
            kill(policy->get_pid(), SIGINT);
            waitpid(policy->get_pid(), nullptr, 0);
        }
    }
    
    for (auto &[nr, ptr] : active_tests) {
        if (ptr->get_environment_pid() > 0) {
            kill(ptr->get_environment_pid(), SIGINT);
            waitpid(ptr->get_environment_pid(), nullptr, 0);
        }

        auto policy = ptr->steal_policy();
        if (policy) {
            close(policy->get_to_p(1));
            close(policy->get_from_p(0));
            if (policy->get_pid() > 0) {
                kill(policy->get_pid(), SIGINT);
                waitpid(policy->get_pid(), nullptr, 0);
            }
        }
    }

    return interrupted ? 2 : 0;
}