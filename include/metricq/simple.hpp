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

#include <metricq/metadata.hpp>
#include <metricq/types.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace metricq
{
std::string subscribe(const std::string& url, const std::string& token,
                      const std::vector<std::string>& metrics, Duration expires);
std::string subscribe(const std::string& url, const std::string& token, const std::string& metric,
                      Duration expires);

std::unordered_map<std::string, std::vector<TimeValue>>
drain(const std::string& url, const std::string& token, const std::vector<std::string>& metrics,
      const std::string& queue);
std::vector<TimeValue> drain(const std::string& url, const std::string& token,
                             const std::string& metric, const std::string& queue);

std::vector<std::string> get_metrics(const std::string& url, const std::string& token,
                                     const std::string& selector = std::string(""));

// for *all* metrics - use with care
std::unordered_map<std::string, Metadata> get_metadata(const std::string& url,
                                                       const std::string& token);

// selector is a regex
std::unordered_map<std::string, Metadata>
get_metadata(const std::string& url, const std::string& token, const std::string& selector);

// matches exactly the metrics in the selector vector
std::unordered_map<std::string, Metadata> get_metadata(const std::string& url,
                                                       const std::string& token,
                                                       const std::vector<std::string>& selector);
} // namespace metricq
