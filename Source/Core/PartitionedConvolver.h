/*
    MatchMaster - uniformly-partitioned overlap-save FFT convolution with click-free filter swaps.

    Filters ("kernels") are prepared on any non-audio thread with makeKernel() and handed over with
    offer(). The audio thread installs the newest offered kernel at a block boundary and crossfades
    from the old one over a few milliseconds, so the sound follows a knob continuously while it turns.
    Both kernels read the same input history, so a crossfade costs one extra spectral multiply + IFFT.
    Latency: blockSize samples.
*/
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <vector>

namespace mm
{

class PartitionedConvolver
{
public:
    // A filter in the frequency domain: numParts partitions of (blockSize + 1) complex bins.
    struct Kernel
    {
        std::vector<float> re, im; // [part * bins + bin]
    };

    ~PartitionedConvolver() { release(); }

    // Not real-time safe. Call while audio is stopped.
    void prepare (int blockSizeIn, int maxIrLength, int crossfadeBlocksIn = 4)
    {
        release();
        blockSize = blockSizeIn;
        fftSize = 2 * blockSize;
        bins = blockSize + 1;
        numParts = juce::jmax (1, (maxIrLength + blockSize - 1) / blockSize);
        crossfadeBlocks = juce::jmax (1, crossfadeBlocksIn);
        fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSize)));
        scale = inverseScale (*fft, fftSize);

        inputWindow.assign ((size_t) fftSize, 0.0f);
        fdlRe.assign ((size_t) (numParts * bins), 0.0f);
        fdlIm.assign ((size_t) (numParts * bins), 0.0f);
        fdlPos = 0;
        work.assign ((size_t) fftSize * 2, 0.0f);
        accRe.assign ((size_t) bins, 0.0f);
        accIm.assign ((size_t) bins, 0.0f);
        outNew.assign ((size_t) blockSize, 0.0f);
        outOld.assign ((size_t) blockSize, 0.0f);
        inFifo.assign ((size_t) blockSize, 0.0f);
        outFifo.assign ((size_t) blockSize, 0.0f);
        fifoPos = 0;
        fadeBlock = 0;
    }

    // Any thread except audio (not concurrently with prepare). Allocates.
    std::unique_ptr<Kernel> makeKernel (const std::vector<float>& ir) const
    {
        auto k = std::make_unique<Kernel>();
        k->re.assign ((size_t) (numParts * bins), 0.0f);
        k->im.assign ((size_t) (numParts * bins), 0.0f);
        juce::dsp::FFT localFft (juce::roundToInt (std::log2 ((double) fftSize))); // own instance: thread safety
        std::vector<float> buf ((size_t) fftSize * 2);
        for (int p = 0; p < numParts; ++p)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < blockSize; ++i)
            {
                const size_t src = (size_t) (p * blockSize + i);
                if (src < ir.size())
                    buf[(size_t) i] = ir[src];
            }
            localFft.performRealOnlyForwardTransform (buf.data(), false);
            for (int b = 0; b < bins; ++b)
            {
                k->re[(size_t) (p * bins + b)] = buf[(size_t) (2 * b)];
                k->im[(size_t) (p * bins + b)] = buf[(size_t) (2 * b + 1)];
            }
        }
        return k;
    }

    // Hand a kernel to the audio thread. Replaces (and frees) a kernel that was offered but not yet taken.
    void offer (std::unique_ptr<Kernel> k)
    {
        collectGarbage();
        delete pending.exchange (k.release(), std::memory_order_acq_rel);
    }

    // Install a kernel immediately, no crossfade. Only while audio is stopped (e.g. in prepareToPlay).
    void setImmediately (std::unique_ptr<Kernel> k)
    {
        delete pending.exchange (nullptr);
        delete previous;
        previous = nullptr;
        delete current;
        current = k.release();
        fadeBlock = 0;
    }

    // Free kernels the audio thread has finished with. Call from the thread that offers.
    void collectGarbage() { delete retired.exchange (nullptr, std::memory_order_acq_rel); }

    bool isCrossfading() const noexcept { return previous != nullptr; }
    int getLatency() const noexcept { return blockSize; }

    // Audio thread. In place, any number of samples.
    void process (float* data, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            inFifo[(size_t) fifoPos] = data[i];
            data[i] = outFifo[(size_t) fifoPos];
            if (++fifoPos == blockSize)
            {
                fifoPos = 0;
                processBlock();
            }
        }
    }

private:
    static float inverseScale (juce::dsp::FFT& f, int size)
    {
        std::vector<float> d ((size_t) size * 2, 0.0f);
        d[0] = 1.0f;
        f.performRealOnlyForwardTransform (d.data(), false);
        f.performRealOnlyInverseTransform (d.data());
        return std::abs (d[0]) > 1e-12f ? 1.0f / d[0] : 1.0f;
    }

    void processBlock() noexcept
    {
        // start a crossfade to the newest kernel once the previous one is done and its old kernel collected
        if (previous == nullptr && retired.load (std::memory_order_acquire) == nullptr)
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current == nullptr) current = next;
                else { previous = current; current = next; fadeBlock = 0; }
            }

        // slide the input window: [previous block | new block], spectrum into the delay line
        std::copy (inputWindow.begin() + blockSize, inputWindow.end(), inputWindow.begin());
        std::copy (inFifo.begin(), inFifo.end(), inputWindow.begin() + blockSize);
        std::fill (work.begin(), work.end(), 0.0f);
        std::copy (inputWindow.begin(), inputWindow.end(), work.begin());
        fft->performRealOnlyForwardTransform (work.data(), false);
        fdlPos = (fdlPos + numParts - 1) % numParts;
        for (int b = 0; b < bins; ++b)
        {
            fdlRe[(size_t) (fdlPos * bins + b)] = work[(size_t) (2 * b)];
            fdlIm[(size_t) (fdlPos * bins + b)] = work[(size_t) (2 * b + 1)];
        }

        if (current == nullptr)
        {
            std::copy (inFifo.begin(), inFifo.end(), outFifo.begin()); // no filter yet: pass through
            return;
        }
        render (*current, outNew.data());
        if (previous != nullptr)
        {
            render (*previous, outOld.data());
            const float total = (float) (crossfadeBlocks * blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const float w = (float) (fadeBlock * blockSize + i + 1) / total;
                outFifo[(size_t) i] = outOld[(size_t) i] + w * (outNew[(size_t) i] - outOld[(size_t) i]);
            }
            if (++fadeBlock >= crossfadeBlocks)
            {
                retired.store (previous, std::memory_order_release); // freed by the offering thread
                previous = nullptr;
            }
        }
        else
            std::copy (outNew.begin(), outNew.end(), outFifo.begin());
    }

    void render (const Kernel& k, float* out) noexcept
    {
        std::fill (accRe.begin(), accRe.end(), 0.0f);
        std::fill (accIm.begin(), accIm.end(), 0.0f);
        for (int p = 0; p < numParts; ++p)
        {
            const int slot = (fdlPos + p) % numParts;
            const float* xr = fdlRe.data() + slot * bins;
            const float* xi = fdlIm.data() + slot * bins;
            const float* hr = k.re.data() + p * bins;
            const float* hi = k.im.data() + p * bins;
            for (int b = 0; b < bins; ++b)
            {
                accRe[(size_t) b] += xr[b] * hr[b] - xi[b] * hi[b];
                accIm[(size_t) b] += xr[b] * hi[b] + xi[b] * hr[b];
            }
        }
        // full conjugate-symmetric spectrum for the real inverse transform
        for (int b = 0; b < bins; ++b)
        {
            work[(size_t) (2 * b)] = accRe[(size_t) b];
            work[(size_t) (2 * b + 1)] = accIm[(size_t) b];
        }
        for (int b = bins; b < fftSize; ++b)
        {
            work[(size_t) (2 * b)] = accRe[(size_t) (fftSize - b)];
            work[(size_t) (2 * b + 1)] = -accIm[(size_t) (fftSize - b)];
        }
        fft->performRealOnlyInverseTransform (work.data());
        for (int i = 0; i < blockSize; ++i) // overlap-save: keep the second half
            out[i] = work[(size_t) (blockSize + i)] * scale;
    }

    void release()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete previous;
        previous = nullptr;
        delete current;
        current = nullptr;
    }

    int blockSize = 128, fftSize = 256, bins = 129, numParts = 1, crossfadeBlocks = 4;
    float scale = 1.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> inputWindow, fdlRe, fdlIm, work, accRe, accIm, outNew, outOld, inFifo, outFifo;
    int fdlPos = 0, fifoPos = 0, fadeBlock = 0;
    Kernel* current = nullptr;   // audio thread
    Kernel* previous = nullptr;  // audio thread, during a crossfade
    std::atomic<Kernel*> pending { nullptr }, retired { nullptr };
};

} // namespace mm
