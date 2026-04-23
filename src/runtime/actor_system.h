#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <memory>

namespace nova {
namespace actor {

class Message {
public:
    int type;
    std::string payload;
};

class Actor {
public:
    Actor(const std::string& name);
    virtual ~Actor() = default;

    void send(Message msg);
    virtual void receive(Message msg) = 0;   // User implemented logic

    void start();
    void stop();

protected:
    std::string name_;
    std::queue<Message> mailbox_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool running_;
    std::thread thread_;

private:
    void loop();
};

class ActorSystem {
public:
    void register_actor(std::shared_ptr<Actor> a);
    void spawn_all();
    void shutdown_all();
private:
    std::vector<std::shared_ptr<Actor>> actors_;
};

} // namespace actor
} // namespace nova