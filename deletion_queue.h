#pragma once
#include <deque>
#include <functional>

#include "types.h"

/* To properly use this header, you need to define DeletionQueueLifetime before including it!
 *
 *  IMPORTANT: Always include a RANGE at the end!
 *  For example:

enum DeletionQueueLifetime
{
    Core,
    Swapchain,
    RANGE
};
#include "deletion_queue.h"

 */

struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void queue(std::function<void()>&& function)
    {
        deletors.emplace_back(std::move(function));
    }

    void flush()
    {
        for (i32 i = deletors.size(); i --> 0;)
            deletors[i]();

        deletors.clear();
    }
};

static DeletionQueue deletion_queues[DeletionQueueLifetime::RANGE];
#define QUEUE_DELETE(lifetime, function) deletion_queues[lifetime].queue([=](){function;});