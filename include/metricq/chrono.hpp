// Copyright (c) 2018, ZIH,
// Technische Universitaet Dresden,
// Federal Republic of Germany
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//     * Neither the name of metricq nor the names of its contributors
//       may be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <chrono>
#include <string>

#include <cinttypes>
#include <ctime>

namespace metricq
{
using Duration = std::chrono::duration<int64_t, std::nano>;

struct Clock
{
    using duration = Duration;
    using rep = duration::rep;
    using period = duration::period;
    // If this ever breaks, it's all Mario's fault
    using time_point = std::chrono::time_point<std::chrono::system_clock, duration>;
    static const bool is_steady = true;
    static time_point now()
    {
        // TODO use clock_gettime and all the funny stuff
        return time_point(std::chrono::duration_cast<duration>(
            std::chrono::system_clock::now().time_since_epoch()));
    }
    static std::string format_iso(time_point tp);
    static std::string format(time_point tp, std::string fmt);
    static std::time_t to_time_t(time_point tp)
    {
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                tp.time_since_epoch())));
    }
};

using TimePoint = Clock::time_point;

template <typename FromDuration, typename ToDuration = Duration>
constexpr ToDuration duration_cast(const FromDuration& dtn)
{
    return std::chrono::duration_cast<ToDuration>(dtn);
}

Duration duration_parse(const std::string& str);
} // namespace metricq
