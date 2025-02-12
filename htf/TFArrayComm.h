// Copyright (c) 2020 HOOMD-TF Developers

#ifndef m_IPC_ARRAY_COMM_
#define m_IPC_ARRAY_COMM_

#include <hoomd/ExecutionConfiguration.h>
#include <hoomd/GlobalArray.h>
#include <string.h>
#include <sys/mman.h>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include "CommStruct.h"
#ifdef ENABLE_CUDA
#include "TFArrayComm.cuh"
#endif //ENABLE_CUDA

/*! \file TFArrayComm.h
    \brief Declaration of TFArrayComm class
*/

namespace hoomd_tf
    {

    // This uses an enum instead of specialization
    // to treat the CommMode because you cannot do partial specialization of a
    // method. The overhead of the ifs is nothing, since the compiler will see them
    // as if (1 == 0) and if(1 == 1) and optimize them.

    enum class TFCommMode { GPU, CPU };

    /*! TFCommMode class
     *  Template class for TFArrayComm
     *  \tfparam M Whether TF is on CPU or GPU.
     */
    template <TFCommMode M, typename T>
        class TFArrayComm
        {
        public:

        /*! Default constructor. Just checks GPU exists if used
         */
        TFArrayComm()
            {
            checkDevice();
            }

        /*! Normal constructor.
         * \param gpu_array The local array which will store communicated data
         * \param name The name of the array
         */
        TFArrayComm(GlobalArray<T>& gpu_array,
            const char* name,
            std::shared_ptr<const ExecutionConfiguration> exec_conf)
            : m_comm_struct(gpu_array, name),
              m_array(&gpu_array),
              m_exec_conf(exec_conf)
                {
                checkDevice();
                }

        //! Copy constructor
        TFArrayComm(TFArrayComm&& other)
            {
            // use the assignment overloaded operator
            *this = std::move(other);
            }

        //! overloaded assignment operator
        TFArrayComm& operator=(TFArrayComm&& other)
            {
            checkDevice();
            // copy over variables
            m_array = other.m_array;
            m_comm_struct = std::move(other.m_comm_struct);
            m_exec_conf = other.m_exec_conf;
            return *this;
            }

        /*! Copy contents of given array to this array
         *  \param array the array whose contents to copy into this one
         * \param offset how much offset to apply to the given array
         * \param size how much to copy over from given array
         * \param unstuff4 Set to true if the 4th column should be converted from stuffed integers to scalars that can be cast to integers
         */
        void receiveArray(const GlobalArray<T>& array, int offset = 0, unsigned int size = 0, bool unstuff4 = false)
            {
            // convert size into mem size
            if(!size)
                {
                size = m_comm_struct.mem_size;
                }
            else
                {
                size = size * sizeof(T);
                }
            assert(offset + size / sizeof(T) <= array.getNumElements());
            if (M == TFCommMode::CPU)
                {
                ArrayHandle<T> handle(*m_array,
                    access_location::host,
                    access_mode::overwrite);
                ArrayHandle<T> ohandle(array,
                    access_location::host,
                    access_mode::read);
                memcpy(handle.data, ohandle.data + offset, size);
                // now fix-up the type if necessary
                if(unstuff4)
                    for(unsigned int i = 0; i < size / sizeof(T); i++)
                        handle.data[i].w = static_cast<Scalar> (__scalar_as_int(handle.data[i].w));
                }
            else
                {
                #ifdef ENABLE_CUDA
                    ArrayHandle<T> handle(*m_array,
                        access_location::device,
                        access_mode::overwrite);
                    ArrayHandle<T> ohandle(array,
                        access_location::device,
                        access_mode::read);
                    cudaMemcpy(handle.data,
                        ohandle.data + offset,
                        size,
                        cudaMemcpyDeviceToDevice);
                    if(unstuff4)
		      htf_gpu_unstuff4(handle.data, size / sizeof(T), m_comm_struct.stream);
                    CHECK_CUDA_ERROR();
                #endif
                }
            }

        /*! Copy contents of this array to given array
         *  \param array the array whose contents to overwrite
         * \param ignore4 Set to true if the 4th column should not be overwritten
         */
        void sendArray(const GlobalArray<T>& array, bool ignore4 = false)
            {
            unsigned int size = m_comm_struct.mem_size;
            if (M == TFCommMode::CPU)
                {
                ArrayHandle<T> handle(*m_array,
                    access_location::host,
                    access_mode::read);
                ArrayHandle<T> ohandle(array,
                    access_location::host,
                    access_mode::overwrite);
                if(ignore4)
                {
                    for(unsigned int i = 0; i < size / sizeof(T); i++)
                    {
                        ohandle.data[i].x = handle.data[i].x;
                        ohandle.data[i].y = handle.data[i].y;
                        ohandle.data[i].z = handle.data[i].z;
                    }
                }
                else
                    memcpy(ohandle.data, handle.data, size);

                }
            else
                {
                #ifdef ENABLE_CUDA
                    ArrayHandle<T> handle(*m_array,
                        access_location::device,
                        access_mode::read);
                    ArrayHandle<T> ohandle(array,
                        access_location::device,
                        access_mode::overwrite);

                    if(ignore4)
		                htf_gpu_copy3(ohandle.data, handle.data, size / sizeof(T), m_comm_struct.stream);
                    else
                        cudaMemcpy(ohandle.data,
                            handle.data,
                            size,
                            cudaMemcpyDeviceToDevice);
                    CHECK_CUDA_ERROR();
                #endif
                }
            }

        //! Set all values in an array to target value
        //! \param v target int value to fill array with
        void memsetArray(int v)
            {
            if (M == TFCommMode::CPU)
                {
                ArrayHandle<T> handle(*m_array,
                    access_location::host,
                    access_mode::overwrite);
                memset( static_cast<void*> (handle.data), v, m_comm_struct.mem_size);
                }
            else
                {
                #ifdef ENABLE_CUDA
                    ArrayHandle<T> handle(*m_array,
                        access_location::device,
                        access_mode::overwrite);
                    cudaMemset(static_cast<void*> (handle.data), v, m_comm_struct.mem_size);
                    CHECK_CUDA_ERROR();
                #endif
                }
            }

        //! Set offset value for batching
        void setOffset(size_t offset)
        {
        m_comm_struct.offset = offset;
        }

        //! Set batch size
        void setBatchSize(size_t N)
            {
            m_comm_struct.num_elements[0] = N;
            }

        /*! Returns our underlying array as a vector
         */
        std::vector<T> getArray() const
            {
            ArrayHandle<T> handle(*m_array, access_location::host, access_mode::read);
            return std::vector<T>(handle.data, handle.data + m_array->getNumElements());
            }

        /*! Return memory address of the CommStruct wrapper around our array
         */
        int64_t getAddress() const
            {
            //need to cast to base class then get pointer to that.
            return reinterpret_cast<int64_t>(static_cast<const CommStruct*>(&m_comm_struct));
            }

        #ifdef ENABLE_CUDA
            //! Set the CUDA stream that will be used
            void setCudaStream(cudaStream_t s) { m_comm_struct.stream = s;}
            cudaStream_t getCudaStream() const
                {
                return m_comm_struct.stream;
                }
        #endif

        protected:
        //! Make sure that CUDA is enabled before running in GPU mode
        void checkDevice()
            {
            #ifndef ENABLE_CUDA
                if (M == TFCommMode::GPU)
                    throw std::runtime_error(
                        "CUDA compilation not enabled so cannot use GPU CommMode");
            #endif
            }

        using value_type = T;

        private:
        CommStructDerived<T> m_comm_struct;
        GlobalArray<T>* m_array;
        std::shared_ptr<const ExecutionConfiguration> m_exec_conf;
        };

    //! Export python binding
    void export_TFArrayComm(pybind11::module& m);

    }

#endif  //m_IPC_ARRAY_COMM_
