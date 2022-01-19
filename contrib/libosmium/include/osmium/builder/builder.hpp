#ifndef OSMIUM_BUILDER_BUILDER_HPP
#define OSMIUM_BUILDER_BUILDER_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2022 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <osmium/memory/buffer.hpp>
#include <osmium/memory/item.hpp>
#include <osmium/util/compatibility.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace osmium {

    /**
     * @brief Classes for building OSM objects and other items in buffers
     */
    namespace builder {

        /**
         * Parent class for individual builder classes. Instantiate one of
         * its derived classes.
         */
        class Builder {

            osmium::memory::Buffer& m_buffer;
            Builder* m_parent;
            std::size_t m_item_offset;

        protected:

            explicit Builder(osmium::memory::Buffer& buffer, Builder* parent, osmium::memory::item_size_type size) :
                m_buffer(buffer),
                m_parent(parent),
                m_item_offset(buffer.written() - buffer.committed()) {
                reserve_space(size);
                assert(buffer.is_aligned());
                if (m_parent) {
                    assert(m_buffer.builder_count() == 1 && "Only one sub-builder can be open at any time.");
                    m_parent->add_size(size);
                } else {
                    assert(m_buffer.builder_count() == 0 && "Only one builder can be open at any time.");
                }
#ifndef NDEBUG
                m_buffer.increment_builder_count();
#endif
            }

#ifdef NDEBUG
            ~Builder() noexcept = default;
#else
            ~Builder() noexcept {
                m_buffer.decrement_builder_count();
            }
#endif

            unsigned char* item_pos() const noexcept {
                return m_buffer.data() + m_buffer.committed() + m_item_offset;
            }

            osmium::memory::Item& item() const noexcept {
                return *reinterpret_cast<osmium::memory::Item*>(item_pos());
            }

            unsigned char* reserve_space(std::size_t size) {
                return m_buffer.reserve_space(size);
            }

            /**
             * Add padding to buffer (if needed) to align data properly.
             *
             * This calculates how many padding bytes are needed and adds
             * as many zero bytes to the buffer. It also adds this number
             * to the size of the current item (if the "self" param is
             * true) and recursively to all the parent items.
             *
             * @param self If true add number of padding bytes to size
             *             of current item. Size is always added to
             *             parent item (if any).
             *
             */
            void add_padding(bool self = false) {
                // We know the padding is only a very small number, so it will
                // always fit.
                const auto padding = static_cast<osmium::memory::item_size_type>(osmium::memory::align_bytes - (size() % osmium::memory::align_bytes));
                if (padding != osmium::memory::align_bytes) {
                    std::fill_n(reserve_space(padding), padding, 0);
                    if (self) {
                        add_size(padding);
                    } else if (m_parent) {
                        m_parent->add_size(padding);
                        assert(m_parent->size() % osmium::memory::align_bytes == 0);
                    }
                }
            }

            void add_size(osmium::memory::item_size_type size) {
                item().add_size(size);
                if (m_parent) {
                    m_parent->add_size(size);
                }
            }

            uint32_t size() const noexcept {
                return item().byte_size();
            }

            /**
             * Reserve space for an object of class T in buffer and return
             * pointer to it.
             */
            template <typename T>
            T* reserve_space_for() {
                assert(m_buffer.is_aligned());
                return reinterpret_cast<T*>(reserve_space(sizeof(T)));
            }

            /**
             * Append data to buffer.
             *
             * @param data Pointer to data.
             * @param length Length of data in bytes. If data is a
             *               \0-terminated string, length must contain the
             *               \0 byte.
             * @returns The number of bytes appended (length).
             */
            osmium::memory::item_size_type append(const char* data, const osmium::memory::item_size_type length) {
                unsigned char* target = reserve_space(length);
                std::copy_n(reinterpret_cast<const unsigned char*>(data), length, target);
                return length;
            }

            /**
             * Append data to buffer and append an additional \0.
             *
             * @param data Pointer to data.
             * @param length Length of data in bytes.
             * @returns The number of bytes appended (length + 1).
             */
            osmium::memory::item_size_type append_with_zero(const char* data, const osmium::memory::item_size_type length) {
                unsigned char* target = reserve_space(length + 1);
                std::copy_n(reinterpret_cast<const unsigned char*>(data), length, target);
                target[length] = '\0';
                return length + 1;
            }

            /**
             * Append \0-terminated string to buffer.
             *
             * @param str \0-terminated string.
             * @returns The number of bytes appended (strlen(str) + 1).
             */
            osmium::memory::item_size_type append(const char* str) {
                return append(str, static_cast<osmium::memory::item_size_type>(std::strlen(str) + 1));
            }

            /**
             * Append '\0' to the buffer.
             *
             * @deprecated Use append_with_zero() instead.
             *
             * @returns The number of bytes appended (always 1).
             */
            OSMIUM_DEPRECATED osmium::memory::item_size_type append_zero() {
                *reserve_space(1) = '\0';
                return 1;
            }

        public:

            Builder(const Builder&) = delete;
            Builder(Builder&&) = delete;

            Builder& operator=(const Builder&) = delete;
            Builder& operator=(Builder&&) = delete;

            /// Return the buffer this builder is using.
            osmium::memory::Buffer& buffer() noexcept {
                return m_buffer;
            }

            /**
             * Add a subitem to the object being built. This can be something
             * like a TagList or RelationMemberList.
             */
            void add_item(const osmium::memory::Item& item) {
                m_buffer.add_item(item);
                add_size(item.padded_size());
            }

            /**
             * @deprecated Use the version of add_item() taking a
             *             reference instead.
             */
            OSMIUM_DEPRECATED void add_item(const osmium::memory::Item* item) {
                assert(item);
                add_item(*item);
            }

        }; // class Builder

    } // namespace builder

} // namespace osmium

#endif // OSMIUM_BUILDER_BUILDER_HPP
