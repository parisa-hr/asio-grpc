// Copyright 2022 Dennis Hezel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AGRPC_UTILS_THROWING_ALLOCATOR_HPP
#define AGRPC_UTILS_THROWING_ALLOCATOR_HPP

#include <cstddef>
#include <memory>

namespace test
{
template <class T = std::byte>
class ThrowingAllocator
{
  public:
    using value_type = T;

    ThrowingAllocator() = default;

    template <class U>
    constexpr ThrowingAllocator(const test::ThrowingAllocator<U>&) noexcept
    {
    }

    ThrowingAllocator(const ThrowingAllocator&) = default;

    ThrowingAllocator(ThrowingAllocator&&) = default;

    ThrowingAllocator& operator=(const ThrowingAllocator&) = default;

    ThrowingAllocator& operator=(ThrowingAllocator&&) = default;

    T* allocate(std::size_t) { throw std::bad_alloc(); }

    void deallocate(T*, std::size_t) {}

    template <class U>
    friend constexpr bool operator==(const ThrowingAllocator&, const test::ThrowingAllocator<U>&) noexcept
    {
        return true;
    }

    template <class U>
    friend constexpr bool operator!=(const ThrowingAllocator&, const test::ThrowingAllocator<U>&) noexcept
    {
        return false;
    }
};
}

#endif  // AGRPC_UTILS_THROWING_ALLOCATOR_HPP
