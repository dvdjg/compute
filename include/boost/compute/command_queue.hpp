//---------------------------------------------------------------------------//
// Copyright (c) 2013 Kyle Lutz <kyle.r.lutz@gmail.com>
//
// Distributed under the Boost Software License, Version 1.0
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// See http://kylelutz.github.com/compute for more information.
//---------------------------------------------------------------------------//

#ifndef BOOST_COMPUTE_COMMAND_QUEUE_HPP
#define BOOST_COMPUTE_COMMAND_QUEUE_HPP

#include <cstddef>
#include <algorithm>

#include <boost/config.hpp>
#include <boost/assert.hpp>

#include <boost/compute/config.hpp>
#include <boost/compute/event.hpp>
#include <boost/compute/user_event.hpp>
#include <boost/compute/buffer.hpp>
#include <boost/compute/device.hpp>
#include <boost/compute/kernel.hpp>
#include <boost/compute/context.hpp>
#include <boost/compute/exception.hpp>
#include <boost/compute/image/image1d.hpp>
#include <boost/compute/image/image2d.hpp>
#include <boost/compute/image/image3d.hpp>
#include <boost/compute/image/image_object.hpp>
#include <boost/compute/utility/wait_list.hpp>
#include <boost/compute/detail/get_object_info.hpp>
#include <boost/compute/detail/assert_cl_success.hpp>
#include <boost/compute/utility/extents.hpp>

namespace boost {
namespace compute {
namespace detail {

inline void BOOST_COMPUTE_CL_CALLBACK
nullary_native_kernel_trampoline(void *user_func_ptr)
{
    void (*user_func)();
    std::memcpy(&user_func, user_func_ptr, sizeof(user_func));
    user_func();
}

} // end detail namespace

/// \class command_queue
/// \brief A command queue.
///
/// Command queues provide the interface for interacting with compute
/// devices. The command_queue class provides methods to copy data to
/// and from a compute device as well as execute compute kernels.
///
/// Command queues are created for a compute device within a compute
/// context.
///
/// For example, to create a context and command queue for the default device
/// on the system (this is the normal set up code used by almost all OpenCL
/// programs):
/// \code
/// #include <boost/compute/core.hpp>
///
/// // get the default compute device
/// boost::compute::device device = boost::compute::system::default_device();
///
/// // set up a compute context and command queue
/// boost::compute::context context(device);
/// boost::compute::command_queue queue(context, device);
/// \endcode
///
/// The default command queue for the system can be obtained with the
/// system::default_queue() method.
///
/// \see buffer, context, kernel
class command_queue
{
public:
    enum properties {
        enable_profiling = CL_QUEUE_PROFILING_ENABLE,
        enable_out_of_order_execution = CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE
    };

    enum map_flags {
        map_read = CL_MAP_READ,
        map_write = CL_MAP_WRITE
        #ifdef CL_VERSION_2_0
        ,
        map_write_invalidate_region = CL_MAP_WRITE_INVALIDATE_REGION
        #endif
    };

    /// Creates a null command queue.
    command_queue()
        : m_queue(0), m_version(0)
    {
    }

    explicit command_queue(cl_command_queue queue, bool retain = true)
        : m_queue(queue), m_version(0)
    {
        if(m_queue && retain){
            clRetainCommandQueue(m_queue);
        }
    }

    /// Creates a command queue in \p context for \p device with
    /// \p properties.
    ///
    /// \see_opencl_ref{clCreateCommandQueue}
    command_queue(const context &context,
                  const device &device,
                  cl_command_queue_properties properties = 0)
    {
        BOOST_ASSERT(device.id() != 0);

        cl_int error = 0;
        m_version = device.get_version();

        #ifdef CL_VERSION_2_0
        if (get_version() >= 200)
        {
            std::vector<cl_queue_properties> queue_properties;
            if(properties){
                queue_properties.push_back(CL_QUEUE_PROPERTIES);
                queue_properties.push_back(cl_queue_properties(properties));
                queue_properties.push_back(cl_queue_properties(0));
            }

            const cl_queue_properties *queue_properties_ptr =
                queue_properties.empty() ? 0 : &queue_properties[0];

            m_queue = clCreateCommandQueueWithProperties(
                context, device.id(), queue_properties_ptr, &error
            );
        }
        else
        #endif
        {
            m_queue = clCreateCommandQueue(
                context, device.id(), properties, &error
            );
        }

        if(!m_queue){
            BOOST_THROW_EXCEPTION(opencl_error(error));
        }
    }

    /// Creates a new command queue object as a copy of \p other.
    command_queue(const command_queue &other)
        : m_queue(other.m_queue), m_version(other.m_version)
    {
        if(m_queue){
            clRetainCommandQueue(m_queue);
        }
    }

    /// Copies the command queue object from \p other to \c *this.
    command_queue& operator=(const command_queue &other)
    {
        if(this != &other){
            if(m_queue){
                clReleaseCommandQueue(m_queue);
            }

            m_queue = other.m_queue;
            m_version = other.m_version;

            if(m_queue){
                clRetainCommandQueue(m_queue);
            }
        }

        return *this;
    }

    #ifndef BOOST_COMPUTE_NO_RVALUE_REFERENCES
    /// Move-constructs a new command queue object from \p other.
    command_queue(command_queue&& other) BOOST_NOEXCEPT
        : m_queue(other.m_queue), m_version(other.m_version)
    {
        other.m_queue = 0;
        other.m_version = 0;
    }

    /// Move-assigns the command queue from \p other to \c *this.
    command_queue& operator=(command_queue&& other) BOOST_NOEXCEPT
    {
        if(m_queue){
            clReleaseCommandQueue(m_queue);
        }

        m_queue = other.m_queue;
        m_version = other.m_version;
        other.m_queue = 0;

        return *this;
    }
    #endif // BOOST_COMPUTE_NO_RVALUE_REFERENCES

    /// Destroys the command queue.
    ///
    /// \see_opencl_ref{clReleaseCommandQueue}
    ~command_queue()
    {
        if(m_queue){
            BOOST_COMPUTE_ASSERT_CL_SUCCESS(
                clReleaseCommandQueue(m_queue)
            );
        }
    }

    /// Returns the underlying OpenCL command queue.
    cl_command_queue& get() const
    {
        return const_cast<cl_command_queue &>(m_queue);
    }

    /// Returns the device that the command queue issues commands to.
    device get_device() const
    {
        return device(get_info<cl_device_id>(CL_QUEUE_DEVICE));
    }

    /// Returns the context for the command queue.
    context get_context() const
    {
        return context(get_info<cl_context>(CL_QUEUE_CONTEXT));
    }

    /// Returns the numeric version: major * 100 + minor.
    uint_ get_version() const
    {
        if (m_version == 0)
            m_version = get_device().get_version(); // The version of the first device
        return m_version;
    }

    /// Returns information about the command queue.
    ///
    /// \see_opencl_ref{clGetCommandQueueInfo}
    template<class T>
    T get_info(cl_command_queue_info info) const
    {
        return detail::get_object_info<T>(clGetCommandQueueInfo, m_queue, info);
    }

    /// \overload
    template<int Enum>
    typename detail::get_object_info_type<command_queue, Enum>::type
    get_info() const;

    /// Returns the properties for the command queue.
    cl_command_queue_properties get_properties() const
    {
        return get_info<cl_command_queue_properties>(CL_QUEUE_PROPERTIES);
    }

    /// Enqueues a command to read data from \p buffer to host memory.
    ///
    /// \see_opencl_ref{clEnqueueReadBuffer}
    ///
    /// \see copy()
    void enqueue_read_buffer(const buffer &buffer,
                             size_t offset,
                             size_t size,
                             void *host_ptr,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(size <= buffer.size());
        BOOST_ASSERT(buffer.get_context() == this->get_context());
        BOOST_ASSERT(host_ptr != 0);

        cl_int ret = clEnqueueReadBuffer(
            m_queue,
            buffer.get(),
            event_ ? CL_FALSE : CL_TRUE,
            offset,
            size,
            host_ptr,
            events.size(),
            events.get_event_ptr(),
			event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to read data from \p buffer to host memory. The
    /// copy will be performed asynchronously.
    ///
    /// \see_opencl_ref{clEnqueueReadBuffer}
    ///
    /// \see copy_async()
    event enqueue_read_buffer_async(const buffer &buffer,
                                    size_t offset,
                                    size_t size,
                                    void *host_ptr,
                                    const wait_list &events = wait_list())
    {
        event event_;

        enqueue_read_buffer(buffer,
                            offset,
                            size,
                            host_ptr,
                            events,
                            &event_);

        return event_;
    }

    #if defined(CL_VERSION_1_1) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a command to read a rectangular region from \p buffer to
    /// host memory.
    ///
    /// \see_opencl_ref{clEnqueueReadBufferRect}
    ///
    /// \opencl_version_warning{1,1}
    void enqueue_read_buffer_rect(const buffer &buffer,
                                  const size_t buffer_origin[3],
                                  const size_t host_origin[3],
                                  const size_t region[3],
                                  size_t buffer_row_pitch,
                                  size_t buffer_slice_pitch,
                                  size_t host_row_pitch,
                                  size_t host_slice_pitch,
                                  void *host_ptr,
                                  const wait_list &events = wait_list(),
                                  event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(buffer.get_context() == this->get_context());
        BOOST_ASSERT(host_ptr != 0);

        if (get_version() < 110)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueReadBufferRect(
            m_queue,
            buffer.get(),
            event_ ? CL_FALSE : CL_TRUE,
            buffer_origin,
            host_origin,
            region,
            buffer_row_pitch,
            buffer_slice_pitch,
            host_row_pitch,
            host_slice_pitch,
            host_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }
    #endif // CL_VERSION_1_1

    /// Enqueues a command to write data from host memory to \p buffer.
    ///
    /// \see_opencl_ref{clEnqueueWriteBuffer}
    ///
    /// \see copy()
    void enqueue_write_buffer(const buffer &buffer,
                              size_t offset,
                              size_t size,
                              const void *host_ptr,
                              const wait_list &events = wait_list(),
                              event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(size <= buffer.size());
        BOOST_ASSERT(buffer.get_context() == this->get_context());
        BOOST_ASSERT(host_ptr != 0);

        cl_int ret = clEnqueueWriteBuffer(
            m_queue,
            buffer.get(),
            event_ ? CL_FALSE : CL_TRUE,
            offset,
            size,
            host_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to write data from host memory to \p buffer.
    /// The copy is performed asynchronously.
    ///
    /// \see_opencl_ref{clEnqueueWriteBuffer}
    ///
    /// \see copy_async()
    event enqueue_write_buffer_async(const buffer &buffer,
                                     size_t offset,
                                     size_t size,
                                     const void *host_ptr,
                                     const wait_list &events = wait_list())
    {
        event event_;

        enqueue_write_buffer(buffer,
                             offset,
                             size,
                             host_ptr,
                             events,
                             &event_);

         return event_;
    }

    #if defined(CL_VERSION_1_1) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a command to write a rectangular region from host memory
    /// to \p buffer.
    ///
    /// \see_opencl_ref{clEnqueueWriteBufferRect}
    ///
    /// \opencl_version_warning{1,1}
    void enqueue_write_buffer_rect(const buffer &buffer,
                                   const size_t buffer_origin[3],
                                   const size_t host_origin[3],
                                   const size_t region[3],
                                   size_t buffer_row_pitch,
                                   size_t buffer_slice_pitch,
                                   size_t host_row_pitch,
                                   size_t host_slice_pitch,
                                   void *host_ptr,
                                   const wait_list &events = wait_list(),
                                   event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(buffer.get_context() == this->get_context());
        BOOST_ASSERT(host_ptr != 0);

        if (get_version() < 110)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueWriteBufferRect(
            m_queue,
            buffer.get(),
            event_ ? CL_FALSE : CL_TRUE,
            buffer_origin,
            host_origin,
            region,
            buffer_row_pitch,
            buffer_slice_pitch,
            host_row_pitch,
            host_slice_pitch,
            host_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }
    #endif // CL_VERSION_1_1

    /// Enqueues a command to copy data from \p src_buffer to
    /// \p dst_buffer.
    ///
    /// \see_opencl_ref{clEnqueueCopyBuffer}
    ///
    /// \see copy()
    void enqueue_copy_buffer(const buffer &src_buffer,
                              const buffer &dst_buffer,
                              size_t src_offset,
                              size_t dst_offset,
                              size_t size,
                              const wait_list &events = wait_list(),
                              event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(src_offset + size <= src_buffer.size());
        BOOST_ASSERT(dst_offset + size <= dst_buffer.size());
        BOOST_ASSERT(src_buffer.get_context() == this->get_context());
        BOOST_ASSERT(dst_buffer.get_context() == this->get_context());

        cl_int ret = clEnqueueCopyBuffer(
            m_queue,
            src_buffer.get(),
            dst_buffer.get(),
            src_offset,
            dst_offset,
            size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

	event enqueue_copy_buffer_async(const buffer &src_buffer,
		const buffer &dst_buffer,
		size_t src_offset,
		size_t dst_offset,
		size_t size,
		const wait_list &events = wait_list())
	{
		event event_;

		enqueue_copy_buffer(src_buffer,
			dst_buffer,
			src_offset,
			dst_offset,
			size,
			events,
			&event_);

		return event_;
	}

    #if defined(CL_VERSION_1_1) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a command to copy a rectangular region from
    /// \p src_buffer to \p dst_buffer.
    ///
    /// \see_opencl_ref{clEnqueueCopyBufferRect}
    ///
    /// \opencl_version_warning{1,1}
    void enqueue_copy_buffer_rect(const buffer &src_buffer,
                                   const buffer &dst_buffer,
                                   const size_t src_origin[3],
                                   const size_t dst_origin[3],
                                   const size_t region[3],
                                   size_t buffer_row_pitch,
                                   size_t buffer_slice_pitch,
                                   size_t host_row_pitch,
                                   size_t host_slice_pitch,
                                   const wait_list &events = wait_list(),
                                   event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(src_buffer.get_context() == this->get_context());
        BOOST_ASSERT(dst_buffer.get_context() == this->get_context());

        if (get_version() < 110)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueCopyBufferRect(
            m_queue,
            src_buffer.get(),
            dst_buffer.get(),
            src_origin,
            dst_origin,
            region,
            buffer_row_pitch,
            buffer_slice_pitch,
            host_row_pitch,
            host_slice_pitch,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }
    #endif // CL_VERSION_1_1

    #if defined(CL_VERSION_1_2) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a command to fill \p buffer with \p pattern.
    ///
    /// \see_opencl_ref{clEnqueueFillBuffer}
    ///
    /// \opencl_version_warning{1,2}
    ///
    /// \see fill()
    void enqueue_fill_buffer(const buffer &buffer,
                              const void *pattern,
                              size_t pattern_size,
                              size_t offset,
                              size_t size,
                              const wait_list &events = wait_list(),
                              event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(offset + size <= buffer.size());
        BOOST_ASSERT(buffer.get_context() == this->get_context());

        if (get_version() < 120)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueFillBuffer(
            m_queue,
            buffer.get(),
            pattern,
            pattern_size,
            offset,
            size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

	event enqueue_fill_buffer_async(const buffer &buffer,
		const void *pattern,
		size_t pattern_size,
		size_t offset,
		size_t size,
		const wait_list &events = wait_list())
	{
		event event_;

		enqueue_fill_buffer(buffer,
			pattern,
			pattern_size,
			offset,
			size,
			events,
			&event_);

		return event_;
	}
    #endif // CL_VERSION_1_2

    /// Enqueues a command to map \p buffer into the host address space.
    ///
    /// \see_opencl_ref{clEnqueueMapBuffer}
    void* enqueue_map_buffer(const buffer &buffer_,
                             cl_map_flags flags,
                             size_t offset,
                             size_t size,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(offset + size <= buffer_.size());
        BOOST_ASSERT(buffer_.get_context() == this->get_context());

        cl_int ret = 0;
        void *pointer = clEnqueueMapBuffer(
            m_queue,
            buffer_.get(),
            event_ ? CL_FALSE : CL_TRUE,
            flags,
            offset,
            size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL,
            &ret
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }

        return pointer;
    }

    /// Enqueues a command to map \p image into the host address space.
    ///
    /// \see_opencl_ref{clEnqueueMapImage}
    void* enqueue_map_image(const image_object &image,
                            cl_map_flags flags,
                            const size_t *origin,
                            const size_t *region,
                            size_t *row_pitch,
                            size_t *slice_pitch = NULL,
                            const wait_list &events = wait_list(),
                            event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(image.get_context() == this->get_context());

        cl_int ret = 0;
        void *pointer = clEnqueueMapImage(
            m_queue,
            image.get(),
            event_ ? CL_FALSE : CL_TRUE,
            flags,
            origin,
            region,
            row_pitch,
            slice_pitch,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL,
            &ret
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }

        return pointer;
    }


    /// \overload
    template<size_t N>
    void* enqueue_map_image(const image_object& image,
                            cl_map_flags flags,
                            const extents<N> origin,
                            const extents<N> region,
                            size_t *row_pitch,
                            size_t *slice_pitch = NULL,
                            const wait_list &events = wait_list(),
                            event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N <= 3);
        BOOST_ASSERT(image.get_context() == this->get_context());

        size_t origin3[3] = { 0, 0, 0 };
        size_t region3[3] = { 1, 1, 1 };

        std::copy(origin.data(), origin.data() + N, origin3);
        std::copy(region.data(), region.data() + N, region3);

        return enqueue_map_image(
            image, flags ,origin3, region3, row_pitch, slice_pitch, events, event_
        );
    }

    /// Enqueues a command to unmap \p buffer from the host memory space.
    ///
    /// \see_opencl_ref{clEnqueueUnmapMemObject}
    void enqueue_unmap_buffer(const memory_object &mem_object,
                               void *mapped_ptr,
                               const wait_list &events = wait_list(),
                               event * event_ = NULL)
    {
        BOOST_ASSERT(mem_object.get_context() == this->get_context());

        enqueue_unmap_mem_object(mem_object.get(), mapped_ptr, events, event_);
    }

    /// Enqueues a command to unmap \p mem from the host memory space.
    ///
    /// \see_opencl_ref{clEnqueueUnmapMemObject}
    void enqueue_unmap_mem_object(cl_mem mem,
                                   void *mapped_ptr,
                                   const wait_list &events = wait_list(),
                                   event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueUnmapMemObject(
            m_queue,
            mem,
            mapped_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to read data from \p image to host memory.
    ///
    /// \see_opencl_ref{clEnqueueReadImage}
    void enqueue_read_image(const image_object& image,
                             const size_t *origin,
                             const size_t *region,
                             size_t row_pitch,
                             size_t slice_pitch,
                             void *host_ptr,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueReadImage(
            m_queue,
            image.get(),
            event_ ? CL_FALSE : CL_TRUE,
            origin,
            region,
            row_pitch,
            slice_pitch,
            host_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// \overload
    template<size_t N>
    void enqueue_read_image(const image_object& image,
                             const extents<N> origin,
                             const extents<N> region,
                             void *host_ptr,
                             size_t row_pitch = 0,
                             size_t slice_pitch = 0,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N <= 3);
        BOOST_ASSERT(image.get_context() == this->get_context());

        size_t origin3[3] = { 0, 0, 0 };
        size_t region3[3] = { 1, 1, 1 };

        std::copy(origin.data(), origin.data() + N, origin3);
        std::copy(region.data(), region.data() + N, region3);

        enqueue_read_image(
            image, origin3, region3, row_pitch, slice_pitch, host_ptr, events, event_
        );
    }

    /// Enqueues a command to write data from host memory to \p image.
    ///
    /// \see_opencl_ref{clEnqueueWriteImage}
    void enqueue_write_image(image_object& image,
                              const size_t *origin,
                              const size_t *region,
                              const void *host_ptr,
                              size_t input_row_pitch = 0,
                              size_t input_slice_pitch = 0,
                              const wait_list &events = wait_list(),
                              event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueWriteImage(
            m_queue,
            image.get(),
            event_ ? CL_FALSE : CL_TRUE,
            origin,
            region,
            input_row_pitch,
            input_slice_pitch,
            host_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// \overload
    template<size_t N>
    void enqueue_write_image(image_object& image,
                              const extents<N> origin,
                              const extents<N> region,
                              const void *host_ptr,
                              const size_t input_row_pitch = 0,
                              const size_t input_slice_pitch = 0,
                              const wait_list &events = wait_list(),
                              event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N <= 3);
        BOOST_ASSERT(image.get_context() == this->get_context());

        size_t origin3[3] = { 0, 0, 0 };
        size_t region3[3] = { 1, 1, 1 };

        std::copy(origin.data(), origin.data() + N, origin3);
        std::copy(region.data(), region.data() + N, region3);

        enqueue_write_image(
            image, origin3, region3, host_ptr, input_row_pitch, input_slice_pitch, events, event_
        );
    }

    /// Enqueues a command to copy data from \p src_image to \p dst_image.
    ///
    /// \see_opencl_ref{clEnqueueCopyImage}
    void enqueue_copy_image(const image_object& src_image,
                             image_object& dst_image,
                             const size_t *src_origin,
                             const size_t *dst_origin,
                             const size_t *region,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueCopyImage(
            m_queue,
            src_image.get(),
            dst_image.get(),
            src_origin,
            dst_origin,
            region,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// \overload
    template<size_t N>
    void enqueue_copy_image(const image_object& src_image,
                             image_object& dst_image,
                             const extents<N> src_origin,
                             const extents<N> dst_origin,
                             const extents<N> region,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N <= 3);
        BOOST_ASSERT(src_image.get_context() == this->get_context());
        BOOST_ASSERT(dst_image.get_context() == this->get_context());
        BOOST_ASSERT_MSG(src_image.format() == dst_image.format(),
                         "Source and destination image formats must match.");

        size_t src_origin3[3] = { 0, 0, 0 };
        size_t dst_origin3[3] = { 0, 0, 0 };
        size_t region3[3] = { 1, 1, 1 };

        std::copy(src_origin.data(), src_origin.data() + N, src_origin3);
        std::copy(dst_origin.data(), dst_origin.data() + N, dst_origin3);
        std::copy(region.data(), region.data() + N, region3);

        enqueue_copy_image(
            src_image, dst_image, src_origin3, dst_origin3, region3, events, event_
        );
    }

    /// Enqueues a command to copy data from \p src_image to \p dst_buffer.
    ///
    /// \see_opencl_ref{clEnqueueCopyImageToBuffer}
    void enqueue_copy_image_to_buffer(const image_object& src_image,
                                       memory_object& dst_buffer,
                                       const size_t *src_origin,
                                       const size_t *region,
                                       size_t dst_offset,
                                       const wait_list &events = wait_list(),
                                       event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueCopyImageToBuffer(
            m_queue,
            src_image.get(),
            dst_buffer.get(),
            src_origin,
            region,
            dst_offset,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to copy data from \p src_buffer to \p dst_image.
    ///
    /// \see_opencl_ref{clEnqueueCopyBufferToImage}
    void enqueue_copy_buffer_to_image(const memory_object& src_buffer,
                                       image_object& dst_image,
                                       size_t src_offset,
                                       const size_t *dst_origin,
                                       const size_t *region,
                                       const wait_list &events = wait_list(),
                                       event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueCopyBufferToImage(
            m_queue,
            src_buffer.get(),
            dst_image.get(),
            src_offset,
            dst_origin,
            region,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }


#if defined(BOOST_NO_CXX11_LAMBDAS) || !defined(BOOST_COMPUTE_USE_CPP11)
    // Resolve the lambda syntax sugar
    template<class Function>
    struct walk_image
    {
        Function m_walk_elemets;
        char * m_pImage3D;
        size_t m_origin3[3];
        size_t m_region3[3];
        size_t m_row_pitch;
        size_t m_slice_pitch;
        size_t m_element_size;
        user_event m_user_ev;
        walk_image(Function walk_elemets,
                   char * pImage3D,
                   const size_t *origin,
                   const size_t *region,
                   size_t row_pitch,
                   size_t slice_pitch,
                   size_t element_size,
                   user_event user_ev) :
            m_walk_elemets(walk_elemets),
            m_pImage3D(pImage3D),
            m_row_pitch(row_pitch),
            m_slice_pitch(slice_pitch),
            m_element_size(element_size),
            m_user_ev(user_ev)
        {
            std::copy(origin, origin + 3, m_origin3);
            std::copy(region, region + 3, m_region3);
        }

        void operator () () const
        {
            // Walks all the image elements
            char * pImage2D = m_pImage3D;
            for(size_t d = m_origin3[2]; d < m_region3[2]; ++d) {
                 char * pImage1D = pImage2D;
                for(size_t h = m_origin3[1]; h < m_region3[1]; ++h) {
                    char *pElem = pImage1D;
                    for(size_t w = m_origin3[0]; w < m_region3[0]; ++w) {
                        m_walk_elemets((void *)pElem, w, h, d);
                        pElem += m_element_size;
                    }
                    pImage1D += m_row_pitch;
                }
                pImage2D += m_slice_pitch;
            }
            if(m_user_ev.get()) {
                m_user_ev.set_status(event::complete);
            }
        }
    };
#endif

    /// The function specified by \p walk_elemets must be invokable with arguments
    /// (void *pElem, size_t x, size_t y, size_t z),
    /// like std::function<void(void *, size_t, size_t, size_t)>.
    template<class Function>
    void enqueue_walk_image(const image_object& image,
                            Function walk_elemets,
                            cl_map_flags flags = compute::command_queue::map_read,
                            const size_t *origin = NULL,
                            const size_t *region = NULL,
                            const wait_list &events = wait_list(),
                            event * pevent = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(image.get_context() == this->get_context());

        size_t row_pitch = 0;
        size_t slice_pitch = 0;
        event map_event, *pmap_event = NULL;
        user_event user_ev;
        wait_list unmap_wait;
        extents<3> origin3( 0 );
        extents<3> region3;
        region3[0] = image.width();
        region3[1] = (size_t)std::max((size_t)1, image.height());
        region3[2] = (size_t)std::max((size_t)1, image.depth());

        if (origin) {
            origin3[0] = origin[0]; origin3[1] = origin[1]; origin3[2] = origin[2];
        }

        if (region) {
            region3[0] = region[0]; region3[1] = region[1]; region3[2] = region[2];
        }

        if (pevent) {
            // Async exec
            user_ev = user_event(get_context());
            unmap_wait.insert(user_ev);
            pmap_event = &map_event;
        }

        char * const pImage3D = reinterpret_cast<char *>(
                   enqueue_map_image(
                        image,
                        flags,
                        origin3.data(),
                        region3.data(),
                        &row_pitch,
                        &slice_pitch,
                        events,
                        pmap_event)
                    );

        size_t element_size = image.get_image_info<size_t>(CL_IMAGE_ELEMENT_SIZE);

#if defined(BOOST_NO_CXX11_LAMBDAS) || !defined(BOOST_COMPUTE_USE_CPP11)
        walk_image<Function> func(walk_elemets, pImage3D, origin3.data(), region3.data(), row_pitch, slice_pitch, element_size, user_ev);
#else
        auto func = [=]()
        {
            // Walks all the image elements
            char * pImage2D = pImage3D;
            for(size_t d = origin3[2]; d < region3[2]; ++d) {
                 char * pImage1D = pImage2D;
                for(size_t h = origin3[1]; h < region3[1]; ++h) {
                    char *pElem = pImage1D;
                    for(size_t w = origin3[0]; w < region3[0]; ++w) {
                        walk_elemets((void *)pElem, w, h, d);
                        pElem += element_size;
                    }
                    pImage1D += row_pitch;
                }
                pImage2D += slice_pitch;
            }
            if(pevent) {
                user_ev.set_status(event::complete);
            }
        };
#endif
        if (pevent) {
            // Async exec
            pmap_event->set_callback(func);
        } else {
            func();
        }
        enqueue_unmap_buffer(image, pImage3D, unmap_wait, pevent);
    }

    struct fillc
    {
        size_t m_element_size;
        char m_fill_color[16];
        fillc(const fillc & o) : m_element_size(o.m_element_size)
        {
            const char * origin = static_cast<const char *>(o.m_fill_color);
            std::copy(origin, origin + 16, static_cast<char *>(m_fill_color));
        }

        fillc(size_t element_size, const void * fill_color) : m_element_size(element_size)
        {
            const char * origin = static_cast<const char *>(fill_color);
            std::copy(origin, origin + 16, static_cast<char *>(m_fill_color));
        }
        void operator () (void *pelem, size_t, size_t, size_t) const
        {
            std::copy(m_fill_color, m_fill_color + m_element_size, static_cast<char *>(pelem));
        }
        void operator () (void *pelem, size_t, size_t, size_t)
        {
            std::copy(m_fill_color, m_fill_color + m_element_size, static_cast<char *>(pelem));
        }
    };

    /// Enqueues a command to fill \p image with \p fill_color.
    void enqueue_rawfill_image_walking(const image_object& image,
                             const void *fill_color,
                             const size_t *origin,
                             const size_t *region,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        size_t element_size = image.get_image_info<size_t>(CL_IMAGE_ELEMENT_SIZE);

        fillc f(element_size, fill_color);

        enqueue_walk_image(image, f, command_queue::map_write, origin, region, events, event_);
    }

    /// \overload
    template<size_t N>
    void enqueue_rawfill_image_walking(const image_object& image,
                             const void *fill_color,
                             const extents<N> origin,
                             const extents<N> region,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N <= 3);
        BOOST_ASSERT(image.get_context() == this->get_context());

        size_t origin3[3] = { 0, 0, 0 };
        size_t region3[3] = { 1, 1, 1 };

        std::copy(origin.data(), origin.data() + N, origin3);
        std::copy(region.data(), region.data() + N, region3);

        enqueue_rawfill_image_walking(
            image, fill_color, origin3, region3, events, event_
        );
    }

    #if defined(CL_VERSION_1_2) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a command to fill \p image with \p fill_color.
    ///
    /// \see_opencl_ref{clEnqueueFillImage}
    ///
    /// \opencl_version_warning{1,2}
    void enqueue_fill_image(const image_object& image,
                             const void *fill_color,
                             const size_t *origin,
                             const size_t *region,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        if (get_version() < 120)
        {
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));
        }
        else
        {
            BOOST_ASSERT(m_queue != 0);
            BOOST_ASSERT(image.get_context() == this->get_context());

            cl_int ret = clEnqueueFillImage(
                        m_queue,
                        image.get(),
                        fill_color,
                        origin,
                        region,
                        events.size(),
                        events.get_event_ptr(),
                        event_ ? &event_->get() : NULL
                                 );

            if(ret != CL_SUCCESS){
                BOOST_THROW_EXCEPTION(opencl_error(ret));
            }
        }
    }

    /// \overload
    template<size_t N>
    void enqueue_fill_image(const image_object& image,
                             const void *fill_color,
                             const extents<N> origin,
                             const extents<N> region,
                             const wait_list &events = wait_list(),
                             event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N <= 3);
        BOOST_ASSERT(image.get_context() == this->get_context());

        size_t origin3[3] = { 0, 0, 0 };
        size_t region3[3] = { 1, 1, 1 };

        std::copy(origin.data(), origin.data() + N, origin3);
        std::copy(region.data(), region.data() + N, region3);

        enqueue_fill_image(
            image, fill_color, origin3, region3, events, event_
        );
    }

    /// Enqueues a command to migrate \p mem_objects.
    ///
    /// \see_opencl_ref{clEnqueueMigrateMemObjects}
    ///
    /// \opencl_version_warning{1,2}
    void enqueue_migrate_memory_objects(uint_ num_mem_objects,
                                         const cl_mem *mem_objects,
                                         cl_mem_migration_flags flags,
                                         const wait_list &events = wait_list(),
                                         event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        if (get_version() < 120)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueMigrateMemObjects(
            m_queue,
            num_mem_objects,
            mem_objects,
            flags,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }
    #endif // CL_VERSION_1_2

    /// Enqueues a kernel for execution.
    ///
    /// \see_opencl_ref{clEnqueueNDRangeKernel}
    void enqueue_nd_range_kernel(const kernel &kernel,
                                  size_t work_dim,
                                  const size_t *global_work_offset,
                                  const size_t *global_work_size,
                                  const size_t *local_work_size,
                                  const wait_list &events = wait_list(),
                                  event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(work_dim > 0);
        BOOST_ASSERT(kernel.get_context() == this->get_context());

        cl_int ret = clEnqueueNDRangeKernel(
            m_queue,
            kernel,
            static_cast<cl_uint>(work_dim),
            global_work_offset,
            global_work_size,
            local_work_size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// \overload
    template<size_t N>
    void enqueue_nd_range_kernel(const kernel &kernel,
                                 const extents<N> &global_work_offset,
                                 const extents<N> &global_work_size,
                                 const extents<N> &local_work_size,
                                 const wait_list &events = wait_list(),
                                 event * event_ = NULL)
    {
        BOOST_STATIC_ASSERT(N > 0);
        enqueue_nd_range_kernel(
            kernel,
            N,
            global_work_offset.data(),
            global_work_size.data(),
            (local_work_size[0] == 0) ? NULL : local_work_size.data(),
            events,
            event_
        );
    }

    /// Enqueues a kernel for execution.
    ///
    /// \see_opencl_ref{clEnqueueNDRangeKernel}
    event enqueue_nd_range_kernel_async(const kernel &kernel,
                                  size_t work_dim,
                                  const size_t *global_work_offset,
                                  const size_t *global_work_size,
                                  const size_t *local_work_size,
                                  const wait_list &events = wait_list())
    {
        event event_;

        enqueue_nd_range_kernel(kernel,
                                work_dim,
                                global_work_offset,
                                global_work_size,
                                local_work_size,
                                events,
                                &event_);
        return event_;
    }

    /// \overload
    template<size_t N>
    event enqueue_nd_range_kernel_async(const kernel &kernel,
                                  const extents<N> &global_work_offset,
                                  const extents<N> &global_work_size,
                                  const extents<N> &local_work_size,
                                  const wait_list &events = wait_list())
    {
        event event_;

        enqueue_nd_range_kernel(
            kernel,
            N,
            global_work_offset.data(),
            global_work_size.data(),
            (local_work_size[0] == 0) ? NULL : local_work_size.data(),
            events,
            &event_.get()
        );

        return event_;
    }

    /// Convenience method which calls enqueue_nd_range_kernel() with a
    /// one-dimensional range.
    void enqueue_1d_range_kernel(const kernel &kernel,
                                  size_t global_work_offset,
                                  size_t global_work_size,
                                  size_t local_work_size,
                                  const wait_list &events = wait_list(),
                                  event * event_ = NULL)
    {
        enqueue_nd_range_kernel(
            kernel,
            1,
            &global_work_offset,
            &global_work_size,
            local_work_size ? &local_work_size : 0,
            events,
            event_
        );
    }

    event enqueue_1d_range_kernel_async(const kernel &kernel,
                                  size_t global_work_offset,
                                  size_t global_work_size,
                                  size_t local_work_size,
                                  const wait_list &events = wait_list())
    {
        event event_;
        enqueue_1d_range_kernel(kernel,
                                      global_work_offset,
                                      global_work_size,
                                      local_work_size,
                                      events,
                                      &event_);
        return event_;

    }

    /// Enqueues a kernel to execute using a single work-item.
    ///
    /// \see_opencl_ref{clEnqueueTask}
    void enqueue_task(const kernel &kernel,
                       const wait_list &events = wait_list(),
                       event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);
        BOOST_ASSERT(kernel.get_context() == this->get_context());

        cl_int ret;

        // clEnqueueTask() was deprecated in OpenCL 2.0. In that case we
        // just forward to the equivalent clEnqueueNDRangeKernel() call.
        #ifdef CL_VERSION_2_0
        if (get_version() >= 200)
        {
            size_t one = 1;
            ret = clEnqueueNDRangeKernel(
                        m_queue, kernel, 1, 0, &one, &one,
                        events.size(), events.get_event_ptr(), event_ ? &event_->get() : NULL
                        );
        }
        else
        #endif
        {
            ret = clEnqueueTask(
                        m_queue, kernel, events.size(), events.get_event_ptr(), event_ ? &event_->get() : NULL
                        );
        }

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a function to execute on the host.
    void enqueue_native_kernel(void (BOOST_COMPUTE_CL_CALLBACK *user_func)(void *),
                               void *args,
                               size_t cb_args,
                               uint_ num_mem_objects,
                               const cl_mem *mem_list,
                               const void **args_mem_loc,
                               const wait_list &events = wait_list(),
                               event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        cl_int ret = clEnqueueNativeKernel(
            m_queue,
            user_func,
            args,
            cb_args,
            num_mem_objects,
            mem_list,
            args_mem_loc,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );
        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Convenience overload for enqueue_native_kernel() which enqueues a
    /// native kernel on the host with a nullary function.
    void enqueue_native_kernel(void (BOOST_COMPUTE_CL_CALLBACK *user_func)(void),
                                const wait_list &events = wait_list(),
                                event * event_ = NULL)
    {
        enqueue_native_kernel(
            detail::nullary_native_kernel_trampoline,
            reinterpret_cast<void *>(&user_func),
            sizeof(user_func),
            0,
            0,
            0,
            events,
            event_
        );
    }

    /// Flushes the command queue.
    ///
    /// \see_opencl_ref{clFlush}
    void flush()
    {
        BOOST_ASSERT(m_queue != 0);

        clFlush(m_queue);
    }

    /// Blocks until all outstanding commands in the queue have finished.
    ///
    /// \see_opencl_ref{clFinish}
    void finish()
    {
        BOOST_ASSERT(m_queue != 0);

        clFinish(m_queue);
    }

    /// Enqueues a barrier in the queue.
    void enqueue_barrier()
    {
        BOOST_ASSERT(m_queue != 0);

        #ifdef CL_VERSION_1_2
        if (get_version() >= 120)
            clEnqueueBarrierWithWaitList(m_queue, 0, 0, 0);
        else
        #endif
        clEnqueueBarrier(m_queue);
    }

    #if defined(CL_VERSION_1_2) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a barrier in the queue after \p events.
    ///
    /// \opencl_version_warning{1,2}
    void enqueue_barrier(const wait_list &events,
                         event * event_ = NULL)
    {
        BOOST_ASSERT(m_queue != 0);

        if (get_version() < 120)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        clEnqueueBarrierWithWaitList(
            m_queue, events.size(), events.get_event_ptr(), event_ ? &event_->get() : NULL
        );
    }
    #endif // CL_VERSION_1_2

    /// Enqueues a marker in the queue and returns an event that can be
    /// used to track its progress.
    void enqueue_marker(event * event_)
    {
        cl_int ret;
        #ifdef CL_VERSION_1_2
        if (get_version() >= 120)
            ret = clEnqueueMarkerWithWaitList(m_queue, 0, 0, event_ ? &event_->get() : NULL);
        else
        #endif
        ret = clEnqueueMarker(m_queue, event_ ? &event_->get() : NULL);

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    #if defined(CL_VERSION_1_2) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a marker after \p events in the queue and returns an
    /// event that can be used to track its progress.
    ///
    /// \opencl_version_warning{1,2}
    void enqueue_marker(const wait_list &events,
                         event * event_ = NULL)
    {
        if (get_version() < 120)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueMarkerWithWaitList(
            m_queue, events.size(), events.get_event_ptr(), event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }
    #endif // CL_VERSION_1_2

    #if defined(CL_VERSION_2_0) || defined(BOOST_COMPUTE_DOXYGEN_INVOKED)
    /// Enqueues a command to copy \p size bytes of data from \p src_ptr to
    /// \p dst_ptr.
    ///
    /// \opencl_version_warning{2,0}
    ///
    /// \see_opencl2_ref{clEnqueueSVMMemcpy}
    void enqueue_svm_memcpy(void *dst_ptr,
                            const void *src_ptr,
                            size_t size,
                            const wait_list &events = wait_list(),
                            event * event_ = NULL)
    {
        if (get_version() < 200)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueSVMMemcpy(
            m_queue,
            event_ ? CL_FALSE : CL_TRUE,
            dst_ptr,
            src_ptr,
            size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to copy \p size bytes of data from \p src_ptr to
    /// \p dst_ptr. The operation is performed asynchronously.
    ///
    /// \opencl_version_warning{2,0}
    ///
    /// \see_opencl2_ref{clEnqueueSVMMemcpy}
    event enqueue_svm_memcpy_async(void *dst_ptr,
                                   const void *src_ptr,
                                   size_t size,
                                   const wait_list &events = wait_list())
    {
        event event_;

        enqueue_svm_memcpy(dst_ptr,
                           src_ptr,
                           size,
                           events,
                           &event_);

        return event_;
    }

    /// Enqueues a command to fill \p size bytes of data at \p svm_ptr with
    /// \p pattern.
    ///
    /// \opencl_version_warning{2,0}
    ///
    /// \see_opencl2_ref{clEnqueueSVMMemFill}
    void enqueue_svm_fill(void *svm_ptr,
                           const void *pattern,
                           size_t pattern_size,
                           size_t size,
                           const wait_list &events = wait_list(),
                           event * event_ = NULL)

    {
        if (get_version() < 200)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueSVMMemFill(
            m_queue,
            svm_ptr,
            pattern,
            pattern_size,
            size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to free \p svm_ptr.
    ///
    /// \opencl_version_warning{2,0}
    ///
    /// \see_opencl2_ref{clEnqueueSVMFree}
    ///
    /// \see svm_free()
    void enqueue_svm_free(void *svm_ptr,
                           const wait_list &events = wait_list(),
                           event * event_ = NULL)
    {
        if (get_version() < 200)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueSVMFree(
            m_queue,
            1,
            &svm_ptr,
            0,
            0,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to map \p svm_ptr to the host memory space.
    ///
    /// \opencl_version_warning{2,0}
    ///
    /// \see_opencl2_ref{clEnqueueSVMMap}
    void enqueue_svm_map(void *svm_ptr,
                         size_t size,
                         cl_map_flags flags,
                         const wait_list &events = wait_list(),
                         event * event_ = NULL)
    {
        if (get_version() < 200)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueSVMMap(
            m_queue,
            event_ ? CL_FALSE : CL_TRUE,
            flags,
            svm_ptr,
            size,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }

    /// Enqueues a command to unmap \p svm_ptr from the host memory space.
    ///
    /// \opencl_version_warning{2,0}
    ///
    /// \see_opencl2_ref{clEnqueueSVMUnmap}
    void enqueue_svm_unmap(void *svm_ptr,
                            const wait_list &events = wait_list(),
                            event * event_ = NULL)
    {
        if (get_version() < 200)
            BOOST_THROW_EXCEPTION(opencl_error(CL_INVALID_DEVICE));

        cl_int ret = clEnqueueSVMUnmap(
            m_queue,
            svm_ptr,
            events.size(),
            events.get_event_ptr(),
            event_ ? &event_->get() : NULL
        );

        if(ret != CL_SUCCESS){
            BOOST_THROW_EXCEPTION(opencl_error(ret));
        }
    }
    #endif // CL_VERSION_2_0

    /// Returns \c true if the command queue is the same at \p other.
    bool operator==(const command_queue &other) const
    {
        return m_queue == other.m_queue;
    }

    /// Returns \c true if the command queue is different from \p other.
    bool operator!=(const command_queue &other) const
    {
        return m_queue != other.m_queue;
    }

    /// \internal_
    operator cl_command_queue() const
    {
        return m_queue;
    }

    /// \internal_
    bool check_device_version(int major, int minor) const
    {
        int ver = static_cast<int>(get_version());
        int check = major * 100 + minor;
        return check <= ver;
    }

private:
    cl_command_queue m_queue;
    mutable uint_ m_version;
};

inline buffer buffer::clone(command_queue &queue) const
{
    buffer copy(get_context(), size(), get_memory_flags());
    queue.enqueue_copy_buffer(*this, copy, 0, 0, size());
    return copy;
}

inline image1d image1d::clone(command_queue &queue) const
{
    image1d copy(
        get_context(), width(), format(), get_memory_flags()
    );

    queue.enqueue_copy_image(*this, copy, origin(), copy.origin(), size());

    return copy;
}

inline image2d image2d::clone(command_queue &queue) const
{
    image2d copy(
        get_context(), width(), height(), format(), get_memory_flags()
    );

    queue.enqueue_copy_image(*this, copy, origin(), copy.origin(), size());

    return copy;
}

inline image3d image3d::clone(command_queue &queue) const
{
    image3d copy(
        get_context(), width(), height(), depth(), format(), get_memory_flags()
    );

    queue.enqueue_copy_image(*this, copy, origin(), copy.origin(), size());

    return copy;
}

/// \internal_ define get_info() specializations for command_queue
BOOST_COMPUTE_DETAIL_DEFINE_GET_INFO_SPECIALIZATIONS(command_queue,
    ((cl_context, CL_QUEUE_CONTEXT))
    ((cl_device_id, CL_QUEUE_DEVICE))
    ((uint_, CL_QUEUE_REFERENCE_COUNT))
    ((cl_command_queue_properties, CL_QUEUE_PROPERTIES))
)

} // end compute namespace
} // end boost namespace

#endif // BOOST_COMPUTE_COMMAND_QUEUE_HPP
