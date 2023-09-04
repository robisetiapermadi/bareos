/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2023-2023 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/

#ifndef BAREOS_LIB_CHANNEL_H_
#define BAREOS_LIB_CHANNEL_H_

#include <condition_variable>
#include <optional>
#include <vector>
#include <utility>
#include <variant>

#include "lib/thread_util.h"

namespace channel {
// a simple single consumer/ single producer queue
// Its composed of three parts: the input, the output and the
// actual queue data.
// Instead of directly interacting with the queue itself you instead
// interact with either the input or the output.
// This ensures that there is only one producer (who writes to the input)
// and one consumer (who reads from the output).

struct failed_to_acquire_lock {};
struct channel_closed {};

template <typename T> class queue {
  struct internal {
    std::vector<T> data;
    bool in_dead;
    bool out_dead;
  };

  synchronized<internal> shared{};
  std::condition_variable in_update{};
  std::condition_variable out_update{};
  std::size_t max_size;

 public:
  queue(std::size_t max_size) : max_size(max_size) {}

  using locked_type = decltype(shared.lock());

  class handle {
    locked_type locked;
    std::condition_variable* update;

   public:
    handle(locked_type locked, std::condition_variable* update)
        : locked{std::move(locked)}, update(update)
    {
    }

    std::vector<T>& data() { return locked->data; }

    ~handle()
    {
      if (update) { update->notify_one(); }
    }
  };

  /* the *_lock functions return std::nullopt only if the channel is closed
   * otherwise they wait until they get the lock
   * The try_*_lock functions instead return a tristate indicating whether
   * they succeeded, failed to acquire the lock or if the channel was closed
   * from the other side. */


  std::optional<handle> read_lock()
  {
    auto locked = shared.lock();
    if (locked->out_dead) {
      // note(ssura): This happening is programmer error.
      // Maybe we should assert this instead ?
      Dmsg0(50,
            "Tried to read from channel that was closed from the read side.\n");
      return std::nullopt;
    }

    locked.wait(in_update, [](const auto& intern) {
      return intern.data.size() > 0 || intern.in_dead;
    });
    if (locked->data.size() == 0) {
      return std::nullopt;
    } else {
      return std::make_optional<handle>(std::move(locked), &out_update);
    }
  }

  std::optional<handle> write_lock()
  {
    auto locked = shared.lock();
    locked.wait(out_update, [max_size = max_size](const auto& intern) {
      return intern.data.size() < max_size || intern.out_dead;
    });
    if (locked->in_dead) {
      // note(ssura): This happening is programmer error.
      // Maybe we should assert this instead ?
      Dmsg0(50,
            "Tried to write to channel that was closed from the write side.\n");
      return std::nullopt;
    }
    if (locked->out_dead) {
      return std::nullopt;
    } else {
      return std::make_optional<handle>(std::move(locked), &in_update);
    }
  }

  using try_result
      = std::variant<handle, failed_to_acquire_lock, channel_closed>;

  try_result try_read_lock()
  {
    auto locked = shared.try_lock();
    if (!locked) { return failed_to_acquire_lock{}; }
    if (locked.value()->out_dead) {
      // note(ssura): This happening is programmer error.
      // Maybe we should assert this instead ?
      Dmsg0(50,
            "Tried to read from channel that was closed from the read side.\n");
      return channel_closed{};
    }
    if (locked.value()->data.size() == 0) {
      if (locked.value()->in_dead) {
        return channel_closed{};
      } else {
        return failed_to_acquire_lock{};
      }
    }

    return try_result(std::in_place_type<handle>, std::move(locked).value(),
                      &out_update);
  }

  try_result try_write_lock()
  {
    auto locked = shared.try_lock();
    if (!locked) { return failed_to_acquire_lock{}; }
    if (locked.value()->in_dead) {
      // note(ssura): This happening is programmer error.
      // Maybe we should assert this instead ?
      Dmsg0(50,
            "Tried to write to channel that was closed from the write side.\n");
      return channel_closed{};
    }
    if (locked.value()->out_dead) { return channel_closed{}; }
    if (locked.value()->data.size() >= max_size) {
      return failed_to_acquire_lock{};
    }

    return try_result(std::in_place_type<handle>, std::move(locked).value(),
                      &in_update);
  }

  void close_in()
  {
    shared.lock()->in_dead = true;
    in_update.notify_one();
  }

  void close_out()
  {
    shared.lock()->out_dead = true;
    out_update.notify_one();
  }
};

template <typename T> class in {
  std::shared_ptr<queue<T>> shared;
  bool did_close{false};

 public:
  in(std::shared_ptr<queue<T>> shared) : shared{std::move(shared)} {}
  // after move only operator= and ~in are allowed to be called
  in(in&&) = default;
  in& operator=(in&&) = default;
  in(const in&) = delete;
  in& operator=(const in&) = delete;

  template <typename... Args> bool emplace(Args... args)
  {
    if (did_close) { return false; }
    if (auto handle = shared->write_lock()) {
      handle->data().emplace_back(std::forward<Args>(args)...);
      return true;
    } else {
      close();
      return false;
    }
  }

  template <typename... Args> bool try_emplace(Args... args)
  {
    if (did_close) { return false; }
    auto result = shared->try_write_lock();
    if (std::holds_alternative<failed_to_acquire_lock>(result)) {
      return false;
    } else if (std::holds_alternative<channel_closed>(result)) {
      close();
      return false;
    } else {
      std::get<typename queue<T>::handle>(result).data().emplace_back(
          std::forward<Args>(args)...);
      return true;
    }
  }

  void close()
  {
    if (!did_close) {
      shared->close_in();
      did_close = true;
    }
  }

  bool closed() const { return did_close; }

  ~in()
  {
    if (shared) { close(); }
  }
};

template <typename T> class out {
  std::shared_ptr<queue<T>> shared;
  std::vector<T> cache;
  std::size_t cache_iter{0};
  bool did_close{false};

 public:
  out(std::shared_ptr<queue<T>> shared) : shared{std::move(shared)} {}
  // after move only operator= and ~out are allowed to be called
  out(out&&) = default;
  out& operator=(out&&) = default;
  out(const out&) = delete;
  out& operator=(const out&) = delete;

  std::optional<T> get()
  {
    if (did_close) { return std::nullopt; }
    update_cache();

    if (cache.size() > cache_iter) {
      std::optional result
          = std::make_optional<T>(std::move(cache[cache_iter++]));
      return result;
    } else {
      return std::nullopt;
    }
  }

  std::optional<T> try_get()
  {
    if (did_close) { return std::nullopt; }
    try_update_cache();

    if (cache.size() > cache_iter) {
      std::optional result
          = std::make_optional<T>(std::move(cache[cache_iter++]));
      return result;
    } else {
      return std::nullopt;
    }
  }

  void close()
  {
    if (!did_close) {
      cache.clear();
      cache_iter = 0;
      shared->close_out();
      did_close = true;
    }
  }

  bool closed() const { return did_close; }

  ~out()
  {
    if (shared) { close(); }
  }

 private:
  void do_update_cache(std::vector<T>& data)
  {
    cache.clear();
    cache_iter = 0;
    std::swap(data, cache);
  }

  void update_cache()
  {
    if (cache_iter == cache.size()) {
      if (auto handle = shared->read_lock()) {
        do_update_cache(handle->data());
      } else {
        // this can only happen if the channel was closed.
        close();
      }
    }
  }

  void try_update_cache()
  {
    if (cache_iter == cache.size()) {
      auto result = shared->try_read_lock();
      if (std::holds_alternative<failed_to_acquire_lock>(result)) {
        // intentionally left empty
      } else if (std::holds_alternative<channel_closed>(result)) {
        close();
      } else {
        auto& handle = std::get<typename queue<T>::handle>(result);
        do_update_cache(handle.data());
      }
    }
  }
};

template <typename T>
std::pair<in<T>, out<T>> CreateBufferedChannel(std::size_t capacity)
{
  auto shared = std::make_shared<queue<T>>(capacity);
  return std::make_pair(shared, shared);
}
}  // namespace channel

#endif  // BAREOS_LIB_CHANNEL_H_
