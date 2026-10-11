#ifndef TEST_HPP
#define TEST_HPP

#include "policy.hpp"
#include "environment.hpp"

#include <memory>
#include <string>
#include <algorithm>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

enum class Status {
    WAITING_ENV,
    WAITING_POLICY,
    READING_STATE,
    READING_ACTION,
    WRITING_STATE,
    WRITING_ACTION,
    WAITING_TO_END,
    FINISHED
};

class Test {
private:
    std::string name;
    Status status;
    std::unique_ptr<Policy> policy;
    std::unique_ptr<Environment> environment;

    std::string result;

    // Reading 

    int left_to_read;
    int total_bytes_read = 0;
    char read_buf[std::max(STATE_SIZE, ACTION_SIZE)+1];

    int read_fd(int fd) {
        int bytes_read = read(fd, read_buf + total_bytes_read, left_to_read);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            exit(1);
        } else if (bytes_read == 0) {
            exit(1);
        }
        total_bytes_read += bytes_read;
        left_to_read -= bytes_read;
        return bytes_read;
    }

    // Writing

    int left_to_write;
    int total_bytes_written = 0;
    char write_buf[std::max(STATE_SIZE, ACTION_SIZE)+1];

    int write_fd(int fd) {
        int bytes_written = write(fd, write_buf + total_bytes_written, left_to_write);
        if (bytes_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            exit(1);
        }
        total_bytes_written += bytes_written;
        left_to_write -= bytes_written;
        return bytes_written;
    }

    int to_env[2], from_env[2];

public:
    Test(std::string _name)
        : name(_name)
        , status(Status::READING_STATE)
        , policy(nullptr) {
            
        if (pipe(to_env) < 0) exit(1);
        if (pipe(from_env) < 0) exit(1);
        
        fcntl(to_env[0], F_SETFD, FD_CLOEXEC);
        fcntl(to_env[1], F_SETFD, FD_CLOEXEC);
        fcntl(from_env[0], F_SETFD, FD_CLOEXEC);
        fcntl(from_env[1], F_SETFD, FD_CLOEXEC);
        
        // Set non-blocking mode on our end of pipes
        fcntl(to_env[1], F_SETFL, O_NONBLOCK);
        fcntl(from_env[0], F_SETFL, O_NONBLOCK);
           
        environment = std::make_unique<Environment>(name, to_env, from_env);
        left_to_read = STATE_SIZE + 1;
        total_bytes_read = 0;
    }

    ~Test() {
        close(to_env[1]);
        close(from_env[0]);
    }

    void logic(int& working_policies, int& working_envs,
               int max_policies, int max_calls,
               std::vector<std::unique_ptr<Policy>>& free_policies,
               bool& progress) {

        int total_working = working_policies + working_envs;

        if (status == Status::READING_STATE) {
            if (read_fd(from_env[0])) progress = true;
            if (left_to_read) return;
            
            progress = true;
            --working_envs;

            if (read_buf[0] == 'T') {
                result = std::string(read_buf, read_buf+STATE_SIZE);
                status = Status::WAITING_TO_END;
            }
            else {
                for (int i = 0; i < STATE_SIZE; ++i)
                    write_buf[i] = read_buf[i];
                write_buf[STATE_SIZE] = '\n';

                status = Status::WAITING_POLICY;
            }
        }
        else if (status == Status::WAITING_POLICY) {
            if (free_policies.empty() || working_policies+1 > max_policies
                || total_working+1 > max_calls) {

                return;
            }
            progress = true;

            policy = std::move(free_policies.back());
            free_policies.pop_back();

            ++working_policies;

            status = Status::WRITING_STATE;
            left_to_write = STATE_SIZE + 1;
            total_bytes_written = 0;
        }
        else if (status == Status::WRITING_STATE) {
            if (write_fd(policy->get_to_p(1))) progress = true;
            if (left_to_write) return;
            progress = true;
            
            status = Status::READING_ACTION;
            left_to_read = ACTION_SIZE + 1;
            total_bytes_read = 0;
        }
        else if (status == Status::READING_ACTION) {
            if (read_fd(policy->get_from_p(0))) progress = true;
            if (left_to_read) return;
            progress = true;
            
            --working_policies;

            free_policies.emplace_back(std::move(policy));
            // policy = std::make_unique<Policy>(nullptr);

            for (int i = 0; i < ACTION_SIZE; ++i)
                write_buf[i] = read_buf[i];
            write_buf[ACTION_SIZE] = '\n';
            
            status = Status::WAITING_ENV;
        }
        else if (status == Status::WAITING_ENV) {
            if (total_working+1 > max_calls) return;
            progress = true;
            
            ++working_envs;

            status = Status::WRITING_ACTION;
            left_to_write = ACTION_SIZE + 1;
            total_bytes_written = 0;
        }
        else if (status == Status::WRITING_ACTION) {
            if (write_fd(to_env[1])) progress = true;
            if (left_to_write) return;
            progress = true;
            
            status = Status::READING_STATE;
            left_to_read = STATE_SIZE + 1;
            total_bytes_read = 0;
        }
        else if (status == Status::WAITING_TO_END) {
            int result = waitpid(environment->get_pid(), nullptr, WNOHANG);
            if (result < 0) exit(1);
            else if (result > 0) status = Status::FINISHED, progress = true;
        }
    }

    Status get_status() { return status; }
    std::string get_result() { return result; }
    std::string get_name() { return name; }

    int get_environment_pid() { return environment ? environment->get_pid() : -1; }
    auto steal_policy() { return std::move(policy); } // Used only during cleanup
};

#endif