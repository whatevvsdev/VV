#pragma once
#include <vector>
#include <functional>

#include "types.h"

/* To properly use this header, you need to define FunctionQueueLifetime before including it!
 *
 *  IMPORTANT: Always include a RANGE at the end!
 *  For example:

enum FunctionQueueLifetime
{
    CORE,
    SWAPCHAIN,
    RANGE <---- This value is used to create the array of function queues, so don't set any special values in the enum!
};
#include "function_queues.h"

 */

struct FunctionQueue
{
    std::vector<std::function<void()>> functions;

    void queue(std::function<void()>&& function)
    {
        functions.emplace_back(std::move(function));
    }

    void flush()
    {
        for (i32 i = static_cast<i32>(functions.size()); i --> 0;)
            functions[i]();

        functions.clear();
    }
};

static FunctionQueue _function_queues[FunctionQueueLifetime::RANGE];
#define QUEUE_FUNCTION(lifetime, function) _function_queues[lifetime].queue([=](){function;});
#define QUEUE_FLUSH(lifetime) _function_queues[lifetime].flush();