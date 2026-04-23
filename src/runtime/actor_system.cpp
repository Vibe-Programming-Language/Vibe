#include "actor_system.h"
#include <iostream>

namespace nova {
namespace actor {

Actor::Actor(const std::string& name) : name_(name), running_(false) {}

void Actor::send(Message msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    mailbox_.push(msg);
    cv_.notify_one();
}

void Actor::start() {
    running_ = true;
    thread_ = std::thread(&Actor::loop, this);
}

void Actor::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    running_ = false;
    cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Actor::loop() {
    while (running_ || (!mailbox_.empty())) {
        Message msg;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this]{ return !mailbox_.empty() || !running_; });
            if (!mailbox_.empty()) {
                msg = mailbox_.front();
                mailbox_.pop();
            } else {
                continue;
            }
        }
        receive(msg);
    }
}

void ActorSystem::register_actor(std::shared_ptr<Actor> a) {
    actors_.push_back(a);
}

void ActorSystem::spawn_all() {
    for (auto& a : actors_) {
        a->start();
    }
}

void ActorSystem::shutdown_all() {
    for (auto& a : actors_) {
        a->stop();
    }
}

} // namespace actor
} // namespace nova