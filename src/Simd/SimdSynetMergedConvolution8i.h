/*
* Simd Library (http://ermig1979.github.io/Simd).
*
* Copyright (c) 2011-2020 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#ifndef __SimdSynetMergedConvolution8i_h__
#define __SimdSynetMergedConvolution8i_h__

#include "Simd/SimdArray.h"
#include "Simd/SimdPerformance.h"
#include "Simd/SimdRuntime.h"
#include "Simd/SimdSynetConvolution8i.h"

#ifdef _N
#undef _N
#endif

namespace Simd
{
    struct MergConvParam8i
    {
        size_t batch, count;
        SimdSynetCompatibilityType compatibility;
        SimdConvolutionParameters conv[3];

        MergConvParam8i(size_t batch, const SimdConvolutionParameters * convs, size_t count, SimdSynetCompatibilityType compatibility)
        {
            assert(count <= 3);
            this->batch = batch;
            this->count = count;
            this->compatibility = compatibility;
            for (size_t i = 0; i < count; ++i)
                this->conv[i] = convs[i];
        }

        bool Valid()
        {
            if (count < 2 || count > 3)
                return false;
            for (size_t i = 0; i < count; ++i)
            {
                SimdConvolutionParameters & c = conv[i];                
                if ((c.srcT != SimdTensorData32f && c.srcT != SimdTensorData8u) || (c.dstT != SimdTensorData32f && c.dstT != SimdTensorData8u))
                    return false;
                if (c.srcF != SimdTensorFormatNhwc || c.dstF != SimdTensorFormatNhwc)
                    return false;
                if (c.dstH != (c.srcH + c.padY + c.padH - (c.dilationY * (c.kernelY - 1) + 1)) / c.strideY + 1 || c.dstH == 0)
                    return false;
                if (c.dstW != (c.srcW + c.padX + c.padW - (c.dilationY * (c.kernelX - 1) + 1)) / c.strideX + 1 || c.dstW == 0)
                    return false;
                if (c.kernelY != c.kernelX || !(c.kernelY == 1 || c.kernelY == 3 || c.kernelY == 5 || c.kernelY == 7))
                    return false;
                if (c.strideY != c.strideX || !(c.strideY == 1 || c.strideY == 2 || c.strideY == 3))
                    return false;
                if (c.dilationY != 1 || c.dilationX != 1)
                    return false;

                if (c.dstH == (c.srcH + c.padY + c.padH - (c.dilationY * (c.kernelY - 1) + 1) - 1) / c.strideY + 1)
                    c.padH--;
                if (c.dstW == (c.srcW + c.padX + c.padW - (c.dilationY * (c.kernelX - 1) + 1) - 1) / c.strideX + 1)
                    c.padW--;
            }
            if (count == 3)
            {
                if (conv[0].group != 1 || (conv[0].kernelY != 1 && conv[0].kernelY != 3))
                    return false;
                if (conv[1].group != conv[1].srcC || conv[1].group != conv[1].dstC || (conv[1].kernelY != 3 && conv[1].kernelY != 5 && conv[1].kernelY != 7))
                    return false;
                if (conv[2].group != 1 || conv[2].kernelY != 1 || conv[2].strideY != 1)
                    return false;
            }
            else
            {
                if (conv[0].group == 1)
                {
                    if (conv[0].kernelY != 1 && conv[0].kernelY != 3)
                        return false;
                    if (conv[1].group != conv[1].srcC || conv[1].group != conv[1].dstC || (conv[1].kernelY != 3 && conv[1].kernelY != 5 && conv[1].kernelY != 7))
                        return false;
                }
                else
                {
                    if (conv[0].group != conv[0].srcC || conv[0].group != conv[0].dstC || (conv[0].kernelY != 3 && conv[0].kernelY != 5 && conv[0].kernelY != 7))
                        return false;
                    if (conv[1].group != 1 || conv[1].kernelY != 1 || conv[1].strideY != 1)
                        return false;
                }
            }
            return true;
        }

        SIMD_INLINE bool IsPad(size_t index, size_t value) const
        {
            return conv[index].padY == value && conv[index].padX == value && conv[index].padH == value && conv[index].padW == value;
        }

#ifdef SIMD_PERFORMANCE_STATISTIC
        String Info() const
        {
            std::stringstream ss;
            ss << count << ":" << batch << "x" << conv[0].srcC << "x" << conv[0].srcH << "x" << conv[0].srcW;
            for (size_t i = 0; i < count; ++i)
                ss << "-" << (conv[i].group != 1 ? String("") : ToStr(conv[i].dstC) + "x") << conv[i].kernelY << "x" << conv[i].strideY;
            return ss.str();
        }

        long long Flop(size_t i) const
        {
            return batch*conv[i].kernelY * conv[i].kernelX * conv[i].srcC * conv[i].dstH * conv[i].dstW * conv[i].dstC / conv[i].group * 2;
        }

        long long Flop() const
        {
            long long flop = 0;
            for (size_t i = 0; i < count; ++i)
                flop += Flop(i);
            return flop;
        }
#endif
    };

    class SynetMergedConvolution8i : public Deletable
    {
    public:
        virtual const MergConvParam8i & Param() const = 0;

        virtual size_t ExternalBufferSize() const = 0;

        virtual size_t InternalBufferSize() const = 0;

        virtual void SetParams(const float * const * weight, SimdBool * internal, const float * const * bias, const float * const * params, const float* const* stats) = 0;

        virtual void Forward(const uint8_t* src, uint8_t* buf, uint8_t* dst) = 0;

#if defined(SIMD_PERFORMANCE_STATISTIC)
        virtual Base::PerformanceMeasurer* Perf(const String& func) = 0;
#endif
    };

    namespace Base
    {
        class SynetMergedConvolution8i : public Simd::SynetMergedConvolution8i
        {
        public:
            SynetMergedConvolution8i(const MergConvParam8i& p);

            virtual String Desc() const { return "Base"; }
            virtual const MergConvParam8i& Param() const { return _param; }
            virtual size_t ExternalBufferSize() const;
            virtual size_t InternalBufferSize() const;
            virtual void SetParams(const float * const * weight, SimdBool * internal, const float * const * bias, const float * const * params, const float* const* stats);
            virtual void Forward(const uint8_t* src, uint8_t* buf, uint8_t* dst);

#if defined(SIMD_PERFORMANCE_STATISTIC)
            virtual Base::PerformanceMeasurer* Perf(const String& func);
#endif

            enum Term8iType
            {
                Term8iSingle8u,
                Term8iSingle32f,
                Term8iFirst,
                Term8iIterim,
                Term8iLast8u,
                Term8iLast32f,
                Term8iSize
            };

            struct AlgParam
            {
                size_t miC, maC, yStep[3], bufH[3], dp[2], dw[3], sizeC, sizeD;
                int32_t zeroC, zeroD, upper;
            };

            typedef void(*Cvt8uTo32fPtr)(const uint8_t* src, size_t batch, size_t channels, size_t height, size_t width, 
                SimdTensorFormatType format, const float* scale, const float* shift, float* dst, SimdSynetCompatibilityType compatibility);

            typedef void(*Cvt32fTo8uPtr)(const float* src, size_t batch, size_t channels, size_t height, size_t width, 
                SimdTensorFormatType format, const float* scale, const float* shift, uint8_t* dst, SimdSynetCompatibilityType compatibility);

            typedef void(*InputConvolutionPtr)(const uint8_t* src, const SimdConvolutionParameters& p, const AlgParam& a, size_t maC, size_t yBeg, size_t yEnd,
                const int8_t* weight, const float* norm, const float* bias, const float* params, float* dst);

            typedef void(*DepthwiseConvolutionPtr)(const float* src, const SimdConvolutionParameters& p, const AlgParam & a, size_t maC, size_t yBeg, size_t yEnd,
                const float* weight, const float* bias, const float* params, const float* scale, const float* shift, uint8_t * dst);

            typedef void(*OutputConvolutionPtr)(const uint8_t* src, const SimdConvolutionParameters& p, const AlgParam& a, size_t maC, size_t yBeg, size_t yEnd,
                const int8_t* weight, const float* norm, const float* bias, const float* params, const float* scale, const float* shift, int32_t* buf, uint8_t* dst);

        protected:
            uint8_t* GetBuffer(uint8_t* buffer);
            void Quantize(const float* weight, const float* bias, size_t i, size_t q);
            void ReorderInputWeight(const SimdConvolutionParameters& p, Array8i & weight);
            void ReorderDepthwiseWeight(const SimdConvolutionParameters& p, Array32f & weight);
            void ReorderOutputWeight(const SimdConvolutionParameters& p, Array8i& weight);
            void DirectConvolution8i(const uint8_t* src, size_t i, size_t q, uint8_t* buf, int32_t* sum, float* dst);

            MergConvParam8i _param;
            bool _s8u, _d8u, _dw0, _1x1;
            size_t _sizeS, _sizeD, _sizeI[2], _sizeB[5];
            CvtParam _cvt[3];
            Array8u _buffer;
            Array8i _weight8i[2];
            Array32f _weight32f, _norm[2], _bias[3], _params[3];
            AlgParam _alg;
            Cvt8uTo32fPtr _cvt8uTo32f;
            Cvt32fTo8uPtr _cvt32fTo8u;
            InputConvolutionPtr _input;
            DepthwiseConvolutionPtr _depthwise;
            OutputConvolutionPtr _output[Term8iSize];

        private:
#if defined(SIMD_PERFORMANCE_STATISTIC)
            Base::PerformanceMeasurer * _perf;
#endif        
        };

        void * SynetMergedConvolution8iInit(size_t batch, const SimdConvolutionParameters * convs, size_t count, SimdSynetCompatibilityType compatibility);
    }

#ifdef SIMD_SSE41_ENABLE    
    namespace Sse41
    {
    }
#endif//SIMD_SSE41_ENABLE

#ifdef SIMD_AVX2_ENABLE    
    namespace Avx2
    {
    }
#endif//SIMD_AVX2_ENABLE

#ifdef SIMD_AVX512BW_ENABLE    
    namespace Avx512bw
    {
    }
#endif//SIMD_AVX512BW_ENABLE

#ifdef SIMD_AVX512VNNI_ENABLE    
    namespace Avx512vnni
    {
    }
#endif//SIMD_AVX512VNNI_ENABLE
}
#endif//__SimdSynetMergedConvolution8i_h__
