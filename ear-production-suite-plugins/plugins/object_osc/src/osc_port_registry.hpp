#pragma once

#include "JuceHeader.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace ear {
namespace plugin {

// A UDP port can only be bound by one socket. Several "EAR Object OSC"
// instances (all the same loaded plugin module within one host process,
// e.g. several REAPER tracks) legitimately want to listen on the SAME
// port and tell themselves apart by ControlGRIS's "OSC plugin ID" (this
// plugin's Src# field) - each instance owning its own OSCReceiver and
// independently calling connect(port) does not give them that: only the
// first one to connect actually binds the socket, and every other
// instance stays silently disconnected even though its UI shows the OSC
// toggle on.
//
// This process-wide singleton owns exactly one OSCReceiver per port and
// fans every message received on that port out to every ObjectsAudioProcessor
// currently subscribed to it. Each processor still does its own
// pluginId/Src# (and protocol/address) filtering on the messages it
// receives, exactly as it would if it owned the socket itself.
class OscPortRegistry {
 public:
  class Subscriber {
   public:
    virtual ~Subscriber() = default;
    virtual void handleOscMessage(const OSCMessage& message) = 0;
  };

  static OscPortRegistry& getInstance() {
    static OscPortRegistry instance;
    return instance;
  }

  // Returns false if the port could not be bound (e.g. already in use by
  // something outside this registry, such as another application).
  bool subscribe(int port, Subscriber* subscriber) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ports_.find(port);
    if (it == ports_.end()) {
      auto listener = std::make_unique<PortListener>();
      if (!listener->connect(port)) return false;
      it = ports_.emplace(port, std::move(listener)).first;
    }
    it->second->addSubscriber(subscriber);
    return true;
  }

  void unsubscribe(int port, Subscriber* subscriber) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ports_.find(port);
    if (it == ports_.end()) return;
    it->second->removeSubscriber(subscriber);
    if (it->second->empty()) ports_.erase(it);
  }

 private:
  class PortListener : private OSCReceiver::Listener<OSCReceiver::MessageLoopCallback> {
   public:
    PortListener() { receiver_.addListener(this); }
    ~PortListener() override { receiver_.removeListener(this); }

    bool connect(int port) { return receiver_.connect(port); }

    void addSubscriber(Subscriber* subscriber) {
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      subscribers_.push_back(subscriber);
    }

    void removeSubscriber(Subscriber* subscriber) {
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      subscribers_.erase(
          std::remove(subscribers_.begin(), subscribers_.end(), subscriber),
          subscribers_.end());
    }

    bool empty() const {
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      return subscribers_.empty();
    }

   private:
    void oscMessageReceived(const OSCMessage& message) override {
      // Copied under lock, then dispatched outside it: a subscriber's
      // handler must never be called while holding this lock, in case it
      // (now or later) ends up calling back into subscribe/unsubscribe.
      std::vector<Subscriber*> subscribersCopy;
      {
        std::lock_guard<std::mutex> lock(subscribersMutex_);
        subscribersCopy = subscribers_;
      }
      for (auto* subscriber : subscribersCopy) subscriber->handleOscMessage(message);
    }

    // OSCReceiver::Listener's default oscBundleReceived() is a no-op - a
    // sender that wraps its messages in a bundle (ControlGRIS does) would
    // otherwise be silently ignored.
    void oscBundleReceived(const OSCBundle& bundle) override {
      for (auto const& element : bundle) {
        if (element.isMessage())
          oscMessageReceived(element.getMessage());
        else if (element.isBundle())
          oscBundleReceived(element.getBundle());
      }
    }

    OSCReceiver receiver_;
    mutable std::mutex subscribersMutex_;
    std::vector<Subscriber*> subscribers_;
  };

  std::mutex mutex_;
  std::map<int, std::unique_ptr<PortListener>> ports_;
};

}  // namespace plugin
}  // namespace ear
