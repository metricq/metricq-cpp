#pragma once

#include <utility>

// FROM GSL https://github.com/Microsoft/GSL
///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2015 Microsoft Corporation. All rights reserved.
//
// This code is licensed under the MIT License (MIT).
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
///////////////////////////////////////////////////////////////////////////////

namespace metricq
{
// final_action allows you to ensure something gets run at the end of a scope
template <class F>
class final_action
{
public:
    explicit final_action(F f) noexcept : f_(std::move(f))
    {
    }

    final_action(final_action&& other) noexcept : f_(std::move(other.f_)), invoke_(other.invoke_)
    {
        other.invoke_ = false;
    }

    final_action(const final_action&) = delete;

    final_action& operator=(const final_action&) = delete;

    final_action& operator=(final_action&&) = delete;

    ~final_action() noexcept
    {
        invoke();
    }

    void invoke()
    {
        if (invoke_)
        {
            f_();
            invoke_ = false;
        }
    }

private:
    F f_;
    bool invoke_{ true };
};

// finally() - convenience function to generate a final_action
template <class F>
final_action<F> finally(const F& f) noexcept
{
    return final_action<F>(f);
}

template <class F>
final_action<F> finally(F&& f) noexcept
{
    return final_action<F>(std::forward<F>(f));
}

std::string truncate_string(const std::string& in, std::size_t max_width)
{
    if (in.length() <= max_width)
    {
        return in;
    }
    return in.substr(0, max_width - 6) + " [...]";
}
} // namespace metricq