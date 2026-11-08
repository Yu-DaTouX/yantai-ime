#pragma once

#include <cstddef>
#include <iterator>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

template <typename Key, typename Value> class CircularBuffer
{
  public:
    explicit CircularBuffer(size_t capacity) : _capacity(capacity)
    {
    }

    void insert(const Key &key, const Value &value)
    {
        insert_value(key, value);
    }

    void insert(const Key &key, Value &&value)
    {
        insert_value(key, std::move(value));
    }

    // 命中时把条目挪到最近使用端（真正的 LRU 淘汰），返回指向缓存值的指针而不复制。
    // 候选列表一份就是几百个 WordItem，每键的热路径上应当用它而不是 get()。
    // 指针在下一次 insert / remove / clear 之前有效。
    const Value *find(const Key &key)
    {
        auto it = _map.find(key);
        if (it == _map.end())
        {
            return nullptr;
        }
        _list.splice(_list.end(), _list, it->second.second);
        return &it->second.first;
    }

    std::optional<Value> get(const Key &key) const
    {
        auto it = _map.find(key);
        if (it != _map.end())
        {
            return it->second.first;
        }
        return std::nullopt;
    }

    bool remove(const Key &key)
    {
        auto it = _map.find(key);
        if (it == _map.end())
            return false;

        _list.erase(it->second.second);
        _map.erase(it);
        return true;
    }

    void clear()
    {
        _map.clear();
        _list.clear();
    }

    size_t size() const
    {
        return _map.size();
    }

    bool empty() const
    {
        return _map.empty();
    }

  private:
    template <typename V> void insert_value(const Key &key, V &&value)
    {
        if (_capacity == 0)
        {
            return;
        }
        auto it = _map.find(key);
        if (it != _map.end())
        {
            it->second.first = std::forward<V>(value);
            _list.splice(_list.end(), _list, it->second.second);
            return;
        }

        _list.push_back(key);
        try
        {
            _map.emplace(key, std::make_pair(std::forward<V>(value), std::prev(_list.end())));
        }
        catch (...)
        {
            // A throwing value copy/allocation must not leave an orphan LRU
            // key that can later evict a different incarnation of that key.
            _list.pop_back();
            throw;
        }
        // Do not evict a working entry until the replacement is fully stored.
        if (_list.size() > _capacity)
        {
            _map.erase(_list.front());
            _list.pop_front();
        }
    }

    size_t _capacity;
    std::list<Key> _list;
    std::unordered_map<Key, std::pair<Value, typename std::list<Key>::iterator>> _map;
};
