#include "UploadQueue.h"
#include <cstddef>
#include <string>
#include <functional>
#include <utility>

UploadQueue& UploadQueue::Get() {
    static UploadQueue queue;
    return queue;
}

void UploadQueue::Push(std::string label, std::function<bool()> ready, Task task) {
    _tasks.push_back({std::move(label), std::move(ready), std::move(task)});
}

UploadQueue::PumpResult UploadQueue::Pump(std::size_t budgetBytes) {
    PumpResult result;
    // Oldest first; a task still waiting on its job is skipped, not waited for, so one slow
    // decode does not hold back the uploads queued behind it. Indexed, not iterated: a step
    // may Push() (a deque push invalidates iterators) or Clear() the queue.
    std::size_t index = 0;
    while (index < _tasks.size()) {
        if (result.steps > 0 && result.bytes >= budgetBytes) {
            break;
        }
        if (_tasks[index].ready && !_tasks[index].ready()) {
            ++index;
            continue;
        }
        const std::size_t remaining = budgetBytes > result.bytes ? budgetBytes - result.bytes : 0;
        Task task = std::move(_tasks[index].task);
        const std::size_t clearsBefore = _clears;
        const Step step = task(remaining);
        ++result.steps;
        result.bytes += step.bytes;
        if (_clears != clearsBefore) {
            break; // the queue was cleared under us; `index` names nothing now
        }
        if (step.finished) {
            ++result.finished;
            _tasks.erase(_tasks.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        _tasks[index].task = std::move(task);
        // Unfinished means this task used its share; the next frame continues it.
        break;
    }
    _lastPumpBytes = result.bytes;
    return result;
}

void UploadQueue::Clear() {
    _tasks.clear();
    _lastPumpBytes = 0;
    ++_clears;
}
