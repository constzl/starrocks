// This file is licensed under the Elastic License 2.0. Copyright 2021-present, StarRocks Limited.

#include "exec/pipeline/pipeline_driver_queue.h"

#include "exec/workgroup/work_group.h"
#include "gutil/strings/substitute.h"

namespace starrocks::pipeline {

void QuerySharedDriverQueue::close() {
    std::lock_guard<std::mutex> lock(_global_mutex);
    _is_closed = true;
    _cv.notify_all();
}

void QuerySharedDriverQueue::put_back(const DriverRawPtr driver) {
    int level = driver->driver_acct().get_level() % QUEUE_SIZE;
    driver->set_dispatch_queue_index(level);
    {
        std::lock_guard<std::mutex> lock(_global_mutex);
        _queues[level].queue.emplace(driver);
        _cv.notify_one();
    }
}

void QuerySharedDriverQueue::put_back(const std::vector<DriverRawPtr>& drivers) {
    std::vector<int> levels(drivers.size());
    for (int i = 0; i < drivers.size(); i++) {
        levels[i] = drivers[i]->driver_acct().get_level() % QUEUE_SIZE;
        drivers[i]->set_dispatch_queue_index(levels[i]);
    }

    std::lock_guard<std::mutex> lock(_global_mutex);
    for (int i = 0; i < drivers.size(); i++) {
        _queues[levels[i]].queue.emplace(drivers[i]);
        _cv.notify_one();
    }
}

void QuerySharedDriverQueue::put_back_from_dispatcher(const DriverRawPtr driver) {
    // QuerySharedDriverQueue::put_back_from_dispatcher is identical to put_back.
    put_back(driver);
}

void QuerySharedDriverQueue::put_back_from_dispatcher(const std::vector<DriverRawPtr>& drivers) {
    // QuerySharedDriverQueue::put_back_from_dispatcher is identical to put_back.
    put_back(drivers);
}

StatusOr<DriverRawPtr> QuerySharedDriverQueue::take(int dispatcher_id) {
    // -1 means no candidates; else has candidate.
    int queue_idx = -1;
    double target_accu_time = 0;
    DriverRawPtr driver_ptr;

    {
        std::unique_lock<std::mutex> lock(_global_mutex);
        while (true) {
            if (_is_closed) {
                return Status::Cancelled("Shutdown");
            }

            // Find the queue with the smallest execution time.
            for (int i = 0; i < QUEUE_SIZE; ++i) {
                // we just search for queue has element
                if (!_queues[i].queue.empty()) {
                    double local_target_time = _queues[i].accu_time_after_divisor();
                    if (queue_idx < 0 || local_target_time < target_accu_time) {
                        target_accu_time = local_target_time;
                        queue_idx = i;
                    }
                }
            }

            if (queue_idx >= 0) {
                break;
            }
            _cv.wait(lock);
        }
        // record queue's index to accumulate time for it.
        driver_ptr = _queues[queue_idx].queue.front();
        _queues[queue_idx].queue.pop();
    }

    // next pipeline driver to execute.
    return driver_ptr;
}

size_t QuerySharedDriverQueue::size() {
    size_t size = 0;
    for (const auto& sub_queue : _queues) {
        size += sub_queue.queue.size();
    }
    return size;
}

void QuerySharedDriverQueue::update_statistics(const DriverRawPtr driver) {
    _queues[driver->get_dispatch_queue_index()].update_accu_time(driver);
}

void QuerySharedDriverQueueWithoutLock::put_back(const DriverRawPtr driver) {
    _put_back(driver);
}

void QuerySharedDriverQueueWithoutLock::put_back(const std::vector<DriverRawPtr>& drivers) {
    for (auto driver : drivers) {
        _put_back(driver);
    }
}

void QuerySharedDriverQueueWithoutLock::put_back_from_dispatcher(const DriverRawPtr driver) {
    // QuerySharedDriverQueueWithoutLock::put_back_from_dispatcher is identical to put_back.
    put_back(driver);
}

void QuerySharedDriverQueueWithoutLock::put_back_from_dispatcher(const std::vector<DriverRawPtr>& drivers) {
    // QuerySharedDriverQueueWithoutLock::put_back_from_dispatcher is identical to put_back.
    put_back(drivers);
}

StatusOr<DriverRawPtr> QuerySharedDriverQueueWithoutLock::take(int dispatcher_id) {
    // -1 means no candidates; else has candidate.
    int queue_idx = -1;
    double target_accu_time = 0;
    DriverRawPtr driver_ptr;

    // Find the queue with the smallest execution time.
    for (int i = 0; i < QUEUE_SIZE; ++i) {
        // we just search for queue has element
        if (!_queues[i].queue.empty()) {
            double local_target_time = _queues[i].accu_time_after_divisor();
            if (queue_idx < 0 || local_target_time < target_accu_time) {
                target_accu_time = local_target_time;
                queue_idx = i;
            }
        }
    }

    // Always return non-null driver, which is guaranteed be the callee, e.g. DriverQueueWithWorkGroup::take().
    DCHECK(queue_idx >= 0);

    driver_ptr = _queues[queue_idx].queue.front();
    _queues[queue_idx].queue.pop();

    --_size;

    return driver_ptr;
}

void QuerySharedDriverQueueWithoutLock::update_statistics(const DriverRawPtr driver) {
    _queues[driver->get_dispatch_queue_index()].update_accu_time(driver);
}

void QuerySharedDriverQueueWithoutLock::_put_back(const DriverRawPtr driver) {
    int level = driver->driver_acct().get_level() % QUEUE_SIZE;
    driver->set_dispatch_queue_index(level);
    _queues[level].queue.emplace(driver);
    ++_size;
}

void DriverQueueWithWorkGroup::close() {
    std::lock_guard<std::mutex> lock(_global_mutex);
    _is_closed = true;
    _cv.notify_all();
}

void DriverQueueWithWorkGroup::put_back(const DriverRawPtr driver) {
    std::lock_guard<std::mutex> lock(_global_mutex);
    _put_back<false>(driver);
}

void DriverQueueWithWorkGroup::put_back(const std::vector<DriverRawPtr>& drivers) {
    std::lock_guard<std::mutex> lock(_global_mutex);

    for (const auto driver : drivers) {
        _put_back<false>(driver);
    }
}

void DriverQueueWithWorkGroup::put_back_from_dispatcher(const DriverRawPtr driver) {
    std::lock_guard<std::mutex> lock(_global_mutex);
    _put_back<true>(driver);
}

void DriverQueueWithWorkGroup::put_back_from_dispatcher(const std::vector<DriverRawPtr>& drivers) {
    std::lock_guard<std::mutex> lock(_global_mutex);

    for (const auto driver : drivers) {
        _put_back<true>(driver);
    }
}

StatusOr<DriverRawPtr> DriverQueueWithWorkGroup::take(int dispatcher_id) {
    std::unique_lock<std::mutex> lock(_global_mutex);

    if (_is_closed) {
        return Status::Cancelled("Shutdown");
    }

    while (_ready_wgs.empty()) {
        _cv.wait(lock);
        if (_is_closed) {
            return Status::Cancelled("Shutdown");
        }
    }

    // Try to take driver from any owner workgroup first.
    auto wg = _find_min_owner_wg(dispatcher_id);
    if (wg == nullptr) {
        // All the owner workgroups don't have ready drivers, so select the other workgroup.
        wg = _find_min_wg();
    }
    DCHECK_NOTNULL(wg);

    // If wg only contains one ready driver, it will be not ready anymore after taking away
    // the only one driver.
    if (wg->driver_queue()->size() == 1) {
        _sum_cpu_limit -= wg->cpu_limit();
        _ready_wgs.erase(wg);
    }

    return wg->driver_queue()->take(dispatcher_id);
}

void DriverQueueWithWorkGroup::update_statistics(const DriverRawPtr driver) {
    std::unique_lock<std::mutex> lock(_global_mutex);

    int64_t runtime_ns = driver->driver_acct().get_last_time_spent();
    auto* wg = driver->workgroup();
    wg->driver_queue()->update_statistics(driver);
    wg->increment_real_runtime_ns(runtime_ns);
    workgroup::WorkGroupManager::instance()->increment_cpu_runtime_ns(runtime_ns);
}

size_t DriverQueueWithWorkGroup::size() {
    std::lock_guard<std::mutex> lock(_global_mutex);

    size_t size = 0;
    for (auto wg : _ready_wgs) {
        size += wg->driver_queue()->size();
    }

    return size;
}

template <bool from_dispatcher>
void DriverQueueWithWorkGroup::_put_back(const DriverRawPtr driver) {
    auto* wg = driver->workgroup();
    if (_ready_wgs.find(wg) == _ready_wgs.end()) {
        _sum_cpu_limit += wg->cpu_limit();
        // The runtime needn't be adjusted for the workgroup put back from dispatcher,
        // because it has updated before dispatcher put the workgroup back by update_statistics().
        if constexpr (!from_dispatcher) {
            auto* min_wg = _find_min_wg();
            if (min_wg != nullptr) {
                int64_t origin_real_runtime_ns = wg->real_runtime_ns();

                // The workgroup maybe leaves for a long time, which results in that the runtime of it
                // may be much smaller than the other workgroups. If the runtime isn't adjusted, the others
                // will starve. Therefore, the runtime is adjusted according the minimum vruntime in _ready_wgs,
                // and give it half of ideal runtime in a schedule period as compensation.
                int64_t new_vruntime_ns = std::min(min_wg->vruntime_ns() - _ideal_runtime_ns(wg) / 2,
                                                   min_wg->real_runtime_ns() / int64_t(wg->cpu_limit()));
                wg->set_vruntime_ns(std::max(wg->vruntime_ns(), new_vruntime_ns));

                int64_t diff_real_runtime_ns = wg->real_runtime_ns() - origin_real_runtime_ns;
                workgroup::WorkGroupManager::instance()->increment_cpu_runtime_ns(diff_real_runtime_ns);
            }
        }
        _ready_wgs.emplace(wg);
    }
    wg->driver_queue()->put_back(driver);
    _cv.notify_one();
}

workgroup::WorkGroup* DriverQueueWithWorkGroup::_find_min_owner_wg(int dispatcher_id) {
    workgroup::WorkGroup* min_wg = nullptr;
    int64_t min_vruntime_ns = 0;

    auto owner_wgs = workgroup::WorkGroupManager::instance()->get_owners_of_driver_dispatcher(dispatcher_id);
    if (owner_wgs != nullptr) {
        for (const auto& wg : *owner_wgs) {
            if (_ready_wgs.find(wg.get()) != _ready_wgs.end() &&
                (min_wg == nullptr || min_vruntime_ns > wg->vruntime_ns())) {
                min_wg = wg.get();
                min_vruntime_ns = wg->vruntime_ns();
            }
        }
    }

    return min_wg;
}

workgroup::WorkGroup* DriverQueueWithWorkGroup::_find_min_wg() {
    workgroup::WorkGroup* min_wg = nullptr;
    int64_t min_vruntime_ns = 0;

    for (auto wg : _ready_wgs) {
        if (min_wg == nullptr || min_vruntime_ns > wg->vruntime_ns()) {
            min_wg = wg;
            min_vruntime_ns = wg->vruntime_ns();
        }
    }
    return min_wg;
}

int64_t DriverQueueWithWorkGroup::_ideal_runtime_ns(workgroup::WorkGroup* wg) {
    return DISPATCH_PERIOD_PER_WG_NS * _ready_wgs.size() * wg->cpu_limit() / _sum_cpu_limit;
}

} // namespace starrocks::pipeline